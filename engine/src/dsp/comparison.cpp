#include <ap/engine/dsp/comparison.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ap::engine {

namespace {

// ITU-R BS.1770 K-weighting at any sample rate: the high-shelf pre-filter and
// the RLB high-pass, from their analog design parameters (the same derivation
// libebur128 uses).
void designKWeighting(double fs, std::array<double, 5>& shelf, std::array<double, 5>& highPass)
{
    {
        const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(std::numbers::pi * f0 / fs);
        const double vh = std::pow(10.0, g / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        shelf = {(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(std::numbers::pi * f0 / fs);
        const double a0 = 1.0 + k / q + k * k;
        highPass = {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
}

double runBiquad(const std::array<double, 5>& c, double* st, double x) noexcept
{
    const double y = c[0] * x + c[1] * st[0] + c[2] * st[1] - c[3] * st[2] - c[4] * st[3];
    st[1] = st[0];
    st[0] = x;
    st[3] = st[2];
    st[2] = y;
    return y;
}

} // namespace

double AbComparison::KWeighting::weightedSquare(const double* frame, std::uint16_t channels) noexcept
{
    double sum = 0.0;
    for (std::uint16_t c = 0; c < channels; ++c)
    {
        double* st = state.data() + static_cast<std::size_t>(c) * 8;
        const double y = runBiquad(highPass, st + 4, runBiquad(shelf, st, frame[c]));
        sum += y * y; // channel weights 1 (front channels; surround weighting is not needed for a ratio)
    }
    return sum;
}

void AbComparison::prepare(std::uint32_t sampleRate, std::uint16_t channels, std::size_t chainLatency)
{
    channels_ = channels;
    limiter_.prepare(sampleRate, channels);
    match_.setTarget(1.0);
    match_.prepare(sampleRate);
    matchDb_ = 0.0;
    levelDifferenceDb_ = 0.0;
    for (auto* k : {&kOriginal_, &kProcessed_, &kOut_})
    {
        designKWeighting(sampleRate, k->shelf, k->highPass);
        k->state.assign(static_cast<std::size_t>(channels) * 8, 0.0);
        k->energy = 0.0;
    }
    updateEvery_ = std::max<std::uint32_t>(1, sampleRate / 100); // every 10 ms
    untilUpdate_ = updateEvery_;
    delayFrames_ = chainLatency + limiter_.latencyFrames() + 1;
    delay_.assign(delayFrames_ * channels, 0.0);
    delayPos_ = 0;
    mix_ = selected_ == ComparisonSide::Processed ? 1.0 : 0.0;
    mixStep_ = 1.0 / std::max(1.0, sampleRate * kCrossfadeSeconds);
}

void AbComparison::process(std::span<const double> original, std::span<double> processed,
                           std::uint16_t channels) noexcept
{
    if (channels != channels_ || channels == 0 || original.size() != processed.size())
        return;
    const std::size_t frames = processed.size() / channels;

    // 1. Loudness of both sides (before the match), and the matching gain.
    for (std::size_t f = 0; f < frames; ++f)
    {
        kOriginal_.energy += kOriginal_.weightedSquare(original.data() + f * channels, channels);
        kProcessed_.energy += kProcessed_.weightedSquare(processed.data() + f * channels, channels);
        if (--untilUpdate_ == 0)
        {
            untilUpdate_ = updateEvery_;
            if (kOriginal_.energy > 1e-20 && kProcessed_.energy > 1e-20)
            {
                const double db =
                    std::clamp(10.0 * std::log10(kOriginal_.energy / kProcessed_.energy), -kMaxMatchDb, kMaxMatchDb);
                matchDb_.store(db, std::memory_order_relaxed);
                match_.setTarget(std::pow(10.0, db / 20.0));
            }
        }
    }

    // 2. The processed side: matched, then limited; how loud it comes out.
    match_.apply(processed, channels);
    limiter_.process(processed, channels);
    for (std::size_t f = 0; f < frames; ++f)
        kOut_.energy += kOut_.weightedSquare(processed.data() + f * channels, channels);
    if (kOriginal_.energy > 1e-20 && kOut_.energy > 1e-20)
        levelDifferenceDb_.store(10.0 * std::log10(kOut_.energy / kOriginal_.energy), std::memory_order_relaxed);

    // 3. The original side, delayed to line up, and the crossfade.
    const double target = selected_.load(std::memory_order_relaxed) == ComparisonSide::Processed ? 1.0 : 0.0;
    double* p = processed.data();
    const double* o = original.data();
    for (std::size_t f = 0; f < frames; ++f, p += channels, o += channels)
    {
        double* slot = delay_.data() + delayPos_ * channels;
        const double* oldest = delay_.data() + ((delayPos_ + 1) % delayFrames_) * channels;
        for (std::uint16_t c = 0; c < channels; ++c)
            slot[c] = o[c];

        if (mix_ < target)
            mix_ = std::min(target, mix_ + mixStep_);
        else if (mix_ > target)
            mix_ = std::max(target, mix_ - mixStep_);
        if (mix_ == 1.0)
            ; // processed as it is
        else if (mix_ == 0.0)
            for (std::uint16_t c = 0; c < channels; ++c)
                p[c] = oldest[c];
        else
        {
            const double w = 0.5 - 0.5 * std::cos(std::numbers::pi * mix_); // raised cosine
            for (std::uint16_t c = 0; c < channels; ++c)
                p[c] = oldest[c] + (p[c] - oldest[c]) * w;
        }
        delayPos_ = (delayPos_ + 1) % delayFrames_;
    }
}

} // namespace ap::engine
