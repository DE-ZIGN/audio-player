#pragma once

// The meters on what goes to the output (task 4-4): True Peak and loudness of
// the samples handed to the output layer, after processing (on the direct
// path, the original values). Measuring never changes the samples.
//
// The playback thread (or the ASIO driver's thread) only copies each
// period's bytes into a ring of preallocated slots; the 8x interpolation and
// the K-weighting run on the meter's own thread, so they do not add to the
// time the playback thread needs. When the meter thread falls behind and the
// ring is full, the data is not measured and counted (droppedFrames) rather
// than waited for.
//
// DoP is not PCM audio and is not measured.

#include <ap/engine/dsp/level_meter.h>
#include <ap/engine/pcm_format.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace ap::engine {

enum class MeterState
{
    Idle,      // no output session (the last values are kept)
    Measuring, // an output session is running
    NotPcm,    // the output carries DoP: nothing to measure
};

struct MeterStatus
{
    MeterState state = MeterState::Idle;
    std::optional<LevelReading> reading;
    std::size_t queueIndex = 0;       // the track the reading's track values (maximum, integrated) belong to
    std::uint64_t droppedFrames = 0;  // since the queue was set: not measured, the meter fell behind
};

class OutputMeter
{
public:
    // Slots of this many frames; the ring holds about kRingSeconds of audio.
    static constexpr std::size_t kSlotFrames = 1024;
    static constexpr double kRingSeconds = 1.0;

    // waitWhenFull: wait for the meter thread instead of skipping data. For
    // outputs not paced by a device (the dummy output in tests), where data
    // arrives faster than real time; never for a real device.
    explicit OutputMeter(bool waitWhenFull = false);
    ~OutputMeter();
    OutputMeter(const OutputMeter&) = delete;
    OutputMeter& operator=(const OutputMeter&) = delete;

    // Playback thread, before the output runs. `measurable` is false for DoP.
    // `startPosition` is the track position the session starts at: a session
    // that goes on with the same track from where it was (resumed after a
    // stop, or reopened to switch processing) keeps the track's values; one
    // that starts a track from its beginning starts them again.
    void beginSession(const PcmFormat& output, bool measurable, std::uint64_t startPosition);

    // Playback thread, after the output has stopped. Measures what is still in
    // the ring, then lets the interpolator run on into the silence that follows.
    void endSession();

    // From the render callback (real-time: no locks, no allocation).
    // A track starts at `frame` (counted from the session's first frame);
    // may be called before that frame is delivered.
    void trackStarts(std::uint64_t frame, std::size_t queueIndex) noexcept;
    // The bytes handed to the output (in the session's format).
    void delivered(std::span<const std::byte> bytes, std::size_t frames) noexcept;

    // A new queue: forget the values. Not during a session.
    void clear();

    // Thread-safe.
    MeterStatus status() const;

    // Waits until everything delivered so far has been measured (for tests).
    void flush();

private:
    struct Slot
    {
        std::uint64_t startFrame = 0;
        std::uint32_t frames = 0;
        bool trackStart = false;
        std::size_t queueIndex = 0;
        std::vector<std::byte> bytes;
    };
    struct Marker
    {
        std::uint64_t frame = 0;
        std::size_t queueIndex = 0;
    };

    void threadMain();
    void drainLocked(); // measures the slots in the ring; meterMutex_ held
    void measureSlotLocked(const Slot& s);
    void publishLocked();
    void writeSlot(const std::byte* data, std::size_t frames, const Marker* marker) noexcept;

    const bool waitWhenFull_;

    // Producer side (render thread during a session).
    PcmFormat format_;
    std::size_t bytesPerFrame_ = 0;
    std::atomic<bool> accepting_{false};
    std::uint64_t deliveredFrames_ = 0; // since the session started
    static constexpr std::size_t kMaxMarkers = 16;
    Marker markers_[kMaxMarkers]{};
    std::size_t markerCount_ = 0;
    std::optional<Marker> carriedMarker_; // its slot was dropped: put it on the next one

    // Ring (single producer, single consumer).
    std::vector<Slot> slots_;
    std::atomic<std::uint64_t> head_{0}; // slots written
    std::atomic<std::uint64_t> tail_{0}; // slots measured
    std::atomic<std::uint64_t> dropped_{0};

    // Consumer side.
    std::mutex meterMutex_; // the LevelMeter and the session's measuring state
    std::unique_ptr<LevelMeter> meter_;
    std::vector<double> samples_;
    std::size_t meterQueueIndex_ = 0;
    bool firstMarker_ = true;
    std::uint64_t startPosition_ = 0;

    mutable std::mutex statusMutex_;
    MeterStatus status_;

    std::mutex wakeMutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    bool sessionRunning_ = false;
    std::thread thread_;
};

} // namespace ap::engine
