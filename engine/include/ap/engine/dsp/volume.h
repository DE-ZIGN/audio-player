#pragma once

// Digital volume (task 3-2): a gain of 0 dB or less on the 64-bit path.
//
// At 0 dB (100%) the stage is identity, so playback takes the direct path and
// stays bit-perfect. A change ramps linearly over 10 ms so the waveform has no
// step (a step would be heard as a click). Gains above 0 dB are not offered:
// they would clip, and headroom is the job of the ISP-safe gain (task 3-5).

#include <ap/engine/dsp/gain_ramp.h>
#include <ap/engine/dsp/processing.h>

#include <atomic>
#include <cstdint>

namespace ap::engine {

class VolumeStage final : public ProcessingStage
{
public:
    static constexpr double kRampSeconds = dsp::GainRamp::kRampSeconds;

    // Thread-safe. Values above 0 dB are clamped to 0 dB; -infinity mutes.
    void setGainDb(double db) noexcept;
    double gainDb() const noexcept { return targetDb_; }

    std::string name() const override { return "volume"; }
    void prepare(std::uint32_t sampleRate, std::uint16_t) override { ramp_.prepare(sampleRate); }
    // True at 0 dB once any ramp towards it has finished.
    bool isIdentity() const noexcept override { return ramp_.isUnity(); }
    double peakGain() const noexcept override { return ramp_.target(); }
    std::optional<double> displayGainDb() const noexcept override { return gainDb(); }
    void process(std::span<double> samples, std::uint16_t channels) noexcept override
    {
        ramp_.apply(samples, channels);
    }

private:
    std::atomic<double> targetDb_{0.0};
    dsp::GainRamp ramp_;
};

} // namespace ap::engine
