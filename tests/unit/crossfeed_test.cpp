// Task 9-5: crossfeed for headphones (the bs2b method) - how much of each
// side reaches the other and at which frequencies, the levels, mono and the
// bypass, and smooth switching.

#include <ap/engine/dsp/crossfeed.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using namespace ap::engine;

namespace {

constexpr double kRate = 48000;

double dB(double x)
{
    return 20.0 * std::log10(std::max(x, 1e-300));
}

struct Out
{
    double left, right; // amplitudes of the sine in each output channel
};

// A sine at f into the left channel (and into the right too when `mono`),
// measured on the second half second by a least-squares fit.
Out measure(CrossfeedStage& c, double f, bool mono)
{
    const std::size_t n = static_cast<std::size_t>(kRate);
    std::vector<double> x(n * 2);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double v = 0.25 * std::sin(2 * std::numbers::pi * f * static_cast<double>(i) / kRate);
        x[2 * i] = v;
        x[2 * i + 1] = mono ? v : 0.0;
    }
    for (std::size_t i = 0; i < x.size(); i += 960)
        c.process(std::span(x).subspan(i, 960), 2);
    auto fit = [&](int ch) {
        double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
        for (std::size_t i = n / 2; i < n; ++i)
        {
            const double ph = 2 * std::numbers::pi * f * static_cast<double>(i) / kRate;
            const double s = std::sin(ph), co = std::cos(ph), y = x[2 * i + ch];
            ss += s * s, cc += co * co, sc += s * co, ys += y * s, yc += y * co;
        }
        const double det = ss * cc - sc * sc;
        return std::hypot((ys * cc - yc * sc) / det, (yc * ss - ys * sc) / det) / 0.25;
    };
    return {fit(0), fit(1)};
}

std::unique_ptr<CrossfeedStage> prepared(CrossfeedLevel level)
{
    auto c = std::make_unique<CrossfeedStage>();
    c->setLevel(level);
    c->setEnabled(true);
    c->prepare(48000, 2);
    return c;
}

} // namespace

TEST_CASE("Crossfeed: off is the identity; no latency", "[dsp][crossfeed]")
{
    CrossfeedStage c;
    c.prepare(48000, 2);
    CHECK(c.isIdentity());
    CHECK(c.latencyFrames() == 0);
    c.setEnabled(true);
    c.prepare(48000, 2);
    CHECK_FALSE(c.isIdentity());
    // Only stereo is crossfed: mono and more channels pass as they are.
    c.prepare(48000, 1);
    CHECK(c.isIdentity());
    c.prepare(48000, 6);
    CHECK(c.isIdentity());
}

TEST_CASE("Crossfeed: low frequencies reach the other side at the level's feed, high ones hardly", "[dsp][crossfeed]")
{
    struct Case
    {
        CrossfeedLevel level;
        double feedDb;
    };
    for (const auto [level, feed] : {Case{CrossfeedLevel::Default, 4.5}, Case{CrossfeedLevel::Cmoy, 6.0},
                                     Case{CrossfeedLevel::Jmeier, 9.5}})
    {
        CAPTURE(static_cast<int>(level));
        auto c = prepared(level);
        CHECK(c->feedDb() == feed);
        const auto low = measure(*c, 30, false);
        CHECK(std::abs(dB(low.right / low.left) + feed) < 0.3);
        const auto high = measure(*c, 10000, false);
        CHECK(dB(high.right / high.left) < -20.0);
    }
}

TEST_CASE("Crossfeed: a centred (mono) sound keeps its level at low frequencies", "[dsp][crossfeed]")
{
    auto c = prepared(CrossfeedLevel::Default);
    const auto low = measure(*c, 30, true);
    CHECK(std::abs(dB(low.left)) < 0.1);
    CHECK(std::abs(low.left - low.right) < 1e-9);
    // At high frequencies the crossfed part has gone and the direct part is
    // not lifted all the way back: the centre comes out a little lower (what
    // the method does; measured, not hidden).
    const auto high = measure(*c, 15000, true);
    CAPTURE(dB(high.left));
    CHECK(dB(high.left) < -1.0);
    CHECK(dB(high.left) > -3.0);
}

TEST_CASE("Crossfeed: switching on, off or the level while playing has no step", "[dsp][crossfeed]")
{
    CrossfeedStage c;
    c.setEnabled(false);
    c.prepare(48000, 2);
    std::vector<double> x(48000 * 2);
    for (std::size_t i = 0; i < x.size() / 2; ++i)
    {
        x[2 * i] = 0.5 * std::sin(2 * std::numbers::pi * 100 * static_cast<double>(i) / kRate);
        x[2 * i + 1] = -x[2 * i]; // opposite sides: the crossfeed changes the most
    }
    for (std::size_t i = 0; i < x.size(); i += 960)
    {
        if (i == 960 * 10)
            c.setEnabled(true);
        if (i == 960 * 20)
            c.setLevel(CrossfeedLevel::Jmeier);
        if (i == 960 * 30)
            c.setEnabled(false);
        c.process(std::span(x).subspan(i, 960), 2);
    }
    double worst = 0;
    for (std::size_t i = 2; i < x.size(); ++i)
        worst = std::max(worst, std::abs(x[i] - x[i - 2]));
    CHECK(worst < 0.01); // a 100 Hz sine at 0.5 moves at most 0.0065 a sample
    CHECK(c.isIdentity()); // off again, once faded out
}
