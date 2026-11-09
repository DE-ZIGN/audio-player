// CLAUDE.md chapter 5:
//   原音モード・音量 100%：出力のハッシュが入力 PCM と一致する（16/24/32bit、44.1〜384 kHz）
//
// Input: a generated WAV file. Output: the bytes the engine hands to the
// output layer, captured by DummyOutput. The SHA-256 of the output must equal
// the SHA-256 of the input file's data chunk.

#include <ap/engine/aiff_file_source.h>
#include <ap/engine/alac_file_source.h>
#include <ap/engine/flac_file_source.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/playback.h>
#include <ap/engine/sha256.h>
#include <ap/engine/wav_file_source.h>
#include <ap/signalgen/flac_writer.h>
#include <ap/signalgen/signals.h>
#include <ap/signalgen/wav_writer.h>

#include "aiff_test_files.h"
#include "alac_m4a_writer.h"
#include "temp_dir.h"
#include "wav_reader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <vector>

using namespace ap::engine;
namespace sg = ap::signalgen;

namespace {

std::vector<std::byte> readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<std::byte> out(raw.size());
    std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

std::span<const std::byte> asBytes(const std::vector<std::uint8_t>& v)
{
    return std::as_bytes(std::span(v));
}

// Signals that together cover every bit (bit pattern), full-scale values
// (ISP signal) and low-level detail (1 kHz -90 dBFS).
std::vector<std::uint8_t> makeWav(const std::string& signal, std::uint32_t rate, sg::SampleFormat format)
{
    const sg::Layout layout{rate, 2, rate / 4}; // 0.25 s
    if (signal == "bitpattern")
        return sg::encodeWav(sg::bitPattern(layout, format));
    if (signal == "isp")
        return sg::encodeWav(sg::quantize(sg::ispSine(layout, 0.0), rate, 2, format));
    return sg::encodeWav(sg::quantize(sg::sine(layout, 1000.0, -90.0), rate, 2, format));
}

} // namespace

TEST_CASE("Bit-perfect: dummy output hash equals input PCM hash", "[acceptance][bitperfect]")
{
    const auto rate = GENERATE(44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u);
    const auto format = GENERATE(sg::SampleFormat::Int16, sg::SampleFormat::Int24, sg::SampleFormat::Int32);
    const auto signal = GENERATE(std::string("bitpattern"), std::string("isp"), std::string("sine"));
    CAPTURE(rate, sg::formatName(format), signal);

    ap::test::TempDir dir;
    const auto wav = makeWav(signal, rate, format);
    const auto inPath = dir.write("in.wav", wav);
    const auto parsed = ap::test::parseWav(wav);
    const auto inputHash = sha256(asBytes(parsed.data));

    WavFileSource source(inPath);
    DummyOutput output({.path = dir.path() / "out.raw", .periodFrames = {441, 4096, 7, 1000}, .check = {}});
    const auto report = play(source, output);

    CHECK(report.plan.repack == Repack::None);
    CHECK(report.plan.output == source.format());
    CHECK(report.stats.framesRendered == rate / 4);
    CHECK(toHex(sha256File(dir.path() / "out.raw")) == toHex(inputHash));
}

TEST_CASE("Bit-perfect from FLAC: dummy output hash equals the encoded PCM hash", "[acceptance][bitperfect][flac]")
{
    const auto rate = GENERATE(44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u);
    const auto format = GENERATE(sg::SampleFormat::Int16, sg::SampleFormat::Int24, sg::SampleFormat::Int32);
    const auto signal = GENERATE(std::string("bitpattern"), std::string("isp"), std::string("sine"));
    CAPTURE(rate, sg::formatName(format), signal);

    ap::test::TempDir dir;
    // The same PCM as the WAV case; its WAV data chunk is the reference.
    const auto wav = makeWav(signal, rate, format);
    const auto parsed = ap::test::parseWav(wav);
    std::vector<std::int32_t> samples;
    const auto bytes = static_cast<std::size_t>(sg::bitsPerSample(format) / 8);
    for (std::size_t i = 0; i < parsed.data.size(); i += bytes)
    {
        std::uint32_t u = 0;
        for (std::size_t k = 0; k < bytes; ++k)
            u |= static_cast<std::uint32_t>(parsed.data[i + k]) << (8 * k);
        const unsigned shift = 32u - static_cast<unsigned>(bytes) * 8u;
        samples.push_back(static_cast<std::int32_t>(u << shift) >> shift); // sign-extend
    }
    const auto inPath = dir.path() / "in.flac";
    sg::writeFlac(inPath, rate, 2, static_cast<unsigned>(sg::bitsPerSample(format)), samples);

    FlacFileSource source(inPath);
    DummyOutput output({.path = dir.path() / "out.raw", .periodFrames = {441, 4096, 7, 1000}, .check = {}});
    const auto report = play(source, output);

    CHECK(report.plan.repack == Repack::None);
    CHECK(report.stats.framesRendered == rate / 4);
    CHECK(toHex(sha256File(dir.path() / "out.raw")) == toHex(sha256(asBytes(parsed.data))));
    CHECK(source.integrity().state == IntegrityState::Passed);
}

TEST_CASE("Bit-perfect from ALAC: dummy output hash equals the encoded PCM hash", "[acceptance][bitperfect][alac]")
{
    const auto rate = GENERATE(44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u);
    const auto format = GENERATE(sg::SampleFormat::Int16, sg::SampleFormat::Int24, sg::SampleFormat::Int32);
    const auto signal = GENERATE(std::string("bitpattern"), std::string("isp"), std::string("sine"));
    CAPTURE(rate, sg::formatName(format), signal);

    ap::test::TempDir dir;
    const auto parsed = ap::test::parseWav(makeWav(signal, rate, format));
    std::vector<std::byte> pcm(parsed.data.size());
    std::memcpy(pcm.data(), parsed.data.data(), pcm.size());
    const auto inPath = dir.path() / "in.m4a";
    ap::test::writeAppleAlacM4a(inPath, rate, 2, static_cast<std::uint16_t>(sg::bitsPerSample(format)), pcm);

    AlacFileSource source(inPath);
    DummyOutput output({.path = dir.path() / "out.raw", .periodFrames = {441, 4096, 7, 1000}, .check = {}});
    const auto report = play(source, output);
    CHECK(report.plan.repack == Repack::None);
    CHECK(report.stats.framesRendered == rate / 4);
    CHECK(toHex(sha256File(dir.path() / "out.raw")) == toHex(sha256(asBytes(parsed.data))));
}

TEST_CASE("Bit-perfect from AIFF: the output is the input with each sample's bytes reversed", "[acceptance][bitperfect][aiff]")
{
    const auto format = GENERATE(sg::SampleFormat::Int16, sg::SampleFormat::Int24, sg::SampleFormat::Int32);
    CAPTURE(sg::formatName(format));
    ap::test::TempDir dir;
    const auto wav = makeWav("bitpattern", 96000, format);
    const auto parsed = ap::test::parseWav(wav); // little-endian reference
    const auto bytes = static_cast<std::uint16_t>(sg::bitsPerSample(format) / 8);
    std::vector<std::uint8_t> bigEndian(parsed.data.size());
    for (std::size_t i = 0; i < parsed.data.size(); i += bytes)
        for (std::size_t k = 0; k < bytes; ++k)
            bigEndian[i + k] = parsed.data[i + bytes - 1 - k];

    AiffFileSource source(dir.write(
        "in.aiff", ap::test::makeAiff({.sampleSize = static_cast<std::uint16_t>(bytes * 8), .sampleRate = 96000}, bigEndian)));
    DummyOutput output({.path = dir.path() / "out.raw", .periodFrames = {441, 4096, 7, 1000}, .check = {}});
    play(source, output);
    CHECK(toHex(sha256File(dir.path() / "out.raw")) == toHex(sha256(asBytes(parsed.data))));
}

TEST_CASE("Lossless repack: 24-bit into a 32-bit-only device keeps every value", "[acceptance][bitperfect]")
{
    ap::test::TempDir dir;
    const auto wav = makeWav("bitpattern", 96000, sg::SampleFormat::Int24);
    const auto parsed = ap::test::parseWav(wav);

    WavFileSource source(dir.write("in.wav", wav));
    DummyOutput output({.path = dir.path() / "out.raw",
                        .periodFrames = {480},
                        .check = [](const PcmFormat& f) {
                            return f.containerBits == 32 ? FormatCheck::ok()
                                                         : FormatCheck::rejected(FormatRejection::BitDepth);
                        }});
    const auto report = play(source, output);

    CHECK(report.plan.repack == Repack::WiderContainer);
    CHECK(report.plan.output.containerBits == 32);
    CHECK(report.plan.output.validBits == 24);

    // Dropping the zero low byte of every 32-bit slot gives back the input exactly.
    const auto out = readFile(dir.path() / "out.raw");
    REQUIRE(out.size() == parsed.data.size() / 3 * 4);
    std::vector<std::uint8_t> unpacked;
    for (std::size_t i = 0; i < out.size(); i += 4)
    {
        CHECK(out[i] == std::byte{0});
        for (std::size_t k = 1; k < 4; ++k)
            unpacked.push_back(static_cast<std::uint8_t>(out[i + k]));
    }
    CHECK(unpacked == parsed.data);
}

TEST_CASE("Unsupported formats are refused with a reason, never converted", "[acceptance][bitperfect]")
{
    ap::test::TempDir dir;
    const auto wav = makeWav("bitpattern", 352800, sg::SampleFormat::Int24);
    WavFileSource source(dir.write("in.wav", wav));

    SECTION("rate not supported")
    {
        DummyOutput output({.path = dir.path() / "out.raw",
                            .periodFrames = {512},
                            .check = [](const PcmFormat& f) {
                                return f.sampleRate <= 192000 ? FormatCheck::ok()
                                                              : FormatCheck::rejected(FormatRejection::SampleRate);
                            }});
        try
        {
            play(source, output);
            FAIL("play() should have thrown");
        }
        catch (const FormatNotSupportedError& e)
        {
            CHECK(e.check().reason == FormatRejection::SampleRate);
        }
        CHECK_FALSE(std::filesystem::exists(dir.path() / "out.raw"));
    }

    SECTION("repack disabled")
    {
        DummyOutput output({.path = dir.path() / "out.raw",
                            .periodFrames = {512},
                            .check = [](const PcmFormat& f) {
                                return f.containerBits == 32 ? FormatCheck::ok()
                                                             : FormatCheck::rejected(FormatRejection::BitDepth);
                            }});
        CHECK_THROWS_AS(play(source, output, PlaybackOptions{.allowRepack = false}), FormatNotSupportedError);
    }
}
