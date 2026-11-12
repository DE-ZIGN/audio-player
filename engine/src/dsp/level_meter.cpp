#include <ap/engine/dsp/level_meter.h>

#include <ap/engine/dsp/true_peak.h>

#include <ebur128.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace ap::engine {

namespace {

constexpr std::size_t W = static_cast<std::size_t>(dsp::kTruePeakWindow);

ebur128_state* st(void* p)
{
    return static_cast<ebur128_state*>(p);
}

void* makeState(std::uint32_t rate, std::uint16_t channels, int mode)
{
    ebur128_state* s = ebur128_init(channels, rate, mode);
    if (!s)
        throw std::runtime_error("ebur128_init failed");
    if (channels == 1)
        ebur128_set_channel(s, 0, EBUR128_DUAL_MONO); // as in the analysis (LoudnessMeter)
    return s;
}

void destroyState(void*& p)
{
    if (auto* s = st(p))
    {
        ebur128_destroy(&s);
        p = nullptr;
    }
}

double finiteOrMinusInf(double lufs)
{
    return std::isfinite(lufs) ? lufs : -std::numeric_limits<double>::infinity();
}

} // namespace

LevelMeter::LevelMeter(std::uint32_t sampleRate, std::uint16_t channels)
    : rate_(sampleRate), channels_(channels), ch_(channels)
{
    if (sampleRate == 0 || channels == 0)
        throw std::invalid_argument("LevelMeter needs a sample rate and channels");
    for (auto& c : ch_)
        c.history.assign(2 * W, 0.0);
    // Blocks of 0.1 s: the reading takes the last complete block and the one
    // being filled, so it covers the last 0.1 to 0.2 s.
    blockFrames_ = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::llround(kRecentSeconds * rate_)));
    silence_.assign(channels, 0.0);
    // Two equal one-pole stages reach 99% of a step at 6.64 time constants;
    // 6.7 leaves room for the ripple of a rectified tone (99% by 300 ms).
    vuCoef_ = std::exp(-1.0 / (kVuSeconds / 6.7 * rate_));
    windows_ = makeState(rate_, channels_, EBUR128_MODE_S); // includes M
    try
    {
        track_ = makeState(rate_, channels_, EBUR128_MODE_I);
    }
    catch (...)
    {
        destroyState(windows_);
        throw;
    }
}

LevelMeter::~LevelMeter()
{
    destroyState(windows_);
    destroyState(track_);
}

void LevelMeter::push(Channel& c, double x) noexcept
{
    // Newest first: history[pos + j] = x[m - j], kept twice so the window is contiguous.
    c.pos = (c.pos + W - 1) % W;
    c.history[c.pos] = x;
    c.history[c.pos + W] = x;
    // The largest interpolated value on [m - delay, m - delay + 1).
    const double p = dsp::interpolatedPeak(c.history.data() + c.pos);
    c.recentNow = std::max(c.recentNow, p);
    if (carryOver_ == 0)
        c.trackMax = std::max(c.trackMax, p);
    // VU: the rectified signal through two averaging stages.
    c.vu1 = std::abs(x) + vuCoef_ * (c.vu1 - std::abs(x));
    c.vu2 = c.vu1 + vuCoef_ * (c.vu2 - c.vu1);
}

void LevelMeter::finishFrame() noexcept
{
    if (carryOver_ > 0)
        --carryOver_;
    if (++blockFill_ >= blockFrames_)
    {
        for (auto& c : ch_)
        {
            c.recentPrev = c.recentNow;
            c.recentNow = 0.0;
        }
        blockFill_ = 0;
    }
}

void LevelMeter::process(std::span<const double> samples)
{
    const std::size_t n = channels_;
    const std::size_t frames = samples.size() / n;
    for (std::size_t i = 0; i < frames; ++i)
    {
        for (std::size_t c = 0; c < n; ++c)
            push(ch_[c], samples[i * n + c]);
        finishFrame();
    }
    if (frames > 0)
    {
        if (ebur128_add_frames_double(st(windows_), samples.data(), frames) != EBUR128_SUCCESS
            || ebur128_add_frames_double(st(track_), samples.data(), frames) != EBUR128_SUCCESS)
            throw std::runtime_error("ebur128_add_frames_double failed");
    }
    trackFrames_ += frames;
}

void LevelMeter::startTrack()
{
    for (auto& c : ch_)
        c.trackMax = 0.0;
    carryOver_ = static_cast<std::uint64_t>(dsp::kTruePeakDelay);
    trackFrames_ = 0;
    // A fresh state for the integrated loudness: libebur128 keeps every gating block.
    void* fresh = makeState(rate_, channels_, EBUR128_MODE_I);
    destroyState(track_);
    track_ = fresh;
}

void LevelMeter::finish()
{
    // Only the interpolator runs on: the loudness windows hold what was heard.
    for (int j = 0; j < dsp::kTruePeakWindow; ++j)
    {
        for (auto& c : ch_)
            push(c, 0.0);
        finishFrame();
    }
}

LevelReading LevelMeter::reading() const
{
    LevelReading r;
    r.channels = channels_;
    for (const auto& c : ch_)
    {
        r.truePeakRecent.push_back(std::max(c.recentPrev, c.recentNow));
        r.truePeakMax.push_back(c.trackMax);
        // The average of a rectified sine is 2/pi of its peak; its RMS is 1/sqrt 2.
        r.vu.push_back(c.vu2 * std::numbers::pi / (2.0 * std::numbers::sqrt2));
    }
    double m = 0.0, s = 0.0, i = 0.0;
    if (ebur128_loudness_momentary(st(windows_), &m) != EBUR128_SUCCESS
        || ebur128_loudness_shortterm(st(windows_), &s) != EBUR128_SUCCESS
        || ebur128_loudness_global(st(track_), &i) != EBUR128_SUCCESS)
        throw std::runtime_error("ebur128 loudness failed");
    r.momentaryLufs = finiteOrMinusInf(m);
    r.shortTermLufs = finiteOrMinusInf(s);
    r.integratedLufs = finiteOrMinusInf(i);
    r.trackFrames = trackFrames_;
    return r;
}

} // namespace ap::engine
