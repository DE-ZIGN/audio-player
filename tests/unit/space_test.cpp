// Task 9-6: the space stage - the wet signal is the input convolved with the
// room's IR (normalised, low cut, pre-delay), the dry signal is not delayed,
// MIX 0 or OFF is identity (full bypass), switching rooms crossfades with the
// same input history; and the built-in rooms' measured numbers.

#include <ap/engine/dsp/eq.h>
#include <ap/engine/dsp/room_ir.h>
#include <ap/engine/dsp/space.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

using namespace ap::engine;

namespace {

constexpr double kRate = 48000;

SpaceStage::Params params(SpaceRoom room, double mix, double pre = 10.0, double cut = 0.0)
{
    SpaceStage::Params p;
    p.room = room;
    p.mixPercent = mix;
    p.preDelayMs = pre;
    p.lowCutHz = cut;
    return p;
}

void run(SpaceStage& s, std::vector<double>& x, std::size_t channels = 2, std::size_t chunk = 480)
{
    for (std::size_t i = 0; i < x.size(); i += chunk * channels)
    {
        const auto n = std::min(chunk * channels, x.size() - i);
        if (!s.isIdentity())
            s.process(std::span(x).subspan(i, n), static_cast<std::uint16_t>(channels));
    }
}

std::vector<double> noise(std::size_t n, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> d(0.0, 0.1);
    std::vector<double> v(n);
    for (auto& x : v)
        x = d(rng);
    return v;
}

// |H(f)| of a sequence (one DFT bin at f).
double magnitudeAt(const std::vector<double>& h, double f, double rate)
{
    std::complex<double> s = 0.0;
    for (std::size_t i = 0; i < h.size(); ++i)
        s += h[i] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(i) / rate);
    return std::abs(s);
}

// T20 of one band (Schroeder backward integration, -5 to -25 dB, times 3).
double bandRt(std::vector<double> h, double lo, double hi, double rate)
{
    for (const auto [type, f] : {std::pair{EqFilterType::HighPass, lo}, std::pair{EqFilterType::LowPass, hi}})
    {
        const auto q = *designBiquad(EqBand{type, f, 0.0, std::numbers::sqrt2 / 2.0, true}, rate);
        for (int pass = 0; pass < 2; ++pass)
        {
            double z1 = 0, z2 = 0;
            for (auto& v : h)
            {
                const double y = q.b0 * v + z1;
                z1 = q.b1 * v - q.a1 * y + z2;
                z2 = q.b2 * v - q.a2 * y;
                v = y;
            }
        }
    }
    std::vector<double> edc(h.size());
    double acc = 0.0;
    for (std::size_t i = h.size(); i-- > 0;)
        edc[i] = (acc += h[i] * h[i]);
    auto when = [&](double db) {
        for (std::size_t i = 0; i < edc.size(); ++i)
            if (10.0 * std::log10(edc[i] / edc[0]) <= db)
                return static_cast<double>(i) / rate;
        return static_cast<double>(edc.size()) / rate;
    };
    return 3.0 * (when(-25.0) - when(-5.0));
}

} // namespace

TEST_CASE("OFF, or MIX 0, is identity; a room with MIX above 0 is not", "[space]")
{
    SpaceStage s;
    s.prepare(48000, 2);
    CHECK(s.isIdentity());
    CHECK(s.latencyFrames() == 0);
    s.set(params(SpaceRoom::Hall, 0.0));
    CHECK(s.isIdentity());
    s.set(params(SpaceRoom::Hall, 20.0));
    CHECK_FALSE(s.isIdentity());
    CHECK(s.displayDetail() == "hall;20");
    std::vector<double> x(2 * 4800, 0.1);
    run(s, x);
    s.set(params(SpaceRoom::Off, 20.0));
    // MIX fades out first (no sudden cut), then the stage is identity.
    CHECK_FALSE(s.isIdentity());
    run(s, x);
    CHECK(s.isIdentity());
}

TEST_CASE("The stage's impulse response: the dry impulse at once, the wet IR from the pre-delay", "[space]")
{
    SpaceStage s;
    s.set(params(SpaceRoom::Club, 30.0, 10.0, 0.0));
    s.prepare(48000, 2);
    const auto wet = s.wetResponse(kRate);
    REQUIRE(wet.size() == 2);
    // 10 ms: the first 480 frames of the wet response are zeros.
    for (std::size_t i = 0; i < 480; ++i)
        REQUIRE(wet[0][i] == 0.0);
    // Unit energy per channel on average (no low cut here).
    double e = 0.0;
    for (const auto& c : wet)
        for (const double v : c)
            e += v * v;
    CHECK(std::abs(e / 2.0 - 1.0) < 1e-9);

    const std::size_t settle = 4800; // MIX reaches 30 % in 20 ms
    const std::size_t len = wet[0].size();
    std::vector<double> x(2 * (settle + len + 1000), 0.0);
    x[2 * settle] = 1.0;     // left
    x[2 * settle + 1] = 0.5; // right
    run(s, x);
    // The dry part is not delayed: at the impulse itself, 70 % of it (the wet starts later).
    CHECK(std::abs(x[2 * settle] - 0.7) < 1e-15);
    CHECK(std::abs(x[2 * settle + 1] - 0.35) < 1e-15);
    double err = 0.0;
    for (std::size_t j = 1; j < len; ++j)
    {
        err = std::max(err, std::abs(x[2 * (settle + j)] - 0.3 * wet[0][j]));
        err = std::max(err, std::abs(x[2 * (settle + j) + 1] - 0.3 * 0.5 * wet[1][j]));
    }
    CHECK(err < 1e-12);
}

TEST_CASE("The pre-delay is at least the convolution's head block; the stage itself adds no latency", "[space]")
{
    SpaceStage s;
    s.set(params(SpaceRoom::Hall, 20.0, 0.0));
    s.prepare(48000, 2);
    CHECK(s.latencyFrames() == 0);
    CHECK(std::abs(s.effectivePreDelayMs() - 1000.0 * 128 / 48000) < 1e-9);
    const auto wet = s.wetResponse(kRate);
    for (std::size_t i = 0; i < 128; ++i)
        REQUIRE(wet[0][i] == 0.0);
    s.set(params(SpaceRoom::Hall, 20.0, 25.0));
    CHECK(s.effectivePreDelayMs() == 25.0);
    const auto later = s.wetResponse(kRate);
    CHECK(later[0].size() == wet[0].size() + 1200 - 128);
}

TEST_CASE("MIX down to 0: the output becomes the input exactly, then the stage is identity", "[space]")
{
    SpaceStage s;
    s.set(params(SpaceRoom::Hall, 40.0));
    s.prepare(48000, 2);
    auto x = noise(2 * 48000, 1);
    run(s, x);
    s.set(params(SpaceRoom::Hall, 0.0));
    std::vector<double> y = noise(2 * 4800, 2);
    const auto input = y;
    run(s, y); // 100 ms: the fade (20 ms) ends inside
    CHECK(s.isIdentity());
    // After the fade, every sample is the input's.
    bool same = true;
    for (std::size_t i = 2 * 1440; i < y.size(); ++i)
        same = same && y[i] == input[i];
    CHECK(same);
}

TEST_CASE("Switching rooms keeps the input history: after the crossfade, as if the new room had been on", "[space]")
{
    const auto input = noise(2 * 96000, 3);
    SpaceStage a, b;
    a.set(params(SpaceRoom::Club, 25.0));
    b.set(params(SpaceRoom::Hall, 25.0));
    a.prepare(48000, 2);
    b.prepare(48000, 2);
    auto xa = input, xb = input;
    const std::size_t half = 2 * 48000;
    std::vector<double> firstA(xa.begin(), xa.begin() + half), firstB(xb.begin(), xb.begin() + half);
    run(a, firstA);
    run(b, firstB);
    a.set(params(SpaceRoom::Hall, 25.0));
    CHECK(a.displayDetail() == "hall;25");
    std::vector<double> restA(xa.begin() + half, xa.end()), restB(xb.begin() + half, xb.end());
    run(a, restA);
    run(b, restB);
    // The crossfade takes over at the next 2048-frame boundary and lasts 2048
    // frames; from then on, the two are the same.
    double err = 0.0;
    for (std::size_t i = 2 * 8192; i < restA.size(); ++i)
        err = std::max(err, std::abs(restA[i] - restB[i]));
    CHECK(err < 1e-12);
    // No step at the switch: the largest change between neighbouring samples
    // stays like the steady state's.
    double stepSwitch = 0.0, stepSteady = 0.0;
    for (std::size_t i = 2; i < 2 * 8192; i += 2)
        stepSwitch = std::max(stepSwitch, std::abs(restA[i] - restA[i - 2]));
    for (std::size_t i = 2 * 20000; i < 2 * 28192; i += 2)
        stepSteady = std::max(stepSteady, std::abs(restB[i] - restB[i - 2]));
    CHECK(stepSwitch < 1.5 * stepSteady);
}

TEST_CASE("The low cut is a second-order Butterworth high-pass on the wet signal", "[space]")
{
    SpaceStage s;
    s.set(params(SpaceRoom::Hall, 20.0, 10.0, 0.0));
    const auto plain = s.wetResponse(kRate);
    s.set(params(SpaceRoom::Hall, 20.0, 10.0, 120.0));
    const auto cut = s.wetResponse(kRate);
    for (const double f : {30.0, 60.0, 1000.0})
    {
        const double r = f / 120.0;
        const double expect = 20.0 * std::log10(r * r / std::sqrt(1.0 + r * r * r * r));
        const double got = 20.0 * std::log10(magnitudeAt(cut[0], f, kRate) / magnitudeAt(plain[0], f, kRate));
        CAPTURE(f, expect, got);
        CHECK(std::abs(got - expect) < 0.5);
    }
}

TEST_CASE("The built-in rooms: deterministic, decorrelated, decaying at their RT60", "[space][room]")
{
    for (const auto room : {SpaceRoom::Club, SpaceRoom::Hall, SpaceRoom::Arena})
    {
        CAPTURE(spaceRoomKey(room));
        const auto d = roomDesign(room);
        const auto ir = synthesizeRoom(room, kRate);
        REQUIRE(ir.channels.size() == 2);
        CHECK(ir.frames() == static_cast<std::size_t>(std::llround(d.seconds * kRate)));
        CHECK(synthesizeRoom(room, kRate).channels == ir.channels);
        // No direct sound: nothing at time 0.
        CHECK(ir.channels[0][0] == 0.0);
        // The late part of the two channels is (nearly) uncorrelated.
        const auto from = static_cast<std::size_t>(2 * d.mixingSeconds * kRate);
        double ll = 0, rr = 0, lr = 0;
        for (std::size_t i = from; i < ir.frames(); ++i)
        {
            ll += ir.channels[0][i] * ir.channels[0][i];
            rr += ir.channels[1][i] * ir.channels[1][i];
            lr += ir.channels[0][i] * ir.channels[1][i];
        }
        CHECK(std::abs(lr / std::sqrt(ll * rr)) < 0.2);
        // The decay in the 250 Hz - 2 kHz band within 20 % of its RT60.
        const double rt = bandRt(ir.channels[0], 250.0, 2000.0, kRate);
        CAPTURE(rt, d.rt60[1]);
        CHECK(std::abs(rt / d.rt60[1] - 1.0) < 0.2);
    }
}

TEST_CASE("A user IR replaces the room's built-in one, resampled to the output rate", "[space]")
{
    auto ir = std::make_shared<ImpulseResponse>();
    ir->sampleRate = 44100;
    ir->channels = {std::vector<double>(4410, 0.0)};
    ir->channels[0][0] = 1.0;
    ir->channels[0][2205] = -0.5;
    SpaceStage s;
    auto p = params(SpaceRoom::Club, 20.0, 10.0, 0.0);
    p.ir[0] = ir;
    p.source[0] = SpaceIrSource::File;
    s.set(p);
    CHECK(s.displayDetail() == "club;20;user");
    const auto wet = s.wetResponse(kRate);
    REQUIRE(wet.size() == 1); // mono: one channel, used for both sides
    CHECK(wet[0].size() == 480 + 4800);
    // The two clicks, at 0 and 50 ms after the pre-delay, in the same ratio.
    CHECK(wet[0][480] > 0.5);
    CHECK(std::abs(wet[0][480 + 2400] / wet[0][480] + 0.5) < 1e-6);
    // The resampler: a 1 kHz sine keeps its frequency and level.
    std::vector<double> sine(44100);
    for (std::size_t i = 0; i < sine.size(); ++i)
        sine[i] = std::sin(2 * std::numbers::pi * 1000.0 * static_cast<double>(i) / 44100.0);
    const auto up = resampleIr(sine, 44100, 48000);
    CHECK(up.size() == 48000);
    double e = 0.0;
    for (std::size_t i = 4800; i < 43200; ++i)
        e = std::max(e, std::abs(up[i] - std::sin(2 * std::numbers::pi * 1000.0 * static_cast<double>(i) / 48000.0)));
    CHECK(e < 1e-3);
}

TEST_CASE("The bundled measured HALL: St Paul's Hall (MAIR, AB pair, S01)", "[space][room]")
{
    CHECK(hasMeasuredIr(SpaceRoom::Hall));
    CHECK_FALSE(hasMeasuredIr(SpaceRoom::Club));
    CHECK_FALSE(hasMeasuredIr(SpaceRoom::Arena));
    CHECK(defaultSpaceIrSource(SpaceRoom::Hall) == SpaceIrSource::Measured);
    CHECK(defaultSpaceIrSource(SpaceRoom::Club) == SpaceIrSource::Synthesized);
    CHECK(measuredRoom(SpaceRoom::Club) == nullptr);
    for (const auto s : {SpaceIrSource::Measured, SpaceIrSource::Synthesized, SpaceIrSource::File})
        CHECK(spaceIrSourceFrom(spaceIrSourceKey(s)) == s);

    const auto hall = measuredRoom(SpaceRoom::Hall);
    REQUIRE(hall != nullptr);
    CHECK(hall == measuredRoom(SpaceRoom::Hall)); // read once
    CHECK(hall->sampleRate == 44100.0);
    REQUIRE(hall->channels.size() == 2);
    CHECK(std::abs(hall->seconds() - 2.965) < 0.01);
    double peak = 0.0;
    for (const auto& c : hall->channels)
        for (const double v : c)
            peak = std::max(peak, std::abs(v));
    CHECK(std::abs(20.0 * std::log10(peak) - (-4.6)) < 0.2);
    // Its decay in the 250 Hz - 2 kHz band (St Paul's Hall: RT60 2.1 s as published).
    const double rt = bandRt(hall->channels[0], 250.0, 2000.0, hall->sampleRate);
    CAPTURE(rt);
    CHECK(rt > 1.7);
    CHECK(rt < 2.4);

    // As a room's IR in the stage: shown as measured.
    SpaceStage st;
    auto p = params(SpaceRoom::Hall, 20.0, 10.0, 0.0);
    p.ir[1] = hall;
    p.source[1] = SpaceIrSource::Measured;
    st.set(p);
    CHECK(st.displayDetail() == "hall;20;measured");
    CHECK(st.wetResponse(kRate).size() == 2);
}
