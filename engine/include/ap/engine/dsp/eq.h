#pragma once

// Parametric EQ (task 3-6): minimum-phase IIR biquads (the Audio EQ Cookbook
// by Robert Bristow-Johnson) in 64-bit floating point.
//
//  - Preamp, automatic: the combined response is evaluated from 20 Hz to
//    just below Nyquist, and the level is lowered by its largest boost, so the
//    EQ itself never lifts any frequency above 0 dB (CLAUDE.md chapter 4: EQ
//    の最大ブーストはプリアンプに自動で反映).
//  - Changing the settings while playing crossfades from the old filters to
//    the new ones over 10 ms (no click from a sudden change of coefficients).
//  - All bands at 0 dB (and no low- or high-pass): identity, so playback takes
//    the direct path.
//  - Linear phase is not here: it needs long FIR convolution, which comes with
//    the IR convolution in phase 2.

#include <ap/engine/dsp/processing.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ap::engine {

enum class EqFilterType
{
    Peak,
    LowShelf,
    HighShelf,
    LowPass,
    HighPass,
};

struct EqBand
{
    EqFilterType type = EqFilterType::Peak;
    double frequency = 1000.0; // Hz
    double gainDb = 0.0;       // Peak and shelves
    double q = 0.7071067811865476;
    bool enabled = true;

    bool operator==(const EqBand&) const = default;
};

// Normalized biquad: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

// The band's biquad at `sampleRate`; nullopt when it cannot be realised there
// (frequency at or above 0.49 of the sample rate, or not positive; Q not positive).
std::optional<Biquad> designBiquad(const EqBand& band, double sampleRate);

// The bands' combined response at `frequency` in dB (without the preamp).
double eqResponseDb(const std::vector<EqBand>& bands, double frequency, double sampleRate);

// The automatic preamp: minus the largest boost of the combined response
// from 20 Hz to 0.49 of the sample rate (0 if nothing is boosted).
double eqAutoPreampDb(const std::vector<EqBand>& bands, double sampleRate);

// True when the bands leave every sample unchanged (all gains 0 dB, no pass filters).
bool eqIsFlat(const std::vector<EqBand>& bands);

// AutoEQ "ParametricEQ.txt" and Equalizer APO lines:
//   Preamp: -6.2 dB
//   Filter 1: ON PK Fc 105 Hz Gain -3.1 dB Q 0.70
// Types PK/PEQ, LS/LSC, HS/HSC, LP/LPQ, HP/HPQ. OFF filters and unknown lines
// are skipped and reported in `warnings` (Japanese, for display).
struct EqPreset
{
    std::vector<EqBand> bands;
    std::optional<double> filePreampDb; // as written in the file (the stage computes its own)
    std::vector<std::string> warnings;
};
EqPreset parseEqPreset(std::string_view text);
EqPreset loadEqPreset(const std::filesystem::path& path); // throws std::runtime_error

class EqStage final : public ProcessingStage
{
public:
    static constexpr std::size_t kMaxBands = 32;
    static constexpr double kCrossfadeSeconds = 0.010;

    // Thread-safe (the UI thread). Bands beyond kMaxBands are ignored.
    void setBands(std::vector<EqBand> bands);
    std::vector<EqBand> bands() const;
    // The preamp in use (dB, 0 or negative), at the last prepared sample rate.
    double preampDb() const noexcept { return preampDb_; }
    // Bands that could not be realised at the last prepared sample rate.
    std::size_t skippedBands() const noexcept { return skipped_; }

    std::string name() const override { return "eq"; }
    void prepare(std::uint32_t sampleRate, std::uint16_t channels) override;
    // Flat, and any crossfade towards flat has finished (no sudden removal of the filters).
    bool isIdentity() const noexcept override { return flat_ && settled_; }
    std::optional<double> displayGainDb() const noexcept override { return preampDb(); } // the automatic preamp
    void process(std::span<double> samples, std::uint16_t channels) noexcept override;

private:
    struct Design
    {
        std::array<Biquad, kMaxBands> sections{};
        std::size_t count = 0;
        double preamp = 1.0;
    };
    struct Bank
    {
        Design design;
        std::vector<double> state; // per channel and section: x1, x2, y1, y2
    };

    Design designFor(const std::vector<EqBand>& bands, double sampleRate, std::size_t* skipped) const;
    void runBank(Bank& bank, double* frame, std::uint16_t channels) noexcept;

    mutable std::mutex mutex_; // guards bands_ and pending_ (the playback thread only try-locks)
    std::vector<EqBand> bands_;
    Design pending_;
    std::atomic<bool> dirty_{false};
    std::atomic<bool> flat_{true};
    std::atomic<bool> settled_{true}; // the playback thread runs no sections and no crossfade
    std::atomic<double> preampDb_{0.0};
    std::atomic<std::size_t> skipped_{0};
    std::atomic<double> sampleRate_{0.0};

    // Playback thread only.
    std::uint16_t channels_ = 0;
    Bank current_;
    Bank next_;
    std::uint32_t fadeLeft_ = 0;
    std::uint32_t fadeFrames_ = 480;
};

} // namespace ap::engine
