#pragma once

// The safety limiter (task 3-5, CLAUDE.md chapter 4: リミッターは保険): the last
// stage before dither. It keeps the true peak at or below -0.3 dBTP, just
// above the ISP-safe target (-0.5 dBTP), so it normally does nothing; it
// catches what earlier stages did not foresee (the fixed -3 dB mode, tracks
// without a measured peak, EQ boosts). It counts how often and by how much it
// acted, so the display can show it.
//
// Peaks are detected between samples with the True Peak meter's 8x
// interpolator. The gain looks ahead (about 1.3 ms, at least 64 samples): it
// is the minimum of the required gains around each sample, smoothed by a
// moving average, so it is already down when the peak arrives, and returns
// over 50 ms. Both channels get the same gain. The audio is delayed by the
// look-ahead (latencyFrames()); play() compensates, so the output keeps its
// length and timing. Not limiting, the samples pass exactly unchanged.
//
// It never calls for processing by itself (isIdentity() is true): the direct
// path stays bit-perfect. It runs whenever the processing path is taken.

#include <ap/engine/dsp/processing.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace ap::engine {

struct LimiterStats
{
    std::uint64_t events = 0;        // times it started to lower the gain (by more than 0.001 dB)
    std::uint64_t limitedFrames = 0; // frames played lowered by more than 0.001 dB
    double maxReductionDb = 0.0;     // the largest reduction (0 or negative)
};

class SafetyLimiter final : public ProcessingStage
{
public:
    static constexpr double kCeilingDbtp = -0.3;
    static constexpr double kLookAheadSeconds = 0.00133;
    static constexpr double kReleaseSeconds = 0.050;

    std::string name() const override { return "limiter"; }
    void prepare(std::uint32_t sampleRate, std::uint16_t channels) override;
    bool isIdentity() const noexcept override { return true; }
    bool runsWhenProcessing() const noexcept override { return true; }
    std::size_t latencyFrames() const noexcept override { return latency_; }
    void process(std::span<double> samples, std::uint16_t channels) noexcept override;

    LimiterStats stats() const noexcept; // thread-safe; since construction or resetStats()
    void resetStats() noexcept;

private:
    std::uint16_t channels_ = 0;
    std::size_t lookAhead_ = 64;
    std::size_t latency_ = 0;
    double ceiling_ = 1.0;
    double release_ = 0.0;

    // Per channel: the last samples, newest first (doubled for a contiguous window).
    std::vector<double> history_;
    std::size_t historyPos_ = 0;
    double previousInterval_ = 0.0;

    // Required gains (sliding minimum over 2 * lookAhead + 1).
    std::vector<double> req_;
    std::size_t reqPos_ = 0;
    // Sliding minimum, as a monotonic queue of indices into a running count.
    std::vector<std::uint64_t> minIdx_;
    std::vector<double> minVal_;
    std::size_t minHead_ = 0, minTail_ = 0;
    std::uint64_t n_ = 0;
    // Moving average of the minima over lookAhead + 1.
    std::vector<double> box_;
    std::size_t boxPos_ = 0;
    double boxSum_ = 0.0;
    std::size_t boxBelowOne_ = 0;
    // Delayed audio (frames).
    std::vector<double> delay_;
    std::size_t delayPos_ = 0;

    double gain_ = 1.0;
    bool limiting_ = false;

    std::atomic<std::uint64_t> events_{0};
    std::atomic<std::uint64_t> limitedFrames_{0};
    std::atomic<double> minGain_{1.0};
};

} // namespace ap::engine
