// Task 9-6: reading the user's IR files - mono or stereo, any rate, the
// silence before the first arrival removed, longer files cut at 5 s; files
// that cannot be IRs give the reason (a kind, task 11-3).

#include <ap/engine/dsp/room_ir.h>
#include <ap/signalgen/dsd.h>
#include <ap/signalgen/flac_writer.h>
#include <ap/signalgen/pcm.h>
#include <ap/signalgen/wav_writer.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
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

namespace {

// What loadImpulseResponse reports for a file (None when it reads it).
IrFileProblem problemOf(const std::filesystem::path& path)
{
    try
    {
        loadImpulseResponse(path);
    }
    catch (const IrFileError& e)
    {
        CHECK(std::string(e.what()).size() > 0); // the technical text, for logs
        return e.problem();
    }
    return IrFileProblem::None;
}

} // namespace

TEST_CASE("Files that cannot be IRs give the reason, as a kind (the page writes the words)", "[space][irfile]")
{
    ap::test::TempDir dir;
    const auto silent = dir.path() / "silent.wav";
    sg::writeWav(silent, floatPcm(48000, 1, std::vector<double>(4800, 0.0)));
    CHECK(problemOf(silent) == IrFileProblem::Silent);

    const auto text = dir.path() / "notes.wav";
    const std::uint8_t junk[] = {'h', 'e', 'l', 'l', 'o'};
    dir.write("notes.wav", junk);
    CHECK(problemOf(text) == IrFileProblem::NotAudio);

    const auto dsf = dir.path() / "dsd.dsf";
    sg::writeBytes(dsf, sg::encodeDsf(sg::dsdSine(2822400, 2, 4096, 1000.0, -20.0), 2822400));
    CHECK(problemOf(dsf) == IrFileProblem::Dsd);

    CHECK(problemOf(dir.path() / "missing.wav") == IrFileProblem::Missing);
    CHECK(problemOf(dir.path()) == IrFileProblem::Missing); // a folder is not a file

    // Longer than 60 s.
    const auto longer = dir.path() / "long.wav";
    std::vector<double> s(static_cast<std::size_t>(8000) * 61, 0.0);
    s[0] = 0.5;
    sg::writeWav(longer, floatPcm(8000, 1, s));
    CHECK(problemOf(longer) == IrFileProblem::TooLong);

    // Each kind's key, for the status (stable, not words).
    CHECK(std::string(irFileProblemKey(IrFileProblem::Missing)) == "missing");
    CHECK(std::string(irFileProblemKey(IrFileProblem::TooLong)) == "tooLong");
}
