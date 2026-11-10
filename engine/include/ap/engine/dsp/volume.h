#pragma once

// Digital volume (task 3-2): a gain of 0 dB or less on the 64-bit path.
//
// At 0 dB (100%) the stage is identity, so playback takes the direct path and
// stays bit-perfect. A change ramps linearly over 10 ms so the waveform has no
// step (a step would be heard as a click). Gains above 0 dB are not offered:
// they would clip, and headroom is the job of the ISP-safe gain (task 3-5).

#include <ap/engine/dsp/processing.h>

#include <atomic>
#include <cstdint>

namespace ap::engine {

class VolumeStage final : public ProcessingStage
{
public:
    static constexpr double kRampSeconds = 0.010;

    // Thread-safe. Values above 0 dB are clamped to 0 dB; -infinity mutes.
    void setGainDb(double db) noexcept;
    double gainDb() const noexcept;

    std::string name() const override { return "volume"; }
    void prepare(std::uint32_t sampleRate, std::uint16_t channels) override;
    // True at 0 dB once any ramp towards it has finished.
    bool isIdentity() const noexcept override;
    void process(std::span<double> samples, std::uint16_t channels) noexcept override;

private:
    std::atomic<double> targetDb_{0.0};
    std::atomic<double> target_{1.0};  // linear
    std::atomic<double> current_{1.0}; // linear, written by the playback thread

    // Playback thread only.
    double rampTarget_ = 1.0;
    double step_ = 0.0;
    std::uint32_t rampLeft_ = 0;
    std::uint32_t rampFrames_ = 480;
};

} // namespace ap::engine
