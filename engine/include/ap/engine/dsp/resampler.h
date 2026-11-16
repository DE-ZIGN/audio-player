#pragma once

// The resampler (task 10-1, CLAUDE.md chapter 4): r8brain-free-src's
// linear-phase resampler, one per channel, on interleaved 64-bit samples.
//
// It sits at the very end of the processing path (after the safety limiter,
// before dither; decided 2026-11-16): the stages run at the track's rate and
// the result is converted to the output rate. What it does and what it
// measures is what is said; never that a higher rate sounds better or brings
// anything back (CLAUDE.md chapter 6).
//
// The filter: linear phase, stop band kAttenuationDb (r8brain's 24-bit
// setting; at least 140 dB is required, CLAUDE.md chapter 5), transition band
// kTransitionBandPercent of the narrower Nyquist band. r8brain removes its own
// latency: the output's frame k is the input's time k * in / out, and the
// output holds exactly outputFrames(input frames) frames once finish() is called.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ap::engine::dsp {

class Resampler
{
public:
    static constexpr double kAttenuationDb = 180.15;
    static constexpr double kTransitionBandPercent = 2.0;

    Resampler();
    ~Resampler();
    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;

    // Allocates: not on the audio callback. maxInFrames: the most frames one
    // process() call takes (longer input is split).
    void prepare(std::uint32_t inRate, std::uint32_t outRate, std::uint16_t channels, std::size_t maxInFrames);
    // Starts over at the same rates (allocation free).
    void reset() noexcept;

    bool active() const noexcept { return inRate_ != outRate_; }
    std::uint32_t inRate() const noexcept { return inRate_; }
    std::uint32_t outRate() const noexcept { return outRate_; }

    // The output frames for `inFrames` input frames: the same duration at the
    // output rate, rounded up (the frames whose time lies inside the input).
    static std::uint64_t outputFrames(std::uint64_t inFrames, std::uint32_t inRate, std::uint32_t outRate) noexcept;
    // The most frames one process() of `inFrames` can append (for reserving).
    std::size_t maxOutputFrames(std::size_t inFrames) const noexcept;

    // Converts interleaved `in` and appends the result to `out`. Allocation
    // free when `out` has the capacity (maxOutputFrames). When not active,
    // appends `in` unchanged.
    void process(std::span<const double> in, std::vector<double>& out) noexcept;
    // The input has ended: appends the rest, so that the output holds
    // outputFrames(all input) frames in all.
    void finish(std::vector<double>& out) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint32_t inRate_ = 0, outRate_ = 0;
    std::uint16_t channels_ = 0;
    std::size_t maxIn_ = 0;
    std::uint64_t inFrames_ = 0, outFrames_ = 0;
    bool finished_ = false;
};

} // namespace ap::engine::dsp
