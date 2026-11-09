// Task 2-1: FLAC decoding. The decoded bytes must equal the PCM that was
// encoded (left-justified in the container), and the MD5 stored in the file
// must be checked when the whole file is decoded from the start.

#include <ap/engine/audio_file.h>
#include <ap/engine/flac_file_source.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>
#include <ap/engine/prefetching_source.h>
#include <ap/signalgen/flac_writer.h>
#include <ap/signalgen/signals.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace ap::engine;
namespace sg = ap::signalgen;

namespace {

unsigned containerFor(unsigned bits)
{
    return bits <= 16 ? 16 : bits <= 24 ? 24 : 32;
}

// The bytes the source must deliver: each sample shifted to the top of its
// container, little-endian.
std::vector<std::byte> pack(const std::vector<std::int32_t>& samples, unsigned bits)
{
    const unsigned container = containerFor(bits);
    const unsigned shift = container - bits;
    std::vector<std::byte> out;
    out.reserve(samples.size() * container / 8);
    for (const auto s : samples)
    {
        const auto u = static_cast<std::uint32_t>(s) << shift;
        for (unsigned b = 0; b < container / 8; ++b)
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

// Pseudo-random samples covering the whole range of `bits`.
std::vector<std::int32_t> randomSamples(std::size_t count, unsigned bits, std::uint32_t seed)
{
    std::vector<std::int32_t> v(count);
    std::uint64_t x = seed;
    for (auto& s : v)
    {
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        const auto r = static_cast<std::int64_t>(x >> 32) & ((std::int64_t{1} << bits) - 1);
        s = static_cast<std::int32_t>(r - (std::int64_t{1} << (bits - 1)));
    }
    return v;
}

constexpr std::size_t kStreamInfoMd5Offset = 4 + 4 + 18; // "fLaC", block header, STREAMINFO fields before the MD5

} // namespace

TEST_CASE("FLAC decodes to the encoded PCM, byte for byte, and the MD5 matches", "[engine][flac]")
{
    const auto rate = GENERATE(44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u);
    const auto format = GENERATE(sg::SampleFormat::Int16, sg::SampleFormat::Int24, sg::SampleFormat::Int32);
    CAPTURE(rate, sg::formatName(format));

    ap::test::TempDir dir;
    const auto pcm = sg::bitPattern(sg::Layout{rate, 2, rate / 10}, format);
    const auto path = dir.path() / "in.flac";
    sg::writeFlac(path, pcm);

    FlacFileSource src(path);
    const auto bits = static_cast<unsigned>(sg::bitsPerSample(format));
    CHECK(src.format() == PcmFormat{rate, 2, static_cast<std::uint16_t>(bits), static_cast<std::uint16_t>(bits),
                                    SampleType::Int, 0x3});
    CHECK(src.totalFrames() == rate / 10);
    CHECK(src.containerName() == "FLAC");
    CHECK(src.integrity().state == IntegrityState::Pending);

    CHECK(readAll(src, 777) == pack(pcm.intSamples, bits));
    CHECK(src.position() == rate / 10);
    CHECK(src.integrity().state == IntegrityState::Passed);
    CHECK(src.integrity().decodeErrors == 0u);
}

TEST_CASE("FLAC with an odd bit depth is left-justified in its container", "[engine][flac]")
{
    const auto bits = GENERATE(12u, 20u);
    CAPTURE(bits);
    ap::test::TempDir dir;
    const auto samples = randomSamples(2 * 9000, bits, bits);
    const auto path = dir.path() / "odd.flac";
    sg::writeFlac(path, 96000, 2, bits, samples);

    FlacFileSource src(path);
    CHECK(src.format().containerBits == containerFor(bits));
    CHECK(src.format().validBits == bits);
    CHECK(src.bitsPerSample() == bits);
    CHECK(readAll(src) == pack(samples, bits));
    CHECK(src.integrity().state == IntegrityState::Passed); // the MD5 covers the unshifted samples
}

TEST_CASE("FLAC channel counts get the standard speaker masks", "[engine][flac]")
{
    struct Case
    {
        std::uint16_t channels;
        std::uint32_t mask;
    };
    const auto c = GENERATE(Case{1, 0x4}, Case{2, 0x3}, Case{6, 0x3F}, Case{8, 0x63F});
    CAPTURE(c.channels);
    ap::test::TempDir dir;
    const auto samples = randomSamples(std::size_t{c.channels} * 4800, 24, c.channels);
    const auto path = dir.path() / "multi.flac";
    sg::writeFlac(path, 48000, c.channels, 24, samples);

    FlacFileSource src(path);
    CHECK(src.format().channels == c.channels);
    CHECK(src.format().channelMask == c.mask);
    CHECK(readAll(src) == pack(samples, 24));
}

TEST_CASE("FLAC: a wrong stored MD5 is reported, the samples are still delivered", "[engine][flac][integrity]")
{
    ap::test::TempDir dir;
    const auto samples = randomSamples(2 * 20000, 24, 7);
    const auto path = dir.path() / "md5.flac";
    sg::writeFlac(path, 44100, 2, 24, samples);
    auto bytes = fileBytes(path);
    bytes[kStreamInfoMd5Offset + 3] ^= 0x01;
    writeBytes(path, bytes);

    FlacFileSource src(path);
    CHECK(readAll(src) == pack(samples, 24));
    CHECK(src.integrity().state == IntegrityState::Failed);
    CHECK(src.integrity().decodeErrors == 0u);
}

TEST_CASE("FLAC: a damaged frame is reported as a decode error", "[engine][flac][integrity]")
{
    ap::test::TempDir dir;
    const auto samples = randomSamples(2 * 44100, 16, 8);
    const auto path = dir.path() / "damaged.flac";
    sg::writeFlac(path, 44100, 2, 16, samples, {.seekPointSeconds = 0});
    auto bytes = fileBytes(path);
    for (std::size_t i = bytes.size() / 2; i < bytes.size() / 2 + 16; ++i)
        bytes[i] ^= 0x5A;
    writeBytes(path, bytes);

    FlacFileSource src(path);
    readAll(src);
    CHECK(src.integrity().state == IntegrityState::Failed);
    CHECK(src.integrity().decodeErrors > 0u);
}

TEST_CASE("FLAC without a stored MD5 says the check is not available", "[engine][flac][integrity]")
{
    ap::test::TempDir dir;
    const auto samples = randomSamples(2 * 4800, 16, 9);
    const auto path = dir.path() / "nomd5.flac";
    sg::writeFlac(path, 48000, 2, 16, samples, {.storeMd5 = false});

    FlacFileSource src(path);
    CHECK(src.integrity().state == IntegrityState::NotAvailable);
    CHECK(readAll(src) == pack(samples, 16));
    CHECK(src.integrity().state == IntegrityState::NotAvailable);
}

TEST_CASE("FLAC seeks to an exact frame; a seek skips the MD5 check until a seek back to 0", "[engine][flac][seek]")
{
    ap::test::TempDir dir;
    const std::uint32_t rate = 48000;
    const auto samples = randomSamples(2 * std::size_t{rate} * 3, 24, 10); // 3 s
    const auto expected = pack(samples, 24);
    const auto path = dir.path() / "seek.flac";
    sg::writeFlac(path, rate, 2, 24, samples);
    const std::size_t bpf = 6;

    FlacFileSource src(path);
    std::vector<std::byte> buf(1000 * bpf);
    REQUIRE(src.read(buf) == 1000u);
    CHECK(src.integrity().state == IntegrityState::Pending);

    for (const std::uint64_t target : {std::uint64_t{70001}, std::uint64_t{12}, std::uint64_t{143999}})
    {
        CAPTURE(target);
        REQUIRE(src.seek(target));
        CHECK(src.position() == target);
        const auto n = src.read(buf);
        const auto want = std::min<std::uint64_t>(1000, rate * 3 - target);
        REQUIRE(n == want);
        CHECK(std::memcmp(buf.data(), expected.data() + target * bpf, n * bpf) == 0);
    }
    CHECK(src.integrity().state == IntegrityState::Skipped);

    REQUIRE(src.seek(src.totalFrames()));
    CHECK(src.read(buf) == 0u);

    REQUIRE(src.seek(0));
    CHECK(src.integrity().state == IntegrityState::Pending);
    CHECK(readAll(src) == expected);
    CHECK(src.integrity().state == IntegrityState::Passed);
}

TEST_CASE("openAudioFile opens FLAC by content, also behind an ID3v2 tag", "[engine][flac]")
{
    ap::test::TempDir dir;
    const auto samples = randomSamples(2 * 4800, 16, 11);
    const auto plain = dir.path() / "plain.flac";
    sg::writeFlac(plain, 48000, 2, 16, samples);

    // ID3v2.3 header with a 20-byte (synchsafe) body of padding, then the FLAC stream.
    std::vector<std::uint8_t> tagged{'I', 'D', '3', 3, 0, 0, 0, 0, 0, 20};
    tagged.resize(tagged.size() + 20, 0);
    const auto flac = fileBytes(plain);
    tagged.insert(tagged.end(), flac.begin(), flac.end());
    const auto taggedPath = dir.path() / "tagged.wav"; // the extension does not decide the format
    writeBytes(taggedPath, tagged);

    for (const auto& p : {plain, taggedPath})
    {
        CAPTURE(p.filename().string());
        const auto src = openAudioFile(p);
        CHECK(src->containerName() == "FLAC");
        CHECK(readAll(*src) == pack(samples, 16));
        CHECK(src->integrity().state == IntegrityState::Passed);
    }
}

TEST_CASE("Player reports the MD5 check, also for tracks already played", "[engine][flac][integrity][player]")
{
    ap::test::TempDir dir;
    const auto a = randomSamples(2 * 9600, 24, 21);
    const auto b = randomSamples(2 * 9600, 24, 22);
    const auto bad = dir.path() / "1_bad.flac";
    const auto good = dir.path() / "2_good.flac";
    sg::writeFlac(bad, 48000, 2, 24, a);
    sg::writeFlac(good, 48000, 2, 24, b);
    auto bytes = fileBytes(bad);
    bytes[kStreamInfoMd5Offset] ^= 0x80;
    writeBytes(bad, bytes);

    auto item = [](const std::filesystem::path& p) {
        return QueueItem{p.filename().string(), [p] { return std::make_unique<PrefetchingSource>(openAudioFile(p)); }};
    };
    Player player([out = dir.path() / "out.raw"] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out, .periodFrames = {480}});
    });
    player.setQueue({item(bad), item(good)});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.transitions.size() == 1u);
    CHECK(s.transitions[0].kind == TransitionKind::Gapless); // the check does not stop playback
    CHECK(s.integrity.state == IntegrityState::Passed);       // the current (last) track
    CHECK(s.integrityFailures == std::vector<std::size_t>{0}); // the first track
}

TEST_CASE("A file that is not FLAC is refused", "[engine][flac]")
{
    ap::test::TempDir dir;
    const auto path = dir.path() / "not.flac";
    writeBytes(path, {'f', 'L', 'a', 'C', 0, 0, 0, 0});
    CHECK_THROWS_AS(FlacFileSource(path), FlacFormatError);
}
