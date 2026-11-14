#pragma once

// The tube stage (task 9-1; docs/dsp-modeling-research.md, quality-measures 13).
// Waveshaping with a transfer curve worked out from tube models and load lines
// (not a circuit simulation: the circuit's DC operating point and its load line
// are solved once, into a table), plus a simple time element derived from the
// circuit's time constants. Nothing here is claimed to sound better; what it
// does and what it measures is what is said (CLAUDE.md chapter 1).
//
// The types:
//   SE 300B     one 300B triode (Koren's triode equations), single-ended,
//               self (cathode) bias: mostly 2nd harmonic.
//   PP EL34     two EL34 pentodes (Koren's pentode equations) in push-pull,
//               class AB, fixed bias: the halves' even harmonics cancel,
//               mostly 3rd.
//   PP class A  the same pair biased hotter (class A): less distortion at the
//               same drive.
//
// Level reference (quality-measures 13): with DRIVE at 0 dB a 0 dBFS peak swings
// the grid from the bias point exactly to 0 V (where grid current starts;
// beyond it the swing is softly limited). DRIVE scales that swing. The stage
// is normalised to unity gain for small signals, so DRIVE changes how much
// the curve bends, not the level.
//
// Aliasing: the curve runs oversampled (4x up to 50 kHz, 2x up to 100 kHz;
// Kaiser-windowed FIR, passband to 0.45 fs, stopband from 0.55 fs, about
// 100 dB) with first-order antiderivative anti-aliasing (ADAA). Above 100 kHz
// neither (what would fold back lies above 90 kHz).
//
// The time element: the curve's bending makes the average current change with
// the signal (rectification). Its slow part shifts the operating point:
//   - SE 300B (cathode bias): the cathode voltage follows the average current
//     (time constant from Rk and the bypass capacitor), moving the bias;
//   - every type: the supply sags with the average current (supply impedance
//     and reservoir capacitor), lowering the gain slightly.
// Only the current's nonlinear part is used, so the stage's frequency response
// stays flat (a self-biased stage would otherwise also lose low-frequency gain).
//
// Switching the stage on or off changes its latency (the oversampling
// filters), so the player reopens the output (see ProcessingChain::needsReprepare).
// The type and DRIVE change smoothly (20 ms) without a reopen.
//
// The type that plays (task 9-7): the track's, when the genre table or an
// album's choice gave it one (TrackInfo::tubeType, at the track's first
// sample), else the manual type (setType).

#include <ap/engine/dsp/processing.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace ap::engine {

enum class TubeType
{
    Se300B,
    PpEl34,
    PpClassA,
};

// "se300b", "ppel34", "ppclassa": in the settings file, the status and the page.
const char* tubeTypeKey(TubeType type) noexcept;
std::optional<TubeType> tubeTypeFrom(std::string_view key) noexcept;

// The transfer curve of one type, worked out once from the tube model.
class TubeCurve
{
public:
    static const TubeCurve& forType(TubeType type);

    struct Point
    {
        double vout;         // output (volts across the load, 0 at idle; positive for a positive grid swing)
        double totalCurrent; // all the tubes' plate current (A)
    };
    // The model itself, solved at a grid swing `vin` (volts from the bias point).
    Point solve(double vin) const;

    // From the table (linear interpolation; the antiderivative is exact for it).
    double out(double vin) const noexcept;
    double antiderivative(double vin) const noexcept;
    // The total current's nonlinear part: what the curve adds to the idle
    // current beyond the straight line through it (A).
    double nonlinearCurrent(double vin) const noexcept;

    double gridToZero() const noexcept { return gridToZero_; } // |bias|: the swing to 0 V grid
    double smallSignalGain() const noexcept { return gain_; }  // d vout / d vin at idle
    double range() const noexcept { return range_; }           // the table covers +-range volts

    // The time element's constants.
    double cathodeOhms = 0.0;    // 0: fixed bias (no bias shift)
    double biasSeconds = 0.1;
    double supplyOhms = 50.0;
    double supplyVolts = 450.0;
    double sagSeconds = 0.05;

private:
    friend struct TubeCurveBuilder;
    TubeType type_{};
    double gridToZero_ = 1.0, gain_ = 1.0, range_ = 1.0, step_ = 1.0, currentSlope_ = 0.0, idleCurrent_ = 0.0;
    std::vector<double> out_, integral_, current_;
    // The circuit (for solve()).
    struct Circuit;
    std::shared_ptr<const Circuit> circuit_;
};

class TubeStage final : public ProcessingStage
{
public:
    static constexpr double kMinDriveDb = -30.0;
    static constexpr double kMaxDriveDb = 6.0;
    static constexpr double kDefaultDriveDb = -12.0;
    static constexpr double kSmoothingSeconds = 0.020;

    TubeStage();
    ~TubeStage() override;

    // Thread-safe.
    void setEnabled(bool on) noexcept { enabled_ = on; }
    bool enabled() const noexcept { return enabled_; }
    void setType(TubeType t) noexcept { type_ = t; } // the manual type
    TubeType type() const noexcept { return type_; }
    // The current track's chosen type (task 9-7); none: the manual type. Set
    // by trackStarted; the app sets it too when the choice changes mid-track.
    void setTrackType(std::optional<TubeType> t) noexcept { trackType_ = t ? static_cast<int>(*t) : -1; }
    TubeType playingType() const noexcept
    {
        const int t = trackType_;
        return t < 0 ? type_.load() : static_cast<TubeType>(t);
    }
    void setDriveDb(double db) noexcept;
    double driveDb() const noexcept { return driveDb_; }

    // 4, 2 or 1 at the rate it was prepared for.
    unsigned oversampling() const noexcept { return factor_; }

    std::string name() const override { return "tube"; }
    void prepare(std::uint32_t sampleRate, std::uint16_t channels) override;
    bool isIdentity() const noexcept override { return !enabled_; }
    std::size_t latencyFrames() const noexcept override { return enabled_ ? latency_ : 0; }
    // For the path display: DRIVE, and the type.
    std::optional<double> displayGainDb() const noexcept override { return driveDb_.load(); }
    std::string displayDetail() const override { return tubeTypeKey(playingType()); }
    void trackStarted(const TrackInfo& track) noexcept override { setTrackType(track.tubeType); }
    void process(std::span<double> samples, std::uint16_t channels) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> enabled_{false};
    std::atomic<TubeType> type_{TubeType::Se300B};
    std::atomic<int> trackType_{-1};
    std::atomic<double> driveDb_{kDefaultDriveDb};
    unsigned factor_ = 1;
    std::size_t latency_ = 0;
};

} // namespace ap::engine
