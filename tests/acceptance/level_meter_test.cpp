// Task 4-4 (docs/phase1-tasks.md): True Peak と LUFS のメーター — テスト信号で値が合う
//
// The loudness signals are built like the EBU Tech 3341 test signals: 1 kHz
// sine waves in phase on both channels, levels in dBFS per channel (so a
// steady tone at L dBFS reads L LUFS). Two of them switch between levels with
// a cycle exactly as long as the meter's window, so a correct sliding window
// reads the same value all the time:
//   momentary (400 ms): -20 dBFS for 0.18 s, -30 dBFS for 0.22 s
//     (0.18 * 10^-2 + 0.22 * 10^-3) / 0.4  ->  -22.97 LUFS
//   short-term (3 s):   -20 dBFS for 1.34 s, -30 dBFS for 1.66 s
//     (1.34 * 10^-2 + 1.66 * 10^-3) / 3    ->  -22.99 LUFS
// Each must read -23.0 +-0.1 LU (the Tech 3341 tolerance) once the window is full.
//
// True Peak: the ISP test signal (fs/4, 45 degrees, samples at 0 dBFS) reads
// +3.01 dBTP +-0.02 dB, as in the analysis (task 3-3), and the running meter
// gives exactly what the whole-file analysis gives.

#include <ap/engine/dsp/level_meter.h>
#include <ap/engine/dsp/loudness.h>
#include <ap/engine/dsp/true_peak.h>
#include <ap/signalgen/signals.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

using namespace ap::engine;
using Catch::Matchers::WithinAbs;

namespace {

constexpr std::uint32_t kRate = 48000;

double db(double x)
{
    return 20.0 * std::log10(x);
}

// Concatenated 1 kHz stereo tones, in phase: (level dBFS, seconds), repeated `times`.
std::vector<double> tones(const std::vector<std::pair<double, double>>& parts, int times = 1,
                          std::uint32_t rate = kRate, std::uint16_t channels = 2)
{
    std::vector<double> v;
    std::size_t n = 0;
    for (int t = 0; t < times; ++t)
        for (const auto& [level, seconds] : parts)
        {
            const double a = std::pow(10.0, level / 20.0);
            const auto frames = static_cast<std::size_t>(std::llround(seconds * rate));
            for (std::size_t i = 0; i < frames; ++i, ++n)
            {
                const double s = a * std::sin(2 * std::numbers::pi * 1000.0 * static_cast<double>(n) / rate);
                for (std::uint16_t c = 0; c < channels; ++c)
                    v.push_back(s);
            }
        }
    return v;
}

// Feeds `signal` in 10 ms steps and calls `look` after each step with the time fed so far.
template <typename F>
void feed(LevelMeter& m, const std::vector<double>& signal, F look)
{
    const std::size_t step = m.sampleRate() / 100 * m.channels();
    for (std::size_t i = 0; i < signal.size(); i += step)
    {
        const std::size_t n = std::min(step, signal.size() - i);
        m.process(std::span(signal).subspan(i, n));
        look(static_cast<double>(i + n) / m.channels() / m.sampleRate());
    }
}

} // namespace

TEST_CASE("Momentary loudness of a signal switching with a 400 ms cycle stays at -23.0 LUFS",
          "[acceptance][meter]")
{
    LevelMeter m(kRate, 2);
    const auto signal = tones({{-20.0, 0.18}, {-30.0, 0.22}}, 25);
    double lo = 0.0, hi = -100.0;
    feed(m, signal, [&](double t) {
        if (t < 0.4)
            return; // the window is not full yet
        const double v = m.reading().momentaryLufs;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    });
    INFO("momentary between " << lo << " and " << hi);
    CHECK_THAT(lo, WithinAbs(-23.0, 0.1));
    CHECK_THAT(hi, WithinAbs(-23.0, 0.1));
}

TEST_CASE("Short-term loudness of a signal switching with a 3 s cycle stays at -23.0 LUFS", "[acceptance][meter]")
{
    LevelMeter m(kRate, 2);
    const auto signal = tones({{-20.0, 1.34}, {-30.0, 1.66}}, 5);
    double lo = 0.0, hi = -100.0;
    feed(m, signal, [&](double t) {
        if (t < 3.0)
            return;
        const double v = m.reading().shortTermLufs;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    });
    INFO("short-term between " << lo << " and " << hi);
    CHECK_THAT(lo, WithinAbs(-23.0, 0.1));
    CHECK_THAT(hi, WithinAbs(-23.0, 0.1));
}

TEST_CASE("A steady tone reads its level as momentary, short-term and integrated loudness", "[acceptance][meter]")
{
    const auto rate = GENERATE(44100u, 48000u, 96000u);
    LevelMeter m(rate, 2);
    m.process(tones({{-23.0, 20.0}}, 1, rate));
    const auto r = m.reading();
    CHECK_THAT(r.momentaryLufs, WithinAbs(-23.0, 0.1));
    CHECK_THAT(r.shortTermLufs, WithinAbs(-23.0, 0.1));
    CHECK_THAT(r.integratedLufs, WithinAbs(-23.0, 0.1));
}

TEST_CASE("The integrated loudness matches the analysis, gating included", "[acceptance][meter]")
{
    // Tech 3341 case 4 levels: the absolute and relative gates both matter.
    const auto signal = tones({{-72.0, 10.0}, {-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}, {-72.0, 10.0}});
    LevelMeter m(kRate, 2);
    m.process(signal);
    LoudnessMeter analysis(kRate, 2);
    analysis.process(signal);
    CHECK_THAT(m.reading().integratedLufs, WithinAbs(-23.0, 0.1));
    CHECK(m.reading().integratedLufs == analysis.integratedLufs());
}

TEST_CASE("The integrated loudness starts again with each track; the windows run on", "[acceptance][meter]")
{
    LevelMeter m(kRate, 2);
    m.process(tones({{-23.0, 10.0}}));
    REQUIRE_THAT(m.reading().integratedLufs, WithinAbs(-23.0, 0.1));

    m.startTrack();
    // Just after the boundary the windows still hold the previous track.
    m.process(tones({{-33.0, 0.1}}));
    CHECK(m.reading().shortTermLufs > -26.0);
    m.process(tones({{-33.0, 9.9}}));
    const auto r = m.reading();
    CHECK_THAT(r.integratedLufs, WithinAbs(-33.0, 0.1)); // the first track does not count
    CHECK_THAT(r.shortTermLufs, WithinAbs(-33.0, 0.1));
    CHECK(r.trackFrames == 10 * kRate);
}

TEST_CASE("Mono is measured as dual mono", "[acceptance][meter]")
{
    LevelMeter m(kRate, 1);
    m.process(tones({{-23.0, 10.0}}, 1, kRate, 1));
    CHECK_THAT(m.reading().integratedLufs, WithinAbs(-23.0, 0.1));
    CHECK_THAT(m.reading().momentaryLufs, WithinAbs(-23.0, 0.1));
}

TEST_CASE("The ISP test signal reads +3.01 dBTP on the meter", "[acceptance][meter]")
{
    const auto rate = GENERATE(44100u, 48000u, 96000u, 192000u, 384000u);
    ap::signalgen::Layout layout{rate, 2, static_cast<std::size_t>(rate)}; // 1 s
    auto signal = ap::signalgen::ispSine(layout);
    // Faded in and out: a tone that starts or stops abruptly really overshoots
    // at its ends (+3.10 dBTP, task 3-3), which is not what this checks.
    const std::size_t fade = rate / 20;
    for (std::size_t i = 0; i < fade; ++i)
    {
        const double g = 0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(i) / fade);
        for (std::size_t c = 0; c < 2; ++c)
        {
            signal[i * 2 + c] *= g;
            signal[(layout.frames - 1 - i) * 2 + c] *= g;
        }
    }
    double samplePeak = 0.0;
    for (double x : signal)
        samplePeak = std::max(samplePeak, std::abs(x));

    LevelMeter m(rate, 2);
    m.process(signal);
    m.finish();
    const auto r = m.reading();

    TruePeakMeter analysis(2);
    analysis.process(signal);
    analysis.finish();
    for (std::uint16_t c = 0; c < 2; ++c)
    {
        CHECK_THAT(db(r.truePeakMax[c] / samplePeak), WithinAbs(3.01, 0.02));
        CHECK(r.truePeakMax[c] == analysis.truePeak(c)); // the same interpolator, the same values
    }
}

TEST_CASE("Each channel's true peak is its own", "[acceptance][meter]")
{
    // Left at -6 dBFS, right at -12 dBFS, sine at fs/8: the 8x grid catches
    // the crest within 1 - cos(pi/64) (0.01 dB). Faded in and out (an abrupt
    // start overshoots).
    LevelMeter m(kRate, 2);
    std::vector<double> v;
    const std::size_t fade = kRate / 20;
    for (std::size_t i = 0; i < kRate; ++i)
    {
        const std::size_t edge = std::min(i, kRate - 1 - i);
        const double g = edge < fade ? 0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(edge) / fade) : 1.0;
        const double s = g * std::sin(2 * std::numbers::pi * static_cast<double>(i) / 8.0 + 0.3);
        v.push_back(0.5 * s);
        v.push_back(0.25 * s);
    }
    m.process(v);
    const auto r = m.reading();
    CHECK_THAT(db(r.truePeakMax[0]), WithinAbs(db(0.5), 0.02));
    CHECK_THAT(db(r.truePeakMax[1]), WithinAbs(db(0.25), 0.02));
    CHECK(r.truePeakMax[0] <= 0.5 * (1.0 + 1e-4)); // not above the crest (beyond the filter's ripple)
}
