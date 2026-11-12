// Task 6-3: the settings screen's model. 原音モード / 音作りモード, the
// processing settings and the output, how they set the processing chain, and
// how they are saved and read back.

#include <ap/engine/listening_settings.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/playback.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>

using namespace ap::engine;
using Catch::Matchers::WithinAbs;

namespace {

double fromDb(double db)
{
    return std::pow(10.0, db / 20.0);
}

ListeningSettings everythingOn(ListeningMode mode)
{
    ListeningSettings s;
    s.mode = mode;
    s.replayGain = ReplayGainMode::Track;
    s.ispSafeOriginal = IspSafeMode::Off;
    s.ispSafeProcessed = IspSafeMode::Measured;
    s.eqBands = {EqBand{EqFilterType::Peak, 1000.0, 6.0, 1.0, true},
                 EqBand{EqFilterType::HighShelf, 8000.0, -4.0, 0.7071, true}};
    return s;
}

// Track at -12 LUFS (ReplayGain -6 dB), true peak -1 dBTP; album at -14 LUFS, +1 dBTP.
TrackInfo loudTrack()
{
    return TrackInfo{0, ReplayGainInfo{-6.0, fromDb(-1.0), -4.0, fromDb(1.0)}};
}

PcmFormat int16(std::uint32_t rate = 48000)
{
    return PcmFormat{.sampleRate = rate, .channels = 2, .containerBits = 16, .validBits = 16, .type = SampleType::Int};
}

std::vector<std::byte> sine16(int frames)
{
    std::vector<std::byte> out;
    for (int i = 0; i < frames; ++i)
    {
        const auto v = static_cast<std::int16_t>(std::lround(8000.0 * std::sin(2.0 * std::numbers::pi * 1000.0 * i / 48000.0)));
        for (int c = 0; c < 2; ++c)
        {
            out.push_back(static_cast<std::byte>(v & 0xff));
            out.push_back(static_cast<std::byte>((v >> 8) & 0xff));
        }
    }
    return out;
}

std::vector<std::byte> readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<std::byte> out(raw.size());
    if (!raw.empty())
        std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

} // namespace

TEST_CASE("Defaults: 原音モード, nothing applied, ISP-safe off here and measured in 音作りモード", "[settings]")
{
    const ListeningSettings s;
    CHECK(s.mode == ListeningMode::Original);
    CHECK(s.replayGain == ReplayGainMode::Off);
    CHECK(s.ispSafeOriginal == IspSafeMode::Off);
    CHECK(s.ispSafeProcessed == IspSafeMode::Measured);
    CHECK(s.eqEnabled);
    CHECK(s.eqBands.empty());

    ListeningChain c;
    c.apply(s);
    CHECK(c.chain()->bypassed());
}

TEST_CASE("原音モード applies neither ReplayGain nor the EQ, and keeps their settings", "[settings]")
{
    ListeningChain c;
    const auto s = everythingOn(ListeningMode::Original);
    c.apply(s);
    CHECK(c.replayGain->mode() == ReplayGainMode::Off);
    CHECK(c.eq->bands().empty());
    CHECK(c.ispSafe->mode() == IspSafeMode::Off);
    c.chain()->trackStarted(loudTrack());
    CHECK(c.chain()->bypassed()); // bit-perfect at 100% volume
    CHECK(s.eqBands.size() == 2); // the settings themselves are untouched
}

TEST_CASE("音作りモード applies ReplayGain, the EQ and the ISP-safe gain as set", "[settings]")
{
    ListeningChain c;
    c.apply(everythingOn(ListeningMode::Processed));
    CHECK(c.replayGain->mode() == ReplayGainMode::Track);
    CHECK(c.eq->bands().size() == 2);
    CHECK(c.ispSafe->mode() == IspSafeMode::Measured);
    c.chain()->trackStarted(loudTrack());
    CHECK_FALSE(c.chain()->bypassed());
    const auto names = c.chain()->activeStageNames();
    CHECK(names == std::vector<std::string>{"replaygain", "eq", "limiter"});
}

TEST_CASE("Album ReplayGain makes the ISP-safe gain use the album's true peak", "[settings]")
{
    // A raw chain of the two stages, so the ISP-safe gain sees no other gain.
    auto run = [](ReplayGainMode rg) {
        ListeningChain c;
        auto s = everythingOn(ListeningMode::Processed);
        s.replayGain = rg;
        c.apply(s);
        c.replayGain->setMode(ReplayGainMode::Off); // keep the upstream gain at 1
        c.chain()->trackStarted(loudTrack());
        c.ispSafe->setUpstreamGain(1.0);
        return c.ispSafe->gainDb();
    };
    CHECK_THAT(run(ReplayGainMode::Track), WithinAbs(0.0, 1e-9));  // -1 dBTP: nothing to do
    CHECK_THAT(run(ReplayGainMode::Album), WithinAbs(-1.5, 1e-9)); // album +1 dBTP: 1 + 0.5 dB
}

TEST_CASE("The EQ switched off in 音作りモード is not applied, its bands kept", "[settings]")
{
    ListeningChain c;
    auto s = everythingOn(ListeningMode::Processed);
    s.eqEnabled = false;
    c.apply(s);
    CHECK(c.eq->bands().empty());
    CHECK(s.eqBands.size() == 2);
}

TEST_CASE("音作りモード with everything off or flat is bypassed (CLAUDE.md chapter 5)", "[settings]")
{
    ListeningChain c;
    ListeningSettings s;
    s.mode = ListeningMode::Processed;
    s.ispSafeProcessed = IspSafeMode::Off;
    s.eqBands = {EqBand{EqFilterType::Peak, 1000.0, 0.0, 1.0, true}, EqBand{EqFilterType::LowShelf, 100.0, 0.0, 0.7, true}};
    c.apply(s);
    c.chain()->trackStarted(loudTrack());
    CHECK(c.chain()->bypassed());
}

TEST_CASE("The ISP-safe gain can be switched on in 原音モード", "[settings]")
{
    ListeningChain c;
    ListeningSettings s;
    s.ispSafeOriginal = IspSafeMode::Fixed;
    c.apply(s);
    CHECK(c.ispSafe->mode() == IspSafeMode::Fixed);
    CHECK_FALSE(c.chain()->bypassed());
    CHECK(c.replayGain->mode() == ReplayGainMode::Off);
}

TEST_CASE("Switching back to 原音モード brings back the direct path", "[settings]")
{
    ListeningChain c;
    auto s = everythingOn(ListeningMode::Processed);
    s.eqBands.clear(); // the EQ's own crossfade to flat needs playback to settle
    c.apply(s);
    c.chain()->trackStarted(loudTrack());
    CHECK_FALSE(c.chain()->bypassed());
    s.mode = ListeningMode::Original;
    c.apply(s);
    CHECK(c.chain()->bypassed());
}

TEST_CASE("Played through the chain: 原音モード is bit-perfect, 音作りモード changes the samples", "[settings][playback]")
{
    ap::test::TempDir dir;
    const auto data = sine16(48000 / 4);
    auto playWith = [&](const ListeningSettings& s, const char* name) {
        ListeningChain c;
        c.apply(s);
        MemoryPcmSource src(int16(), data);
        DummyOutput out(DummyOutput::Options{.path = dir.path() / name, .periodFrames = {480}});
        PlaybackOptions o;
        o.processing = c.chain();
        const auto report = play(src, out, o);
        return std::pair{report.plan, readFile(dir.path() / name)};
    };

    const auto [originalPlan, original] = playWith(everythingOn(ListeningMode::Original), "original.raw");
    CHECK_FALSE(originalPlan.processing);
    CHECK(original == data);

    const auto [processedPlan, processed] = playWith(everythingOn(ListeningMode::Processed), "processed.raw");
    CHECK(processedPlan.processing);
    // No analysis values: no ReplayGain, and the ISP-safe gain falls back to -3 dB.
    CHECK(processedPlan.processingStages == std::vector<std::string>{"eq", "isp-safe", "limiter"});
    CHECK(processed != data);
}

TEST_CASE("Settings are saved as text and read back exactly", "[settings][text]")
{
    AppSettings a;
    a.output = OutputChoice{OutputKind::Asio, "{8A3C2E9B-0000-4000-8000-000000000001}", "SSL 2 MKII ASIO ドライバー", true};
    a.listening = everythingOn(ListeningMode::Processed);
    a.listening.replayGain = ReplayGainMode::Album;
    a.listening.ispSafeOriginal = IspSafeMode::Fixed;
    a.listening.eqEnabled = false;
    a.listening.eqBands.push_back(EqBand{EqFilterType::LowPass, 18000.0 / 3.0, 0.0, 0.1 + 0.2, false});
    a.listening.eqBands.push_back(EqBand{EqFilterType::HighPass, 20.0, 0.0, 0.5, true});
    a.listening.eqBands.push_back(EqBand{EqFilterType::LowShelf, 105.0, -3.1, 0.7, true});

    const auto text = settingsToText(a);
    CHECK(settingsFromText(text) == a);

    AppSettings wasapi;
    wasapi.output = OutputChoice{OutputKind::Wasapi, "{0.0.0.00000000}.{a1b2}", "スピーカー (KA11)", false};
    CHECK(settingsFromText(settingsToText(wasapi)) == wasapi);
    CHECK(settingsFromText(settingsToText(AppSettings{})) == AppSettings{});
}

TEST_CASE("Reading settings: empty, unknown or bad values keep the defaults", "[settings][text]")
{
    CHECK(settingsFromText("") == AppSettings{});
    CHECK(settingsFromText("garbage\n=\nmode\n") == AppSettings{});

    const auto s = settingsFromText("future.key=1\r\n"
                                    "mode=processed\r\n"
                                    "replaygain=loud\r\n"         // unknown: off
                                    "isp.processed=fixed\r\n"
                                    "eq.band=on peak 1000 x 1\r\n" // bad number: skipped
                                    "eq.band=on peak nan 3 1\r\n"  // not finite: skipped
                                    "eq.band=on bell 1000 3 1\r\n" // unknown type: skipped
                                    "eq.band=off peak 250 -2.5 1.4\r\n");
    CHECK(s.listening.mode == ListeningMode::Processed);
    CHECK(s.listening.replayGain == ReplayGainMode::Off);
    CHECK(s.listening.ispSafeProcessed == IspSafeMode::Fixed);
    REQUIRE(s.listening.eqBands.size() == 1);
    CHECK(s.listening.eqBands[0] == EqBand{EqFilterType::Peak, 250.0, -2.5, 1.4, false});
}

TEST_CASE("Reading settings: at most 32 EQ bands", "[settings][text]")
{
    std::string text;
    for (int i = 0; i < 40; ++i)
        text += "eq.band=on peak " + std::to_string(100 + i) + " 1 1\n";
    CHECK(settingsFromText(text).listening.eqBands.size() == EqStage::kMaxBands);
}

TEST_CASE("Saving settings: a line break in a device name cannot add a line", "[settings][text]")
{
    AppSettings a;
    a.output.name = "DAC\nmode=processed";
    const auto back = settingsFromText(settingsToText(a));
    CHECK(back.listening.mode == ListeningMode::Original);
    CHECK(back.output.name == "DAC mode=processed");
}
