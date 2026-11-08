#include <ap/signalgen/signals.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace ap::signalgen;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;

double toDb(double linear) { return 20.0 * std::log10(linear); }

// Amplitude of the component at exactly `cycles` cycles over the buffer (mono).
double dftAmplitude(const std::vector<double>& x, double cycles)
{
    std::complex<double> acc{};
    const double n = static_cast<double>(x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        acc += x[i] * std::polar(1.0, -2.0 * pi * cycles * static_cast<double>(i) / n);
    return 2.0 * std::abs(acc) / n;
}

// Band-limited value at fractional position t, by Hann-windowed sinc
// interpolation over +-halfWidth samples. Reference only; the real True Peak
// meter (task 3-3) uses proper oversampling.
double sincInterpolate(const std::vector<double>& x, double t, int halfWidth)
{
    const auto centre = static_cast<long long>(std::floor(t));
    double sum = 0.0;
    for (long long k = centre - halfWidth + 1; k <= centre + halfWidth; ++k)
    {
        if (k < 0 || k >= static_cast<long long>(x.size()))
            continue;
        const double d = t - static_cast<double>(k);
        const double sinc = d == 0.0 ? 1.0 : std::sin(pi * d) / (pi * d);
        const double w = 0.5 * (1.0 + std::cos(pi * d / halfWidth));
        sum += x[static_cast<std::size_t>(k)] * sinc * w;
    }
    return sum;
}

std::size_t zeroCrossings(const std::vector<double>& x, std::size_t begin, std::size_t end)
{
    std::size_t count = 0;
    for (std::size_t i = std::max<std::size_t>(begin, 1); i < end; ++i)
        if ((x[i - 1] < 0.0) != (x[i] < 0.0))
            ++count;
    return count;
}

} // namespace

// ---------------------------------------------------------------------------
// ISP signal
// ---------------------------------------------------------------------------

TEST_CASE("ISP signal samples are +A,+A,-A,-A in every channel", "[signalgen][isp]")
{
    const Layout layout{44100, 2, 1000};
    const auto x = ispSine(layout, 0.0);
    REQUIRE(x.size() == layout.frames * layout.channels);

    for (std::size_t n = 0; n < layout.frames; ++n)
    {
        const double expected = (n % 4 < 2) ? 1.0 : -1.0;
        CHECK(x[n * 2] == expected);
        CHECK(x[n * 2 + 1] == expected);
    }
}

TEST_CASE("ISP signal equals a fs/4 sine at 45 degrees with sqrt(2) gain", "[signalgen][isp]")
{
    const Layout layout{48000, 1, 64};
    const auto x = ispSine(layout, -6.0);
    const double a = dbToLinear(-6.0);
    for (std::size_t n = 0; n < x.size(); ++n)
    {
        const double ref = std::numbers::sqrt2 * a * std::sin(pi * static_cast<double>(n) / 2.0 + pi / 4.0);
        CHECK_THAT(x[n], WithinAbs(ref, 1e-12));
    }
}

TEST_CASE("ISP signal reconstructs to +3.01 dB above the sample peak", "[signalgen][isp]")
{
    const Layout layout{48000, 1, 32768};
    const auto x = ispSine(layout, 0.0);

    // Midway between samples 16384 and 16385 the 45-degree phase reaches 90 degrees.
    const double peak = sincInterpolate(x, 16384.5, 8192);
    CHECK_THAT(toDb(peak), WithinAbs(20.0 * std::log10(std::numbers::sqrt2), 0.01)); // 3.0103 dB
}

TEST_CASE("ISP signal quantizes to full-scale codes", "[signalgen][isp]")
{
    const auto format = GENERATE(SampleFormat::Int16, SampleFormat::Int24, SampleFormat::Int32);
    const Layout layout{96000, 2, 400};
    const auto pcm = quantize(ispSine(layout, 0.0), layout.sampleRate, layout.channels, format);

    const auto fs = maxCode(format);
    for (std::size_t n = 0; n < layout.frames; ++n)
    {
        const std::int32_t expected = (n % 4 < 2) ? fs : -fs;
        REQUIRE(pcm.intSamples[n * 2] == expected);
        REQUIRE(pcm.intSamples[n * 2 + 1] == expected);
    }
}

// ---------------------------------------------------------------------------
// Sine
// ---------------------------------------------------------------------------

TEST_CASE("1 kHz -90 dBFS sine has the requested level and frequency", "[signalgen][sine]")
{
    const Layout layout{48000, 1, 48000}; // exactly 1000 cycles
    const auto x = sine(layout, 1000.0, -90.0);

    CHECK_THAT(toDb(dftAmplitude(x, 1000.0)), WithinAbs(-90.0, 0.001));
    CHECK(dftAmplitude(x, 999.0) < dbToLinear(-200.0));

    const auto peak = std::ranges::max(x, {}, [](double v) { return std::abs(v); });
    CHECK(std::abs(peak) <= dbToLinear(-90.0));
}

TEST_CASE("1 kHz -90 dBFS sine at 24 bit keeps its level", "[signalgen][sine]")
{
    const Layout layout{48000, 1, 48000};
    const auto pcm = quantize(sine(layout, 1000.0, -90.0), layout.sampleRate, 1, SampleFormat::Int24);

    // 8388607 * 10^(-90/20) = 265.27 LSB
    const auto maxAbs = std::ranges::max(pcm.intSamples, {}, [](std::int32_t v) { return std::abs(v); });
    CHECK(std::abs(maxAbs) == 265);

    std::vector<double> back(pcm.intSamples.begin(), pcm.intSamples.end());
    for (auto& v : back)
        v /= maxCode(SampleFormat::Int24);
    CHECK_THAT(toDb(dftAmplitude(back, 1000.0)), WithinAbs(-90.0, 0.1));
}

TEST_CASE("1 kHz -90 dBFS sine at 16 bit uses only codes -1, 0, +1", "[signalgen][sine]")
{
    const Layout layout{44100, 1, 44100};
    const auto pcm = quantize(sine(layout, 1000.0, -90.0), layout.sampleRate, 1, SampleFormat::Int16);
    CHECK(std::ranges::all_of(pcm.intSamples, [](std::int32_t v) { return v >= -1 && v <= 1; }));
    CHECK(std::ranges::count(pcm.intSamples, 1) > 0);
    CHECK(std::ranges::count(pcm.intSamples, -1) > 0);
}

TEST_CASE("sine rejects frequencies outside (0, fs/2)", "[signalgen][sine]")
{
    const Layout layout{44100, 1, 100};
    CHECK_THROWS_AS(sine(layout, 0.0, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(sine(layout, 22050.0, 0.0), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Sweep
// ---------------------------------------------------------------------------

TEST_CASE("Log sweep follows the exponential frequency law", "[signalgen][sweep]")
{
    constexpr std::uint32_t fs = 48000;
    constexpr double seconds = 10.0;
    const Layout layout{fs, 1, static_cast<std::size_t>(fs * seconds)};
    const auto x = logSweep(layout, 20.0, 20000.0, -6.0);

    CHECK(x.front() == 0.0);
    const double a = dbToLinear(-6.0);
    CHECK(std::ranges::all_of(x, [a](double v) { return std::abs(v) <= a; }));

    // Zero crossings in a window = phase advance / pi.
    for (double t0 : {0.0, 2.5, 5.0, 7.5, 9.5})
    {
        const double t1 = t0 + 0.5;
        const auto expected = (logSweepPhase(t1, seconds, 20.0, 20000.0)
                               - logSweepPhase(t0, seconds, 20.0, 20000.0)) / pi;
        const auto counted = zeroCrossings(x, static_cast<std::size_t>(t0 * fs),
                                           static_cast<std::size_t>(t1 * fs));
        CHECK_THAT(static_cast<double>(counted), WithinAbs(expected, 2.0));
    }
}

TEST_CASE("Log sweep rejects invalid ranges", "[signalgen][sweep]")
{
    const Layout layout{44100, 1, 1000};
    CHECK_THROWS_AS(logSweep(layout, 20.0, 22050.0, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(logSweep(layout, 1000.0, 500.0, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(logSweep(layout, 0.0, 1000.0, 0.0), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Bit pattern
// ---------------------------------------------------------------------------

TEST_CASE("Bit pattern sets and clears every bit", "[signalgen][bitpattern]")
{
    const auto format = GENERATE(SampleFormat::Int16, SampleFormat::Int24, SampleFormat::Int32);
    const int bits = bitsPerSample(format);
    const std::uint32_t mask = bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);

    const auto seq = bitPatternSequence(format);
    std::uint32_t everSet = 0;
    std::uint32_t everClear = 0;
    for (auto v : seq)
    {
        REQUIRE(v >= minCode(format));
        REQUIRE(v <= maxCode(format));
        const auto u = static_cast<std::uint32_t>(v) & mask;
        everSet |= u;
        everClear |= ~u & mask;
    }
    CHECK(everSet == mask);
    CHECK(everClear == mask);

    CHECK(std::ranges::count(seq, 0) > 0);
    CHECK(std::ranges::count(seq, maxCode(format)) > 0);
    CHECK(std::ranges::count(seq, minCode(format)) > 0);
    for (int k = 0; k < bits - 1; ++k)
    {
        CHECK(std::ranges::count(seq, static_cast<std::int32_t>(1LL << k)) > 0);
        CHECK(std::ranges::count(seq, static_cast<std::int32_t>(-(1LL << k))) > 0);
    }
}

TEST_CASE("Bit pattern repeats the sequence and inverts odd channels", "[signalgen][bitpattern]")
{
    const auto format = GENERATE(SampleFormat::Int16, SampleFormat::Int24, SampleFormat::Int32);
    const auto seq = bitPatternSequence(format);
    const Layout layout{192000, 2, seq.size() * 3 + 7};
    const auto pcm = bitPattern(layout, format);

    REQUIRE(pcm.format == format);
    REQUIRE(pcm.frameCount() == layout.frames);
    for (std::size_t n = 0; n < layout.frames; ++n)
    {
        REQUIRE(pcm.intSamples[n * 2] == seq[n % seq.size()]);
        REQUIRE(pcm.intSamples[n * 2 + 1] == ~seq[n % seq.size()]);
    }
}

TEST_CASE("Bit pattern is integer-only", "[signalgen][bitpattern]")
{
    CHECK_THROWS_AS(bitPattern(Layout{48000, 2, 10}, SampleFormat::Float32), std::invalid_argument);
}
