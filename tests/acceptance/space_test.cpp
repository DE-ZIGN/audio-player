// Task 9-6, acceptance (played through the dummy output, byte for byte):
//   - the space OFF, or at MIX 0: full bypass, the output equals the direct
//     path's (原音モード) bytes;
//   - the delay is compensated: with the space on, the dry signal comes out
//     at exactly the input's position (no shift, the same length) and the wet
//     signal starts exactly at the pre-delay.

#include <ap/engine/dsp/processing.h>
#include <ap/engine/dsp/space.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/playback.h>
#include <ap/engine/sha256.h>
#include <ap/engine/wav_file_source.h>
#include <ap/signalgen/signals.h>
#include <ap/signalgen/wav_writer.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace ap::engine;
namespace sg = ap::signalgen;

namespace {

std::vector<std::uint8_t> readAll(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::shared_ptr<SpaceStage> space(SpaceRoom room, double mix, double preDelayMs = 10.0)
{
    auto s = std::make_shared<SpaceStage>();
    SpaceStage::Params p;
    p.room = room;
    p.mixPercent = mix;
    p.preDelayMs = preDelayMs;
    s->set(p);
    return s;
}

std::string play(const std::filesystem::path& in, const std::filesystem::path& out,
                 std::shared_ptr<ProcessingChain> chain, PlaybackPlan& plan,
                 std::function<bool(const PcmFormat&)> accepts = {})
{
    WavFileSource source(in);
    DummyOutput output({.path = out, .periodFrames = {441, 4096, 7, 1000}, .accepts = accepts});
    PlaybackOptions options;
    options.processing = std::move(chain);
    plan = play(source, output, options).plan;
    return toHex(sha256File(out));
}

} // namespace

TEST_CASE("The space OFF or at MIX 0: full bypass, the same bytes as the direct path", "[acceptance][space]")
{
    const auto rate = GENERATE(44100u, 96000u, 192000u);
    const auto format = GENERATE(sg::SampleFormat::Int16, sg::SampleFormat::Int24, sg::SampleFormat::Int32);
    CAPTURE(rate, sg::formatName(format));
    ap::test::TempDir dir;
    const sg::Layout layout{rate, 2, rate / 4};
    const auto in = dir.write("in.wav", sg::encodeWav(sg::bitPattern(layout, format)));

    PlaybackPlan direct, spaced;
    const auto directHash = play(in, dir.path() / "direct.raw", nullptr, direct);
    const auto spacedHash = play(in, dir.path() / "space.raw",
                                 std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{
                                     space(SpaceRoom::Hall, 0.0), space(SpaceRoom::Off, 50.0)}),
                                 spaced);
    CHECK_FALSE(spaced.processing);
    CHECK(spaced.output == direct.output);
    CHECK(spacedHash == directHash);
}

TEST_CASE("The space on: the dry signal is not shifted and the wet one starts exactly at the pre-delay", "[acceptance][space]")
{
    const auto rate = GENERATE(44100u, 48000u, 96000u, 192000u);
    CAPTURE(rate);
    ap::test::TempDir dir;
    const std::size_t frames = rate / 2, at = rate / 20; // the impulse 50 ms in (MIX has reached its value)
    std::vector<double> x(2 * frames, 0.0);
    x[2 * at] = 0.5;
    x[2 * at + 1] = 0.5;
    const auto in = dir.write("in.wav", sg::encodeWav(sg::quantize(x, rate, 2, sg::SampleFormat::Int24)));

    PlaybackPlan plan;
    const double preMs = 10.0;
    play(in, dir.path() / "out.raw",
         std::make_shared<ProcessingChain>(
             std::vector<std::shared_ptr<ProcessingStage>>{space(SpaceRoom::Club, 30.0, preMs)}),
         plan, [](const PcmFormat& f) { return f.type == SampleType::Float; }); // no dither
    REQUIRE(plan.processing);
    const auto bytes = readAll(dir.path() / "out.raw");
    REQUIRE(bytes.size() == frames * 2 * 4); // the same length
    auto sample = [&](std::size_t frame) {
        float f;
        std::memcpy(&f, &bytes[frame * 8], 4);
        return static_cast<double>(f);
    };
    const auto pre = static_cast<std::size_t>(std::llround(preMs * rate / 1000.0));
    // Silence before the impulse; the dry impulse at its own frame (70 %).
    for (std::size_t i = 0; i < at; ++i)
        REQUIRE(sample(i) == 0.0);
    CHECK(std::abs(sample(at) - 0.35) < 1e-6);
    // Between the dry impulse and the pre-delay: nothing (but rounding).
    double between = 0.0;
    for (std::size_t i = at + 1; i < at + pre; ++i)
        between = std::max(between, std::abs(sample(i)));
    CHECK(between < 1e-9);
    // The wet signal from the pre-delay on.
    double after = 0.0;
    for (std::size_t i = at + pre; i < at + pre + rate / 10; ++i)
        after = std::max(after, std::abs(sample(i)));
    CHECK(after > 1e-3);
}
