#include <ap/engine/player.h>

#include <ap/engine/output/wasapi_exclusive_output.h>

#include <stdexcept>

namespace ap::engine {

namespace {
struct DopNotAllowed
{
};
} // namespace

// The source the output pulls from during one output session (one format).
// Applies seeks requested from other threads, publishes the position, and at
// the end of a track continues straight into the next one when it has the
// same format (gapless). A track with another format ends the session so the
// playback thread can reopen the output. Never touches sample values.
class Player::QueueReader final : public PcmSource
{
public:
    explicit QueueReader(Player& p) : p_(p), format_(p.source_->format()), dop_(p.source_->dop().has_value()) {}

    const PcmFormat& format() const noexcept override { return format_; }
    std::uint64_t totalFrames() const noexcept override { return p_.source_->totalFrames(); }
    std::uint64_t position() const noexcept override { return p_.source_->position(); }
    bool seek(std::uint64_t frame) override { return p_.source_->seek(frame); }

    std::size_t read(std::span<std::byte> dst) override
    {
        if (p_.stopRequested_)
            return 0;

        if (p_.reopenPending_)
        {
            // First data request after reopening for a new format.
            auto t = *p_.reopenPending_;
            p_.reopenPending_.reset();
            t.switchMilliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_.dataEndedAt_).count();
            std::lock_guard lock(p_.mutex_);
            p_.status_.transitions.push_back(t);
        }

        const std::int64_t target = p_.pendingSeek_.exchange(-1);
        if (target >= 0)
            p_.source_->seek(static_cast<std::uint64_t>(target));

        const std::size_t bpf = format_.bytesPerFrame();
        const std::size_t want = dst.size() / bpf;
        std::size_t got = p_.source_->read(dst);
        p_.position_ = p_.source_->position();

        while (got < want && !p_.stopRequested_ && p_.index_ + 1 < p_.queue_.size())
        {
            // The current track is exhausted; take the next one (opened in the background).
            std::unique_ptr<PcmSource> next;
            try
            {
                next = p_.nextLoad_.valid() ? p_.nextLoad_.get() : nullptr;
                if (!next)
                    throw std::runtime_error("track could not be opened");
            }
            catch (const std::exception& e)
            {
                p_.loadFailed_ = true;
                p_.loadError_ = e.what();
                p_.dataEndedAt_ = std::chrono::steady_clock::now();
                break;
            }

            // A change between PCM and DoP is never joined, even at the same PCM format:
            // the output is reopened so the DoP check applies and the DAC can switch modes.
            if (!(next->format() == format_) || next->dop().has_value() != dop_)
            {
                p_.pendingNext_ = std::move(next);
                p_.dataEndedAt_ = std::chrono::steady_clock::now();
                break; // short read: the session ends after this data has played
            }

            // Gapless: the next track's first frame directly follows this track's last frame.
            TrackTransition t;
            t.fromIndex = p_.index_;
            t.toIndex = p_.index_ + 1;
            t.kind = TransitionKind::Gapless;
            t.fromFormat = format_;
            t.toFormat = next->format();
            {
                std::lock_guard lock(p_.mutex_);
                p_.retireSourceLocked();
                ++p_.index_;
                p_.source_ = std::move(next);
                p_.publishTrackLocked();
                p_.status_.transitions.push_back(t);
                p_.startLoadingNextLocked();
            }
            got += p_.source_->read(dst.subspan(got * bpf));
            p_.position_ = p_.source_->position();
        }
        return got;
    }

private:
    Player& p_;
    PcmFormat format_;
    bool dop_ = false;
};

Player::Player(OutputFactory outputFactory, PlaybackOptions options)
    : outputFactory_(std::move(outputFactory)), options_(std::move(options))
{
}

Player::~Player()
{
    stop();
}

void Player::load(std::unique_ptr<PcmSource> source)
{
    if (!source)
    {
        setQueue({});
        return;
    }
    // A queue of one. The source can be handed over only once; replaying it
    // seeks back to 0 instead of reopening (see play()).
    auto holder = std::make_shared<std::unique_ptr<PcmSource>>(std::move(source));
    setQueue({QueueItem{"", [holder]() -> std::unique_ptr<PcmSource> {
                            if (!*holder)
                                throw std::runtime_error("source already in use");
                            return std::move(*holder);
                        }}});
}

void Player::setQueue(std::vector<QueueItem> items, std::size_t startIndex)
{
    stop();
    std::lock_guard lock(mutex_);
    queue_ = std::move(items);
    index_ = queue_.empty() ? 0 : std::min(startIndex, queue_.size() - 1);
    source_.reset();
    nextLoad_ = {};
    pendingNext_.reset();
    loadFailed_ = false;
    loadError_.clear();
    reopenPending_.reset();
    position_ = 0;
    pendingSeek_ = -1;
    retiredSourceHealth_ = {};
    retiredOutputHealth_ = {};

    status_ = PlayerStatus{};
    status_.queueLength = queue_.size();
    if (queue_.empty())
        return;
    if (openCurrentLocked())
    {
        status_.state = PlayerState::Stopped;
        startLoadingNextLocked();
    }
}

bool Player::openCurrentLocked()
{
    try
    {
        source_ = queue_[index_].open();
        if (!source_)
            throw std::runtime_error("track could not be opened");
    }
    catch (const std::exception& e)
    {
        source_.reset();
        status_.state = PlayerState::Error;
        status_.error = PlayerError{PlayerErrorKind::Load, {}, OutputError::DeviceError, e.what()};
        status_.queueIndex = index_;
        status_.trackName = queue_[index_].name;
        status_.sourceFormat.reset();
        status_.dop.reset();
        status_.totalFrames = 0;
        return false;
    }
    if (source_->position() != 0)
        source_->seek(0);
    source_->waitUntilReady();
    position_ = 0;
    publishTrackLocked();
    return true;
}

void Player::startLoadingNextLocked()
{
    if (index_ + 1 < queue_.size())
        nextLoad_ = std::async(std::launch::async, queue_[index_ + 1].open);
    else
        nextLoad_ = {};
}

void Player::retireSourceLocked()
{
    if (!source_)
        return;
    const auto h = source_->health();
    retiredSourceHealth_.starvations += h.starvations;
    retiredSourceHealth_.starvedFrames += h.starvedFrames;
    retiredSourceHealth_.seekFillFrames += h.seekFillFrames;
}

void Player::publishTrackLocked()
{
    status_.queueIndex = index_;
    status_.queueLength = queue_.size();
    status_.trackName = queue_.empty() ? std::string{} : queue_[index_].name;
    status_.sourceFormat = source_ ? std::optional<PcmFormat>(source_->format()) : std::nullopt;
    status_.dop = source_ ? source_->dop() : std::nullopt;
    status_.totalFrames = source_ ? source_->totalFrames() : 0;
}

void Player::play()
{
    std::lock_guard lock(mutex_);
    if (queue_.empty() || running_)
        return;
    if (thread_.joinable())
        thread_.join(); // previous run has finished; reap it

    if (status_.state == PlayerState::Ended)
    {
        if (index_ == 0 && source_)
        {
            source_->seek(0);
            source_->waitUntilReady(); // not playing: start with real data, not refill silence
            position_ = 0;
        }
        else
        {
            retireSourceLocked();
            index_ = 0;
            source_.reset();
        }
        pendingNext_.reset();
    }
    if (!source_)
    {
        if (!openCurrentLocked())
            return;
        startLoadingNextLocked();
    }

    status_.state = PlayerState::Playing;
    status_.error = PlayerError{};
    status_.plan.reset();
    loadFailed_ = false;
    loadError_.clear();
    stopRequested_ = false;
    running_ = true;
    thread_ = std::thread([this] { threadMain(); });
}

void Player::stop()
{
    {
        std::lock_guard lock(mutex_);
        if (!running_)
        {
            joinThread();
            return;
        }
        stopRequested_ = true;
        if (activeOutput_)
            activeOutput_->requestStop();
    }
    joinThread();
}

void Player::seek(std::uint64_t frame)
{
    std::lock_guard lock(mutex_);
    if (!source_)
        return;
    frame = std::min(frame, source_->totalFrames());
    if (running_)
    {
        pendingSeek_ = static_cast<std::int64_t>(frame);
        position_ = frame;
        return;
    }
    source_->seek(frame);
    source_->waitUntilReady(); // not playing: start with real data, not refill silence
    position_ = source_->position();
    if (status_.state == PlayerState::Ended)
        status_.state = PlayerState::Stopped;
}

PlayerStatus Player::status() const
{
    std::lock_guard lock(mutex_);
    PlayerStatus s = status_;
    s.positionFrames = position_;

    SourceHealth src = retiredSourceHealth_;
    if (source_)
    {
        const auto h = source_->health();
        src.starvations += h.starvations;
        src.starvedFrames += h.starvedFrames;
    }
    OutputHealth out = retiredOutputHealth_;
    if (activeOutput_)
    {
        const auto h = activeOutput_->health();
        out.lateBuffers += h.lateBuffers;
        out.timeouts += h.timeouts;
        out.realtimePriority = out.realtimePriority || h.realtimePriority;
    }
    s.dropouts = {out.lateBuffers, out.timeouts, src.starvations, src.starvedFrames, out.realtimePriority};
    return s;
}

void Player::waitUntilIdle()
{
    {
        std::unique_lock lock(mutex_);
        idle_.wait(lock, [this] { return !running_; });
    }
    joinThread();
}

void Player::joinThread()
{
    // Called without holding mutex_ (the thread takes it on exit), or from
    // stop() after running_ is already false.
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
        thread_.join();
}

void Player::threadMain()
{
    ScopedComInit com; // WASAPI needs COM on the thread that drives the device
    PlayerError error;
    bool ended = false;
    std::unique_ptr<AudioOutput> output; // outlives the try block so its counters can be kept

    try
    {
        output = outputFactory_();
        {
            std::lock_guard lock(mutex_);
            activeOutput_ = output.get();
            status_.outputName = output->deviceName();
        }

        PlaybackOptions options = options_;
        options.onStart = [this, output = output.get(), userOnStart = options_.onStart](const PlaybackPlan& plan) {
            {
                std::lock_guard lock(mutex_);
                status_.plan = plan;
            }
            // open() clears an earlier stop request inside the output; re-issue it.
            if (stopRequested_)
                output->requestStop();
            if (userOnStart)
                userOnStart(plan);
        };

        // One iteration per output session (per run of tracks with one format).
        for (;;)
        {
            if (source_->dop() && !options_.allowDop)
                throw DopNotAllowed{};
            QueueReader reader(*this);
            ap::engine::play(reader, *output, options);

            if (pendingNext_)
            {
                // The next track has another format: switch to it and reopen.
                TrackTransition t;
                t.fromIndex = index_;
                t.toIndex = index_ + 1;
                t.kind = TransitionKind::Reopened;
                t.fromFormat = source_->format();
                t.toFormat = pendingNext_->format();
                std::lock_guard lock(mutex_);
                retireSourceLocked();
                ++index_;
                source_ = std::move(pendingNext_);
                position_ = 0;
                publishTrackLocked();
                startLoadingNextLocked();
                if (!stopRequested_)
                    reopenPending_ = t;
            }

            if (stopRequested_)
                break;
            if (reopenPending_)
                continue;

            if (loadFailed_)
            {
                std::lock_guard lock(mutex_);
                retireSourceLocked();
                ++index_;
                source_.reset();
                status_.queueIndex = index_;
                status_.trackName = queue_[index_].name;
                status_.sourceFormat.reset();
                status_.dop.reset();
                status_.totalFrames = 0;
                position_ = 0;
                error = {PlayerErrorKind::Load, {}, OutputError::DeviceError, loadError_};
                break;
            }

            ended = true;
            position_ = source_->position();
            break;
        }
    }
    catch (const DopNotAllowed&)
    {
        error = {PlayerErrorKind::DopNotAllowed, {}, OutputError::FormatNotSupported, "DoP is not enabled for this output"};
    }
    catch (const FormatNotSupportedError& e)
    {
        error = {PlayerErrorKind::FormatNotSupported, e.check(), OutputError::FormatNotSupported, e.what()};
    }
    catch (const OutputException& e)
    {
        error = {PlayerErrorKind::Output, {}, e.code(), e.what()};
    }
    catch (const std::exception& e)
    {
        error = {PlayerErrorKind::Other, {}, OutputError::DeviceError, e.what()};
    }

    std::lock_guard lock(mutex_);
    if (output)
    {
        const auto h = output->health();
        retiredOutputHealth_.lateBuffers += h.lateBuffers;
        retiredOutputHealth_.timeouts += h.timeouts;
        retiredOutputHealth_.realtimePriority = retiredOutputHealth_.realtimePriority || h.realtimePriority;
    }
    activeOutput_ = nullptr;
    reopenPending_.reset();
    if (error.kind != PlayerErrorKind::None)
    {
        status_.state = PlayerState::Error;
        status_.error = error;
    }
    else if (stopRequested_)
    {
        status_.state = PlayerState::Stopped;
    }
    else if (ended)
    {
        status_.state = PlayerState::Ended;
    }
    running_ = false;
    idle_.notify_all();
}

} // namespace ap::engine
