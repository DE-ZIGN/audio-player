#include <ap/engine/dsp/limiter.h>

#include <ap/engine/dsp/true_peak.h>

#include <algorithm>
#include <cmath>

namespace ap::engine {

namespace {
// For the counters only: a reduction under 0.001 dB counts as not limiting
// (the gain itself returns to exactly 1 on its own, much later).
constexpr double kActingBelow = 0.999885; // -0.001 dB
constexpr std::size_t W = dsp::kTruePeakWindow;
constexpr std::size_t kDetectorDelay = dsp::kTruePeakDelay;
} // namespace

void SafetyLimiter::prepare(std::uint32_t sampleRate, std::uint16_t channels)
{
    channels_ = channels;
    lookAhead_ = std::max<std::size_t>(64, static_cast<std::size_t>(std::lround(sampleRate * kLookAheadSeconds)));
    lookAhead_ += lookAhead_ % 2; // even: the gain window is centred on a sample
    // The gain for sample s is ready once the required gains up to
    // s + 1.5 * lookAhead are known, which needs the detector's delay on top.
    latency_ = kDetectorDelay + lookAhead_ + lookAhead_ / 2;
    ceiling_ = std::pow(10.0, kCeilingDbtp / 20.0);
    release_ = 1.0 - std::exp(-1.0 / (sampleRate * kReleaseSeconds));

    history_.assign(static_cast<std::size_t>(channels) * 2 * W, 0.0);
    historyPos_ = 0;
    previousInterval_ = 0.0;

    const std::size_t window = 2 * lookAhead_ + 1;
    minIdx_.assign(window + 1, 0);
    minVal_.assign(window + 1, 1.0);
    minHead_ = minTail_ = 0;
    n_ = 0;

    box_.assign(lookAhead_ + 1, 1.0);
    boxPos_ = 0;
    boxSum_ = static_cast<double>(lookAhead_ + 1);
    boxBelowOne_ = 0;

    delay_.assign((latency_ + 1) * channels, 0.0);
    delayPos_ = 0;

    gain_ = 1.0;
    limiting_ = false;
}

void SafetyLimiter::process(std::span<double> samples, std::uint16_t channels) noexcept
{
    if (channels != channels_ || channels_ == 0)
        return;
    const std::size_t frames = samples.size() / channels;
    const std::size_t window = 2 * lookAhead_ + 1;
    const std::size_t queueSize = minIdx_.size();
    const std::size_t delayFrames = latency_ + 1;
    double* s = samples.data();

    std::uint64_t limitedNow = 0;
    double minGain = minGain_.load(std::memory_order_relaxed);

    for (std::size_t f = 0; f < frames; ++f, s += channels)
    {
        // 1. The largest value between the samples t and t + 1 (t = n - delay), over the channels.
        historyPos_ = (historyPos_ + W - 1) % W;
        double interval = 0.0;
        for (std::uint16_t c = 0; c < channels; ++c)
        {
            double* h = history_.data() + static_cast<std::size_t>(c) * 2 * W;
            h[historyPos_] = s[c];
            h[historyPos_ + W] = s[c];
            interval = std::max(interval, dsp::interpolatedPeak(h + historyPos_));
        }
        // 2. The gain sample t needs: both intervals next to it within the ceiling.
        const double peak = std::max(interval, previousInterval_);
        previousInterval_ = interval;
        const double req = peak > ceiling_ ? ceiling_ / peak : 1.0;

        // 3. Sliding minimum of the required gains over 2 * lookAhead + 1 samples.
        while (minTail_ != minHead_ && minVal_[(minTail_ + queueSize - 1) % queueSize] >= req)
            minTail_ = (minTail_ + queueSize - 1) % queueSize;
        minIdx_[minTail_] = n_;
        minVal_[minTail_] = req;
        minTail_ = (minTail_ + 1) % queueSize;
        while (minIdx_[minHead_] + window <= n_)
            minHead_ = (minHead_ + 1) % queueSize;
        const double held = minVal_[minHead_];
        ++n_;

        // 4. Moving average over lookAhead + 1 of the minima (exactly 1 when none is below 1).
        const double old = box_[boxPos_];
        if (old < 1.0)
            --boxBelowOne_;
        if (held < 1.0)
            ++boxBelowOne_;
        box_[boxPos_] = held;
        boxPos_ = (boxPos_ + 1) % box_.size();
        double target;
        if (boxBelowOne_ == 0)
        {
            boxSum_ = static_cast<double>(box_.size()); // no drift once back to unity
            target = 1.0;
        }
        else
        {
            boxSum_ += held - old;
            target = std::min(1.0, boxSum_ / static_cast<double>(box_.size()));
        }

        // 5. Down at once (the look-ahead already smoothed it), up over the release time.
        if (target <= gain_)
            gain_ = target;
        else
        {
            gain_ += (target - gain_) * release_;
            if (target == 1.0 && 1.0 - gain_ < 1e-12)
                gain_ = 1.0;
        }

        // 6. The delayed frame, scaled.
        double* slot = delay_.data() + delayPos_ * channels;
        const double* oldest = delay_.data() + ((delayPos_ + 1) % delayFrames) * channels;
        for (std::uint16_t c = 0; c < channels; ++c)
            slot[c] = s[c];
        for (std::uint16_t c = 0; c < channels; ++c)
            s[c] = gain_ == 1.0 ? oldest[c] : oldest[c] * gain_;
        delayPos_ = (delayPos_ + 1) % delayFrames;

        // 7. Counters.
        if (gain_ < kActingBelow)
        {
            ++limitedNow;
            if (!limiting_)
                events_.fetch_add(1, std::memory_order_relaxed);
            limiting_ = true;
            minGain = std::min(minGain, gain_);
        }
        else
        {
            limiting_ = false;
        }
    }
    if (limitedNow)
    {
        limitedFrames_.fetch_add(limitedNow, std::memory_order_relaxed);
        minGain_.store(minGain, std::memory_order_relaxed);
    }
}

LimiterStats SafetyLimiter::stats() const noexcept
{
    LimiterStats st;
    st.events = events_.load();
    st.limitedFrames = limitedFrames_.load();
    const double g = minGain_.load();
    st.maxReductionDb = g >= 1.0 ? 0.0 : 20.0 * std::log10(g);
    return st;
}

void SafetyLimiter::resetStats() noexcept
{
    events_ = 0;
    limitedFrames_ = 0;
    minGain_ = 1.0;
}

} // namespace ap::engine
