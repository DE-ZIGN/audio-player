#pragma once

#include <ap/engine/pcm_source.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ap::engine {

// Reads ahead from another source on its own thread into a ring buffer, so
// the playback thread never waits for the disk. This is for preventing
// dropouts only; the bytes delivered are the bytes of the inner source.
//
// Contract for read() (called from one consumer thread, e.g. the playback thread):
//   - Delivers the inner source's bytes unchanged.
//   - If the read-ahead has fallen behind, the missing frames are filled with
//     silence, the read still returns the full count, and the event is
//     counted in health().starvations / starvedFrames.
//   - Right after seek(), until the reader has refilled from the new
//     position, silence is returned and counted as seekFillFrames (not a dropout).
//   - Returns fewer frames than requested only at the end of the source.
class PrefetchingSource final : public PcmSource
{
public:
    // `bufferFrames` = ring capacity; 0 picks about two seconds.
    explicit PrefetchingSource(std::unique_ptr<PcmSource> inner, std::size_t bufferFrames = 0);
    ~PrefetchingSource() override;
    PrefetchingSource(const PrefetchingSource&) = delete;
    PrefetchingSource& operator=(const PrefetchingSource&) = delete;

    const PcmFormat& format() const noexcept override { return format_; }
    std::uint64_t totalFrames() const noexcept override { return totalFrames_; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t frame) override;
    std::uint64_t position() const noexcept override { return position_; }
    SourceHealth health() const noexcept override;
    void waitUntilReady() override;
    std::optional<DopInfo> dop() const override { return dop_; }
    std::string containerName() const override { return container_; }

    std::size_t bufferFrames() const noexcept { return capacity_; }

    // For tests: frames currently buffered and ready.
    std::size_t bufferedFrames() const noexcept;

private:
    void readerMain();

    std::unique_ptr<PcmSource> inner_;
    PcmFormat format_;
    std::optional<DopInfo> dop_; // copied at construction (the reader thread owns inner_)
    std::string container_;      // likewise
    std::uint64_t totalFrames_ = 0;
    std::size_t bpf_ = 0;
    std::size_t capacity_ = 0;            // frames
    std::vector<std::byte> ring_;

    // Ring positions in frames, ever increasing (index = pos % capacity).
    std::atomic<std::uint64_t> writePos_{0}; // written by the reader thread
    std::atomic<std::uint64_t> readPos_{0};  // written by the consumer

    // Seek handshake: the consumer bumps seekGen_; the reader applies it and
    // publishes restartPos_/restartGen_. Data before restartPos_ is stale.
    std::atomic<std::uint64_t> seekTarget_{0};
    std::atomic<std::uint64_t> seekGen_{0};
    std::atomic<std::uint64_t> restartGen_{0};
    std::atomic<std::uint64_t> restartPos_{0};
    std::uint64_t waitingForGen_ = 0; // consumer only; 0 = not waiting
    bool refilling_ = false;          // consumer only; after a seek until the first new data arrives

    // End of the inner source for the current generation.
    std::atomic<bool> ended_{false};
    std::atomic<std::uint64_t> endPos_{0};

    std::atomic<std::uint64_t> position_{0}; // consumer: frame position in the track

    std::atomic<std::uint32_t> starvations_{0};
    std::atomic<std::uint64_t> starvedFrames_{0};
    std::atomic<std::uint64_t> seekFillFrames_{0};

    std::mutex wakeMutex_;
    std::condition_variable wake_; // wakes the reader when space frees up or a seek arrives
    std::atomic<bool> quit_{false};
    std::thread reader_;
};

} // namespace ap::engine
