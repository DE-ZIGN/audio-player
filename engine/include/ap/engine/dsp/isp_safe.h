#pragma once

// ISP-safe gain (task 3-5, CLAUDE.md chapter 4): lowers the level by a linear
// gain so the true peak stays at or below -0.5 dBTP, so a DAC's oversampling
// filter has room for the peaks between samples.
//
//  - Measured: by max(0, true peak + earlier gains + 0.5 dB), from the track's
//    true peak measured by the engine (the album's in album mode). The earlier
//    stages' gains (ReplayGain, volume) are taken into account, so what the
//    volume already lowered is not lowered again. A track without a measured
//    peak gets the fixed reduction.
//  - Fixed: always -3 dB.
// The reduction switches at a track's first sample and ramps over 10 ms when
// an earlier gain changes. The safety limiter after it catches what is left.

#include <ap/engine/dsp/gain_ramp.h>
#include <ap/engine/dsp/processing.h>

#include <atomic>

namespace ap::engine {

enum class IspSafeMode
{
    Off,
    Measured,
    Fixed,
};

class IspSafeStage final : public ProcessingStage
{
public:
    static constexpr double kTargetDbtp = -0.5;
    static constexpr double kFixedDb = -3.0;

    void setMode(IspSafeMode mode) noexcept; // thread-safe
    IspSafeMode mode() const noexcept { return mode_; }
    // Use the album's true peak (with ReplayGain in album mode, so the album's
    // tracks keep their relative levels). Thread-safe.
    void setUseAlbumPeak(bool album) noexcept;

    // The reduction it applies now, in dB (0 or negative).
    double gainDb() const noexcept;

    std::string name() const override { return "isp-safe"; }
    void prepare(std::uint32_t sampleRate, std::uint16_t) override { ramp_.prepare(sampleRate); }
    bool isIdentity() const noexcept override { return ramp_.isUnity(); }
    double peakGain() const noexcept override { return ramp_.target(); }
    void setUpstreamGain(double gain) noexcept override;
    void trackStarted(const TrackInfo& track) noexcept override;
    void process(std::span<double> samples, std::uint16_t channels) noexcept override
    {
        ramp_.apply(samples, channels);
    }

private:
    void updateTarget() noexcept;

    std::atomic<IspSafeMode> mode_{IspSafeMode::Off};
    std::atomic<bool> useAlbum_{false};
    std::atomic<double> upstream_{1.0};
    std::atomic<bool> hasPeak_{false};
    std::atomic<double> trackPeak_{0.0};
    std::atomic<bool> hasAlbumPeak_{false};
    std::atomic<double> albumPeak_{0.0};
    dsp::GainRamp ramp_;
};

} // namespace ap::engine
