// Task 10-1: the resampler (r8brain-free-src, linear phase). What is
// measured: the stop band (aliases and images at least 140 dB down,
// CLAUDE.md chapter 5), the pass band, the length (the input's duration at
// the new rate), the timing (an impulse lands where the input had it), the
// channels and the blocks. Nothing here says it sounds better.

#include <ap/engine/dsp/resampler.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

using namespace ap::engine;

namespace {

constexpr double kPi = std::numbers::pi;

double dB(double x)
{
    return 20.0 * std::log10(std::max(x, 1e-300));
}

// Runs interleaved `x` through a resampler in blocks of varying size, then finishes.
std::vector<double> resample(const std::vector<double>& x, std::uint32_t from, std::uint32_t to, std::uint16_t channels,
                             std::uint32_t seed = 1)
{
    dsp::Resampler r;
    constexpr std::size_t kMaxIn = 4096;
    r.prepare(from, to, channels, kMaxIn);
    std::vector<double> out;
    out.reserve(r.maxOutputFrames(x.size() / channels) * channels + 64);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> len(1, kMaxIn);
    for (std::size_t i = 0; i < x.size();)
    {
        const std::size_t frames = std::min(len(rng), (x.size() - i) / channels);
        r.process(std::span(x).subspan(i, frames * channels), out);
        i += frames * channels;
    }
    r.finish(out);
    return out;
}

std::vector<double> tone(double freq, double amplitude, std::size_t frames, double rate, std::uint16_t channels = 1,
                         std::uint16_t onChannel = 0)
{
    std::vector<double> x(frames * channels, 0.0);
    for (std::size_t i = 0; i < frames; ++i)
        x[i * channels + onChannel] = amplitude * std::sin(2 * kPi * freq * static_cast<double>(i) / rate);
    return x;
}

// Amplitude of `freq` in channel `ch` over frames [from, from + n) (whole cycles: no leakage).
double amplitude(const std::vector<double>& x, std::uint16_t channels, std::uint16_t ch, std::size_t from, std::size_t n,
                 double freq, double rate)
{
    std::complex<double> s{};
    for (std::size_t i = 0; i < n; ++i)
        s += x[(from + i) * channels + ch] * std::polar(1.0, -2 * kPi * freq * static_cast<double>(from + i) / rate);
    return std::abs(s) * 2.0 / static_cast<double>(n);
}

} // namespace

TEST_CASE("Resampler: the same rate passes the samples unchanged", "[dsp][resampler]")
{
    const auto x = tone(1000, 0.5, 10000, 44100, 2);
    const auto y = resample(x, 44100, 44100, 2);
    CHECK(y == x);
    dsp::Resampler r;
    r.prepare(48000, 48000, 2, 1024);
    CHECK_FALSE(r.active());
}

TEST_CASE("Resampler: the output lasts as long as the input", "[dsp][resampler]")
{
    struct Case
    {
        std::uint32_t from, to;
    };
    for (const auto [from, to] : {Case{44100, 48000}, Case{44100, 88200}, Case{44100, 352800}, Case{96000, 44100},
                                  Case{48000, 44100}, Case{192000, 96000}, Case{88200, 96000}, Case{384000, 44100}})
        for (const std::size_t frames : {std::size_t{1}, std::size_t{1000}, std::size_t{44101}})
        {
            CAPTURE(from, to, frames);
            const std::vector<double> x(frames * 2, 0.25);
            const auto y = resample(x, from, to, 2);
            const auto expected = dsp::Resampler::outputFrames(frames, from, to);
            CHECK(expected == static_cast<std::uint64_t>(std::ceil(static_cast<double>(frames) * to / from - 1e-9)));
            CHECK(y.size() == expected * 2);
        }
}

TEST_CASE("Resampler: an impulse lands where the input had it (linear phase, no delay)", "[dsp][resampler]")
{
    struct Case
    {
        std::uint32_t from, to;
    };
    for (const auto [from, to] : {Case{44100, 88200}, Case{48000, 192000}, Case{96000, 48000}, Case{44100, 48000},
                                  Case{48000, 44100}})
    {
        CAPTURE(from, to);
        const std::size_t at = 10000;
        std::vector<double> x(30000, 0.0);
        x[at] = 1.0;
        const auto y = resample(x, from, to, 1);
        const auto peak = static_cast<std::size_t>(std::max_element(y.begin(), y.end(), [](double a, double b) {
                                                       return std::abs(a) < std::abs(b);
                                                   }) - y.begin());
        const double where = static_cast<double>(at) * to / from;
        CHECK(std::abs(static_cast<double>(peak) - where) <= 0.5 + 1e-9);
        // Symmetric around it (linear phase), for the integer ratios.
        if (to % from == 0 || from % to == 0)
            for (std::size_t k = 1; k < 50; ++k)
                CHECK(std::abs(y[peak + k] - y[peak - k]) < 1e-9);
    }
}

TEST_CASE("Resampler: the pass band keeps its level", "[dsp][resampler]")
{
    struct Case
    {
        std::uint32_t from, to;
        double freq;
    };
    for (const auto [from, to, freq] :
         {Case{44100, 48000, 1000}, Case{44100, 48000, 20000}, Case{44100, 88200, 20000}, Case{96000, 44100, 1000},
          Case{96000, 44100, 20000}, Case{48000, 192000, 20000}})
    {
        CAPTURE(from, to, freq);
        const auto x = tone(freq, 0.5, from * 2, from); // 2 s
        const auto y = resample(x, from, to, 1);
        const double a = amplitude(y, 1, 0, to / 2, to, freq, to); // the middle second
        CAPTURE(dB(a / 0.5));
        CHECK(std::abs(dB(a / 0.5)) < 0.01);
    }
}

TEST_CASE("Resampler: aliases and images are at least 140 dB down", "[dsp][resampler]")
{
    struct Case
    {
        std::uint32_t from, to;
        double freq;     // the input tone (whole cycles in a second at both rates)
        double unwanted; // where an alias or image would land in the output
    };
    // Down: 30 kHz is above the new Nyquist (24 kHz); it would fold to 18 kHz.
    // Up: a 10 kHz tone's image lies at 34.1 kHz (44.1 - 10).
    // 44.1 -> 48: a 15 kHz tone's image at 29.1 kHz folds to 18.9 kHz.
    // 96 -> 44.1: 30 kHz would fold to 14.1 kHz.
    for (const auto [from, to, freq, unwanted] :
         {Case{96000, 48000, 30000, 18000}, Case{44100, 88200, 10000, 34100}, Case{44100, 48000, 15000, 18900},
          Case{96000, 44100, 30000, 14100}, Case{48000, 192000, 15000, 33000},
          // Just past the band's edge: 24.5 kHz would fold to 23.5 kHz; a 21 kHz
          // tone's image lies at 23.1 kHz.
          Case{96000, 48000, 24500, 23500}, Case{44100, 88200, 21000, 23100}})
    {
        CAPTURE(from, to, freq, unwanted);
        const auto x = tone(freq, 1.0, from * 3, from); // 3 s at full scale
        const auto y = resample(x, from, to, 1);
        const double a = amplitude(y, 1, 0, to, to, unwanted, to); // the middle second
        CAPTURE(dB(a));
        CHECK(dB(a) < -140.0);
    }
}

TEST_CASE("Resampler: the channels stay apart; the blocks do not matter", "[dsp][resampler]")
{
    // A tone on the left only: the right stays silent.
    const auto x = tone(1000, 0.5, 48000, 44100, 2, 0);
    const auto y = resample(x, 44100, 48000, 2);
    double right = 0;
    for (std::size_t i = 1; i < y.size(); i += 2)
        right = std::max(right, std::abs(y[i]));
    CHECK(right == 0.0);
    // The same input in other blocks: the same output.
    CHECK(resample(x, 44100, 48000, 2, 7) == y);
    // Each channel as if alone.
    std::vector<double> left(x.size() / 2);
    for (std::size_t i = 0; i < left.size(); ++i)
        left[i] = x[i * 2];
    const auto mono = resample(left, 44100, 48000, 1);
    REQUIRE(mono.size() * 2 == y.size());
    for (std::size_t i = 0; i < mono.size(); ++i)
        REQUIRE(mono[i] == y[i * 2]);
}

TEST_CASE("Resampler: reset starts over", "[dsp][resampler]")
{
    dsp::Resampler r;
    r.prepare(44100, 96000, 1, 1024);
    const auto x = tone(440, 0.5, 5000, 44100);
    std::vector<double> a, b;
    a.reserve(20000);
    b.reserve(20000);
    r.process(x, a);
    r.finish(a);
    r.reset();
    r.process(x, b);
    r.finish(b);
    CHECK(a == b);
}
