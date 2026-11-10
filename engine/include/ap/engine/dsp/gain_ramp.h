#pragma once

// A gain that moves to a new value over 10 ms (no step in the waveform), or
// jumps to it at the start of a track. Shared by the gain stages (volume,
// ReplayGain, ISP-safe). The target may be set from any thread; apply() runs
// on the playback thread.

#include <atomic>
#include <cstdint>
#include <span>

namespace ap::engine::dsp {

class GainRamp
{
public:
    static constexpr double kRampSeconds = 0.010;

    // Thread-safe.
    void setTarget(double gain) noexcept { target_ = gain; }
    double target() const noexcept { return target_.load(std::memory_order_relaxed); }
    // The next apply() starts at the target without a ramp (a new track).
    void jump() noexcept { jump_ = true; }

    // True when the gain is 1 and no change is pending.
    bool isUnity() const noexcept
    {
        return target_.load(std::memory_order_relaxed) == 1.0 && current_.load(std::memory_order_relaxed) == 1.0;
    }

    // A new output session starts at the target.
    void prepare(std::uint32_t sampleRate) noexcept;

    void apply(std::span<double> samples, std::uint16_t channels) noexcept;

private:
    std::atomic<double> target_{1.0};
    std::atomic<double> current_{1.0}; // written by the playback thread
    std::atomic<bool> jump_{false};

    // Playback thread only.
    double rampTarget_ = 1.0;
    double step_ = 0.0;
    std::uint32_t rampLeft_ = 0;
    std::uint32_t rampFrames_ = 480;
};

} // namespace ap::engine::dsp
