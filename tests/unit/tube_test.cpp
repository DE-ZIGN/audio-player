// Task 9-1: the tube stage - the transfer curves from the tube models, the
// level reference, the harmonics each type makes, aliasing, the time element,
// smooth changes and the bypass.

#include <ap/engine/dsp/tube.h>
#include <ap/engine/dsp/volume.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>

#include "temp_dir.h"
#include "trigger_source.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <complex>
#include <cstring>
#include <filesystem>
#include <memory>
#include <numbers>
#include <vector>

using namespace ap::engine;

namespace {

constexpr double kPi = std::numbers::pi;

double dB(double x)
{
    return 20.0 * std::log10(std::max(x, 1e-300));
}

// Runs a mono signal through the stage in blocks, like playback does.
std::vector<double> run(TubeStage& stage, std::vector<double> x, std::size_t block = 480)
{
    for (std::size_t i = 0; i < x.size(); i += block)
        stage.process(std::span(x).subspan(i, std::min(block, x.size() - i)), 1);
    return x;
}

std::vector<double> sine(double freq, double amplitude, std::size_t frames, double rate)
{
    std::vector<double> x(frames);
    for (std::size_t i = 0; i < frames; ++i)
        x[i] = amplitude * std::sin(2 * kPi * freq * static_cast<double>(i) / rate);
    return x;
}

// Amplitude and phase of `freq` in x[from, from + n) (a whole number of cycles).
std::complex<double> component(const std::vector<double>& x, std::size_t from, std::size_t n, double freq, double rate)
{
    std::complex<double> s{};
    for (std::size_t i = 0; i < n; ++i)
        s += x[from + i] * std::polar(1.0, -2 * kPi * freq * static_cast<double>(from + i) / rate);
    return s * (2.0 / static_cast<double>(n));
}

double amplitude(const std::vector<double>& x, std::size_t from, std::size_t n, double freq, double rate)
{
    return std::abs(component(x, from, n, freq, rate));
}

std::unique_ptr<TubeStage> prepared(TubeType type, double driveDb, double rate)
{
    auto t = std::make_unique<TubeStage>();
    t->setEnabled(true);
    t->setType(type);
    t->setDriveDb(driveDb);
    t->prepare(static_cast<std::uint32_t>(rate), 1);
    return t;
}

struct Harmonics
{
    double h2, h3, thd; // dB relative to the fundamental
};

// 1 kHz at `levelDb` through the stage at 48 kHz, measured over the last second of two.
Harmonics harmonics(TubeType type, double driveDb, double levelDb)
{
    const double rate = 48000;
    auto t = prepared(type, driveDb, rate);
    const auto y = run(*t, sine(1000, std::pow(10.0, levelDb / 20), 96000, rate));
    const double f1 = amplitude(y, 48000, 48000, 1000, rate);
    double sum = 0;
    double h[6]{};
    for (int k = 2; k <= 5; ++k)
    {
        h[k] = amplitude(y, 48000, 48000, 1000.0 * k, rate);
        sum += h[k] * h[k];
    }
    return {dB(h[2] / f1), dB(h[3] / f1), dB(std::sqrt(sum) / f1)};
}

const TubeType kTypes[] = {TubeType::Se300B, TubeType::PpEl34, TubeType::PpClassA};

// The harmonics up to the 20th (the curves' high orders count when driven hard).
double distortion(TubeType type, double driveDb, double levelDb)
{
    const double rate = 48000;
    auto t = prepared(type, driveDb, rate);
    const auto y = run(*t, sine(1000, std::pow(10.0, levelDb / 20), 72000, rate));
    double sum = 0;
    for (int k = 2; k <= 20; ++k)
    {
        const double a = amplitude(y, 48000, 24000, 1000.0 * k, rate);
        sum += a * a;
    }
    return std::sqrt(sum) / amplitude(y, 48000, 24000, 1000, rate);
}

} // namespace

// What the note of PP CLASS A says (corrected 2026-11-22: it is not so when driven harder).
TEST_CASE("Tube: PP class A distorts least at 1 kHz up to DRIVE -6 dB, and not when driven hard", "[dsp][tube]")
{
    for (const double level : {-20.0, -12.0, -6.0, -3.0, -1.0, 0.0})
        for (double drive = -30; drive <= -6; drive += 2)
        {
            CAPTURE(level, drive);
            const double a = distortion(TubeType::PpClassA, drive, level);
            CHECK(a < distortion(TubeType::Se300B, drive, level));
            CHECK(a < distortion(TubeType::PpEl34, drive, level));
        }
    const double hard = distortion(TubeType::PpClassA, 6, -6);
    CHECK(hard > distortion(TubeType::Se300B, 6, -6));
    CHECK(hard > distortion(TubeType::PpEl34, 6, -6));
}

// What the stage's note says (the oversampling with ADAA, up to 100 kHz).
TEST_CASE("Tube: about 0.5 dB down at 20 kHz from 44.1 to 96 kHz, flat at 192 kHz", "[dsp][tube]")
{
    for (const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
        for (const auto type : kTypes)
        {
            CAPTURE(rate, static_cast<int>(type));
            const auto at = [&](double freq) {
                auto t = prepared(type, -30, rate);
                const auto second = static_cast<std::size_t>(rate);
                const auto y = run(*t, sine(freq, 0.001, 2 * second, rate));
                return amplitude(y, second, second, freq, rate);
            };
            const double drop = dB(at(20000) / at(1000));
            CAPTURE(drop);
            if (rate <= 96000)
            {
                CHECK(drop < -0.45);
                CHECK(drop > -0.60);
            }
            else
                CHECK(std::abs(drop) < 0.01);
        }
}

TEST_CASE("Tube curve: each type's model sits on its load line, idle at 0", "[dsp][tube]")
{
    for (const auto type : kTypes)
    {
        CAPTURE(static_cast<int>(type));
        const auto& c = TubeCurve::forType(type);
        CHECK(std::abs(c.solve(0.0).vout) < 1e-6);
        CHECK(std::abs(c.out(0.0)) < 1e-6);
        CHECK(c.gridToZero() > 10.0);
        CHECK(c.smallSignalGain() > 0.0);
        // The table follows the model.
        for (const double f : {-0.9, -0.5, -0.1, 0.05, 0.3, 0.8})
        {
            const double v = f * c.gridToZero();
            const double model = c.solve(v).vout;
            CHECK(std::abs(c.out(v) - model) < 1e-4 * c.gridToZero() * c.smallSignalGain());
        }
        // Rising: more grid, more output.
        double last = -1e9;
        for (double v = -c.gridToZero(); v <= c.gridToZero(); v += c.gridToZero() / 50)
        {
            CHECK(c.out(v) > last);
            last = c.out(v);
        }
    }
    // Push-pull is symmetric (even harmonics cancel); single-ended is not.
    for (const auto type : {TubeType::PpEl34, TubeType::PpClassA})
    {
        const auto& c = TubeCurve::forType(type);
        const double v = 0.7 * c.gridToZero();
        CHECK(std::abs(c.out(v) + c.out(-v)) < 1e-6 * std::abs(c.out(v)));
    }
    const auto& se = TubeCurve::forType(TubeType::Se300B);
    const double v = 0.7 * se.gridToZero();
    CHECK(std::abs(se.out(v) + se.out(-v)) > 0.01 * std::abs(se.out(v)));
}

TEST_CASE("Tube curve: the antiderivative belongs to the table", "[dsp][tube]")
{
    const auto& c = TubeCurve::forType(TubeType::Se300B);
    const double a = -0.4 * c.gridToZero(), b = 0.6 * c.gridToZero();
    double sum = 0;
    const int n = 20000;
    for (int i = 0; i < n; ++i)
        sum += c.out(a + (b - a) * (i + 0.5) / n) * (b - a) / n;
    CHECK(std::abs((c.antiderivative(b) - c.antiderivative(a)) - sum) < 1e-6 * std::abs(sum));
}

TEST_CASE("Tube: small signals pass at unity gain, delayed by the latency it reports", "[dsp][tube]")
{
    struct Case
    {
        double rate;
        unsigned factor;
    };
    for (const auto [rate, factor] : {Case{44100, 4}, Case{48000, 4}, Case{96000, 2}, Case{192000, 1}})
        for (const auto type : kTypes)
        {
            CAPTURE(rate, static_cast<int>(type));
            auto t = prepared(type, TubeStage::kDefaultDriveDb, rate);
            CHECK(t->oversampling() == factor);
            // At 1 kHz the 2 Hz DC blocker's phase lead is negligible (0.11 degree).
            const double f = 1000;
            const auto n = static_cast<std::size_t>(rate); // 1 s: whole cycles
            const auto x = sine(f, 0.001, 2 * n, rate);    // -60 dBFS
            const auto y = run(*t, x);
            const auto in = component(x, n, n, f, rate), out = component(y, n, n, f, rate);
            CHECK(std::abs(dB(std::abs(out) / std::abs(in))) < 0.01);
            // The delay as a phase lag, taken in the period nearest the latency.
            const double period = rate / f;
            double delay = std::arg(in / out) / (2 * kPi) * period;
            delay += std::round((static_cast<double>(t->latencyFrames()) - delay) / period) * period;
            CHECK(std::abs(delay - static_cast<double>(t->latencyFrames())) < 0.3);
        }
}

TEST_CASE("Tube: SE is mostly 2nd harmonic, PP EL34 mostly 3rd with the 2nd cancelled, PP class A bends less", "[dsp][tube]")
{
    const auto se = harmonics(TubeType::Se300B, 0.0, -6.0);
    const auto ab = harmonics(TubeType::PpEl34, 0.0, -6.0);
    const auto a = harmonics(TubeType::PpClassA, 0.0, -6.0);
    CAPTURE(se.h2, se.h3, se.thd, ab.h2, ab.h3, ab.thd, a.h2, a.h3, a.thd);
    CHECK(se.h2 > se.h3 + 6.0);
    CHECK(ab.h3 > ab.h2 + 40.0);
    CHECK(ab.h2 < -100.0);
    CHECK(a.h2 < -100.0);
    CHECK(a.thd < ab.thd);
}

TEST_CASE("Tube: more drive, more distortion (the level stays)", "[dsp][tube]")
{
    for (const auto type : kTypes)
    {
        CAPTURE(static_cast<int>(type));
        double last = -1e9;
        for (const double drive : {-24.0, -12.0, 0.0})
        {
            const auto h = harmonics(type, drive, -6.0);
            CAPTURE(drive, h.thd, h.h2, h.h3);
            CHECK(h.thd > last + 3.0);
            last = h.thd;
        }
    }
}

TEST_CASE("Tube: aliasing stays low with the oversampling and ADAA", "[dsp][tube]")
{
    // 9 kHz near full scale, DRIVE 0 dB, at 48 kHz: its harmonics above 24 kHz
    // must not come back folded into the audio band (27 kHz -> 21 kHz is above
    // 20 kHz; 36 kHz -> 12 kHz, 45 kHz -> 3 kHz, 54 kHz -> 6 kHz are not).
    const double rate = 48000;
    for (const auto type : kTypes)
    {
        CAPTURE(static_cast<int>(type));
        auto t = prepared(type, 0.0, rate);
        const auto y = run(*t, sine(9000, 0.7, 48000, rate));
        const std::size_t from = 24000, n = 4800; // 10 Hz bins, whole cycles
        const double f1 = amplitude(y, from, n, 9000, rate);
        double worst = -1e9, at = 0;
        for (double f = 100; f <= 20000; f += 10) // (below: the bias settling, not aliasing)
        {
            if (std::abs(f - 9000) < 25 || std::abs(f - 18000) < 25)
                continue;
            const double a = dB(amplitude(y, from, n, f, rate) / f1);
            if (a > worst)
                worst = a, at = f;
        }
        CAPTURE(worst, at);
        CHECK(worst < -90.0);
    }
}

TEST_CASE("Tube: off is the identity (bypassed, no latency)", "[dsp][tube]")
{
    TubeStage t;
    t.prepare(48000, 2);
    CHECK(t.isIdentity());
    CHECK(t.latencyFrames() == 0);
    t.setEnabled(true);
    CHECK_FALSE(t.isIdentity());
    CHECK(t.latencyFrames() == 64);
}

TEST_CASE("Tube: DRIVE and the type change smoothly", "[dsp][tube]")
{
    const double rate = 48000;
    auto t = prepared(TubeType::Se300B, -30.0, rate);
    auto x = sine(100, 0.5, 48000, rate);
    // Steep changes in the middle: drive -30 -> +6 dB, then the type twice.
    std::vector<double> y(x);
    for (std::size_t i = 0; i < y.size(); i += 480)
    {
        if (i == 9600)
            t->setDriveDb(6.0);
        if (i == 19200)
            t->setType(TubeType::PpEl34);
        if (i == 28800)
            t->setType(TubeType::PpClassA);
        t->process(std::span(y).subspan(i, 480), 1);
    }
    double worst = 0;
    for (std::size_t i = 1; i < y.size(); ++i)
        worst = std::max(worst, std::abs(y[i] - y[i - 1]));
    // A 100 Hz sine at 0.5 moves at most 0.0065 a sample; a jump would be far more.
    CAPTURE(worst);
    CHECK(worst < 0.012);
}

TEST_CASE("Tube: the time element - the bias moves after a loud passage and recovers", "[dsp][tube]")
{
    // SE 300B (cathode bias): a quiet tone, a loud second, the quiet tone again.
    const double rate = 48000;
    auto t = prepared(TubeType::Se300B, 0.0, rate);
    std::vector<double> x = sine(1000, 0.1, 48000 * 4, rate);
    for (std::size_t i = 48000; i < 96000; ++i)
        x[i] *= 10.0; // 0 dBFS for a second
    const auto y = run(*t, x);
    auto h2 = [&](std::size_t from) {
        return dB(amplitude(y, from, 2400, 2000, rate) / amplitude(y, from, 2400, 1000, rate));
    };
    const double before = h2(48000 - 2400);
    const double justAfter = h2(96000 + 480);
    const double later = h2(48000 * 4 - 2400);
    CAPTURE(before, justAfter, later);
    CHECK(std::abs(justAfter - before) > 0.3);
    CHECK(std::abs(later - before) < 0.1);
}

TEST_CASE("Tube: the time element - the supply sags under a loud passage", "[dsp][tube]")
{
    // PP EL34 class AB: the average current rises with the signal, the supply
    // sags, the gain comes down a little.
    const double rate = 48000;
    auto t = prepared(TubeType::PpEl34, 0.0, rate);
    const auto y = run(*t, sine(1000, 0.9, 48000, rate));
    const double early = amplitude(y, 192, 480, 1000, rate);
    const double late = amplitude(y, 48000 - 4800, 4800, 1000, rate);
    const double sag = dB(early / late);
    CAPTURE(sag);
    CHECK(sag > 0.02);
    CHECK(sag < 1.0);
}

TEST_CASE("Tube: no DC comes out", "[dsp][tube]")
{
    for (const auto type : kTypes)
    {
        auto t = prepared(type, 0.0, 48000);
        const auto y = run(*t, sine(1000, 0.9, 48000 * 3, 48000));
        double mean = 0;
        for (std::size_t i = 48000 * 2; i < y.size(); ++i)
            mean += y[i];
        mean /= 48000.0;
        CAPTURE(static_cast<int>(type), mean);
        CHECK(std::abs(mean) < 1e-4);
    }
}

TEST_CASE("Player: switching the tube on or off reopens the output at the position, nothing lost", "[dsp][tube][player]")
{
    // The volume keeps the processing path; the tube's latency comes and goes,
    // so each switch ends the session and the next one starts where it ended.
    ap::test::TempDir dir;
    constexpr std::size_t total = 30000;
    std::vector<std::int16_t> v(total * 2);
    for (std::size_t i = 0; i < total; ++i)
        v[2 * i] = v[2 * i + 1] = static_cast<std::int16_t>(8000.0 * std::sin(2 * kPi * 440 * static_cast<double>(i) / 48000));
    std::vector<std::byte> bytes(v.size() * 2);
    std::memcpy(bytes.data(), v.data(), bytes.size());

    auto vol = std::make_shared<VolumeStage>();
    vol->setGainDb(-6.0);
    auto tube = std::make_shared<TubeStage>();
    PlaybackOptions options;
    options.processing = std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{vol, tube});
    auto log = std::make_shared<std::vector<PcmFormat>>();
    Player player([out = dir.path() / "o.raw", log] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out, .periodFrames = {480}, .sessionLog = log});
    }, options);
    auto src = std::make_unique<ap::test::TriggerSource>(
        std::make_unique<MemoryPcmSource>(PcmFormat{48000, 2, 16, 16, SampleType::Int, 0x3}, bytes),
        ap::test::TriggerSource::Triggers{{5000, [tube] { tube->setEnabled(true); }},
                                          {20000, [tube] { tube->setEnabled(false); }}});
    player.load(std::move(src));
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(log->size() == 3);
    REQUIRE(s.transitions.size() == 2);
    for (const auto& t : s.transitions)
        CHECK(t.kind == TransitionKind::ProcessingChanged);
    REQUIRE(s.path.has_value());
    CHECK(s.path->processing);
    CHECK(std::find(s.path->processingStages.begin(), s.path->processingStages.end(), "tube") ==
          s.path->processingStages.end()); // off again at the end
    // Every frame once: 32-bit frames of 2 channels.
    CHECK(std::filesystem::file_size(dir.path() / "o.raw") == total * 8);
}

TEST_CASE("Tube: processing time (benchmark, run by hand)", "[.][tube][benchmark]")
{
    for (const double rate : {44100.0, 96000.0, 192000.0})
    {
        auto t = std::make_unique<TubeStage>();
        t->setEnabled(true);
        t->setDriveDb(0.0);
        t->prepare(static_cast<std::uint32_t>(rate), 2);
        std::vector<double> x(static_cast<std::size_t>(rate) * 10 * 2);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = 0.5 * std::sin(2 * kPi * 440 * static_cast<double>(i / 2) / rate);
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < x.size(); i += 960)
            t->process(std::span(x).subspan(i, std::min<std::size_t>(960, x.size() - i)), 2);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        WARN("rate " << rate << ": 10 s of stereo in " << seconds << " s (" << 10.0 / seconds << "x real time)");
    }
}
