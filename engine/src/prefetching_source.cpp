#include <ap/engine/prefetching_source.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace ap::engine {

namespace {

void copyFromRing(const std::vector<std::byte>& ring, std::size_t capacity, std::size_t bpf, std::uint64_t pos,
                  std::byte* dst, std::size_t frames)
{
    const std::size_t start = static_cast<std::size_t>(pos % capacity);
    const std::size_t first = std::min(frames, capacity - start);
    std::memcpy(dst, ring.data() + start * bpf, first * bpf);
    if (frames > first)
        std::memcpy(dst + first * bpf, ring.data(), (frames - first) * bpf);
}

void copyToRing(std::vector<std::byte>& ring, std::size_t capacity, std::size_t bpf, std::uint64_t pos,
                const std::byte* src, std::size_t frames)
{
    const std::size_t start = static_cast<std::size_t>(pos % capacity);
    const std::size_t first = std::min(frames, capacity - start);
    std::memcpy(ring.data() + start * bpf, src, first * bpf);
    if (frames > first)
        std::memcpy(ring.data(), src + first * bpf, (frames - first) * bpf);
}

} // namespace

PrefetchingSource::PrefetchingSource(std::unique_ptr<PcmSource> inner, std::size_t bufferFrames)
    : inner_(std::move(inner))
{
    if (!inner_)
        throw std::invalid_argument("PrefetchingSource: no source");
    format_ = inner_->format();
    totalFrames_ = inner_->totalFrames();
    bpf_ = format_.bytesPerFrame();
    capacity_ = bufferFrames != 0 ? bufferFrames : std::max<std::size_t>(65536, format_.sampleRate * 2u);
    ring_.resize(capacity_ * bpf_);
    position_ = inner_->position();

    reader_ = std::thread([this] { readerMain(); });

    // Fill the buffer (or read the whole source) before playback can start.
    const std::uint64_t target = std::min<std::uint64_t>(capacity_, totalFrames_ - std::min(totalFrames_, position_.load()));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ended_.load(std::memory_order_acquire) && writePos_.load(std::memory_order_acquire) < target
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

PrefetchingSource::~PrefetchingSource()
{
    quit_ = true;
    wake_.notify_all();
    if (reader_.joinable())
        reader_.join();
}

void PrefetchingSource::readerMain()
{
    // Above normal so the read-ahead keeps up under load; the playback thread itself uses MMCSS.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    const std::size_t chunk = std::max<std::size_t>(256, std::min<std::size_t>(capacity_ / 4, 16384));
    std::vector<std::byte> temp(chunk * bpf_);
    std::uint64_t appliedGen = 0;

    auto waitFor = [this](std::chrono::milliseconds ms) {
        std::unique_lock lock(wakeMutex_);
        wake_.wait_for(lock, ms);
    };

    while (!quit_)
    {
        const std::uint64_t gen = seekGen_.load(std::memory_order_acquire);
        if (gen != appliedGen)
        {
            inner_->seek(seekTarget_.load(std::memory_order_acquire));
            appliedGen = gen;
            ended_.store(false, std::memory_order_release);
            restartPos_.store(writePos_.load(std::memory_order_relaxed), std::memory_order_release);
            restartGen_.store(gen, std::memory_order_release);
        }

        if (ended_.load(std::memory_order_relaxed))
        {
            waitFor(std::chrono::milliseconds(20));
            continue;
        }

        const std::uint64_t w = writePos_.load(std::memory_order_relaxed);
        // Before the consumer has caught up with a restart, frames before restartPos_ are stale but
        // still occupy space; count space from the later of readPos_ and restartPos_.
        std::uint64_t r = readPos_.load(std::memory_order_acquire);
        if (restartGen_.load(std::memory_order_relaxed) != 0)
            r = std::max(r, std::min(w, restartPos_.load(std::memory_order_relaxed)));
        const std::size_t space = capacity_ - static_cast<std::size_t>(w - r);
        // Fill whatever is free, so the buffer can become completely full
        // (the capacity need not be a multiple of the chunk size).
        if (space == 0)
        {
            waitFor(std::chrono::milliseconds(2));
            continue;
        }

        const std::size_t want = std::min(space, chunk);
        const std::size_t got = inner_->read(std::span(temp).first(want * bpf_));

        // A seek that arrived during the read makes this data stale.
        if (seekGen_.load(std::memory_order_acquire) != appliedGen)
            continue;

        copyToRing(ring_, capacity_, bpf_, w, temp.data(), got);
        writePos_.store(w + got, std::memory_order_release);
        if (got < want)
        {
            endPos_.store(w + got, std::memory_order_release);
            ended_.store(true, std::memory_order_release);
        }
    }
}

std::size_t PrefetchingSource::read(std::span<std::byte> dst)
{
    const std::size_t want = dst.size() / bpf_;
    if (want == 0)
        return 0;

    if (waitingForGen_ != 0)
    {
        if (restartGen_.load(std::memory_order_acquire) == waitingForGen_)
        {
            readPos_.store(restartPos_.load(std::memory_order_acquire), std::memory_order_release);
            waitingForGen_ = 0;
            refilling_ = true;
        }
        else
        {
            std::memset(dst.data(), 0, want * bpf_);
            seekFillFrames_ += want;
            return want;
        }
    }

    const bool wasRefilling = refilling_;
    std::size_t delivered = 0;
    for (int attempt = 0; attempt < 2 && delivered < want; ++attempt)
    {
        const std::uint64_t r = readPos_.load(std::memory_order_relaxed);
        const std::uint64_t avail = writePos_.load(std::memory_order_acquire) - r;
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(avail, want - delivered));
        if (n > 0)
        {
            copyFromRing(ring_, capacity_, bpf_, r, dst.data() + delivered * bpf_, n);
            readPos_.store(r + n, std::memory_order_release);
            delivered += n;
            position_ += n;
        }
        if (delivered < want && ended_.load(std::memory_order_acquire))
            continue; // the reader may have written the final chunk just now; look once more
        break;
    }
    if (delivered > 0)
        refilling_ = false;
    wake_.notify_one();

    if (delivered == want)
        return delivered;

    const bool atEnd = ended_.load(std::memory_order_acquire)
                    && readPos_.load(std::memory_order_relaxed) >= endPos_.load(std::memory_order_acquire);
    if (atEnd)
        return delivered;

    // Not at the end: the read-ahead fell behind. Fill with silence and record it.
    const std::size_t missing = want - delivered;
    std::memset(dst.data() + delivered * bpf_, 0, missing * bpf_);
    if (wasRefilling)
    {
        seekFillFrames_ += missing;
    }
    else
    {
        ++starvations_;
        starvedFrames_ += missing;
    }
    return want;
}

bool PrefetchingSource::seek(std::uint64_t frame)
{
    frame = std::min(frame, totalFrames_);
    seekTarget_.store(frame, std::memory_order_release);
    waitingForGen_ = seekGen_.fetch_add(1, std::memory_order_acq_rel) + 1;
    position_ = frame;
    wake_.notify_one();
    return true;
}

void PrefetchingSource::waitUntilReady()
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    if (waitingForGen_ != 0)
    {
        while (restartGen_.load(std::memory_order_acquire) != waitingForGen_
               && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (restartGen_.load(std::memory_order_acquire) != waitingForGen_)
            return; // the reader is stuck; read() will report it
        readPos_.store(restartPos_.load(std::memory_order_acquire), std::memory_order_release);
        waitingForGen_ = 0;
        refilling_ = false;
    }
    // Wait for a full buffer (or the rest of the source).
    const std::uint64_t remaining = totalFrames_ - std::min(totalFrames_, position_.load());
    const std::uint64_t target = std::min<std::uint64_t>(capacity_, remaining);
    while (!ended_.load(std::memory_order_acquire)
           && writePos_.load(std::memory_order_acquire) - readPos_.load(std::memory_order_relaxed) < target
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

SourceHealth PrefetchingSource::health() const noexcept
{
    return {starvations_.load(), starvedFrames_.load(), seekFillFrames_.load()};
}

std::size_t PrefetchingSource::bufferedFrames() const noexcept
{
    return static_cast<std::size_t>(writePos_.load() - readPos_.load());
}

} // namespace ap::engine
