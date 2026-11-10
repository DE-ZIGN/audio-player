// Task 3-4: ReplayGain 2.0 values, the gain limit, the stage, and switching
// exactly at the boundary between gapless tracks.

#include <ap/engine/dsp/loudness.h>
#include <ap/engine/dsp/replay_gain.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>

using namespace ap::engine;

namespace {

constexpr std::uint32_t kRate = 48000;

double fromDb(double db)
{
    return std::pow(10.0, db / 20.0);
}

PcmFormat float32(std::uint16_t channels = 2)
{
    return PcmFormat{kRate, channels, 32, 32, SampleType::Float, 0};
}

std::vector<float> tone(std::size_t frames, std::uint16_t channels, double level, double freq = 1000.0)
{
    std::vector<float> v(frames * channels);
    const double a = fromDb(level);
    for (std::size_t i = 0; i < frames; ++i)
        for (std::uint16_t c = 0; c < channels; ++c)
            v[i * channels + c] =
                static_cast<float>(a * std::sin(2 * std::numbers::pi * freq * static_cast<double>(i) / kRate));
    return v;
}

std::vector<std::byte> bytesOf(const std::vector<float>& v)
{
    std::vector<std::byte> b(v.size() * 4);
    std::memcpy(b.data(), v.data(), b.size());
    return b;
}

std::vector<float> readFloats(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<float> v(raw.size() / 4);
    std::memcpy(v.data(), raw.data(), v.size() * 4);
    return v;
}

TrackAnalysis analyze(const std::vector<float>& v, std::uint16_t channels)
{
    MemoryPcmSource src(float32(channels), bytesOf(v));
    return analyzeTrack(src);
}

ReplayGainInfo rg(double trackDb, double trackPeak = 0.5)
{
    return ReplayGainInfo{trackDb, trackPeak, std::nullopt, std::nullopt};
}

std::vector<double> run(ReplayGainStage& s, std::size_t frames, double value = 0.5)
{
    std::vector<double> v(frames * 2, value);
    s.process(v, 2);
    return v;
}

} // namespace

TEST_CASE("ReplayGain: -18 LUFS minus the loudness; silence gets no gain", "[dsp][replaygain]")
{
    CHECK(replayGainDb(-23.0) == 5.0);
    CHECK(replayGainDb(-9.5) == -8.5);
    CHECK(replayGainDb(-std::numeric_limits<double>::infinity()) == 0.0);
}

TEST_CASE("ReplayGain: the gain never lifts the true peak above 0 dBTP", "[dsp][replaygain]")
{
    const double peak = fromDb(-2.0);
    CHECK(std::abs(peakLimitedGainDb(6.0, peak) - 2.0) < 1e-12);
    CHECK(peakLimitedGainDb(1.5, peak) == 1.5);
    CHECK(peakLimitedGainDb(-4.0, fromDb(1.0)) == -4.0);
    CHECK(std::abs(peakLimitedGainDb(-0.5, fromDb(1.0)) - -1.0) < 1e-12); // a peak above 0 dBTP lowers it
    CHECK(peakLimitedGainDb(3.0, 0.0) == 3.0);
}

TEST_CASE("Loudness: silence is -inf LUFS; mono counts as dual mono", "[dsp][loudness]")
{
    CHECK(analyze(std::vector<float>(kRate * 4 * 2, 0.0f), 2).integratedLufs == -std::numeric_limits<double>::infinity());
    const double stereo = analyze(tone(kRate * 10, 2, -23.0), 2).integratedLufs;
    const double mono = analyze(tone(kRate * 10, 1, -23.0), 1).integratedLufs;
    CHECK(std::abs(stereo - -23.0) < 0.1);
    CHECK(std::abs(mono - stereo) < 0.01);
}

TEST_CASE("Loudness: the album is gated over all tracks' blocks together", "[dsp][loudness]")
{
    std::vector<TrackAnalysis> tracks;
    tracks.push_back(analyze(tone(kRate * 10, 2, -23.0), 2));
    tracks.push_back(analyze(tone(kRate * 10, 2, -33.0), 2));
    const auto album = analyzeAlbum(tracks);
    const double expected = 10 * std::log10((std::pow(10.0, -2.3) + std::pow(10.0, -3.3)) / 2);
    CHECK(std::abs(album.integratedLufs - expected) < 0.1);
    CHECK(album.truePeak == std::max(tracks[0].peaks.truePeakMax, tracks[1].peaks.truePeakMax));

    const auto info = makeReplayGain(tracks[1], &album);
    CHECK(std::abs(info.trackGainDb - 15.0) < 0.1);
    REQUIRE(info.albumGainDb.has_value());
    CHECK(std::abs(*info.albumGainDb - (-18.0 - expected)) < 0.1);
    CHECK(info.trackPeak == tracks[1].peaks.truePeakMax);
}

TEST_CASE("ReplayGain stage: off, no values, or 0 dB is identity", "[dsp][replaygain]")
{
    ReplayGainStage s;
    CHECK(s.isIdentity());
    s.trackStarted({0, rg(-6.0)});
    CHECK(s.isIdentity()); // mode Off
    s.setMode(ReplayGainMode::Track);
    CHECK_FALSE(s.isIdentity());
    s.trackStarted({1, std::nullopt});
    s.prepare(kRate, 2);
    CHECK(s.isIdentity());
    s.trackStarted({2, rg(0.0)});
    s.prepare(kRate, 2);
    CHECK(s.isIdentity());
}

TEST_CASE("ReplayGain stage: track and album gains, the limit, album falls back to track", "[dsp][replaygain]")
{
    ReplayGainStage s;
    s.setMode(ReplayGainMode::Track);
    s.trackStarted({0, ReplayGainInfo{-6.0, 0.9, -3.0, 0.95}});
    s.prepare(kRate, 2);
    CHECK(std::abs(run(s, 10)[0] - 0.5 * fromDb(-6.0)) < 1e-12);

    s.setMode(ReplayGainMode::Album);
    s.prepare(kRate, 2);
    CHECK(std::abs(run(s, 10)[0] - 0.5 * fromDb(-3.0)) < 1e-12);

    s.trackStarted({1, rg(-4.0)}); // no album values
    s.prepare(kRate, 2);
    CHECK(std::abs(run(s, 10)[0] - 0.5 * fromDb(-4.0)) < 1e-12);

    s.trackStarted({2, rg(+10.0, fromDb(-3.0))}); // limited to +3 dB
    s.prepare(kRate, 2);
    CHECK(std::abs(run(s, 10)[0] - 0.5 * fromDb(3.0)) < 1e-12);
}

TEST_CASE("ReplayGain stage: a new track jumps, a mode change ramps", "[dsp][replaygain]")
{
    ReplayGainStage s;
    s.setMode(ReplayGainMode::Track);
    s.trackStarted({0, rg(-6.0)});
    s.prepare(kRate, 2);
    run(s, 100);

    s.trackStarted({1, rg(-12.0)});
    const auto jumped = run(s, 1);
    CHECK(std::abs(jumped[0] - 0.5 * fromDb(-12.0)) < 1e-12); // from the first sample

    s.setMode(ReplayGainMode::Off);
    const auto ramp = run(s, 1000);
    CHECK(ramp[0] < 0.5 * 0.3);                     // still near -12 dB at the start
    CHECK(std::abs(ramp[2 * 479] - 0.5) < 1e-12);   // 1.0 after 10 ms
    CHECK(s.isIdentity());
}

TEST_CASE("Player: gapless tracks switch ReplayGain at the exact boundary", "[dsp][replaygain][player]")
{
    ap::test::TempDir dir;
    const auto a = tone(10007, 2, -10.0, 440.0);
    const auto b = tone(7001, 2, -10.0, 660.0);
    auto stage = std::make_shared<ReplayGainStage>();
    stage->setMode(ReplayGainMode::Track);
    PlaybackOptions options;
    options.processing = std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{stage});
    Player player([out = dir.path() / "o.raw"] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{
            .path = out, .periodFrames = {480}, .accepts = [](const PcmFormat& f) { return f.type == SampleType::Float; }});
    }, options);

    QueueItem ia{"a", [a] { return std::make_unique<MemoryPcmSource>(float32(), bytesOf(a)); }, rg(-6.0)};
    QueueItem ib{"b", [b] { return std::make_unique<MemoryPcmSource>(float32(), bytesOf(b)); }, rg(+2.0)};
    player.setQueue({ia, ib});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    REQUIRE(s.transitions.size() == 1);
    CHECK(s.transitions[0].kind == TransitionKind::Gapless);
    const auto out = readFloats(dir.path() / "o.raw");
    REQUIRE(out.size() == a.size() + b.size());
    double worstA = 0, worstB = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        worstA = std::max(worstA, std::abs(out[i] - a[i] * fromDb(-6.0)));
    for (std::size_t i = 0; i < b.size(); ++i)
        worstB = std::max(worstB, std::abs(out[a.size() + i] - b[i] * fromDb(2.0)));
    CHECK(worstA < 1e-7); // float32 rounding only
    CHECK(worstB < 1e-7);
}

TEST_CASE("Player: a direct track followed by one with ReplayGain reopens at the boundary", "[dsp][replaygain][player]")
{
    ap::test::TempDir dir;
    const auto a = tone(5000, 2, -10.0);
    const auto b = tone(5000, 2, -10.0);
    auto stage = std::make_shared<ReplayGainStage>();
    stage->setMode(ReplayGainMode::Track);
    PlaybackOptions options;
    options.processing = std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{stage});
    auto log = std::make_shared<std::vector<PcmFormat>>();
    Player player([out = dir.path() / "o.raw", log] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out,
                                                                  .periodFrames = {480},
                                                                  .accepts = [](const PcmFormat& f) { return f.type == SampleType::Float; },
                                                                  .sessionLog = log});
    }, options);

    QueueItem ia{"a", [a] { return std::make_unique<MemoryPcmSource>(float32(), bytesOf(a)); }, std::nullopt};
    QueueItem ib{"b", [b] { return std::make_unique<MemoryPcmSource>(float32(), bytesOf(b)); }, rg(-6.0)};
    player.setQueue({ia, ib});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    REQUIRE(s.transitions.size() == 1);
    CHECK(s.transitions[0].kind == TransitionKind::Reopened);
    CHECK(log->size() == 2);
    REQUIRE(s.path.has_value());
    CHECK(s.path->processing);
    CHECK(s.path->processingStages == std::vector<std::string>{"replaygain"});
    const auto out = readFloats(dir.path() / "o.raw");
    REQUIRE(out.size() == a.size() + b.size());
    CHECK(std::memcmp(out.data(), a.data(), a.size() * 4) == 0); // the first track is untouched
    CHECK(std::abs(out[a.size() + 100] - b[100] * fromDb(-6.0)) < 1e-7);
}
