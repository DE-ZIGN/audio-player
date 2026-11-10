#pragma once

// The processing path (task 3-1): stages that work on interleaved 64-bit
// floating point, full scale = +-1.0. The stages themselves (volume,
// ReplayGain, EQ, ...) come with the later M3 tasks.
//
// Full bypass: when every stage reports isIdentity(), playback uses the direct
// path and the processing code is not run at all (CLAUDE.md chapter 4).

#include <ap/engine/track_info.h>

#include <cstddef>
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

    // The factor by which this stage scales peaks right now (gain stages:
    // their gain; others: 1). Later stages are told the product of the
    // earlier ones (setUpstreamGain), e.g. so the ISP-safe gain does not lower
    // what the volume has already lowered.
    virtual double peakGain() const noexcept { return 1.0; }
    virtual void setUpstreamGain(double /*gain*/) noexcept {}

    // A stage that never by itself calls for processing but must run whenever
    // the processing path is taken (the safety limiter): isIdentity() is true
    // and this is true.
    virtual bool runsWhenProcessing() const noexcept { return false; }

    // Frames by which the stage delays its output (look-ahead). play()
    // compensates, so the output keeps its length and timing.
    virtual std::size_t latencyFrames() const noexcept { return 0; }

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

    // Names of the stages that run on the processing path right now, in
    // order: those that are not identity, and those that always run.
    std::vector<std::string> activeStageNames() const;

    std::size_t latencyFrames() const noexcept;

    void prepare(std::uint32_t sampleRate, std::uint16_t channels);

    // Runs the stages that are not identity (and those that always run).
    void process(std::span<double> samples, std::uint16_t channels) noexcept;

    // Tells every stage (identity or not) that a track starts.
    void trackStarted(const TrackInfo& track) noexcept;

private:
    // Tells each stage the product of the earlier stages' peak gains.
    void updateUpstreamGains() const noexcept;

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
