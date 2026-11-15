// Task 9-8 (ロマン枠, quality-measures 34): the tube's warm-up and the
// difference between tubes of one type, as a show. What is tested is what
// they do (the model's curves, the level, the harmonics as measured), never
// that anything sounds better.

#include <ap/engine/dsp/tube.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <set>
#include <vector>

using namespace ap::engine;

namespace {

constexpr double kPi = std::numbers::pi;
const TubeType kTypes[] = {TubeType::Se300B, TubeType::PpEl34, TubeType::PpClassA};

double dB(double x)
{
    return 20.0 * std::log10(std::max(x, 1e-300));
}

std::vector<double> run(TubeStage& stage, std::vector<double> x, std::uint16_t channels = 1, std::size_t block = 480)
{
    const std::size_t step = block * channels;
    for (std::size_t i = 0; i < x.size(); i += step)
        stage.process(std::span(x).subspan(i, std::min(step, x.size() - i)), channels);
    return x;
}

// A sine on every channel (interleaved).
std::vector<double> sine(double freq, double amplitude, std::size_t frames, double rate, std::uint16_t channels = 1)
{
    std::vector<double> x(frames * channels);
    for (std::size_t i = 0; i < frames; ++i)
        for (std::uint16_t c = 0; c < channels; ++c)
            x[i * channels + c] = amplitude * std::sin(2 * kPi * freq * static_cast<double>(i) / rate);
    return x;
}

// Amplitude of `freq` in channel `ch` over frames [from, from + n) (whole cycles).
double amplitude(const std::vector<double>& x, std::uint16_t channels, std::uint16_t ch, std::size_t from,
                 std::size_t n, double freq, double rate)
{
    std::complex<double> s{};
    for (std::size_t i = 0; i < n; ++i)
        s += x[(from + i) * channels + ch] * std::polar(1.0, -2 * kPi * freq * static_cast<double>(from + i) / rate);
    return std::abs(s) * 2.0 / static_cast<double>(n);
}

struct Measured
{
    double level; // the fundamental's gain (dB)
    double h2, h3; // dB relative to the fundamental
};

// 1 kHz at `levelDb` on every channel at 48 kHz; the last second of two.
std::vector<Measured> measure(TubeStage& t, double levelDb, std::uint16_t channels = 1)
{
    const double rate = 48000, a = std::pow(10.0, levelDb / 20);
    const auto y = run(t, sine(1000, a, 96000, rate, channels), channels);
    std::vector<Measured> m;
    for (std::uint16_t c = 0; c < channels; ++c)
    {
        const double f1 = amplitude(y, channels, c, 48000, 48000, 1000, rate);
        m.push_back({dB(f1 / a), dB(amplitude(y, channels, c, 48000, 48000, 2000, rate) / f1),
                     dB(amplitude(y, channels, c, 48000, 48000, 3000, rate) / f1)});
    }
    return m;
}

std::unique_ptr<TubeStage> prepared(TubeType type, double driveDb, std::uint16_t channels = 1)
{
    auto t = std::make_unique<TubeStage>();
    t->setEnabled(true);
    t->setType(type);
    t->setDriveDb(driveDb);
    t->prepare(48000, channels);
    return t;
}

} // namespace

TEST_CASE("Tube units: each number gives the same tubes every time, within the tolerances", "[dsp][tube][tube-character]")
{
    std::set<double> emissions;
    for (int n = kMinTubeUnit; n <= kMaxTubeUnit; ++n)
        for (const auto type : kTypes)
            for (int ch = 0; ch < 2; ++ch)
                for (int tube = 0; tube < 2; ++tube)
                {
                    const auto u = tubeUnitFor(n, type, ch, tube);
                    CHECK(u == tubeUnitFor(n, type, ch, tube));
                    CHECK(std::abs(u.emission - 1.0) <= kTubeEmissionTolerance);
                    CHECK(std::abs(u.mu - 1.0) <= kTubeMuTolerance);
                    emissions.insert(u.emission);
                }
    // Different numbers, channels and tubes are different tubes.
    CHECK(emissions.size() == static_cast<std::size_t>(kMaxTubeUnit - kMinTubeUnit + 1) * 3 * 2 * 2);
    // They spread over the range (not all near 1).
    CHECK(*emissions.begin() < 1.0 - 0.8 * kTubeEmissionTolerance);
    CHECK(*emissions.rbegin() > 1.0 + 0.8 * kTubeEmissionTolerance);
}

TEST_CASE("Tube variant curves: nominal tubes at full emission are the type's own curve", "[dsp][tube][tube-character]")
{
    for (const auto type : kTypes)
    {
        CAPTURE(static_cast<int>(type));
        const auto& n = TubeCurve::forType(type);
        const auto v = TubeCurve::variant(type, TubeUnit{}, TubeUnit{}, 1.0);
        CHECK(v->range() == n.range());
        CHECK(std::abs(v->smallSignalGain() / n.smallSignalGain() - 1.0) < 1e-9);
        for (const double f : {-0.9, -0.3, 0.2, 0.7, 1.5})
        {
            const double x = f * n.gridToZero();
            CHECK(std::abs(v->out(x) - n.out(x)) < 1e-9 * n.gridToZero() * n.smallSignalGain());
        }
    }
}

TEST_CASE("Tube variant curves: a cold tube draws less current; the curve still idles at 0", "[dsp][tube][tube-character]")
{
    for (const auto type : kTypes)
    {
        CAPTURE(static_cast<int>(type));
        const auto warm = TubeCurve::variant(type, TubeUnit{}, TubeUnit{}, 1.0);
        const auto cold = TubeCurve::variant(type, TubeUnit{}, TubeUnit{}, TubeStage::kColdEmission);
        CHECK(cold->idleCurrent() < warm->idleCurrent());
        CHECK(std::abs(cold->out(0.0)) < 1e-9);
        CHECK(cold->smallSignalGain() > 0.0);
        CHECK(cold->smallSignalGain() < warm->smallSignalGain());
    }
}

TEST_CASE("Tube variant curves: mismatched push-pull tubes no longer cancel the even harmonics", "[dsp][tube][tube-character]")
{
    const TubeUnit a{1.08, 1.02}, b{0.94, 0.99};
    for (const auto type : {TubeType::PpEl34, TubeType::PpClassA})
    {
        CAPTURE(static_cast<int>(type));
        const auto matched = TubeCurve::variant(type, a, a, 1.0);
        const auto mismatched = TubeCurve::variant(type, a, b, 1.0);
        const double v = 0.7 * TubeCurve::forType(type).gridToZero();
        CHECK(std::abs(matched->out(v) + matched->out(-v)) < 1e-6 * std::abs(matched->out(v)));
        CHECK(std::abs(mismatched->out(v) + mismatched->out(-v)) > 1e-3 * std::abs(mismatched->out(v)));
    }
}

TEST_CASE("Tube warm-up: the cold share falls from 1 at switch-on to 0 at the settled time", "[dsp][tube][tube-character]")
{
    CHECK(TubeStage::coldShare(0.0) == 1.0);
    CHECK(TubeStage::coldShare(-5.0) == 1.0);
    CHECK(TubeStage::coldShare(TubeStage::kWarmUpSeconds) == 0.0);
    CHECK(TubeStage::coldShare(1e9) == 0.0);
    double last = 2.0;
    for (double s = 0.0; s <= TubeStage::kWarmUpSeconds; s += 30.0)
    {
        const double c = TubeStage::coldShare(s);
        CHECK(c < last);
        last = c;
    }
    // Most of it in the first minutes (time constant kWarmUpTauSeconds).
    CHECK(TubeStage::coldShare(TubeStage::kWarmUpTauSeconds) < 0.4);
    CHECK(TubeStage::coldShare(TubeStage::kWarmUpTauSeconds) > 0.3);
}

TEST_CASE("Tube character: off, or warmed up with no unit, is the plain stage to the bit", "[dsp][tube][tube-character]")
{
    for (const auto type : kTypes)
    {
        CAPTURE(static_cast<int>(type));
        const auto x = sine(1000, 0.7, 24000, 48000, 2);
        auto plain = prepared(type, 0.0, 2);
        const auto expected = run(*plain, x, 2);

        auto off = prepared(type, 0.0, 2);
        off->setCharacter(false, std::nullopt);
        CHECK(run(*off, x, 2) == expected);

        auto warm = std::make_unique<TubeStage>();
        warm->setEnabled(true);
        warm->setType(type);
        warm->setDriveDb(0.0);
        warm->setCharacter(true, std::nullopt);
        warm->setWarmUpSeconds(TubeStage::kWarmUpSeconds);
        warm->prepare(48000, 2);
        CHECK(run(*warm, x, 2) == expected);
    }
}

TEST_CASE("Tube warm-up: a cold tube bends differently at the same level", "[dsp][tube][tube-character]")
{
    for (const auto type : kTypes)
    {
        CAPTURE(static_cast<int>(type));
        auto warm = prepared(type, 0.0);
        const auto w = measure(*warm, -6.0)[0];
        auto cold = prepared(type, 0.0);
        cold->setCharacter(true, std::nullopt);
        cold->setWarmUpSeconds(0.0);
        const auto c = measure(*cold, -6.0)[0];
        CAPTURE(w.level, w.h2, w.h3, c.level, c.h2, c.h3);
        // Small signals keep unity gain (below); at this level the curves compress
        // a little differently.
        CHECK(std::abs(c.level - w.level) < 0.6);
        // The bending changes measurably.
        CHECK(std::max(std::abs(c.h2 - w.h2), std::abs(c.h3 - w.h3)) > 0.5);
    }
}

TEST_CASE("Tube warm-up: small signals keep unity gain at every stage of the warm-up", "[dsp][tube][tube-character]")
{
    for (const auto type : kTypes)
        for (const double s : {0.0, 120.0, 600.0})
        {
            CAPTURE(static_cast<int>(type), s);
            auto t = prepared(type, TubeStage::kDefaultDriveDb);
            t->setCharacter(true, std::nullopt);
            t->setWarmUpSeconds(s);
            CHECK(std::abs(measure(*t, -60.0)[0].level) < 0.01);
        }
}

TEST_CASE("Tube units: each number keeps unity gain, the channels differ, push-pull shows a 2nd harmonic", "[dsp][tube][tube-character]")
{
    for (const auto type : kTypes)
    {
        CAPTURE(static_cast<int>(type));
        auto nominal = prepared(type, 0.0, 2);
        const auto n = measure(*nominal, -6.0, 2);
        for (int unit : {1, 2, 7})
        {
            CAPTURE(unit);
            auto small = prepared(type, TubeStage::kDefaultDriveDb, 2);
            small->setCharacter(false, unit);
            for (const auto& m : measure(*small, -60.0, 2))
                CHECK(std::abs(m.level) < 0.01);

            auto t = prepared(type, 0.0, 2);
            t->setCharacter(false, unit);
            const auto m = measure(*t, -6.0, 2);
            CAPTURE(m[0].h2, m[0].h3, m[1].h2, m[1].h3, n[0].h2, n[0].h3);
            for (const auto& c : m)
                CHECK(std::abs(c.level - n[0].level) < 0.6);
            // Left and right are different tubes.
            CHECK(std::max(std::abs(m[0].h2 - m[1].h2), std::abs(m[0].h3 - m[1].h3)) > 0.05);
            if (type != TubeType::Se300B)
                for (const auto& c : m)
                    CHECK(c.h2 > n[0].h2 + 20.0); // nominal push-pull: below -100 dB
        }
    }
}

TEST_CASE("Tube units: the same number plays the same, another number differently", "[dsp][tube][tube-character]")
{
    const auto x = sine(1000, 0.5, 24000, 48000, 2);
    auto one = prepared(TubeType::Se300B, 0.0, 2);
    one->setCharacter(false, 3);
    auto again = prepared(TubeType::Se300B, 0.0, 2);
    again->setCharacter(false, 3);
    auto other = prepared(TubeType::Se300B, 0.0, 2);
    other->setCharacter(false, 4);
    const auto a = run(*one, x, 2);
    CHECK(run(*again, x, 2) == a);
    CHECK(run(*other, x, 2) != a);
    CHECK(one->unit() == 3);
}

TEST_CASE("Tube character: switching the warm-up and the unit while playing is smooth", "[dsp][tube][tube-character]")
{
    auto t = prepared(TubeType::PpEl34, 6.0);
    auto y = sine(100, 0.5, 48000, 48000);
    for (std::size_t i = 0; i < y.size(); i += 480)
    {
        if (i == 9600)
        {
            t->setCharacter(true, std::nullopt);
            t->setWarmUpSeconds(0.0);
        }
        if (i == 19200)
            t->setCharacter(true, 5);
        if (i == 28800)
            t->setCharacter(false, 9);
        if (i == 38400)
            t->setCharacter(false, std::nullopt);
        t->process(std::span(y).subspan(i, 480), 1);
    }
    double worst = 0;
    for (std::size_t i = 1; i < y.size(); ++i)
        worst = std::max(worst, std::abs(y[i] - y[i - 1]));
    // A 100 Hz sine at 0.5 moves at most 0.0065 a sample; a jump would be far more.
    CAPTURE(worst);
    CHECK(worst < 0.012);
}

TEST_CASE("Tube character: the path display carries the warm-up time and the unit", "[dsp][tube][tube-character]")
{
    auto t = prepared(TubeType::PpEl34, 0.0);
    CHECK(t->displayDetail() == "ppel34");
    t->setCharacter(true, std::nullopt);
    t->setWarmUpSeconds(185.4);
    CHECK(t->displayDetail() == "ppel34;warmup=185");
    t->setCharacter(true, 12);
    CHECK(t->displayDetail() == "ppel34;warmup=185;unit=12");
    t->setCharacter(false, 12);
    CHECK(t->displayDetail() == "ppel34;unit=12");
    // Out of range: kept within the numbers there are.
    t->setCharacter(false, 1000);
    CHECK(t->unit() == kMaxTubeUnit);
}
