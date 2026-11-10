#include <ap/engine/dsp/volume.h>

#include <algorithm>
#include <cmath>

namespace ap::engine {

void VolumeStage::setGainDb(double db) noexcept
{
    if (std::isnan(db))
        return;
    db = std::min(db, 0.0);
    targetDb_ = db;
    // Exactly 1.0 at 0 dB, so the stage is recognised as identity.
    target_ = db == 0.0 ? 1.0 : std::pow(10.0, db / 20.0);
}

double VolumeStage::gainDb() const noexcept
{
    return targetDb_;
}

void VolumeStage::prepare(std::uint32_t sampleRate, std::uint16_t)
{
    // A new output session starts at the target gain: there is nothing to ramp from.
    rampFrames_ = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(sampleRate * kRampSeconds)));
    rampTarget_ = target_;
    current_ = rampTarget_;
    rampLeft_ = 0;
}

bool VolumeStage::isIdentity() const noexcept
{
    return target_.load(std::memory_order_relaxed) == 1.0 && current_.load(std::memory_order_relaxed) == 1.0;
}

void VolumeStage::process(std::span<double> samples, std::uint16_t channels) noexcept
{
    const double target = target_.load(std::memory_order_relaxed);
    double g = current_.load(std::memory_order_relaxed);
    if (target != rampTarget_)
    {
        rampTarget_ = target;
        rampLeft_ = rampFrames_;
        step_ = (target - g) / rampFrames_;
    }

    const std::size_t frames = samples.size() / channels;
    double* s = samples.data();
    for (std::size_t f = 0; f < frames; ++f)
    {
        if (rampLeft_ > 0)
            g = --rampLeft_ == 0 ? rampTarget_ : g + step_;
        for (std::uint16_t c = 0; c < channels; ++c)
            *s++ *= g;
    }
    current_.store(g, std::memory_order_relaxed);
}

} // namespace ap::engine
