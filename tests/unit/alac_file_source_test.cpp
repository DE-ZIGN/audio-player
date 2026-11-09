// Task 2-2: ALAC in .m4a.
//
// Test files come from two encoders:
//  - Apple's reference encoder (test-only copy) with a minimal MP4 writer, as
//    Apple's tools make them: exact length (short last packet), every bit
//    depth (16/20/24/32), any rate, 1..8 channels;
//  - the Windows (Microsoft) ALAC encoder, an implementation independent of
//    Apple's code. It pads the last packet with silence and does not record
//    the real length, so its files are as long as a whole number of packets.
// Output is cross-checked with the Windows decoder. Damaged and hostile files
// must be refused or decoded without memory errors (run under the "asan" preset too).

#include <ap/engine/alac_file_source.h>
#include <ap/engine/audio_file.h>
#include <ap/engine/mp4_reader.h>

#include "alac_m4a_writer.h"
#include "mf_audio.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ap::engine;
using ap::test::MfPcm;

namespace {

std::uint16_t containerOf(std::uint16_t bits)
{
    return bits == 20 ? 24 : bits;
}

// Interleaved little-endian samples in ALAC's PCM layout: 20-bit samples are
// left-justified in 3 bytes. `random` gives incompressible data (the
// encoder's escape path), otherwise a smooth signal (the predictor paths).
std::vector<std::byte> makePcm(std::uint32_t rate, std::uint16_t channels, std::uint16_t bits, std::size_t frames,
                               bool random = false, std::uint32_t seed = 1)
{
    const unsigned bytes = containerOf(bits) / 8u;
    const unsigned shift = containerOf(bits) - bits;
    const double full = static_cast<double>((1ll << (bits - 1)) - 1);
    std::vector<std::byte> out;
    out.reserve(frames * channels * bytes);
    std::uint64_t x = seed;
    for (std::size_t i = 0; i < frames; ++i)
        for (std::uint16_t c = 0; c < channels; ++c)
        {
            std::int64_t s = 0;
            if (random)
            {
                x = x * 6364136223846793005ull + 1442695040888963407ull;
                s = static_cast<std::int64_t>(x >> (64 - bits)) - (1ll << (bits - 1));
            }
            else
            {
                const double v = 0.5 * std::sin(2.0 * 3.14159265358979 * (440.0 * (c + 1)) * static_cast<double>(i) / rate);
                s = std::llround(v * full);
            }
            const auto u = static_cast<std::uint64_t>(s) << shift;
            for (unsigned b = 0; b < bytes; ++b)
                out.push_back(static_cast<std::byte>(u >> (8 * b)));
        }
    return out;
}

std::vector<std::byte> readAll(PcmSource& src, std::size_t chunkFrames = 1000)
{
    std::vector<std::byte> out;
    std::vector<std::byte> buf(chunkFrames * src.format().bytesPerFrame());
    while (const auto n = src.read(buf))
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n * src.format().bytesPerFrame()));
    return out;
}

std::vector<std::uint8_t> fileBytes(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeBytes(const std::filesystem::path& p, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::size_t findType(const std::vector<std::uint8_t>& file, const char* type, int occurrence = 1)
{
    int found = 0;
    for (std::size_t i = 4; i + 4 <= file.size(); ++i)
        if (std::memcmp(&file[i], type, 4) == 0 && ++found == occurrence)
            return i;
    throw std::runtime_error(std::string("no ") + type);
}

// Offset of the ALACSpecificConfig (inside the 'alac' configuration box, after version/flags).
std::size_t configOffset(const std::vector<std::uint8_t>& file)
{
    return findType(file, "alac", 2) + 4 + 4;
}

std::filesystem::path appleFile(const ap::test::TempDir& dir, const char* name, std::uint32_t rate, std::uint16_t channels,
                                std::uint16_t bits, const std::vector<std::byte>& pcm, ap::test::AppleAlacOptions options = {})
{
    const auto path = dir.path() / name;
    ap::test::writeAppleAlacM4a(path, rate, channels, bits, pcm, options);
    return path;
}

} // namespace

TEST_CASE("ALAC decodes to the encoded PCM, byte for byte, with the exact length", "[engine][alac]")
{
    const auto rate = GENERATE(44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u);
    const auto bits = GENERATE(std::uint16_t{16}, std::uint16_t{20}, std::uint16_t{24}, std::uint16_t{32});
    CAPTURE(rate, bits);
    ap::test::TempDir dir;
    const std::size_t frames = rate / 10 + 123; // the last packet is a partial one
    const auto pcm = makePcm(rate, 2, bits, frames);
    const auto path = appleFile(dir, "in.m4a", rate, 2, bits, pcm);

    AlacFileSource src(path);
    CHECK(src.format() == PcmFormat{rate, 2, containerOf(bits), bits, SampleType::Int, 0x3});
    CHECK(src.containerName() == "ALAC");
    CHECK(src.totalFrames() == frames);
    CHECK(readAll(src, 777) == pcm);
    CHECK(src.integrity().state == IntegrityState::NotAvailable);
    CHECK(src.integrity().decodeErrors == 0u);
}

TEST_CASE("ALAC: incompressible data (escape path), 1 to 8 channels", "[engine][alac]")
{
    struct Case
    {
        std::uint16_t channels;
        std::uint32_t mask;
    };
    const auto c = GENERATE(Case{1, 0x4}, Case{2, 0x3}, Case{3, 0}, Case{6, 0}, Case{8, 0});
    const auto bits = GENERATE(std::uint16_t{16}, std::uint16_t{24});
    const auto random = GENERATE(false, true);
    CAPTURE(c.channels, bits, random);
    ap::test::TempDir dir;
    const auto pcm = makePcm(48000, c.channels, bits, 4096 * 2 + 5, random, c.channels * 31u + bits);
    AlacFileSource src(appleFile(dir, "ch.m4a", 48000, c.channels, bits, pcm));
    CHECK(src.format().channels == c.channels);
    CHECK(src.format().channelMask == c.mask);
    CHECK(readAll(src) == pcm);
}

TEST_CASE("ALAC: an edit list trims the start and the length", "[engine][alac]")
{
    ap::test::TempDir dir;
    const std::size_t frames = 4096 * 3;
    const auto pcm = makePcm(44100, 2, 16, frames);
    AlacFileSource src(appleFile(dir, "edit.m4a", 44100, 2, 16, pcm, {.editStart = 1000, .editFrames = frames - 3000}));
    CHECK(src.totalFrames() == frames - 3000);
    const std::vector<std::byte> expected(pcm.begin() + 1000 * 4, pcm.begin() + static_cast<std::ptrdiff_t>((frames - 2000) * 4));
    CHECK(readAll(src) == expected);
}

TEST_CASE("ALAC from the Windows encoder: the padding it stores is played as it is, like Windows plays it",
          "[engine][alac]")
{
    const auto rate = GENERATE(44100u, 96000u, 192000u);
    const auto bits = GENERATE(std::uint16_t{16}, std::uint16_t{24}, std::uint16_t{32});
    CAPTURE(rate, bits);
    ap::test::TempDir dir;
    const std::size_t frames = rate / 10 + 123;
    const auto pcm = makePcm(rate, 2, bits, frames);
    const auto path = dir.path() / "win.m4a";
    ap::test::mfWriteAlacM4a(path, MfPcm{rate, 2, bits, pcm});

    AlacFileSource src(path);
    const std::size_t padded = (frames + 4095) / 4096 * 4096;
    CHECK(src.totalFrames() == padded); // the encoder records whole packets only
    const auto ours = readAll(src);
    REQUIRE(ours.size() == padded * 2 * (bits / 8u));
    CHECK(std::equal(pcm.begin(), pcm.end(), ours.begin()));
    CHECK(std::all_of(ours.begin() + static_cast<std::ptrdiff_t>(pcm.size()), ours.end(),
                      [](std::byte b) { return b == std::byte{0}; }));
    CHECK(ap::test::mfDecode(path, bits).data == ours); // the Windows decoder gives exactly the same
}

TEST_CASE("ALAC from Apple's encoder agrees with the Windows decoder", "[engine][alac]")
{
    const auto bits = GENERATE(std::uint16_t{16}, std::uint16_t{24});
    CAPTURE(bits);
    ap::test::TempDir dir;
    const auto pcm = makePcm(96000, 2, bits, 20000);
    const auto path = appleFile(dir, "x.m4a", 96000, 2, bits, pcm);
    AlacFileSource src(path);
    const auto ours = readAll(src);
    const auto theirs = ap::test::mfDecode(path, bits).data;
    REQUIRE(theirs.size() >= ours.size()); // Windows may not shorten the partial last packet
    CHECK(std::equal(ours.begin(), ours.end(), theirs.begin()));
}

TEST_CASE("ALAC seeks to an exact frame", "[engine][alac][seek]")
{
    ap::test::TempDir dir;
    const std::size_t frames = 4096 * 5 + 1000;
    const auto pcm = makePcm(44100, 2, 24, frames);
    const std::size_t bpf = 6;
    AlacFileSource src(appleFile(dir, "seek.m4a", 44100, 2, 24, pcm));

    std::vector<std::byte> buf(500 * bpf);
    for (const std::uint64_t target : {std::uint64_t{0}, std::uint64_t{4095}, std::uint64_t{4096}, std::uint64_t{12345},
                                       std::uint64_t{frames - 10}, std::uint64_t{3}})
    {
        CAPTURE(target);
        REQUIRE(src.seek(target));
        CHECK(src.position() == target);
        const auto n = src.read(buf);
        REQUIRE(n == std::min<std::uint64_t>(500, frames - target));
        CHECK(std::memcmp(buf.data(), pcm.data() + target * bpf, n * bpf) == 0);
    }
    REQUIRE(src.seek(frames));
    CHECK(src.read(buf) == 0u);
}

TEST_CASE("openAudioFile opens ALAC and AAC .m4a files by their codec", "[engine][alac][audiofile]")
{
    ap::test::TempDir dir;
    const auto pcm = makePcm(44100, 2, 16, 8000);
    const auto alac = appleFile(dir, "a.m4a", 44100, 2, 16, pcm);
    CHECK(detectAudioFile(alac) == AudioFileKind::Mp4);
    CHECK(openAudioFile(alac)->containerName() == "ALAC");

    const auto aac = dir.path() / "b.m4a";
    ap::test::mfWriteAacM4a(aac, MfPcm{44100, 2, 16, pcm});
    CHECK(detectAudioFile(aac) == AudioFileKind::Mp4);
    CHECK(openAudioFile(aac)->containerName() == "AAC");
}

TEST_CASE("ALAC: hostile configurations are refused before they size anything", "[engine][alac][hardening]")
{
    ap::test::TempDir dir;
    const auto good = fileBytes(appleFile(dir, "ok.m4a", 44100, 2, 16, makePcm(44100, 2, 16, 5000)));
    const auto cfg = configOffset(good);

    struct Patch
    {
        const char* what;
        std::size_t at;
        std::vector<std::uint8_t> bytes;
    };
    const auto p = GENERATE_COPY(Patch{"frame length 0", cfg, {0, 0, 0, 0}},
                                 Patch{"frame length 2^31", cfg, {0x80, 0, 0, 0}},
                                 Patch{"frame length 16385", cfg, {0, 0, 0x40, 0x01}},
                                 Patch{"newer version", cfg + 4, {1}},
                                 Patch{"bit depth 8", cfg + 5, {8}},
                                 Patch{"bit depth 33", cfg + 5, {33}},
                                 Patch{"kb 40", cfg + 8, {40}},
                                 Patch{"0 channels", cfg + 9, {0}},
                                 Patch{"9 channels", cfg + 9, {9}},
                                 Patch{"sample rate 0", cfg + 20, {0, 0, 0, 0}});
    CAPTURE(p.what);
    auto bad = good;
    std::copy(p.bytes.begin(), p.bytes.end(), bad.begin() + static_cast<std::ptrdiff_t>(p.at));
    writeBytes(dir.path() / "bad.m4a", bad);
    CHECK_THROWS_AS(AlacFileSource(dir.path() / "bad.m4a"), AlacFormatError);
}

TEST_CASE("ALAC: a partial-frame count above the frame length is rejected (the ALHACK overflow)",
          "[engine][alac][hardening]")
{
    // In a stereo packet the header is: element tag (3 bits), instance (4), unused (12),
    // partial / shift / escape (4), then, for a partial frame, the 32-bit sample count.
    // The last packet of an Apple-made file is a partial one: put a huge count there.
    const auto count = GENERATE(0x00004001u, 0x00100000u, 0xFFFFFFFFu);
    CAPTURE(count);
    ap::test::TempDir dir;
    const std::size_t frames = 4096 + 100;
    const auto path = appleFile(dir, "ok.m4a", 44100, 2, 16, makePcm(44100, 2, 16, frames));
    auto bytes = fileBytes(path);
    const auto track = readMp4AudioTrack(path);
    const auto packet = static_cast<std::size_t>(track.packets.back().offset);

    auto bitAt = [&](std::size_t bit) { return (bytes[packet + bit / 8] >> (7 - bit % 8)) & 1u; };
    REQUIRE(bitAt(19) == 1u); // the partial-frame flag is set
    for (std::size_t i = 0; i < 32; ++i)
    {
        const std::size_t bit = 23 + i;
        auto& byte = bytes[packet + bit / 8];
        const auto mask = static_cast<std::uint8_t>(0x80u >> (bit % 8));
        byte = static_cast<std::uint8_t>(((count >> (31 - i)) & 1u) ? (byte | mask) : (byte & ~mask));
    }
    writeBytes(dir.path() / "bad.m4a", bytes);

    AlacFileSource src(dir.path() / "bad.m4a");
    const auto out = readAll(src);
    CHECK(src.integrity().decodeErrors > 0u);
    CHECK(out.size() <= track.totalFrames * 4);
}

TEST_CASE("ALAC: a large packet flagged as partial (garbage sample count) is rejected", "[engine][alac][hardening]")
{
    // Noise of about 12 bits in 16-bit samples: compressed (not the escape path) but large
    // packets. Setting the partial-frame flag makes the decoder take the next 32 bits as the
    // sample count; without the checks it decodes far beyond its 4096-sample buffers.
    ap::test::TempDir dir;
    const std::size_t frames = 4096 * 3;
    std::vector<std::byte> pcm;
    std::uint64_t x = 99;
    for (std::size_t i = 0; i < frames * 2; ++i)
    {
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        const auto s = static_cast<std::int16_t>(static_cast<std::int32_t>(x >> 52) - 2048);
        pcm.push_back(static_cast<std::byte>(s & 0xFF));
        pcm.push_back(static_cast<std::byte>((s >> 8) & 0xFF));
    }
    const auto path = appleFile(dir, "noise.m4a", 44100, 2, 16, pcm);
    const auto track = readMp4AudioTrack(path);
    REQUIRE(track.packets[1].size > 8000u); // a large compressed packet
    auto bytes = fileBytes(path);
    const auto packet = static_cast<std::size_t>(track.packets[1].offset);
    REQUIRE(((bytes[packet + 19 / 8] >> (7 - 19 % 8)) & 1u) == 0u); // not partial yet
    bytes[packet + 19 / 8] |= static_cast<std::uint8_t>(0x80u >> (19 % 8));
    writeBytes(dir.path() / "bad.m4a", bytes);

    AlacFileSource src(dir.path() / "bad.m4a");
    const auto out = readAll(src);
    CHECK(src.integrity().decodeErrors > 0u);
    CHECK(out.size() == pcm.size()); // the damaged packet became silence of the same length
}

TEST_CASE("ALAC: damaged packets never crash; problems are counted", "[engine][alac][hardening]")
{
    const auto bits = GENERATE(std::uint16_t{16}, std::uint16_t{24}, std::uint16_t{32});
    CAPTURE(bits);
    ap::test::TempDir dir;
    const std::size_t frames = 4096 * 4 + 77;
    const auto path = appleFile(dir, "ok.m4a", 48000, 2, bits, makePcm(48000, 2, bits, frames));
    const auto good = fileBytes(path);
    const auto track = readMp4AudioTrack(path);
    const auto first = track.packets.front().offset;
    const auto last = track.packets.back().offset + track.packets.back().size;
    const std::size_t bpf = 2u * (bits / 8u);

    std::uint64_t x = 12345 + bits;
    std::uint32_t withErrors = 0;
    for (int round = 0; round < 200; ++round)
    {
        auto bad = good;
        const int flips = 1 + round % 8;
        for (int k = 0; k < flips; ++k)
        {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            const auto at = first + (x >> 33) % (last - first);
            bad[static_cast<std::size_t>(at)] ^= static_cast<std::uint8_t>(1u << ((x >> 20) % 8));
        }
        writeBytes(dir.path() / "bad.m4a", bad);
        AlacFileSource src(dir.path() / "bad.m4a");
        const auto out = readAll(src);
        // A rejected packet becomes silence of the length the table gives: never longer than that.
        CHECK(out.size() <= track.totalFrames * bpf);
        if (src.integrity().decodeErrors > 0)
        {
            ++withErrors;
            CHECK(src.integrity().state == IntegrityState::Failed);
        }
    }
    CHECK(withErrors > 0u); // the damage is noticed in at least some rounds
}

TEST_CASE("MP4: damaged tables are refused, truncated files are handled", "[engine][alac][hardening]")
{
    ap::test::TempDir dir;
    const auto good = fileBytes(appleFile(dir, "ok.m4a", 44100, 2, 16, makePcm(44100, 2, 16, 9000)));

    struct Patch
    {
        const char* what;
        std::size_t at;
        std::vector<std::uint8_t> bytes;
    };
    const auto p = GENERATE_COPY(
        Patch{"stsz count huge", findType(good, "stsz") + 4 + 4 + 4, {0x7F, 0xFF, 0xFF, 0xFF}},
        Patch{"stts entries huge", findType(good, "stts") + 4 + 4, {0x7F, 0xFF, 0xFF, 0xFF}},
        Patch{"stco offset outside the file", findType(good, "stco") + 4 + 4 + 4, {0x7F, 0xFF, 0xFF, 0x00}},
        Patch{"moov size too large", findType(good, "moov") - 4, {0x7F, 0xFF, 0xFF, 0xFF}},
        Patch{"stsc first chunk 0", findType(good, "stsc") + 4 + 4 + 4, {0, 0, 0, 0}},
        Patch{"timescale differs", findType(good, "mdhd") + 4 + 4 + 8, {0, 0, 0x03, 0xE8}});
    CAPTURE(p.what);
    auto bad = good;
    std::copy(p.bytes.begin(), p.bytes.end(), bad.begin() + static_cast<std::ptrdiff_t>(p.at));
    writeBytes(dir.path() / "bad.m4a", bad);
    CHECK_THROWS(AlacFileSource(dir.path() / "bad.m4a"));

    // Cut anywhere: refused or read to where the data ends, never past the end of the file.
    for (std::size_t cut : {std::size_t{8}, std::size_t{40}, good.size() / 3, good.size() / 2, good.size() - 1})
    {
        CAPTURE(cut);
        writeBytes(dir.path() / "cut.m4a",
                   std::vector<std::uint8_t>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut)));
        try
        {
            AlacFileSource src(dir.path() / "cut.m4a");
            readAll(src);
        }
        catch (const std::exception&)
        {
        }
    }
}

// Compares our decode of %AP_PROBE_IN% (any ALAC .m4a) with the Windows decoder's
// (run explicitly: set AP_PROBE_IN, then run "Probe: compare a file with the Windows decoder").
TEST_CASE("Probe: compare a file with the Windows decoder", "[.probe]")
{
    wchar_t* in = nullptr;
    std::size_t len = 0;
    if (_wdupenv_s(&in, &len, L"AP_PROBE_IN") != 0 || !in)
        return;
    const std::filesystem::path path(in);
    std::free(in);

    AlacFileSource src(path);
    const auto ours = readAll(src, 65536);
    const auto theirs = ap::test::mfDecode(path, src.format().containerBits).data;
    const std::size_t bpf = src.format().bytesPerFrame();
    std::size_t firstDiff = SIZE_MAX;
    for (std::size_t i = 0; i < std::min(ours.size(), theirs.size()); ++i)
        if (ours[i] != theirs[i])
        {
            firstDiff = i;
            break;
        }
    std::printf("ours %zu frames, Windows %zu frames, identical over the common part: %s",
                ours.size() / bpf, theirs.size() / bpf, firstDiff == SIZE_MAX ? "yes" : "no");
    if (firstDiff != SIZE_MAX)
        std::printf(" (first difference at frame %zu)", firstDiff / bpf);
    std::printf("\n");
    CHECK(firstDiff == SIZE_MAX);
}

// Writes a Windows-made AAC file to %AP_PROBE_OUT% for inspection (run with "[.probe]").
TEST_CASE("Probe: keep a Windows AAC file", "[.probe]")
{
    wchar_t* out = nullptr;
    std::size_t len = 0;
    if (_wdupenv_s(&out, &len, L"AP_PROBE_OUT") != 0 || !out)
        return;
    const std::filesystem::path path(out);
    std::free(out);
    ap::test::mfWriteAacM4a(path, MfPcm{44100, 2, 16, makePcm(44100, 2, 16, 4410 * 3 + 123)});
}

// Lists what the Windows ALAC encoder accepts (run explicitly with "[.probe]").
TEST_CASE("Probe: Windows ALAC encoder formats", "[.probe]")
{
    ap::test::TempDir dir;
    for (const std::uint32_t rate : {44100u, 192000u, 352800u, 384000u})
        for (const std::uint16_t bits : {std::uint16_t{16}, std::uint16_t{24}, std::uint16_t{32}})
        {
            const auto path = dir.path() / "p.m4a";
            try
            {
                ap::test::mfWriteAlacM4a(path, MfPcm{rate, 2, bits, makePcm(rate, 2, bits, rate / 10)});
                std::printf("%6u Hz %2u bit: OK\n", rate, bits);
            }
            catch (const std::exception& e)
            {
                std::printf("%6u Hz %2u bit: %s\n", rate, bits, e.what());
            }
        }
}
