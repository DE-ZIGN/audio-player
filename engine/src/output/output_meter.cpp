#include <ap/engine/output/output_meter.h>

#include <ap/engine/dsp/sample_convert.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace ap::engine {

namespace {
// How often the meter thread measures what has arrived, and at most how
// often it publishes a reading (a screen polls at 10 Hz).
constexpr auto kPoll = std::chrono::milliseconds(10);
constexpr auto kPublish = std::chrono::milliseconds(50);
} // namespace

OutputMeter::OutputMeter(bool waitWhenFull) : waitWhenFull_(waitWhenFull)
{
    thread_ = std::thread([this] { threadMain(); });
}

OutputMeter::~OutputMeter()
{
    {
        std::lock_guard lock(wakeMutex_);
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void OutputMeter::beginSession(const PcmFormat& output, bool measurable, std::uint64_t startPosition)
{
    accepting_ = false;
    {
        std::lock_guard lock(meterMutex_);
        drainLocked(); // nothing should be left, but measure it rather than mix it into this session

        format_ = output;
        bytesPerFrame_ = output.bytesPerFrame();
        deliveredFrames_ = 0;
        markerCount_ = 0;
        carriedMarker_.reset();
        head_ = 0;
        tail_ = 0;

        MeterState state = MeterState::NotPcm;
        if (measurable)
        {
            state = MeterState::Measuring;
            const auto count = std::max<std::size_t>(
                8, static_cast<std::size_t>(std::ceil(output.sampleRate * kRingSeconds / kSlotFrames)));
            slots_.resize(count);
            for (auto& s : slots_)
                s.bytes.resize(kSlotFrames * bytesPerFrame_);
            samples_.resize(kSlotFrames * output.channels);
            // The same rate and channels (e.g. reopened for the processing
            // path at another sample format) keep the meter and its windows.
            if (!meter_ || meter_->sampleRate() != output.sampleRate || meter_->channels() != output.channels)
                meter_ = std::make_unique<LevelMeter>(output.sampleRate, output.channels);
            firstMarker_ = true;
            startPosition_ = startPosition;
        }
        else
        {
            meter_.reset(); // the next PCM session starts afresh
        }
        {
            std::lock_guard s(statusMutex_);
            status_.state = state;
        }
        if (measurable)
            publishLocked();
        accepting_ = measurable;
    }
    {
        std::lock_guard lock(wakeMutex_);
        sessionRunning_ = measurable;
    }
    wake_.notify_all();
}

void OutputMeter::endSession()
{
    const bool was = accepting_.exchange(false);
    {
        std::lock_guard lock(wakeMutex_);
        sessionRunning_ = false;
    }
    std::lock_guard lock(meterMutex_);
    drainLocked();
    if (was && meter_)
    {
        meter_->finish(); // silence follows: a peak in the last samples still counts
        publishLocked();
    }
    std::lock_guard s(statusMutex_);
    status_.state = MeterState::Idle;
}

void OutputMeter::trackStarts(std::uint64_t frame, std::size_t queueIndex) noexcept
{
    if (!accepting_.load(std::memory_order_acquire))
        return;
    if (markerCount_ < kMaxMarkers)
        markers_[markerCount_++] = Marker{frame, queueIndex};
}

void OutputMeter::delivered(std::span<const std::byte> bytes, std::size_t frames) noexcept
{
    if (!accepting_.load(std::memory_order_acquire) || frames == 0)
        return;
    frames = std::min(frames, bytes.size() / bytesPerFrame_);
    const std::byte* p = bytes.data();
    while (frames > 0)
    {
        const std::uint64_t pos = deliveredFrames_;
        // A track that starts here (or, late, before here) begins this slot;
        // one that starts later ends it.
        std::size_t take = std::min(frames, kSlotFrames);
        std::optional<Marker> starts;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < markerCount_; ++i)
        {
            const Marker m = markers_[i];
            if (m.frame <= pos)
            {
                if (!starts || m.frame >= starts->frame)
                    starts = m;
                continue; // consumed
            }
            take = std::min<std::size_t>(take, static_cast<std::size_t>(m.frame - pos));
            markers_[kept++] = m;
        }
        markerCount_ = kept;

        writeSlot(p, take, starts ? &*starts : nullptr);
        p += take * bytesPerFrame_;
        frames -= take;
        deliveredFrames_ += take;
    }
}

void OutputMeter::writeSlot(const std::byte* data, std::size_t frames, const Marker* marker) noexcept
{
    const std::uint64_t h = head_.load(std::memory_order_relaxed);
    std::uint64_t t = tail_.load(std::memory_order_acquire);
    while (waitWhenFull_ && h - t >= slots_.size())
    {
        std::this_thread::yield();
        t = tail_.load(std::memory_order_acquire);
    }
    if (h - t >= slots_.size())
    {
        // The meter thread is behind: skip this data rather than wait for it.
        dropped_.fetch_add(frames, std::memory_order_relaxed);
        if (marker)
            carriedMarker_ = *marker;
        return;
    }
    const Marker* m = marker ? marker : (carriedMarker_ ? &*carriedMarker_ : nullptr);
    Slot& s = slots_[static_cast<std::size_t>(h % slots_.size())];
    s.startFrame = deliveredFrames_;
    s.frames = static_cast<std::uint32_t>(frames);
    s.trackStart = m != nullptr;
    s.queueIndex = m ? m->queueIndex : 0;
    std::memcpy(s.bytes.data(), data, frames * bytesPerFrame_);
    carriedMarker_.reset();
    head_.store(h + 1, std::memory_order_release);
}

void OutputMeter::drainLocked()
{
    std::uint64_t t = tail_.load(std::memory_order_relaxed);
    const std::uint64_t h = head_.load(std::memory_order_acquire);
    while (t < h)
    {
        measureSlotLocked(slots_[static_cast<std::size_t>(t % slots_.size())]);
        tail_.store(++t, std::memory_order_release);
    }
}

void OutputMeter::measureSlotLocked(const Slot& s)
{
    if (!meter_)
        return;
    if (s.trackStart)
    {
        // The session's first track goes on only when the session resumed it
        // mid-way; any later start in the session is a new track (gapless).
        const bool goesOn = firstMarker_ && s.queueIndex == meterQueueIndex_ && startPosition_ != 0;
        if (!goesOn)
            meter_->startTrack();
        meterQueueIndex_ = s.queueIndex;
        firstMarker_ = false;
    }
    const auto values = std::span(samples_).first(std::size_t{s.frames} * format_.channels);
    dsp::toDouble(std::span(s.bytes).first(std::size_t{s.frames} * bytesPerFrame_), format_, values);
    meter_->process(values);
}

void OutputMeter::publishLocked()
{
    if (!meter_)
        return;
    auto r = meter_->reading();
    std::lock_guard lock(statusMutex_);
    status_.reading = std::move(r);
    status_.queueIndex = meterQueueIndex_;
}

void OutputMeter::clear()
{
    std::lock_guard lock(meterMutex_);
    meter_.reset();
    meterQueueIndex_ = 0;
    dropped_ = 0;
    std::lock_guard s(statusMutex_);
    status_ = MeterStatus{};
}

MeterStatus OutputMeter::status() const
{
    std::lock_guard lock(statusMutex_);
    MeterStatus s = status_;
    s.droppedFrames = dropped_.load(std::memory_order_relaxed);
    return s;
}

void OutputMeter::flush()
{
    std::lock_guard lock(meterMutex_);
    drainLocked();
    publishLocked();
}

void OutputMeter::threadMain()
{
    auto lastPublish = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::unique_lock lock(wakeMutex_);
            wake_.wait(lock, [this] { return stop_ || sessionRunning_; });
            if (wake_.wait_for(lock, kPoll, [this] { return stop_; }))
                return;
        }
        std::lock_guard lock(meterMutex_);
        const bool any = tail_.load(std::memory_order_relaxed) != head_.load(std::memory_order_acquire);
        drainLocked();
        const auto now = std::chrono::steady_clock::now();
        if (any && now - lastPublish >= kPublish)
        {
            publishLocked();
            lastPublish = now;
        }
    }
}

} // namespace ap::engine
