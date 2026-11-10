#include <ap/engine/dsp/true_peak.h>

#include <ap/engine/dsp/sample_convert.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace ap::engine {

namespace {

constexpr int L = TruePeakMeter::kOversampling;
constexpr int J = TruePeakMeter::kTapsPerPhase + 1; // taps per phase, padded to the longest phase
constexpr int N = L * TruePeakMeter::kTapsPerPhase + 1; // filter length: odd, centred on a multiple of L
constexpr int kCentre = (N - 1) / 2;
constexpr double kKaiserBeta = 8.0; // about 80 dB of image rejection for this length

double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
}

// phases[k][j] = h[k + j*L]: output (m*L + k) = sum_j phases[k][j] * x[m - j].
using Phases = std::array<std::array<double, J>, L>;

const Phases& phases()
{
    static const Phases table = [] {
        Phases p{};
        const double i0Beta = besselI0(kKaiserBeta);
        for (int n = 0; n < N; ++n)
        {
            const int d = n - kCentre; // in output samples
            double h;
            if (d == 0)
                h = 1.0;
            else if (d % L == 0)
                h = 0.0; // exact zero crossings: phase 0 passes the input unchanged
            else
            {
                const double x = std::numbers::pi * d / L;
                const double r = static_cast<double>(d) / kCentre;
                h = std::sin(x) / x * besselI0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
            }
            p[static_cast<std::size_t>(n % L)][static_cast<std::size_t>(n / L)] = h;
        }
        return p;
    }();
    return table;
}

double toDb(double x)
{
    return x > 0.0 ? 20.0 * std::log10(x) : -std::numeric_limits<double>::infinity();
}

} // namespace

TruePeakMeter::TruePeakMeter(std::uint16_t channels) : channels_(channels)
{
    reset();
}

void TruePeakMeter::reset()
{
    for (auto& c : channels_)
    {
        c.history.assign(2 * J, 0.0);
        c.pos = 0;
        c.pushed = 0;
        c.truePeak = 0.0;
        c.truePeakPosition = 0.0;
        c.samplePeak = 0.0;
    }
    finished_ = false;
}

void TruePeakMeter::push(Channel& c, double x) noexcept
{
    // Newest first: window[j] = x[m - j].
    c.pos = (c.pos + J - 1) % J;
    c.history[c.pos] = x;
    c.history[c.pos + J] = x;
    const double* w = c.history.data() + c.pos;
    c.samplePeak = std::max(c.samplePeak, std::abs(x));

    const auto& p = phases();
    // Output (m*L + k) lies at input time m - kCentre/L + k/L, m = this sample's index.
    const double m = static_cast<double>(c.pushed++) - static_cast<double>(kCentre) / L;
    for (int k = 0; k < L; ++k)
    {
        const double* h = p[static_cast<std::size_t>(k)].data();
        double y = 0.0;
        for (int j = 0; j < J; ++j)
            y += h[j] * w[j];
        if (std::abs(y) > c.truePeak)
        {
            c.truePeak = std::abs(y);
            c.truePeakPosition = m + static_cast<double>(k) / L;
        }
    }
}

void TruePeakMeter::process(std::span<const double> samples)
{
    const std::size_t ch = channels_.size();
    for (std::size_t i = 0; i + ch <= samples.size(); i += ch)
        for (std::size_t c = 0; c < ch; ++c)
            push(channels_[c], samples[i + c]);
}

void TruePeakMeter::finish()
{
    if (finished_)
        return;
    finished_ = true;
    for (auto& c : channels_)
    {
        // The sample peak is not affected by the implied zeros.
        const double sp = c.samplePeak;
        for (int j = 0; j < J; ++j)
            push(c, 0.0);
        c.samplePeak = sp;
    }
}

double TruePeakMeter::truePeak() const noexcept
{
    double m = 0.0;
    for (const auto& c : channels_)
        m = std::max(m, c.truePeak);
    return m;
}

double TruePeakMeter::truePeak(std::uint16_t channel) const noexcept
{
    return channels_[channel].truePeak;
}

double TruePeakMeter::truePeakDb() const noexcept
{
    return toDb(truePeak());
}

double TruePeakMeter::truePeakDb(std::uint16_t channel) const noexcept
{
    return toDb(truePeak(channel));
}

double TruePeakMeter::samplePeak() const noexcept
{
    double m = 0.0;
    for (const auto& c : channels_)
        m = std::max(m, c.samplePeak);
    return m;
}

double TruePeakMeter::samplePeak(std::uint16_t channel) const noexcept
{
    return channels_[channel].samplePeak;
}

double TruePeakMeter::truePeakPosition() const noexcept
{
    const Channel* best = nullptr;
    for (const auto& c : channels_)
        if (!best || c.truePeak > best->truePeak)
            best = &c;
    return best ? best->truePeakPosition : 0.0;
}

double TruePeakMeter::truePeakPosition(std::uint16_t channel) const noexcept
{
    return channels_[channel].truePeakPosition;
}

PeakAnalysis analyzePeaks(PcmSource& source)
{
    if (source.dop())
        throw std::invalid_argument("DoP is not PCM audio");
    const PcmFormat f = source.format();
    TruePeakMeter meter(f.channels);
    constexpr std::size_t kChunk = 16384;
    std::vector<std::byte> bytes(kChunk * f.bytesPerFrame());
    std::vector<double> samples(kChunk * f.channels);
    PeakAnalysis a;
    while (const std::size_t n = source.read(bytes))
    {
        const auto block = std::span(samples).first(n * f.channels);
        dsp::toDouble(std::span(bytes).first(n * f.bytesPerFrame()), f, block);
        meter.process(block);
        a.frames += n;
    }
    meter.finish();
    for (std::uint16_t c = 0; c < f.channels; ++c)
    {
        a.samplePeak.push_back(meter.samplePeak(c));
        a.truePeak.push_back(meter.truePeak(c));
    }
    a.samplePeakMax = meter.samplePeak();
    a.truePeakMax = meter.truePeak();
    a.truePeakPosition = meter.truePeakPosition();
    return a;
}

} // namespace ap::engine
