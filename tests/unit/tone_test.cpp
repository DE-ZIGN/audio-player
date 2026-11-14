// Task 9-2: the tone controls (BASS, TREBLE) - two gentle shelves with the
// EQ's automatic preamp, separate from the parametric EQ.

#include <ap/engine/dsp/tone.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace ap::engine;

namespace {

constexpr double kRate = 48000;

// The level of a sine at f after the stage (dB), from a least-squares fit on
// the second half second, once the filters have settled.
double measuredDb(ToneStage& t, double f)
{
    const std::size_t n = static_cast<std::size_t>(kRate);
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = 0.25 * std::sin(2 * std::numbers::pi * f * static_cast<double>(i) / kRate);
    for (std::size_t i = 0; i < n; i += 480)
        t.process(std::span(x).subspan(i, 480), 1);
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
    for (std::size_t i = n / 2; i < n; ++i)
    {
        const double ph = 2 * std::numbers::pi * f * static_cast<double>(i) / kRate;
        const double s = std::sin(ph), c = std::cos(ph);
        ss += s * s, cc += c * c, sc += s * c, ys += x[i] * s, yc += x[i] * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    return 20 * std::log10(std::hypot(a, b) / 0.25);
}

} // namespace

TEST_CASE("Tone: both at 0 dB is the identity (bypassed)", "[dsp][tone]")
{
    ToneStage t;
    t.prepare(48000, 2);
    CHECK(t.isIdentity());
    CHECK(t.latencyFrames() == 0);
    t.setBassDb(3.0);
    t.prepare(48000, 2);
    CHECK_FALSE(t.isIdentity());
    t.setBassDb(0.0);
    t.prepare(48000, 2); // a new session: no crossfade to wait for
    CHECK(t.isIdentity());
}

TEST_CASE("Tone: the controls are a low shelf and a high shelf, within +-10 dB", "[dsp][tone]")
{
    ToneStage t;
    t.setBassDb(4.5);
    t.setTrebleDb(-30.0);
    CHECK(t.bassDb() == 4.5);
    CHECK(t.trebleDb() == -ToneStage::kMaxDb);
    const auto b = t.bands();
    REQUIRE(b.size() == 2);
    CHECK(b[0] == EqBand{EqFilterType::LowShelf, ToneStage::kBassHz, 4.5, ToneStage::kShelfQ, true});
    CHECK(b[1] == EqBand{EqFilterType::HighShelf, ToneStage::kTrebleHz, -ToneStage::kMaxDb, ToneStage::kShelfQ, true});
    t.setBassDb(std::nan(""));
    CHECK(t.bassDb() == 0.0);
}

TEST_CASE("Tone: the measured response follows the shelves, with the boost taken off by the preamp", "[dsp][tone]")
{
    ToneStage t;
    t.setBassDb(6.0);
    t.setTrebleDb(-4.0);
    t.prepare(48000, 1);
    // The largest boost (the bass end) becomes 0 dB.
    CHECK(std::abs(t.preampDb() - eqAutoPreampDb(t.bands(), kRate)) < 1e-9);
    CHECK(t.preampDb() < -5.5);
    for (const double f : {30.0, 150.0, 1000.0, 4500.0, 16000.0})
    {
        CAPTURE(f);
        const double expected = eqResponseDb(t.bands(), f, kRate) + t.preampDb();
        CHECK(std::abs(measuredDb(t, f) - expected) < 0.01);
    }
    // Shape: the bass end is 6 dB over the middle at its extreme, the treble end 4 dB under.
    CHECK(eqResponseDb(t.bands(), 20, kRate) - eqResponseDb(t.bands(), 1000, kRate) > 5.0);
    CHECK(eqResponseDb(t.bands(), 20000, kRate) - eqResponseDb(t.bands(), 1000, kRate) < -3.0);
}

TEST_CASE("Tone: turning a control while playing crossfades (no step)", "[dsp][tone]")
{
    ToneStage t;
    t.setBassDb(-10.0);
    t.prepare(48000, 1);
    std::vector<double> x(48000);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = 0.5 * std::sin(2 * std::numbers::pi * 100 * static_cast<double>(i) / kRate);
    for (std::size_t i = 0; i < x.size(); i += 480)
    {
        if (i == 24000)
            t.setBassDb(10.0);
        t.process(std::span(x).subspan(i, 480), 1);
    }
    double worst = 0;
    for (std::size_t i = 1; i < x.size(); ++i)
        worst = std::max(worst, std::abs(x[i] - x[i - 1]));
    CHECK(worst < 0.01); // a 100 Hz sine at 0.5 moves at most 0.0065 a sample
}
