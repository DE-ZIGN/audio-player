#pragma once

// Listening comparison between the original and the processed sound (task
// 3-7; decided with the user: a separate comparison mode, the processed side
// matched to the original).
//
// Normal switching between 原音モード and 音作りモード reopens the output (the
// original is then truly bit-perfect). For comparing, the output is opened in
// the processing format and both sounds are made from the same input:
//   original:  the input, unchanged, delayed to line up with the processed side
//   processed: the processing chain, then a matching gain, then its own
//              safety limiter (the match may lift it back above the ceiling)
// Switching crossfades over 5 ms. The matching gain brings the processed
// side's loudness to the original's: both are K-weighted (ITU-R BS.1770) and
// their energies summed from the start of the comparison; the ratio gives the
// gain, within 0.1 dB after a few seconds (gain-only differences at once).
// On this path the original side is not bit-perfect (it is quantized with
// dither like everything on the processing path), only its values are kept.
//
// When the original's peaks are above the limiter's ceiling (a loud master),
// the matched processed side goes over it and the limiter lowers it, so the
// two cannot be equally loud without clipping. The match is not pushed
// further (that would run away); the remaining difference after the limiter
// is measured and reported (levelDifferenceDb), e.g. for the ABX's 0.1 dB check.

#include <ap/engine/dsp/gain_ramp.h>
#include <ap/engine/dsp/limiter.h>
#include <ap/engine/pcm_source.h>
#include <ap/engine/track_info.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ap::engine {

enum class ComparisonSide
{
    Original,
    Processed,
};

class AbComparison
{
public:
    static constexpr double kCrossfadeSeconds = 0.005;
    static constexpr double kMaxMatchDb = 24.0;

    // Thread-safe.
    void select(ComparisonSide side) noexcept { selected_ = side; }
    // A matching gain worked out beforehand for the whole track (see
    // measureMatchDb), used instead of the running estimate: with an EQ the
    // running estimate moves while the music changes. nullopt: running estimate.
    void setFixedMatchDb(std::optional<double> db) noexcept
    {
        fixedDb_ = db.value_or(0.0);
        fixed_ = db.has_value();
    }
    ComparisonSide selected() const noexcept { return selected_; }
    // The gain applied to the processed side now (dB).
    double matchDb() const noexcept { return matchDb_; }
    static constexpr double kLevelWarmUpSeconds = 3.0;

    // The processed side's loudness as it comes out (after the matching gain
    // and the limiter) minus the original's, both lined up in time, from
    // kLevelWarmUpSeconds after the comparison started (dB). Before that it is
    // not ready: the first moments (the processed side still delayed, the
    // filters settling) say nothing about the match.
    double levelDifferenceDb() const noexcept { return levelDifferenceDb_; }
    bool levelDifferenceReady() const noexcept { return levelReady_; }
    // The comparison's own limiter on the processed side.
    LimiterStats limiterStats() const noexcept { return limiter_.stats(); }

    // Playback thread. `chainLatency`: frames by which the chain delays the processed side.
    void prepare(std::uint32_t sampleRate, std::uint16_t channels, std::size_t chainLatency);
    std::size_t latencyFrames() const noexcept { return limiter_.latencyFrames(); }

    // original: the chain's input; processed: the chain's output (both the same
    // length). Writes the mix into processed. Allocates only on the first call.
    void process(std::span<const double> original, std::span<double> processed, std::uint16_t channels) noexcept;

private:
    struct KWeighting
    {
        std::array<double, 5> shelf{}, highPass{}; // b0 b1 b2 a1 a2
        std::vector<double> state;                 // per channel: 4 for each filter
        double energy = 0.0;
        double weightedSquare(const double* frame, std::uint16_t channels) noexcept;
    };

    std::atomic<ComparisonSide> selected_{ComparisonSide::Processed};
    std::atomic<bool> fixed_{false};
    std::atomic<double> fixedDb_{0.0};
    std::atomic<double> matchDb_{0.0};
    std::atomic<double> levelDifferenceDb_{0.0};
    std::atomic<bool> levelReady_{false};

    std::uint16_t channels_ = 0;
    SafetyLimiter limiter_;
    dsp::GainRamp match_;
    KWeighting kOriginal_, kProcessed_, kOut_, kAligned_; // kAligned_: the delayed original
    std::uint64_t warmUpLeft_ = 0;
    std::uint32_t updateEvery_ = 4800;
    std::uint32_t untilUpdate_ = 0;
    // Original side, delayed by the chain's latency plus the comparison limiter's.
    std::vector<double> delay_;
    std::size_t delayFrames_ = 1;
    std::size_t delayPos_ = 0;
    double mix_ = 1.0; // 0 = original, 1 = processed
    double mixStep_ = 0.0;
};

class ProcessingChain;

// The matching gain for what will be compared: reads `source` from its
// current position, to the end or for `frames` frames, through `chain` (a
// separate chain with the same settings as the one that will play, not the
// playing one) and returns how much the processed side must be lifted to be as
// loud as the original (dB, K-weighted energies). With an EQ the answer
// depends on the music, so it is worked out over the same part that is
// compared. Throws std::invalid_argument for DoP.
double measureMatchDb(PcmSource& source, ProcessingChain& chain, const TrackInfo& track,
                      std::optional<std::uint64_t> frames = std::nullopt);

} // namespace ap::engine
