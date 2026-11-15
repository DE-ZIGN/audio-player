// Task 9-6: reading the user's IR files - mono or stereo, any rate, the
// silence before the first arrival removed, longer files cut at 5 s; files
// that cannot be IRs give a message.

#include <ap/engine/dsp/room_ir.h>
#include <ap/signalgen/dsd.h>
#include <ap/signalgen/flac_writer.h>
#include <ap/signalgen/pcm.h>
#include <ap/signalgen/wav_writer.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <stdexcept>
#include <vector>

using namespace ap::engine;
namespace sg = ap::signalgen;

namespace {

sg::PcmData floatPcm(std::uint32_t rate, std::uint16_t channels, std::vector<double> samples)
{
    sg::PcmData p;
    p.sampleRate = rate;
    p.channels = channels;
    p.format = sg::SampleFormat::Float32;
    p.floatSamples = std::move(samples);
    return p;
}

} // namespace

TEST_CASE("A stereo IR file: the leading silence removed, both channels kept", "[space][irfile]")
{
    ap::test::TempDir dir;
    const std::size_t frames = 9600, lead = 300;
    std::vector<double> s(2 * frames, 0.0);
    for (std::size_t i = lead; i < frames; ++i)
    {
        const double env = std::exp(-static_cast<double>(i - lead) / 2000.0);
        s[2 * i] = 0.5 * env * ((i % 7) - 3.0) / 3.0;
        s[2 * i + 1] = -0.25 * env * ((i % 5) - 2.0) / 2.0;
    }
    s[2 * lead] = 0.5;
    const auto path = dir.path() / "hall.wav";
    sg::writeWav(path, floatPcm(48000, 2, s));
    const auto ir = loadImpulseResponse(path);
    CHECK(ir.fileChannels == 2);
    CHECK_FALSE(ir.truncated);
    CHECK(ir.trimmedFrames == lead);
    REQUIRE(ir.ir.channels.size() == 2);
    CHECK(ir.ir.sampleRate == 48000);
    CHECK(ir.ir.frames() == frames - lead);
    CHECK(ir.ir.channels[0][0] == 0.5);
    CHECK(ir.ir.channels[1][5] == static_cast<double>(static_cast<float>(s[2 * (lead + 5) + 1])));
}

TEST_CASE("A long mono FLAC IR is cut at 5 s with a fade-out", "[space][irfile]")
{
    ap::test::TempDir dir;
    const std::uint32_t rate = 44100;
    std::vector<double> s(static_cast<std::size_t>(rate) * 7, 0.0);
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = 0.5 * std::exp(-static_cast<double>(i) / (rate * 3.0)) * std::sin(0.37 * static_cast<double>(i));
    s[0] = 0.5;
    const auto path = dir.path() / "long.flac";
    sg::writeFlac(path, sg::quantize(s, rate, 1, sg::SampleFormat::Int24));
    const auto ir = loadImpulseResponse(path);
    CHECK(ir.truncated);
    REQUIRE(ir.ir.channels.size() == 1);
    CHECK(ir.ir.frames() == static_cast<std::size_t>(kMaxIrSeconds * rate));
    CHECK(ir.ir.channels[0].back() == 0.0);
}

TEST_CASE("Files that cannot be IRs give a message", "[space][irfile]")
{
    ap::test::TempDir dir;
    const auto silent = dir.path() / "silent.wav";
    sg::writeWav(silent, floatPcm(48000, 1, std::vector<double>(4800, 0.0)));
    CHECK_THROWS_AS(loadImpulseResponse(silent), std::runtime_error);

    const auto text = dir.path() / "notes.wav";
    const std::uint8_t junk[] = {'h', 'e', 'l', 'l', 'o'};
    dir.write("notes.wav", junk);
    CHECK_THROWS_AS(loadImpulseResponse(text), std::runtime_error);

    const auto dsf = dir.path() / "dsd.dsf";
    sg::writeBytes(dsf, sg::encodeDsf(sg::dsdSine(2822400, 2, 4096, 1000.0, -20.0), 2822400));
    CHECK_THROWS_AS(loadImpulseResponse(dsf), std::runtime_error);
}
