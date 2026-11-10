// Task 3-3: True Peak by 8x oversampling (CLAUDE.md: 8 倍以上).
//
// The meter interpolates 7 values between every two samples with a windowed
// sinc and takes the largest magnitude. Its phase 0 passes the samples
// themselves unchanged, so the true peak is never below the sample peak.
// With 8x, a sine can read low by at most 1 - cos(pi f / 8 fs): 0.042 dB at
// fs/4, 0.14 dB at 0.45 fs.

#include <ap/engine/dsp/true_peak.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using namespace ap::engine;

namespace {

constexpr double kPi = std::numbers::pi;

double db(double x)
{
    return 20.0 * std::log10(x);
}

std::vector<double> sine(std::size_t frames, std::uint16_t channels, double cyclesPerSample, double amplitude,
                         double phase)
{
    std::vector<double> v(frames * channels);
    for (std::size_t i = 0; i < frames; ++i)
        for (std::uint16_t c = 0; c < channels; ++c)
            v[i * channels + c] = amplitude * std::sin(2 * kPi * cyclesPerSample * static_cast<double>(i) + phase);
    return v;
}

// The same with a raised-cosine fade in and out: a sine that starts or stops
// abruptly is not band-limited and really does overshoot at its ends.
std::vector<double> fadedSine(std::size_t frames, double cyclesPerSample, double amplitude, double phase)
{
    auto v = sine(frames, 1, cyclesPerSample, amplitude, phase);
    const std::size_t fade = 2000;
    for (std::size_t i = 0; i < fade; ++i)
    {
        const double g = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / fade);
        v[i] *= g;
        v[frames - 1 - i] *= g;
    }
    return v;
}

double measure(const std::vector<double>& v, std::uint16_t channels)
{
    TruePeakMeter m(channels);
    m.process(v);
    m.finish();
    return m.truePeak();
}

// An independent reference: band-limited interpolation with a long sinc
// (+-4000 samples, Hann window), evaluated on a fine grid around each sample.
double referencePeak(const std::vector<double>& x, int stepsPerSample)
{
    const long n = static_cast<long>(x.size());
    double peak = 0;
    for (long i = 0; i < n; ++i)
        for (int s = 0; s < stepsPerSample; ++s)
        {
            const double t = static_cast<double>(i) + static_cast<double>(s) / stepsPerSample;
            double y = 0;
            for (long j = std::max(0L, i - 4000); j < std::min(n, i + 4001); ++j)
            {
                const double d = t - static_cast<double>(j);
                const double sinc = d == 0 ? 1.0 : std::sin(kPi * d) / (kPi * d);
                const double w = 0.5 + 0.5 * std::cos(kPi * d / 4001.0);
                y += x[static_cast<std::size_t>(j)] * sinc * w;
            }
            peak = std::max(peak, std::abs(y));
        }
    return peak;
}

} // namespace

TEST_CASE("True peak: the ISP signal (fs/4, 45 degrees) reads +3.01 dB over its sample peak", "[dsp][truepeak]")
{
    // Samples +A, +A, -A, -A, ...: the waveform peaks halfway between two samples
    // at A * sqrt(2). Faded in and out: an abrupt start or stop overshoots
    // (see the acceptance test, which measures the generated file).
    const double a = GENERATE(1.0, 0.5, 32767.0 / 32768.0);
    CAPTURE(a);
    const auto mono = fadedSine(9600, 0.25, a * std::sqrt(2.0), kPi / 4);
    std::vector<double> v;
    for (auto x : mono)
        v.insert(v.end(), {x, x});
    TruePeakMeter m(2);
    m.process(v);
    m.finish();
    CHECK(std::abs(m.samplePeak() - a) < 1e-9);
    CHECK(std::abs(db(m.truePeak()) - db(a) - 3.0103) < 0.02);
    CHECK(m.truePeak(0) == m.truePeak(1));
}

TEST_CASE("True peak: an abrupt start overshoots (it is in the band-limited waveform)", "[dsp][truepeak]")
{
    const auto v = sine(9600, 1, 0.25, std::sqrt(2.0), kPi / 4);
    CHECK(db(measure(v, 1)) > 3.0103 + 0.02);
}

TEST_CASE("True peak is never below the sample peak", "[dsp][truepeak]")
{
    std::vector<double> v(20000);
    std::uint64_t x = 7;
    for (auto& s : v)
    {
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        s = (static_cast<double>(x >> 11) * 0x1.0p-53 - 0.5) * 1.8;
    }
    TruePeakMeter m(1);
    m.process(v);
    m.finish();
    CHECK(m.truePeak() >= m.samplePeak());
}

TEST_CASE("True peak of sines up to fs/4 reads at most 0.05 dB low and never high", "[dsp][truepeak]")
{
    const double f = GENERATE(0.001, 0.01, 0.05, 0.1, 0.17, 0.2, 0.23, 0.25);
    const double phase = GENERATE(0.0, 0.3, 1.1, 2.0);
    CAPTURE(f, phase);
    const double tp = db(measure(fadedSine(20000, f, 0.7, phase), 1)) - db(0.7);
    CHECK(tp <= 0.002);
    CHECK(tp >= -0.05);
}

TEST_CASE("True peak of sines up to 0.45 fs reads at most 0.15 dB low", "[dsp][truepeak]")
{
    const double f = GENERATE(0.3, 0.35, 0.4, 0.45);
    const double phase = GENERATE(0.0, 0.5, 1.3);
    CAPTURE(f, phase);
    const double tp = db(measure(fadedSine(20000, f, 0.7, phase), 1)) - db(0.7);
    CHECK(tp <= 0.002);
    CHECK(tp >= -0.15);
}

TEST_CASE("True peak agrees with long-sinc interpolation on band-limited noise", "[dsp][truepeak]")
{
    // White noise low-passed well below Nyquist by summing random sines up to 0.35 fs.
    std::vector<double> v(2000, 0.0);
    std::uint64_t x = 99;
    for (int k = 0; k < 40; ++k)
    {
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        const double f = 0.35 * static_cast<double>(x >> 11) * 0x1.0p-53;
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        const double ph = 2 * kPi * static_cast<double>(x >> 11) * 0x1.0p-53;
        for (std::size_t i = 0; i < v.size(); ++i)
            v[i] += 0.03 * std::sin(2 * kPi * f * static_cast<double>(i) + ph);
    }
    // Fade in and out so the reference's finite window sees the whole signal.
    for (std::size_t i = 0; i < 400; ++i)
    {
        const double g = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / 400.0);
        v[i] *= g;
        v[v.size() - 1 - i] *= g;
    }
    const double ref = referencePeak(v, 16);
    const double ours = measure(v, 1);
    CAPTURE(db(ref), db(ours));
    CHECK(std::abs(db(ours) - db(ref)) < 0.1); // 8x grid vs a 16x grid
    CHECK(db(ours) <= db(ref) + 0.01);
}

TEST_CASE("True peak: processing in pieces gives the same result as at once", "[dsp][truepeak]")
{
    const auto v = sine(10007, 2, 0.21, 0.9, 0.4);
    TruePeakMeter whole(2), pieces(2);
    whole.process(v);
    whole.finish();
    for (std::size_t i = 0; i < v.size();)
    {
        const std::size_t n = std::min<std::size_t>(v.size() - i, 2 * (1 + (i / 2) % 37));
        pieces.process(std::span(v).subspan(i, n));
        i += n;
    }
    pieces.finish();
    CHECK(whole.truePeak(0) == pieces.truePeak(0));
    CHECK(whole.truePeak(1) == pieces.truePeak(1));
}

TEST_CASE("True peak: finish() measures past the last sample", "[dsp][truepeak]")
{
    // The signal ends with two full-scale samples. The overshoot between and
    // after them is only computed once the interpolator has seen the
    // (implied zero) samples that follow; finish() supplies them.
    std::vector<double> v{0.0, 0.0, 0.0, 1.0, 1.0};
    TruePeakMeter m(1);
    m.process(v);
    const double before = m.truePeak();
    m.finish();
    CHECK(m.truePeak() > before);
    CHECK(m.truePeak() > 1.0); // two full-scale samples in a row overshoot between them
}

TEST_CASE("True peak: channels are measured separately; silence is -inf dB", "[dsp][truepeak]")
{
    std::vector<double> v(6000 * 2, 0.0);
    const auto right = fadedSine(6000, 0.1 / (2 * kPi), 0.5, 0.0);
    for (std::size_t i = 0; i < 6000; ++i)
        v[i * 2 + 1] = right[i];
    TruePeakMeter m(2);
    m.process(v);
    m.finish();
    CHECK(m.truePeak(0) == 0.0);
    CHECK(m.truePeakDb(0) == -std::numeric_limits<double>::infinity());
    CHECK(std::abs(m.truePeak(1) - 0.5) < 0.01);
    CHECK(m.truePeak() == m.truePeak(1));
}

TEST_CASE("True peak: reset starts over", "[dsp][truepeak]")
{
    TruePeakMeter m(1);
    m.process(sine(1000, 1, 0.1, 0.9, 0.0));
    m.finish();
    m.reset();
    m.process(sine(1000, 1, 0.1, 0.1, 0.0));
    m.finish();
    CHECK(m.truePeak() < 0.11);
}

TEST_CASE("True peak: the position of the peak, between samples", "[dsp][truepeak]")
{
    // A single full-scale sample at frame 1000: the peak is that sample itself.
    std::vector<double> v(3000, 0.0);
    v[1000] = 1.0;
    TruePeakMeter m(1);
    m.process(v);
    m.finish();
    CHECK(m.truePeak() == 1.0);
    CHECK(m.truePeakPosition() == 1000.0);

    // Two equal samples at 2000 and 2001: the overshoot is halfway between them.
    std::vector<double> w(4000, 0.0);
    w[2000] = w[2001] = 0.8;
    TruePeakMeter n(1);
    n.process(w);
    n.finish();
    CHECK(n.truePeak() > 0.8);
    CHECK(n.truePeakPosition() == 2000.5);
}
