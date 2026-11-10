#pragma once

// The processing path (task 3-1): stages that work on interleaved 64-bit
// floating point, full scale = +-1.0. The stages themselves (volume,
// ReplayGain, EQ, ...) come with the later M3 tasks.
//
// Full bypass: when every stage reports isIdentity(), playback uses the direct
// path and the processing code is not run at all (CLAUDE.md chapter 4).

#include <ap/engine/track_info.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ap::engine {

class ProcessingStage
{
public:
    virtual ~ProcessingStage() = default;

    // Short English identifier for the path record, e.g. "volume".
    virtual std::string name() const = 0;

    // Called on the playback thread before the first process() of an output
    // session. Not called when the chain is bypassed.
    virtual void prepare(std::uint32_t sampleRate, std::uint16_t channels) = 0;

    // True when process() would leave every sample exactly unchanged (e.g.
    // volume at 100%). Must be cheap and thread-safe: it decides the bypass.
    virtual bool isIdentity() const noexcept = 0;

    // A track starts at the next sample passed to process() (and once before
    // an output session, for the track it starts with). For stages whose
    // setting depends on the track, such as ReplayGain. No locks, no allocation.
    virtual void trackStarted(const TrackInfo& /*track*/) noexcept {}

    // Processes samples in place: samples.size() is a multiple of channels.
    // Runs on the playback thread (or the ASIO driver's thread): no locks,
    // no allocation, no exceptions.
    virtual void process(std::span<double> samples, std::uint16_t channels) noexcept = 0;
};

// Stages in their fixed order (CLAUDE.md chapter 4).
class ProcessingChain
{
public:
    explicit ProcessingChain(std::vector<std::shared_ptr<ProcessingStage>> stages = {});

    // True when every stage is identity: the direct path is used.
    bool bypassed() const noexcept;

    // Names of the stages that are not identity right now, in order.
    std::vector<std::string> activeStageNames() const;

    void prepare(std::uint32_t sampleRate, std::uint16_t channels);

    // Runs the stages that are not identity.
    void process(std::span<double> samples, std::uint16_t channels) noexcept;

    // Tells every stage (identity or not) that a track starts.
    void trackStarted(const TrackInfo& track) noexcept;

private:
    std::vector<std::shared_ptr<ProcessingStage>> stages_;
};

// Sets flush-to-zero and denormals-are-zero for the current thread and
// restores the previous state on destruction. Used around processing because
// it may run on a thread the engine does not own (the ASIO driver's).
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept;
    ~ScopedFlushDenormals();
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
    unsigned saved_;
};

} // namespace ap::engine
