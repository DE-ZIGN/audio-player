#include <ap/engine/dsp/gain_ramp.h>

#include <algorithm>
#include <cmath>

namespace ap::engine::dsp {

void GainRamp::prepare(std::uint32_t sampleRate) noexcept
{
    rampFrames_ = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(sampleRate * kRampSeconds)));
    rampTarget_ = target_;
    current_ = rampTarget_;
    rampLeft_ = 0;
    jump_ = false;
}

void GainRamp::apply(std::span<double> samples, std::uint16_t channels) noexcept
{
    const double target = target_.load(std::memory_order_relaxed);
    double g = current_.load(std::memory_order_relaxed);
    if (jump_.exchange(false))
    {
        g = target;
        rampTarget_ = target;
        rampLeft_ = 0;
    }
    else if (target != rampTarget_)
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

} // namespace ap::engine::dsp
