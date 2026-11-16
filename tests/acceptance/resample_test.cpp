// Task 10-3, acceptance (played through the dummy output):
//   - 原音モード (no output rate choice): the rate is never converted; a rate
//     the DAC refuses is still refused;
//   - 音作りモード with nothing on and the track's rate: the direct path, the
//     same bytes;
//   - a conversion (chosen, or because the DAC refuses the rate): the
//     processing path, the output at the new rate, as long as the input, the
//     level kept, an impulse at its own time (also with a stage that has
//     look-ahead, the safety limiter).

#include <ap/engine/dsp/limiter.h>
#include <ap/engine/dsp/processing.h>
#include <ap/engine/dsp/volume.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/playback.h>
#include <ap/engine/player.h>
#include <ap/engine/pcm_source.h>
#include <ap/engine/sha256.h>
#include <ap/engine/wav_file_source.h>
#include <ap/signalgen/signals.h>
#include <ap/signalgen/wav_writer.h>

#include "temp_dir.h"
#include "trigger_source.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>
#include <set>

using namespace ap::engine;
namespace sg = ap::signalgen;

namespace {

std::vector<std::uint8_t> readAll(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// A chain with only the volume (at 100%: identity) and the given output rate choice.
std::shared_ptr<ProcessingChain> chain(std::optional<OutputRateChoice> choice,
                                       std::vector<std::shared_ptr<ProcessingStage>> stages = {})
{
    stages.insert(stages.begin(), std::make_shared<VolumeStage>());
    auto c = std::make_shared<ProcessingChain>(std::move(stages));
    c->setOutputRate(choice);
    return c;
}

PlaybackPlan play(const std::filesystem::path& in, const std::filesystem::path& out,
                  std::shared_ptr<ProcessingChain> c, std::function<bool(const PcmFormat&)> accepts = {})
{
    WavFileSource source(in);
    DummyOutput output({.path = out, .periodFrames = {441, 4096, 7, 1000}, .accepts = accepts});
    PlaybackOptions options;
    options.processing = std::move(c);
    return play(source, output, options).plan;
}

// Float output only (no dither), at these rates.
std::function<bool(const PcmFormat&)> floatAt(std::set<std::uint32_t> rates)
{
    return [rates](const PcmFormat& f) { return f.type == SampleType::Float && rates.contains(f.sampleRate); };
}

std::vector<double> floats(const std::filesystem::path& p, std::uint16_t channels, std::uint16_t ch = 0)
{
    const auto bytes = readAll(p);
    std::vector<double> x(bytes.size() / 4 / channels);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        float f;
        std::memcpy(&f, &bytes[(i * channels + ch) * 4], 4);
        x[i] = f;
    }
    return x;
}

} // namespace

TEST_CASE("Resampling: 原音モード never converts; a rate the DAC refuses is refused", "[acceptance][resample]")
{
    ap::test::TempDir dir;
    const sg::Layout layout{88200, 2, 8820};
    const auto in = dir.write("in.wav", sg::encodeWav(sg::bitPattern(layout, sg::SampleFormat::Int24)));
    const auto only44 = [](const PcmFormat& f) { return f.sampleRate == 44100; };
    CHECK_THROWS_AS(play(in, dir.path() / "a.raw", chain(std::nullopt), only44), FormatNotSupportedError);
    CHECK_THROWS_AS(play(in, dir.path() / "b.raw", nullptr, only44), FormatNotSupportedError);
}

TEST_CASE("Resampling: 音作りモード at the track's rate with nothing on is the direct path, the same bytes", "[acceptance][resample]")
{
    ap::test::TempDir dir;
    const sg::Layout layout{44100, 2, 11025};
    const auto in = dir.write("in.wav", sg::encodeWav(sg::bitPattern(layout, sg::SampleFormat::Int24)));
    const auto direct = play(in, dir.path() / "direct.raw", nullptr);
    const auto track = play(in, dir.path() / "track.raw", chain(OutputRateChoice{}));
    CHECK_FALSE(track.processing);
    CHECK_FALSE(track.resample);
    CHECK(toHex(sha256File(dir.path() / "track.raw")) == toHex(sha256File(dir.path() / "direct.raw")));
    CHECK(direct.output == track.output);
}

TEST_CASE("Resampling: a chosen rate - the processing path, the length, the level and the timing", "[acceptance][resample]")
{
    struct Case
    {
        std::uint32_t from;
        OutputRateChoice choice;
        std::uint32_t to;
    };
    for (const auto& c : {Case{44100, {OutputRateMode::Fixed, 96000}, 96000},
                          Case{44100, {OutputRateMode::Multiple, 0, 2}, 88200},
                          Case{48000, {OutputRateMode::AtLeast, 96000}, 96000},
                          Case{96000, {OutputRateMode::Fixed, 44100}, 44100},
                          Case{192000, {OutputRateMode::Fixed, 48000}, 48000},
                          // 705.6 and 768 kHz (task 10-5).
                          Case{44100, {OutputRateMode::Fixed, 768000}, 768000},
                          Case{96000, {OutputRateMode::Multiple, 0, 8}, 768000},
                          Case{88200, {OutputRateMode::Multiple, 0, 8}, 705600}})
    {
        CAPTURE(c.from, c.to);
        ap::test::TempDir dir;
        const std::size_t frames = c.from / 2, at = c.from / 10;
        // Left: an impulse. Right: 1 kHz at -6 dBFS.
        std::vector<double> x(2 * frames, 0.0);
        x[2 * at] = 0.5;
        for (std::size_t i = 0; i < frames; ++i)
            x[2 * i + 1] = 0.5 * std::sin(2 * std::numbers::pi * 1000.0 * static_cast<double>(i) / c.from);
        const auto in = dir.write("in.wav", sg::encodeWav(sg::quantize(x, c.from, 2, sg::SampleFormat::Int24)));

        const auto plan = play(in, dir.path() / "out.raw", chain(c.choice),
                               floatAt({44100, 48000, 88200, 96000, 192000, 705600, 768000}));
        REQUIRE(plan.processing);
        CHECK(plan.output.sampleRate == c.to);
        REQUIRE(plan.resample);
        CHECK(plan.resample->from == c.from);
        CHECK(plan.resample->to == c.to);
        CHECK(plan.resample->reason == Resampling::Reason::Setting);

        const auto left = floats(dir.path() / "out.raw", 2, 0);
        const auto right = floats(dir.path() / "out.raw", 2, 1);
        // As long as the input.
        CHECK(left.size() == (static_cast<std::uint64_t>(frames) * c.to + c.from - 1) / c.from);
        // The impulse at its time.
        std::size_t peak = 0;
        for (std::size_t i = 1; i < left.size(); ++i)
            if (std::abs(left[i]) > std::abs(left[peak]))
                peak = i;
        CHECK(std::abs(static_cast<double>(peak) - static_cast<double>(at) * c.to / c.from) <= 0.5 + 1e-9);
        // 1 kHz keeps its level (a whole number of cycles in the middle 100 ms).
        const std::size_t from = c.to / 5, n = c.to / 10;
        std::complex<double> s{};
        for (std::size_t i = 0; i < n; ++i)
            s += right[from + i] * std::polar(1.0, -2 * std::numbers::pi * 1000.0 * static_cast<double>(from + i) / c.to);
        const double level = 20 * std::log10(std::abs(s) * 2.0 / n / 0.5);
        CAPTURE(level);
        CHECK(std::abs(level) < 0.01);
    }
}

TEST_CASE("Resampling: a rate the DAC refuses is converted in 音作りモード (its family first)", "[acceptance][resample]")
{
    ap::test::TempDir dir;
    const sg::Layout layout{88200, 2, 8820};
    const auto in = dir.write("in.wav", sg::encodeWav(sg::bitPattern(layout, sg::SampleFormat::Int24)));
    const auto plan = play(in, dir.path() / "out.raw", chain(OutputRateChoice{}), floatAt({44100, 48000}));
    REQUIRE(plan.processing);
    CHECK(plan.output.sampleRate == 44100);
    REQUIRE(plan.resample);
    CHECK(plan.resample->reason == Resampling::Reason::Unsupported);
    CHECK(floats(dir.path() / "out.raw", 2).size() == 4410);
}

TEST_CASE("Resampling: with the safety limiter's look-ahead the timing still holds", "[acceptance][resample]")
{
    ap::test::TempDir dir;
    const std::uint32_t from = 44100, to = 48000;
    const std::size_t frames = 22050, at = 4410;
    std::vector<double> x(2 * frames, 0.0);
    x[2 * at] = 0.25;
    x[2 * at + 1] = 0.25;
    const auto in = dir.write("in.wav", sg::encodeWav(sg::quantize(x, from, 2, sg::SampleFormat::Int24)));
    auto limiter = std::make_shared<SafetyLimiter>();
    const auto plan = play(in, dir.path() / "out.raw", chain(OutputRateChoice{OutputRateMode::Fixed, to}, {limiter}),
                           floatAt({to}));
    REQUIRE(plan.processing);
    const auto left = floats(dir.path() / "out.raw", 2, 0);
    CHECK(left.size() == (static_cast<std::uint64_t>(frames) * to + from - 1) / from);
    std::size_t peak = 0;
    for (std::size_t i = 1; i < left.size(); ++i)
        if (std::abs(left[i]) > std::abs(left[peak]))
            peak = i;
    CHECK(std::abs(static_cast<double>(peak) - static_cast<double>(at) * to / from) <= 0.5 + 1e-9);
}

TEST_CASE("Resampling: changing the output rate while playing reopens at the position, the rate follows", "[acceptance][resample][player]")
{
    ap::test::TempDir dir;
    constexpr std::size_t total = 44100;
    std::vector<std::int16_t> v(total * 2);
    for (std::size_t i = 0; i < total; ++i)
        v[2 * i] = v[2 * i + 1] =
            static_cast<std::int16_t>(8000.0 * std::sin(2 * std::numbers::pi * 440 * static_cast<double>(i) / 44100));
    std::vector<std::byte> bytes(v.size() * 2);
    std::memcpy(bytes.data(), v.data(), bytes.size());

    auto c = chain(OutputRateChoice{}); // 音作りモード, the track's rate: the direct path first
    PlaybackOptions options;
    options.processing = c;
    auto log = std::make_shared<std::vector<PcmFormat>>();
    Player player([out = dir.path() / "o.raw", log] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out, .periodFrames = {441}, .sessionLog = log});
    }, options);
    auto src = std::make_unique<ap::test::TriggerSource>(
        std::make_unique<MemoryPcmSource>(PcmFormat{44100, 2, 16, 16, SampleType::Int, 0x3}, bytes),
        ap::test::TriggerSource::Triggers{{11025, [c] { c->setOutputRate(OutputRateChoice{OutputRateMode::Fixed, 96000}); }},
                                          {33075, [c] { c->setOutputRate(OutputRateChoice{}); }}});
    player.load(std::move(src));
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    REQUIRE(log->size() == 3);
    CHECK((*log)[0].sampleRate == 44100);
    CHECK((*log)[1].sampleRate == 96000);
    CHECK((*log)[2].sampleRate == 44100);
    REQUIRE(s.transitions.size() == 2);
    for (const auto& t : s.transitions)
        CHECK(t.kind == TransitionKind::ProcessingChanged);
    REQUIRE(s.path.has_value());
    CHECK_FALSE(s.path->processing); // back on the direct path
}
