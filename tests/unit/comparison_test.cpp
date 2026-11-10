// Task 3-7: the listening comparison — crossfade, matching, timing, and how
// the player enters and leaves it.

#include <ap/engine/dsp/comparison.h>
#include <ap/engine/dsp/eq.h>
#include <ap/engine/dsp/loudness.h>
#include <ap/engine/dsp/volume.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>

using namespace ap::engine;

TEST_CASE("Comparison: switching crossfades over 5 ms", "[dsp][comparison]")
{
    AbComparison ab;
    ab.select(ComparisonSide::Original);
    ab.prepare(48000, 1, 0);
    std::vector<double> original(20000, 0.0);
    std::vector<double> processed(20000, 0.5);
    ab.process(original, processed, 1);
    for (std::size_t i = 0; i < processed.size(); ++i)
        REQUIRE(processed[i] == 0.0); // the original side

    ab.select(ComparisonSide::Processed);
    std::vector<double> o2(2000, 0.0), p2(2000, 0.5);
    ab.process(o2, p2, 1);
    std::size_t between = 0;
    for (std::size_t i = 0; i < p2.size(); ++i)
    {
        if (i > 0)
            CHECK(p2[i] >= p2[i - 1]); // smooth, one way
        if (p2[i] > 0.0 && p2[i] < 0.5)
            ++between;
    }
    CHECK(between == 239); // 240 frames = 5 ms at 48 kHz (the last one is 0.5)
    CHECK(p2.back() == 0.5);
}

TEST_CASE("Comparison: a gain difference is matched at once", "[dsp][comparison]")
{
    AbComparison ab;
    ab.prepare(48000, 2, 0);
    std::vector<double> o(48000 * 2), p(48000 * 2);
    for (std::size_t i = 0; i < 48000; ++i)
    {
        const double x = 0.2 * std::sin(2 * std::numbers::pi * 1000 * static_cast<double>(i) / 48000);
        o[2 * i] = o[2 * i + 1] = x;
        p[2 * i] = p[2 * i + 1] = x * std::pow(10.0, -6.0 / 20.0);
    }
    ab.process(o, p, 2);
    CHECK(std::abs(ab.matchDb() - 6.0) < 0.001);
    // After the match the processed side equals the original (the limiter does not act at -14 dBFS).
    CHECK(std::abs(p[2 * 40000] - o[2 * (40000 - ab.latencyFrames())]) < 1e-6);
}

TEST_CASE("Comparison: a loud original cannot be matched without clipping; the difference is reported", "[dsp][comparison]")
{
    // The original peaks at 0 dBFS, above the limiter's ceiling (-0.3 dBTP);
    // the processed side is 10 dB lower. Matched, it would be at 0 dBFS too,
    // so the limiter lowers it and the remaining difference is reported.
    AbComparison ab;
    ab.prepare(48000, 2, 0);
    std::vector<double> o(48000 * 4 * 2), p(48000 * 4 * 2);
    for (std::size_t i = 0; i < o.size() / 2; ++i)
    {
        const double x = std::sin(2 * std::numbers::pi * 997 * static_cast<double>(i) / 48000);
        o[2 * i] = o[2 * i + 1] = x;
        p[2 * i] = p[2 * i + 1] = x * std::pow(10.0, -10.0 / 20.0);
    }
    ab.process(o, p, 2);
    CHECK(std::abs(ab.matchDb() - 10.0) < 0.01); // the match itself is not pushed further
    CHECK(ab.limiterStats().events >= 1);
    CHECK(ab.levelDifferenceDb() < -0.2); // about the 0.3 dB the limiter takes
    CHECK(ab.levelDifferenceDb() > -0.5);
}

TEST_CASE("Comparison: the original side lines up with the processed side", "[dsp][comparison]")
{
    // The chain delays the processed side by 100 frames; the original side is delayed to match.
    AbComparison ab;
    ab.select(ComparisonSide::Original);
    ab.prepare(48000, 1, 100);
    std::vector<double> o(5000, 0.0), p(5000, 0.0);
    o[1000] = 0.25;
    ab.process(o, p, 1);
    std::size_t at = 0;
    for (std::size_t i = 0; i < p.size(); ++i)
        if (p[i] != 0.0)
            at = i;
    CHECK(at == 1000 + 100 + ab.latencyFrames());
}

TEST_CASE("Player: entering and leaving the comparison reopens the output", "[dsp][comparison][player]")
{
    ap::test::TempDir dir;
    auto chain = std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{std::make_shared<VolumeStage>()});
    PlaybackOptions options;
    options.processing = chain;
    auto log = std::make_shared<std::vector<PcmFormat>>();
    Player player([out = dir.path() / "o.raw", log] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out, .periodFrames = {480}, .sessionLog = log});
    }, options);
    std::vector<std::int16_t> v(48000 * 2, 1000);
    std::vector<std::byte> b(v.size() * 2);
    std::memcpy(b.data(), v.data(), b.size());

    chain->setComparing(true);
    player.load(std::make_unique<MemoryPcmSource>(PcmFormat{48000, 2, 16, 16, SampleType::Int, 0}, b));
    player.play();
    player.waitUntilIdle();
    auto s = player.status();
    REQUIRE(s.path.has_value());
    CHECK(s.path->comparison);
    CHECK(s.path->processing);
    CHECK(s.path->processingStages == std::vector<std::string>{"comparison"});
    CHECK((*log)[0].containerBits == 32);

    chain->setComparing(false);
    player.play();
    player.waitUntilIdle();
    s = player.status();
    REQUIRE(s.path.has_value());
    CHECK_FALSE(s.path->comparison);
    CHECK_FALSE(s.path->processing); // back to bit-perfect
}

TEST_CASE("Comparison: a match worked out for the whole track keeps the loudness when the music changes", "[dsp][comparison]")
{
    // The first half is mostly low notes, the second mostly high ones; a high
    // shelf boost changes the loudness of the halves differently, so a running
    // estimate moves, while the whole track's match is one fixed value.
    const std::uint32_t rate = 48000;
    std::vector<float> v(rate * 8 * 2);
    for (std::size_t i = 0; i < v.size() / 2; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const double low = 0.3 * std::sin(2 * std::numbers::pi * 150 * t);
        const double high = 0.3 * std::sin(2 * std::numbers::pi * 6000 * t);
        const double x = i < v.size() / 4 ? low + 0.05 * high : 0.05 * low + high;
        v[2 * i] = v[2 * i + 1] = static_cast<float>(x);
    }
    std::vector<std::byte> bytes(v.size() * 4);
    std::memcpy(bytes.data(), v.data(), bytes.size());
    const PcmFormat f{rate, 2, 32, 32, SampleType::Float, 0x3};

    auto makeChain = [] {
        auto eq = std::make_shared<EqStage>();
        eq->setBands({{EqFilterType::HighShelf, 3000, 6.0, 0.7}});
        auto vol = std::make_shared<VolumeStage>();
        vol->setGainDb(-4.0);
        return std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{eq, vol});
    };
    MemoryPcmSource measured(f, bytes);
    auto offline = makeChain();
    const double match = measureMatchDb(measured, *offline, TrackInfo{});

    // Play the processed side with that match and compare the whole output's loudness with the original's.
    auto chain = makeChain();
    chain->setComparing(true);
    chain->comparison().select(ComparisonSide::Processed);
    chain->comparison().setFixedMatchDb(match);
    ap::test::TempDir dir;
    MemoryPcmSource src(f, bytes);
    DummyOutput out({.path = dir.path() / "o.raw", .periodFrames = {480},
                     .accepts = [](const PcmFormat& g) { return g.type == SampleType::Float; }});
    PlaybackOptions options;
    options.processing = chain;
    play(src, out, options);

    std::ifstream in(dir.path() / "o.raw", std::ios::binary);
    std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<double> y(raw.size() / 4);
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        float s;
        std::memcpy(&s, raw.data() + i * 4, 4);
        y[i] = s;
    }
    LoudnessMeter lo(rate, 2), lp(rate, 2);
    std::vector<double> x(v.begin(), v.end());
    lo.process(x);
    lp.process(y);
    CAPTURE(match, lo.integratedLufs(), lp.integratedLufs(), chain->comparison().levelDifferenceDb());
    CHECK(std::abs(lp.integratedLufs() - lo.integratedLufs()) < 0.1);
    CHECK(std::abs(chain->comparison().levelDifferenceDb()) < 0.1);
    CHECK(chain->comparison().matchDb() == match);
}
