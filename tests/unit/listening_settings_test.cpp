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

TEST_CASE("The EQ's preamp: automatic by default, or the user's value", "[settings]")
{
    ListeningSettings s = everythingOn(ListeningMode::Processed);
    CHECK(s.eqAutoPreamp);
    CHECK(s.eqManualPreampDb == 0.0);
    ListeningChain c;
    c.apply(s);
    CHECK_FALSE(c.eq->manualPreamp().has_value());
    s.eqAutoPreamp = false;
    s.eqManualPreampDb = -3.5;
    c.apply(s);
    CHECK(c.eq->manualPreamp() == -3.5);
    s.eqManualPreampDb = 7.0; // never above 0 dB
    c.apply(s);
    CHECK(c.eq->manualPreamp() == 0.0);
}

TEST_CASE("Solo: only the frequencies around one band, not saved", "[settings]")
{
    ListeningChain c;
    auto s = everythingOn(ListeningMode::Processed);
    s.eqAutoPreamp = false;
    s.eqManualPreampDb = -10.0;
    c.apply(s);
    c.setEqSolo(1);
    CHECK(c.eqSolo() == 1u);
    REQUIRE(c.eq->bands().size() == 1);
    CHECK(c.eq->bands()[0] == EqBand{EqFilterType::BandPass, 8000.0, 0.0, 0.7071, true});
    CHECK_FALSE(c.eq->manualPreamp().has_value()); // the band-pass needs no preamp

    // A later change of the settings keeps the solo on the band's new values.
    s.eqBands[1].frequency = 6000.0;
    c.apply(s);
    CHECK(c.eq->bands()[0].frequency == 6000.0);

    c.setEqSolo(std::nullopt);
    CHECK(c.eq->bands() == s.eqBands);
    CHECK(c.eq->manualPreamp() == -10.0);

    c.setEqSolo(5); // no such band: ignored
    CHECK(c.eq->bands() == s.eqBands);
    s.mode = ListeningMode::Original; // 原音モード: no EQ, solo or not
    c.setEqSolo(0);
    c.apply(s);
    CHECK(c.eq->bands().empty());
    CHECK(settingsFromText(settingsToText(AppSettings{{}, s})).listening == s); // the solo is not in the settings
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
    a.listening.eqBands.push_back(EqBand{EqFilterType::BandPass, 440.0, 0.0, 4.0, true});
    a.listening.eqBands.push_back(EqBand{EqFilterType::Notch, 50.0, 0.0, 30.0, false});
    a.listening.eqAutoPreamp = false;
    a.listening.eqManualPreampDb = -4.25;

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

// Task 6-9: EQ presets - an EQ curve (bands and preamp) under a name, saved
// with the settings and called up into them.

TEST_CASE("EQ presets: taken from the settings and put back into them (the curve only)", "[settings][presets]")
{
    ListeningSettings l = everythingOn(ListeningMode::Processed);
    l.eqBands = {EqBand{EqFilterType::LowShelf, 105.0, 5.5, 0.7, true}, EqBand{EqFilterType::Peak, 2900.0, 3.0, 1.0, false}};
    l.eqAutoPreamp = false;
    l.eqManualPreampDb = -2.0;
    const NamedEqPreset p = eqPresetFrom(l, "ヘッドホン A");
    CHECK(p.name == "ヘッドホン A");
    CHECK(p.bands == l.eqBands);
    CHECK_FALSE(p.autoPreamp);
    CHECK(p.manualPreampDb == -2.0);

    ListeningSettings other; // 原音モード, EQ on, no bands
    other.eqEnabled = false;
    applyEqPreset(other, p);
    CHECK(other.eqBands == p.bands);
    CHECK_FALSE(other.eqAutoPreamp);
    CHECK(other.eqManualPreampDb == -2.0);
    // The rest is left as it was: the mode and whether the EQ is used.
    CHECK(other.mode == ListeningMode::Original);
    CHECK_FALSE(other.eqEnabled);
}

TEST_CASE("EQ presets are saved with the settings and read back exactly", "[settings][presets][text]")
{
    AppSettings a;
    a.listening.eqBands = {EqBand{EqFilterType::Peak, 1000.0, -3.5, 0.7071, true}};
    a.eqPresets.push_back(NamedEqPreset{"平ら", {}, true, 0.0});
    a.eqPresets.push_back(NamedEqPreset{"HD 600 (AutoEQ)",
                                   {EqBand{EqFilterType::LowShelf, 105.0, 6.5, 0.7, true},
                                    EqBand{EqFilterType::Notch, 50.0, 0.0, 30.0, false}},
                                   false,
                                   -1.25});
    const auto back = settingsFromText(settingsToText(a));
    CHECK(back == a);
    CHECK(back.listening.eqBands == a.listening.eqBands); // the current EQ is not mixed with a preset's
}

TEST_CASE("Reading EQ presets: leniently, within limits", "[settings][presets][text]")
{
    std::string text = "eqpreset.band=on peak 100 1 1\n" // before any name: no preset to belong to
                       "eqpreset.name=A\n"
                       "eqpreset.band=on peak 100 1 1\n"
                       "eqpreset.band=on bell 100 1 1\n" // unknown type: skipped
                       "eqpreset.preamp=3\n"             // above 0 dB: 0 dB
                       "eqpreset.autopreamp=0\n"
                       "eqpreset.name=\n"                // no name: not a preset
                       "eqpreset.band=on peak 200 1 1\n";
    for (int i = 0; i < 40; ++i)
        text += "eqpreset.band=on peak " + std::to_string(300 + i) + " 1 1\n";
    const auto s = settingsFromText(text);
    REQUIRE(s.eqPresets.size() == 1);
    CHECK(s.eqPresets[0].name == "A");
    CHECK(s.eqPresets[0].bands.size() == 1);
    CHECK(s.eqPresets[0].manualPreampDb == 0.0);
    CHECK_FALSE(s.eqPresets[0].autoPreamp);
    CHECK(s.listening.eqBands.empty());

    std::string many;
    for (std::size_t i = 0; i < kMaxEqPresets + 5; ++i)
        many += "eqpreset.name=P" + std::to_string(i) + "\neqpreset.band=on peak 1000 1 1\n";
    for (int i = 0; i < 40; ++i)
        many += "eqpreset.band=on peak " + std::to_string(300 + i) + " 1 1\n";
    const auto m = settingsFromText(many);
    CHECK(m.eqPresets.size() == kMaxEqPresets);
    CHECK(m.eqPresets.back().bands.size() <= EqStage::kMaxBands);
}

TEST_CASE("Saving EQ presets: a line break in a name cannot add a line", "[settings][presets][text]")
{
    AppSettings a;
    a.eqPresets.push_back(NamedEqPreset{"A\nmode=processed", {}, true, 0.0});
    const auto back = settingsFromText(settingsToText(a));
    CHECK(back.listening.mode == ListeningMode::Original);
    REQUIRE(back.eqPresets.size() == 1);
    CHECK(back.eqPresets[0].name == "A mode=processed");
}

// Task 9-3: the tube and the tone controls in the settings.

TEST_CASE("Tube and tone: off and flat by default, applied only in 音作りモード", "[settings][tube]")
{
    const ListeningSettings d;
    CHECK_FALSE(d.tubeEnabled);
    CHECK(d.tubeType == TubeType::Se300B);
    CHECK(d.tubeDriveDb == TubeStage::kDefaultDriveDb);
    CHECK(d.toneBassDb == 0.0);
    CHECK(d.toneTrebleDb == 0.0);

    ListeningSettings s;
    s.tubeEnabled = true;
    s.tubeType = TubeType::PpEl34;
    s.tubeDriveDb = -6.0;
    s.toneBassDb = 3.0;
    s.toneTrebleDb = -2.0;

    ListeningChain c;
    c.apply(s); // 原音モード
    CHECK_FALSE(c.tube->enabled());
    CHECK(c.tone->bassDb() == 0.0);
    CHECK(c.tone->trebleDb() == 0.0);
    CHECK(c.chain()->bypassed());

    s.mode = ListeningMode::Processed;
    c.apply(s);
    CHECK(c.tube->enabled());
    CHECK(c.tube->type() == TubeType::PpEl34);
    CHECK(c.tube->driveDb() == -6.0);
    CHECK(c.tone->bassDb() == 3.0);
    CHECK(c.tone->trebleDb() == -2.0);
    CHECK_FALSE(c.chain()->bypassed());
}

TEST_CASE("Tube and tone sit after the EQ and before the volume", "[settings][tube]")
{
    ListeningSettings s = everythingOn(ListeningMode::Processed);
    s.tubeEnabled = true;
    s.toneBassDb = 2.0;
    ListeningChain c;
    c.apply(s);
    c.volume->setGainDb(-6.0);
    c.chain()->trackStarted(loudTrack());
    // (No ISP-safe gain: ReplayGain and the volume have already taken the peak below the line.)
    CHECK(c.chain()->activeStageNames() ==
          std::vector<std::string>{"replaygain", "eq", "tone", "tube", "volume", "limiter"});
}

TEST_CASE("Tube and tone: 音作りモード with the tube off and the tone flat is still bypassed", "[settings][tube]")
{
    ListeningSettings s;
    s.mode = ListeningMode::Processed;
    s.ispSafeProcessed = IspSafeMode::Off;
    s.tubeEnabled = false;
    s.tubeType = TubeType::PpClassA;
    ListeningChain c;
    c.apply(s);
    CHECK(c.chain()->bypassed());
}

TEST_CASE("Tube and tone are saved with the settings and read back", "[settings][tube][text]")
{
    AppSettings a;
    a.listening.tubeEnabled = true;
    a.listening.tubeType = TubeType::PpClassA;
    a.listening.tubeDriveDb = -7.25;
    a.listening.toneBassDb = 4.5;
    a.listening.toneTrebleDb = -1.5;
    CHECK(settingsFromText(settingsToText(a)) == a);
    for (const auto t : {TubeType::Se300B, TubeType::PpEl34, TubeType::PpClassA})
    {
        AppSettings b;
        b.listening.tubeType = t;
        CHECK(settingsFromText(settingsToText(b)).listening.tubeType == t);
    }
}

TEST_CASE("Reading tube and tone settings: unknown or out of range keeps them within limits", "[settings][tube][text]")
{
    const auto s = settingsFromText("tube.enabled=1\n"
                                    "tube.type=kt88\n"  // unknown: the default
                                    "tube.drive=50\n"   // above the range: the top
                                    "tone.bass=-40\n"   // below: the bottom
                                    "tone.treble=x\n"); // not a number: 0
    CHECK(s.listening.tubeEnabled);
    CHECK(s.listening.tubeType == TubeType::Se300B);
    CHECK(s.listening.tubeDriveDb == TubeStage::kMaxDriveDb);
    CHECK(s.listening.toneBassDb == -ToneStage::kMaxDb);
    CHECK(s.listening.toneTrebleDb == 0.0);
}

// Task 9-5: the crossfeed in the settings.

TEST_CASE("Crossfeed: off by default, applied only in 音作りモード, after the tube", "[settings][crossfeed]")
{
    const ListeningSettings d;
    CHECK_FALSE(d.crossfeedEnabled);
    CHECK(d.crossfeedLevel == CrossfeedLevel::Default);

    ListeningSettings s;
    s.crossfeedEnabled = true;
    s.crossfeedLevel = CrossfeedLevel::Cmoy;
    ListeningChain c;
    c.apply(s); // 原音モード
    CHECK_FALSE(c.crossfeed->enabled());

    s.mode = ListeningMode::Processed;
    s.ispSafeProcessed = IspSafeMode::Off;
    s.tubeEnabled = true;
    c.apply(s);
    CHECK(c.crossfeed->enabled());
    CHECK(c.crossfeed->level() == CrossfeedLevel::Cmoy);
    c.chain()->prepare(48000, 2);
    CHECK(c.chain()->activeStageNames() == std::vector<std::string>{"tube", "crossfeed", "limiter"});
}

TEST_CASE("Crossfeed settings are saved and read back; unknown levels keep the default", "[settings][crossfeed][text]")
{
    for (const auto level : {CrossfeedLevel::Default, CrossfeedLevel::Cmoy, CrossfeedLevel::Jmeier})
    {
        AppSettings a;
        a.listening.crossfeedEnabled = true;
        a.listening.crossfeedLevel = level;
        CHECK(settingsFromText(settingsToText(a)) == a);
    }
    const auto s = settingsFromText("crossfeed.enabled=1\ncrossfeed.level=loud\n");
    CHECK(s.listening.crossfeedEnabled);
    CHECK(s.listening.crossfeedLevel == CrossfeedLevel::Default);
}

// Task 9-6: the space in the settings.

namespace {

// A minimal 16-bit mono WAV file image.
std::vector<std::uint8_t> wav16Mono(std::uint32_t rate, const std::vector<std::int16_t>& samples)
{
    std::vector<std::uint8_t> b;
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    auto u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::uint8_t>(v));
        b.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto data = static_cast<std::uint32_t>(samples.size() * 2);
    b.insert(b.end(), {'R', 'I', 'F', 'F'});
    u32(36 + data);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    u32(data);
    for (const auto s : samples)
        u16(static_cast<std::uint16_t>(s));
    return b;
}

} // namespace

TEST_CASE("Space: off by default, applied only in 音作りモード, between the tube and the crossfeed", "[settings][space]")
{
    const ListeningSettings d;
    CHECK(d.spaceRoom == SpaceRoom::Off);
    CHECK(d.spaceMixPercent == SpaceStage::kDefaultMixPercent);
    CHECK(d.spacePreDelayMs == SpaceStage::kDefaultPreDelayMs);
    CHECK(d.spaceLowCutHz == SpaceStage::kDefaultLowCutHz);

    ListeningSettings s;
    s.spaceRoom = SpaceRoom::Hall;
    s.spaceMixPercent = 30.0;
    ListeningChain c;
    c.chain()->prepare(48000, 2);
    c.apply(s); // 原音モード: kept, not applied
    CHECK(c.space->room() == SpaceRoom::Off);
    CHECK(c.chain()->bypassed());

    s.mode = ListeningMode::Processed;
    s.ispSafeProcessed = IspSafeMode::Off;
    s.tubeEnabled = true;
    s.crossfeedEnabled = true;
    c.apply(s);
    CHECK(c.space->room() == SpaceRoom::Hall);
    CHECK(c.space->mixPercent() == 30.0);
    CHECK(c.chain()->activeStageNames() == std::vector<std::string>{"tube", "space", "crossfeed", "limiter"});
    // No latency: switching the space on or off never makes the player reopen.
    c.chain()->prepare(48000, 2);
    s.spaceRoom = SpaceRoom::Off;
    c.apply(s);
    CHECK_FALSE(c.chain()->needsReprepare());
    s.spaceRoom = SpaceRoom::Arena;
    c.apply(s);
    CHECK_FALSE(c.chain()->needsReprepare());
}

TEST_CASE("Space at MIX 0, or OFF, in 音作りモード with nothing else is bypassed", "[settings][space]")
{
    ListeningSettings s;
    s.mode = ListeningMode::Processed;
    s.ispSafeProcessed = IspSafeMode::Off;
    s.spaceRoom = SpaceRoom::Arena;
    s.spaceMixPercent = 0.0;
    ListeningChain c;
    c.apply(s);
    CHECK(c.chain()->bypassed());
    s.spaceMixPercent = 10.0;
    c.apply(s);
    CHECK_FALSE(c.chain()->bypassed());
}

TEST_CASE("Space settings are saved and read back, within limits", "[settings][space][text]")
{
    AppSettings a;
    a.listening.spaceRoom = SpaceRoom::Club;
    a.listening.spaceMixPercent = 35.5;
    a.listening.spacePreDelayMs = 22.0;
    a.listening.spaceLowCutHz = 0.0;
    a.listening.spaceIr = {"C:\\IR\\クラブ.wav", "", "D:\\arena.flac"};
    CHECK(settingsFromText(settingsToText(a)) == a);

    const auto s = settingsFromText("space.room=stadium\nspace.mix=250\nspace.predelay=-5\nspace.lowcut=5000\n");
    CHECK(s.listening.spaceRoom == SpaceRoom::Off);
    CHECK(s.listening.spaceMixPercent == 100.0);
    CHECK(s.listening.spacePreDelayMs == 0.0);
    CHECK(s.listening.spaceLowCutHz == SpaceStage::kMaxLowCutHz);
    CHECK(settingsFromText("space.lowcut=5\n").listening.spaceLowCutHz == SpaceStage::kMinLowCutHz);
    CHECK(settingsFromText("space.lowcut=0\n").listening.spaceLowCutHz == 0.0);
}

TEST_CASE("A user IR for a room: loaded when set, reported when it cannot be read", "[settings][space]")
{
    ap::test::TempDir dir;
    std::vector<std::int16_t> pcm(4410, 0);
    pcm[0] = 16000;
    pcm[100] = -8000;
    const auto good = dir.write("room.wav", wav16Mono(44100, pcm));

    ListeningSettings s;
    s.mode = ListeningMode::Processed;
    s.spaceRoom = SpaceRoom::Hall;
    const auto u = good.u8string();
    s.spaceIr[1] = std::string(u.begin(), u.end());
    s.spaceIr[2] = (dir.path() / "missing.wav").string();
    ListeningChain c;
    c.apply(s);
    const auto st = c.spaceIrStatus();
    CHECK(st[0].path.empty());
    CHECK(st[1].loaded);
    CHECK(st[1].error.empty());
    CHECK(st[1].sampleRate == 44100);
    CHECK(st[1].channels == 1);
    CHECK(std::abs(st[1].seconds - 0.1) < 1e-9);
    CHECK_FALSE(st[2].loaded);
    CHECK_FALSE(st[2].error.empty());
    CHECK(c.space->params().user[1] != nullptr);
    CHECK(c.space->params().user[2] == nullptr); // falls back to the built-in room
    CHECK(c.space->displayDetail() == "hall;20;user");
}
