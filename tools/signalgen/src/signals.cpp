#include <ap/signalgen/signals.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace ap::signalgen {

namespace {

constexpr double twoPi = 2.0 * std::numbers::pi;

void validateLayout(const Layout& layout)
{
    if (layout.sampleRate == 0 || layout.channels == 0)
        throw std::invalid_argument("signal: sample rate and channel count must be non-zero");
}

template <typename ValueAt>
std::vector<double> fillAllChannels(const Layout& layout, ValueAt valueAt)
{
    std::vector<double> out(layout.frames * layout.channels);
    for (std::size_t n = 0; n < layout.frames; ++n)
    {
        const double v = valueAt(n);
        for (std::size_t c = 0; c < layout.channels; ++c)
            out[n * layout.channels + c] = v;
    }
    return out;
}

} // namespace

double dbToLinear(double db) noexcept
{
    return std::pow(10.0, db / 20.0);
}

std::vector<double> sine(const Layout& layout, double frequencyHz, double levelDbfs, double phaseRadians)
{
    validateLayout(layout);
    const double fs = layout.sampleRate;
    if (!(frequencyHz > 0.0 && frequencyHz < fs / 2.0))
        throw std::invalid_argument("sine: frequency must be in (0, fs/2)");

    const double amplitude = dbToLinear(levelDbfs);
    return fillAllChannels(layout, [&](std::size_t n) {
        // Keep the argument small: use only the fractional part of the cycle count.
        const double cycles = std::fmod(frequencyHz * static_cast<double>(n) / fs, 1.0);
        return amplitude * std::sin(twoPi * cycles + phaseRadians);
    });
}

std::vector<double> ispSine(const Layout& layout, double samplePeakDbfs)
{
    validateLayout(layout);
    const double a = dbToLinear(samplePeakDbfs);
    // sin(pi n / 2 + pi / 4) * sqrt(2) = +1, +1, -1, -1, ...
    return fillAllChannels(layout, [a](std::size_t n) { return (n % 4 < 2) ? a : -a; });
}

double logSweepPhase(double t, double durationSeconds, double startHz, double endHz) noexcept
{
    const double logRatio = std::log(endHz / startHz);
    return twoPi * startHz * durationSeconds / logRatio * (std::exp(t * logRatio / durationSeconds) - 1.0);
}

std::vector<double> logSweep(const Layout& layout, double startHz, double endHz, double levelDbfs)
{
    validateLayout(layout);
    const double fs = layout.sampleRate;
    if (!(startHz > 0.0 && startHz < endHz && endHz < fs / 2.0))
        throw std::invalid_argument("logSweep: requires 0 < start < end < fs/2");
    if (layout.frames < 2)
        throw std::invalid_argument("logSweep: needs at least 2 frames");

    const double amplitude = dbToLinear(levelDbfs);
    const double duration = static_cast<double>(layout.frames) / fs;
    return fillAllChannels(layout, [&](std::size_t n) {
        const double t = static_cast<double>(n) / fs;
        return amplitude * std::sin(logSweepPhase(t, duration, startHz, endHz));
    });
}

std::vector<std::int32_t> bitPatternSequence(SampleFormat format)
{
    if (!isInteger(format))
        throw std::invalid_argument("bitPattern: integer formats only");

    const int bits = bitsPerSample(format);
    const std::int32_t hi = maxCode(format);
    const std::int32_t lo = minCode(format);

    std::vector<std::int32_t> seq;
    seq.push_back(0);
    for (int k = 0; k < bits - 1; ++k)
        seq.push_back(static_cast<std::int32_t>(1LL << k));
    for (int k = 0; k < bits - 1; ++k)
        seq.push_back(static_cast<std::int32_t>(-(1LL << k)));
    seq.push_back(hi);
    seq.push_back(lo);
    for (std::int32_t v = 0; v <= 255; ++v)
        seq.push_back(v);
    for (std::int32_t v = 1; v <= 256; ++v)
        seq.push_back(-v);

    const std::int32_t alt = static_cast<std::int32_t>(0x55555555) & hi; // 0101...
    const std::int32_t altInv = hi ^ alt;                                  // 1010... (sign bit clear)
    seq.push_back(alt);
    seq.push_back(altInv);
    seq.push_back(~alt);
    seq.push_back(~altInv);
    return seq;
}

PcmData bitPattern(const Layout& layout, SampleFormat format)
{
    validateLayout(layout);
    const auto seq = bitPatternSequence(format);

    PcmData out;
    out.sampleRate = layout.sampleRate;
    out.channels = layout.channels;
    out.format = format;
    out.intSamples.resize(layout.frames * layout.channels);
    for (std::size_t n = 0; n < layout.frames; ++n)
    {
        const std::int32_t v = seq[n % seq.size()];
        for (std::size_t c = 0; c < layout.channels; ++c)
            out.intSamples[n * layout.channels + c] = (c % 2 == 0) ? v : ~v;
    }
    return out;
}

} // namespace ap::signalgen
