#pragma once

// True Peak (task 3-3): the largest magnitude of the band-limited waveform,
// estimated by 8x oversampling (CLAUDE.md chapter 4: 8 倍以上; tag peak values
// are not trusted).
//
// A polyphase windowed-sinc interpolator (Kaiser, 48 taps per phase, cutoff at
// the input's Nyquist frequency) computes 7 values between every two samples.
// Its phase 0 is exactly the input, so the true peak is never below the sample
// peak. A sine can read low by at most 1 - cos(pi f / (8 fs)) (the grid may
// miss its crest): 0.042 dB at fs/4, 0.14 dB at 0.45 fs. The ISP test signal
// (fs/4, 45 degrees) peaks on the grid and reads +3.01 dB over its samples.

#include <ap/engine/pcm_source.h>

#include <cstdint>
#include <span>
#include <vector>

namespace ap::engine {

class TruePeakMeter
{
public:
    static constexpr int kOversampling = 8;
    static constexpr int kTapsPerPhase = 48;

    explicit TruePeakMeter(std::uint16_t channels);

    // Interleaved samples, full scale = +-1.0; samples.size() is a multiple of channels().
    void process(std::span<const double> samples);

    // Lets the interpolator run past the last sample (as if zeros followed), so
    // a peak at the very end is not missed. Call once at the end.
    void finish();

    void reset();

    std::uint16_t channels() const noexcept { return static_cast<std::uint16_t>(channels_.size()); }

    double truePeak() const noexcept;                      // linear, all channels
    double truePeak(std::uint16_t channel) const noexcept; // linear
    double truePeakDb() const noexcept;                    // dBTP (-inf for silence)
    double truePeakDb(std::uint16_t channel) const noexcept;
    double samplePeak() const noexcept;
    double samplePeak(std::uint16_t channel) const noexcept;
    // Where the true peak is, in frames from the first sample processed since
    // reset() (fractional: between samples), for the channel with the largest peak.
    double truePeakPosition() const noexcept;
    double truePeakPosition(std::uint16_t channel) const noexcept;

private:
    struct Channel
    {
        std::vector<double> history; // twice the window, so the window is always contiguous
        std::size_t pos = 0;
        std::uint64_t pushed = 0; // samples pushed since reset (including finish()'s zeros)
        double truePeak = 0.0;
        double truePeakPosition = 0.0;
        double samplePeak = 0.0;
    };

    void push(Channel& c, double x) noexcept;

    std::vector<Channel> channels_;
    bool finished_ = false;
};

namespace dsp {
// The interpolator shared with the safety limiter. `newestFirst` holds the
// last kTruePeakWindow samples of one channel, newest first; the result is
// the largest magnitude of the oversampled values between the samples
// newestFirst[kTruePeakDelay] and newestFirst[kTruePeakDelay - 1] (that is,
// on [t, t + 1) where t is the sample kTruePeakDelay frames ago).
inline constexpr int kTruePeakWindow = TruePeakMeter::kTapsPerPhase + 1;
inline constexpr int kTruePeakDelay = TruePeakMeter::kTapsPerPhase / 2;
double interpolatedPeak(const double* newestFirst) noexcept;
} // namespace dsp

// Peaks of a whole source, read from its current position to the end.
struct PeakAnalysis
{
    std::vector<double> samplePeak; // per channel, linear
    std::vector<double> truePeak;   // per channel, linear
    double samplePeakMax = 0.0;
    double truePeakMax = 0.0;
    double truePeakPosition = 0.0; // frames from the start of the read (fractional)
    std::uint64_t frames = 0;
};

// Throws std::invalid_argument for DoP (not PCM audio).
PeakAnalysis analyzePeaks(PcmSource& source);

} // namespace ap::engine
