// Task 3-6: parametric EQ. 周波数特性の測定テスト: sines are played through the
// stage and their output level is measured; it must match the response
// computed independently from the Audio EQ Cookbook formulas.

#include <ap/engine/dsp/eq.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace ap::engine;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kRate = 48000.0;

// The Cookbook's analog-prototype responses evaluated with the bilinear
// transform's frequency warping, written independently of the engine's code:
// |H| from the s-domain formulas at s = j * tan(w/2) / tan(w0/2).
double referenceDb(const EqBand& b, double f, double fs)
{
    const double a = std::pow(10.0, b.gainDb / 40.0);
    const double w = 2 * kPi * f / fs;
    const double w0 = 2 * kPi * b.frequency / fs;
    const std::complex<double> s(0.0, std::tan(w / 2) / std::tan(w0 / 2));
    const double q = b.q;
    std::complex<double> h;
    switch (b.type)
    {
        case EqFilterType::Peak:
            h = (s * s + s * (a / q) + 1.0) / (s * s + s / (a * q) + 1.0);
            break;
        case EqFilterType::LowShelf:
            h = a * (s * s + (std::sqrt(a) / q) * s + a) / (a * s * s + (std::sqrt(a) / q) * s + 1.0);
            break;
        case EqFilterType::HighShelf:
            h = a * (a * s * s + (std::sqrt(a) / q) * s + 1.0) / (s * s + (std::sqrt(a) / q) * s + a);
            break;
        case EqFilterType::LowPass:
            h = 1.0 / (s * s + s / q + 1.0);
            break;
        case EqFilterType::HighPass:
            h = (s * s) / (s * s + s / q + 1.0);
            break;
    }
    return 20 * std::log10(std::abs(h));
}

// Output level (dB relative to the input) of a steady sine through the stage.
double measuredDb(EqStage& eq, double f, double fs = kRate)
{
    eq.prepare(static_cast<std::uint32_t>(fs), 1);
    const std::size_t settle = static_cast<std::size_t>(fs); // 1 s for the filters to settle
    const std::size_t n = static_cast<std::size_t>(fs);      // then 1 s to measure
    std::vector<double> x(settle + n);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = 0.25 * std::sin(2 * kPi * f * static_cast<double>(i) / fs);
    eq.process(x, 1);
    // Least squares fit of a sine at f on the measured part.
    double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
    for (std::size_t i = settle; i < x.size(); ++i)
    {
        const double ph = 2 * kPi * f * static_cast<double>(i) / fs;
        const double sn = std::sin(ph), cs = std::cos(ph);
        ss += sn * sn;
        sc += sn * cs;
        cc += cs * cs;
        ys += x[i] * sn;
        yc += x[i] * cs;
    }
    const double det = ss * cc - sc * sc;
    const double as = (ys * cc - yc * sc) / det;
    const double ac = (yc * ss - ys * sc) / det;
    return 20 * std::log10(std::hypot(as, ac) / 0.25);
}

} // namespace

TEST_CASE("EQ design: peaks reach their gain at Fc; shelves reach theirs at the ends", "[dsp][eq]")
{
    const EqBand pk{EqFilterType::Peak, 1000, 6.0, 1.4};
    CHECK(std::abs(eqResponseDb({pk}, 1000, kRate) - 6.0) < 1e-9);
    const EqBand cut{EqFilterType::Peak, 3000, -9.0, 4.0};
    CHECK(std::abs(eqResponseDb({cut}, 3000, kRate) - -9.0) < 1e-9);
    const EqBand ls{EqFilterType::LowShelf, 200, 4.0, 0.707};
    CHECK(std::abs(eqResponseDb({ls}, 5, kRate) - 4.0) < 0.01);
    CHECK(std::abs(eqResponseDb({ls}, 20000, kRate)) < 0.01);
    const EqBand hs{EqFilterType::HighShelf, 8000, -5.0, 0.707};
    CHECK(std::abs(eqResponseDb({hs}, 23000, kRate) - -5.0) < 0.05);
    CHECK(std::abs(eqResponseDb({hs}, 20, kRate)) < 0.01);
    const EqBand lp{EqFilterType::LowPass, 5000, 0.0, 0.7071067811865476};
    CHECK(std::abs(eqResponseDb({lp}, 5000, kRate) - -3.0103) < 0.001);
    CHECK_FALSE(designBiquad(EqBand{EqFilterType::Peak, 24000, 3.0, 1.0}, kRate).has_value()); // above 0.49 fs
}

TEST_CASE("EQ design agrees with the Cookbook's analog prototypes (bilinear warping)", "[dsp][eq]")
{
    const auto type = GENERATE(EqFilterType::Peak, EqFilterType::LowShelf, EqFilterType::HighShelf,
                               EqFilterType::LowPass, EqFilterType::HighPass);
    const EqBand band{type, 1500, type == EqFilterType::Peak ? 7.5 : -6.0, 0.9};
    for (double f : {30.0, 200.0, 1000.0, 1500.0, 2500.0, 8000.0, 16000.0, 22000.0})
    {
        CAPTURE(static_cast<int>(type), f);
        CHECK(std::abs(eqResponseDb({band}, f, kRate) - referenceDb(band, f, kRate)) < 1e-6);
    }
}

TEST_CASE("EQ frequency response measured with sines matches the design (and the preamp)", "[dsp][eq]")
{
    const std::vector<EqBand> bands{
        {EqFilterType::LowShelf, 105, 4.5, 0.7},
        {EqFilterType::Peak, 300, -3.2, 1.2},
        {EqFilterType::Peak, 2900, 5.0, 2.0},
        {EqFilterType::HighShelf, 10000, -4.0, 0.7},
    };
    EqStage eq;
    eq.setBands(bands);
    eq.prepare(48000, 1);
    const double preamp = eq.preampDb();
    CHECK(preamp < -4.0); // the largest boost is lowered
    for (double f : {40.0, 105.0, 300.0, 1000.0, 2900.0, 5000.0, 10000.0, 18000.0})
    {
        const double expected = eqResponseDb(bands, f, kRate) + preamp;
        const double got = measuredDb(eq, f);
        CAPTURE(f, expected, got);
        CHECK(std::abs(got - expected) < 0.01);
    }
}

TEST_CASE("EQ preamp: the largest boost of the combined response becomes 0 dB", "[dsp][eq]")
{
    const std::vector<EqBand> bands{{EqFilterType::Peak, 1000, 6.0, 1.0}, {EqFilterType::Peak, 1300, 4.0, 1.0}};
    const double preamp = eqAutoPreampDb(bands, kRate);
    double maxDb = -100;
    for (double f = 500; f < 3000; f += 1)
        maxDb = std::max(maxDb, eqResponseDb(bands, f, kRate) + preamp);
    CHECK(std::abs(maxDb) < 0.01);
    CHECK(eqAutoPreampDb({{EqFilterType::Peak, 1000, -6.0, 1.0}}, kRate) == 0.0); // cuts only: no preamp
}

TEST_CASE("EQ: all bands at 0 dB is identity; pass filters are not", "[dsp][eq]")
{
    EqStage eq;
    CHECK(eq.isIdentity());
    eq.setBands({{EqFilterType::Peak, 1000, 0.0, 1.0}, {EqFilterType::LowShelf, 100, 0.0, 0.7}});
    CHECK(eq.isIdentity());
    eq.setBands({{EqFilterType::HighPass, 20, 0.0, 0.7}});
    CHECK_FALSE(eq.isIdentity());
    eq.setBands({{EqFilterType::Peak, 1000, 3.0, 1.0, false}}); // switched off
    eq.prepare(48000, 2);
    CHECK(eq.isIdentity());
}

TEST_CASE("EQ: a change while playing crossfades (no step in the waveform)", "[dsp][eq]")
{
    EqStage eq;
    eq.setBands({{EqFilterType::Peak, 1000, 0.5, 1.0}});
    eq.prepare(48000, 1);
    std::vector<double> x(48000);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = 0.5 * std::sin(2 * kPi * 1000 * static_cast<double>(i) / 48000);
    std::vector<double> first(x.begin(), x.begin() + 24000), second(x.begin() + 24000, x.end());
    eq.process(first, 1);
    eq.setBands({{EqFilterType::Peak, 1000, 12.0, 1.0}}); // a big jump of settings
    eq.process(second, 1);
    double worst = 0;
    std::vector<double> y(first);
    y.insert(y.end(), second.begin(), second.end());
    for (std::size_t i = 1; i < y.size(); ++i)
        worst = std::max(worst, std::abs(y[i] - y[i - 1]));
    // A 1 kHz sine of amplitude <= 0.5 * 4 (12 dB) moves at most 2 * pi * 1000 / 48000 * 2 per sample.
    CHECK(worst < 2 * kPi * 1000 / 48000 * 2.0 * 1.05);
}

TEST_CASE("EQ: returning to flat stays processed until the crossfade has finished", "[dsp][eq]")
{
    EqStage eq;
    eq.setBands({{EqFilterType::Peak, 1000, 6.0, 1.0}});
    eq.prepare(48000, 1);
    std::vector<double> x(1000, 0.1);
    eq.process(x, 1);
    eq.setBands({{EqFilterType::Peak, 1000, 0.0, 1.0}});
    CHECK_FALSE(eq.isIdentity());
    std::vector<double> y(100, 0.1);
    eq.process(y, 1);
    CHECK_FALSE(eq.isIdentity()); // 100 of 480 frames
    std::vector<double> z(1000, 0.1);
    eq.process(z, 1);
    CHECK(eq.isIdentity());
}

TEST_CASE("AutoEQ and Equalizer APO text is read", "[dsp][eq][autoeq]")
{
    const auto p = parseEqPreset("\xEF\xBB\xBFPreamp: -6.2 dB\r\n"
                                 "Filter 1: ON LSC Fc 105 Hz Gain 5.6 dB Q 0.70\r\n"
                                 "Filter 2: ON PK Fc 31 Hz Gain -2.1 dB Q 1.41\n"
                                 "Filter 3: OFF PK Fc 400 Hz Gain 3 dB Q 1\n"
                                 "Filter 4: ON HSC Fc 10000 Hz Gain -3.4 dB Q 0.70\n"
                                 "# a comment\n"
                                 "Filter: ON PK Fc 2000 Hz Gain 1.5 dB Q 2\n"
                                 "Filter 6: ON BP Fc 1000 Hz Q 1\n"
                                 "Filter 7: ON PK Fc abc Hz Gain 1 dB Q 1\n"
                                 "Channel: L\n");
    REQUIRE(p.filePreampDb.has_value());
    CHECK(*p.filePreampDb == -6.2);
    REQUIRE(p.bands.size() == 4);
    CHECK(p.bands[0] == EqBand{EqFilterType::LowShelf, 105, 5.6, 0.70});
    CHECK(p.bands[1] == EqBand{EqFilterType::Peak, 31, -2.1, 1.41});
    CHECK(p.bands[2] == EqBand{EqFilterType::HighShelf, 10000, -3.4, 0.70});
    CHECK(p.bands[3] == EqBand{EqFilterType::Peak, 2000, 1.5, 2.0});
    CHECK(p.warnings.size() == 3); // BP, the unreadable number, and "Channel:"
}

TEST_CASE("EQ: bands that cannot be realised at the rate are skipped and counted", "[dsp][eq]")
{
    EqStage eq;
    eq.setBands({{EqFilterType::Peak, 1000, 3.0, 1.0}, {EqFilterType::Peak, 22000, 3.0, 1.0}});
    eq.prepare(44100, 2); // 22 kHz is above 0.49 * 44.1 kHz
    CHECK(eq.skippedBands() == 1);
    eq.prepare(96000, 2);
    CHECK(eq.skippedBands() == 0);
}
