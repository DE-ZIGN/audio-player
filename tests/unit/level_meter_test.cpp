// Task 4-4: how the running meter keeps its values (the values themselves
// against test signals are in tests/acceptance/level_meter_test.cpp).

#include <ap/engine/dsp/level_meter.h>
#include <ap/engine/dsp/true_peak.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using namespace ap::engine;
using Catch::Matchers::WithinAbs;

namespace {

constexpr std::uint32_t kRate = 48000;

std::vector<double> sine(std::size_t frames, double amplitude, std::uint16_t channels = 2)
{
    std::vector<double> v(frames * channels);
    for (std::size_t i = 0; i < frames; ++i)
        for (std::uint16_t c = 0; c < channels; ++c)
            v[i * channels + c] = amplitude * std::sin(2 * std::numbers::pi * 997.0 * static_cast<double>(i) / kRate);
    return v;
}

double db(double x)
{
    return 20.0 * std::log10(x);
}

} // namespace

TEST_CASE("A new meter reads silence", "[meter]")
{
    LevelMeter m(kRate, 2);
    const auto r = m.reading();
    REQUIRE(r.channels == 2);
    REQUIRE(r.truePeakRecent.size() == 2);
    REQUIRE(r.truePeakMax.size() == 2);
    CHECK(r.truePeakRecent[0] == 0.0);
    CHECK(r.truePeakMax[1] == 0.0);
    CHECK(r.momentaryLufs == -std::numeric_limits<double>::infinity());
    CHECK(r.shortTermLufs == -std::numeric_limits<double>::infinity());
    CHECK(r.integratedLufs == -std::numeric_limits<double>::infinity());
    CHECK(r.trackFrames == 0);
}

TEST_CASE("The recent true peak follows the signal down; the track maximum holds", "[meter]")
{
    LevelMeter m(kRate, 2);
    m.process(sine(kRate, 0.5));
    CHECK_THAT(db(m.reading().truePeakRecent[0]), WithinAbs(db(0.5), 0.05));

    m.process(sine(kRate / 4, 0.01)); // 0.25 s at -40 dB: longer than the recent window plus a block
    const auto r = m.reading();
    CHECK_THAT(db(r.truePeakRecent[0]), WithinAbs(-40.0, 0.05));
    CHECK_THAT(db(r.truePeakMax[0]), WithinAbs(db(0.5), 0.05));
}

TEST_CASE("The recent true peak covers at least the last 0.1 s", "[meter]")
{
    // A peak 0.09 s ago is still shown (a reader polling at 10 Hz sees every peak).
    LevelMeter m(kRate, 1);
    std::vector<double> v(kRate, 0.0);
    v[kRate - static_cast<std::size_t>(0.09 * kRate)] = 0.8;
    m.process(v);
    CHECK(m.reading().truePeakRecent[0] >= 0.8);
}

TEST_CASE("Feeding in pieces of any size gives the same reading", "[meter]")
{
    const auto signal = sine(kRate * 2, 0.7);
    LevelMeter whole(kRate, 2), pieces(kRate, 2);
    whole.process(signal);
    std::size_t i = 0, step = 1;
    while (i < signal.size())
    {
        const std::size_t n = std::min(step * 2, signal.size() - i);
        pieces.process(std::span(signal).subspan(i, n));
        i += n;
        step = step * 3 % 1021 + 1;
    }
    const auto a = whole.reading(), b = pieces.reading();
    CHECK(a.truePeakMax == b.truePeakMax);
    CHECK(a.truePeakRecent == b.truePeakRecent);
    CHECK(a.momentaryLufs == b.momentaryLufs);
    CHECK(a.shortTermLufs == b.shortTermLufs);
    CHECK(a.integratedLufs == b.integratedLufs);
    CHECK(a.trackFrames == b.trackFrames);
}

TEST_CASE("A new track starts its maximum from nothing", "[meter]")
{
    LevelMeter m(kRate, 2);
    m.process(sine(kRate, 0.9));
    m.process(sine(kRate / 10, 0.0)); // a short gap of silence between the tracks
    m.startTrack();
    m.process(sine(kRate, 0.1));
    const auto r = m.reading();
    CHECK_THAT(db(r.truePeakMax[0]), WithinAbs(-20.0, 0.05));
    CHECK(r.trackFrames == kRate);
}

TEST_CASE("Interpolated values from before a track boundary count for the previous track", "[meter]")
{
    // A full-scale ISP-like pattern stops abruptly at the boundary and the next
    // track is silence. The interpolator lags the input by half its window, so
    // just after startTrack() it still reports values from before the
    // boundary (+3 dB here); those must not become the new track's maximum.
    // What it reports for after the boundary is the ringing of the abrupt
    // stop, far below full scale.
    LevelMeter m(kRate, 1);
    std::vector<double> isp(kRate);
    for (std::size_t i = 0; i < isp.size(); ++i)
        isp[i] = (i % 4 < 2) ? 1.0 : -1.0;
    m.process(isp);
    m.startTrack();
    m.process(std::vector<double>(kRate, 0.0));
    CHECK(m.reading().truePeakMax[0] < 0.5);
}

TEST_CASE("finish() lets a peak in the last samples count", "[meter]")
{
    // The same as the analysis: the interpolator runs past the last sample.
    std::vector<double> v(1000, 0.0);
    v[998] = 0.6;
    v[999] = 0.6;
    LevelMeter m(kRate, 1);
    m.process(v);
    const double before = m.reading().truePeakMax[0];
    m.finish();
    TruePeakMeter analysis(1);
    analysis.process(v);
    analysis.finish();
    CHECK(before < 0.6);
    CHECK(m.reading().truePeakMax[0] == analysis.truePeak());
    CHECK(m.reading().trackFrames == 1000); // the implied silence is not counted as audio
}
