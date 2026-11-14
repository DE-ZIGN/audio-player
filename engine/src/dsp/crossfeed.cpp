#include <ap/engine/dsp/crossfeed.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ap::engine {

namespace {

struct LevelNumbers
{
    double cutoffHz, feedDb;
};

LevelNumbers numbers(CrossfeedLevel l) noexcept
{
    switch (l)
    {
        case CrossfeedLevel::Default: return {700.0, 4.5};
        case CrossfeedLevel::Cmoy:    return {700.0, 6.0};
        case CrossfeedLevel::Jmeier:  return {650.0, 9.5};
    }
    return {700.0, 4.5};
}

} // namespace

const char* crossfeedLevelKey(CrossfeedLevel level) noexcept
{
    switch (level)
    {
        case CrossfeedLevel::Default: return "default";
        case CrossfeedLevel::Cmoy:    return "cmoy";
        case CrossfeedLevel::Jmeier:  return "jmeier";
    }
    return "default";
}

std::optional<CrossfeedLevel> crossfeedLevelFrom(std::string_view key) noexcept
{
    for (const auto l : {CrossfeedLevel::Default, CrossfeedLevel::Cmoy, CrossfeedLevel::Jmeier})
        if (key == crossfeedLevelKey(l))
            return l;
    return std::nullopt;
}

double CrossfeedStage::cutoffHz() const noexcept
{
    return numbers(level_).cutoffHz;
}

double CrossfeedStage::feedDb() const noexcept
{
    return numbers(level_).feedDb;
}

// bs2b's design: the crossfed low-pass sits at -(5/6 feed + 3) dB, the
// direct path's shelf at (feed/6 - 3) dB below unity at low frequencies, so
// their ratio at low frequencies is the feed; the shelf's corner is placed so
// its high-frequency gain balances; `gain` brings a centred sound to 0 dB at
// low frequencies.
void CrossfeedStage::Filter::design(CrossfeedLevel level, double sampleRate) noexcept
{
    const auto [fcLo, feed] = numbers(level);
    const double gbLo = feed * -5.0 / 6.0 - 3.0;
    const double gbHi = feed / 6.0 - 3.0;
    const double gLo = std::pow(10.0, gbLo / 20.0);
    const double gHi = 1.0 - std::pow(10.0, gbHi / 20.0);
    const double fcHi = fcLo * std::pow(2.0, (gbLo - 20.0 * std::log10(gHi)) / 12.0);

    double x = std::exp(-2.0 * std::numbers::pi * fcLo / sampleRate);
    b1Lo = x;
    a0Lo = gLo * (1.0 - x);
    x = std::exp(-2.0 * std::numbers::pi * fcHi / sampleRate);
    b1Hi = x;
    a0Hi = 1.0 - gHi * (1.0 - x);
    a1Hi = -x;
    gain = 1.0 / (1.0 - gHi + gLo);
}

void CrossfeedStage::Filter::run(const double in[2], double out[2]) noexcept
{
    for (int c = 0; c < 2; ++c)
    {
        lo[c] = a0Lo * in[c] + b1Lo * lo[c];
        hi[c] = a0Hi * in[c] + a1Hi * asis[c] + b1Hi * hi[c];
        asis[c] = in[c];
    }
    out[0] = (hi[0] + lo[1]) * gain;
    out[1] = (hi[1] + lo[0]) * gain;
}

void CrossfeedStage::prepare(std::uint32_t sampleRate, std::uint16_t channels)
{
    rate_ = sampleRate;
    channels_ = channels;
    designed_ = level_;
    current_ = Filter{};
    current_.design(designed_, rate_);
    levelFade_ = 0;
    fadeFrames_ = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(sampleRate * kFadeSeconds));
    mix_ = enabled_ ? 1.0 : 0.0;
    settled_ = !enabled_;
}

void CrossfeedStage::process(std::span<double> samples, std::uint16_t channels) noexcept
{
    if (channels != 2 || channels_ != 2)
        return;
    const bool on = enabled_;
    if (on && mix_ == 0.0)
    {
        // Starting from dry: begin from a clean state (the stage was skipped while off).
        current_ = Filter{};
        current_.design(level_, rate_);
        designed_ = level_;
        levelFade_ = 0;
    }
    if (on)
        settled_ = false;
    const CrossfeedLevel wanted = level_;
    if (wanted != designed_ && levelFade_ == 0)
    {
        // A new level: run both for 10 ms and crossfade (a sudden change of
        // coefficients would step the waveform).
        next_ = current_;
        next_.design(wanted, rate_);
        designed_ = wanted;
        levelFade_ = fadeFrames_;
    }
    const double step = 1.0 / fadeFrames_;
    const std::size_t frames = samples.size() / 2;
    for (std::size_t f = 0; f < frames; ++f)
    {
        double* s = samples.data() + f * 2;
        if (mix_ == 0.0 && !on)
            continue; // dry
        double wet[2];
        current_.run(s, wet);
        if (levelFade_ > 0)
        {
            double other[2];
            next_.run(s, other);
            const double t = 1.0 - static_cast<double>(levelFade_) / fadeFrames_; // 0 -> 1
            const double w = 0.5 - 0.5 * std::cos(std::numbers::pi * t);
            wet[0] += (other[0] - wet[0]) * w;
            wet[1] += (other[1] - wet[1]) * w;
            if (--levelFade_ == 0)
                current_ = next_;
        }
        mix_ = on ? std::min(1.0, mix_ + step) : std::max(0.0, mix_ - step);
        const double w = 0.5 - 0.5 * std::cos(std::numbers::pi * mix_);
        s[0] += (wet[0] - s[0]) * w;
        s[1] += (wet[1] - s[1]) * w;
    }
    if (!on && mix_ == 0.0)
        settled_ = true;
}

} // namespace ap::engine
