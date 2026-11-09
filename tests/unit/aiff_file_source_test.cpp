// Task 2-1: AIFF / AIFC. Big-endian samples are delivered little-endian
// (bytes reversed per sample, values unchanged); little-endian AIFC types are
// delivered as stored.

#include <ap/engine/aiff_file_source.h>

#include "aiff_test_files.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

using namespace ap::engine;
using ap::test::AiffSpec;
using ap::test::makeAiff;

namespace {

std::vector<std::uint8_t> pattern(std::size_t n, std::uint32_t seed)
{
    std::vector<std::uint8_t> v(n);
    std::uint32_t x = seed;
    for (auto& b : v)
    {
        x = x * 1664525u + 1013904223u;
        b = static_cast<std::uint8_t>(x >> 24);
    }
    return v;
}

// Each sample of `bytes` bytes reversed.
std::vector<std::byte> swapped(const std::vector<std::uint8_t>& in, std::size_t bytes)
{
    std::vector<std::byte> out(in.size());
    for (std::size_t i = 0; i < in.size(); i += bytes)
        for (std::size_t k = 0; k < bytes; ++k)
            out[i + k] = static_cast<std::byte>(in[i + bytes - 1 - k]);
    return out;
}

std::vector<std::byte> asIs(const std::vector<std::uint8_t>& in)
{
    std::vector<std::byte> out(in.size());
    std::memcpy(out.data(), in.data(), in.size());
    return out;
}

std::vector<std::byte> readAll(PcmSource& src)
{
    std::vector<std::byte> out;
    std::vector<std::byte> buf(333 * src.format().bytesPerFrame());
    while (const auto n = src.read(buf))
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n * src.format().bytesPerFrame()));
    return out;
}

} // namespace

TEST_CASE("AIFF big-endian integer samples come out little-endian, values unchanged", "[engine][aiff]")
{
    const auto bits = GENERATE(std::uint16_t{16}, std::uint16_t{24}, std::uint16_t{32});
    CAPTURE(bits);
    ap::test::TempDir dir;
    const std::size_t bytes = bits / 8u;
    const auto samples = pattern(2 * 1000 * bytes, bits);
    const auto path = dir.write("in.aiff", makeAiff({.sampleSize = bits, .sampleRate = 96000}, samples));

    AiffFileSource src(path);
    CHECK(src.format() == PcmFormat{96000, 2, bits, bits, SampleType::Int, 0x3});
    CHECK(src.totalFrames() == 1000u);
    CHECK(src.containerName() == "AIFF");
    CHECK(src.bigEndian());
    CHECK(readAll(src) == swapped(samples, bytes));
}

TEST_CASE("AIFF 20-bit samples stay left-justified in a 24-bit container", "[engine][aiff]")
{
    ap::test::TempDir dir;
    auto samples = pattern(2 * 500 * 3, 20);
    for (std::size_t i = 2; i < samples.size(); i += 3)
        samples[i] &= 0xF0; // the file's low 4 bits are zero
    const auto path = dir.write("in.aiff", makeAiff({.sampleSize = 20}, samples));

    AiffFileSource src(path);
    CHECK(src.format().containerBits == 24);
    CHECK(src.format().validBits == 20);
    CHECK(readAll(src) == swapped(samples, 3));
}

TEST_CASE("AIFC uncompressed types", "[engine][aiff]")
{
    struct Case
    {
        const char* type;
        std::uint16_t bits;
        bool bigEndian;
        SampleType sampleType;
    };
    const auto c = GENERATE(Case{"NONE", 24, true, SampleType::Int}, Case{"twos", 16, true, SampleType::Int},
                            Case{"in24", 24, true, SampleType::Int}, Case{"in32", 32, true, SampleType::Int},
                            Case{"sowt", 16, false, SampleType::Int}, Case{"sowt", 24, false, SampleType::Int},
                            Case{"23ni", 24, false, SampleType::Int}, Case{"42ni", 32, false, SampleType::Int},
                            Case{"fl32", 32, true, SampleType::Float}, Case{"FL32", 32, true, SampleType::Float},
                            Case{"fl64", 64, true, SampleType::Float}, Case{"FL64", 64, true, SampleType::Float});
    CAPTURE(c.type, c.bits);
    ap::test::TempDir dir;
    const std::size_t bytes = c.bits / 8u;
    const auto samples = pattern(2 * 700 * bytes, c.bits + c.type[0]);
    const auto path =
        dir.write("in.aifc", makeAiff({.aifc = true, .compression = c.type, .sampleSize = c.bits, .sampleRate = 44100},
                                      samples));

    AiffFileSource src(path);
    CHECK(src.containerName() == "AIFC");
    CHECK(src.format().type == c.sampleType);
    CHECK(src.format().containerBits == c.bits);
    CHECK(src.format().validBits == c.bits);
    CHECK(src.bigEndian() == c.bigEndian);
    CHECK(readAll(src) == (c.bigEndian ? swapped(samples, bytes) : asIs(samples)));
}

TEST_CASE("AIFF: SSND offset, padding and unknown chunks are honoured", "[engine][aiff]")
{
    ap::test::TempDir dir;
    const auto samples = pattern(2 * 300 * 2, 5);
    const auto path = dir.write("in.aiff", makeAiff({.sampleSize = 16, .ssndOffset = 12, .extraChunks = true}, samples));
    AiffFileSource src(path);
    CHECK(readAll(src) == swapped(samples, 2));
}

TEST_CASE("AIFF sample rates are read from the 80-bit field exactly", "[engine][aiff]")
{
    const std::uint32_t rate =
        GENERATE(8000u, 22050u, 44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u, 705600u, 768000u);
    CAPTURE(rate);
    ap::test::TempDir dir;
    const auto path = dir.write("in.aiff", makeAiff({.sampleSize = 16, .sampleRate = rate}, pattern(2 * 10 * 2, 6)));
    CHECK(AiffFileSource(path).format().sampleRate == rate);
}

TEST_CASE("AIFF channel masks: mono and stereo known, others unspecified", "[engine][aiff]")
{
    ap::test::TempDir dir;
    CHECK(AiffFileSource(dir.write("1.aiff", makeAiff({.channels = 1, .sampleSize = 16}, pattern(20, 1))))
              .format()
              .channelMask
          == 0x4u);
    CHECK(AiffFileSource(dir.write("6.aiff", makeAiff({.channels = 6, .sampleSize = 16}, pattern(6 * 2 * 10, 2))))
              .format()
              .channelMask
          == 0u);
}

TEST_CASE("AIFF seeks to an exact frame", "[engine][aiff][seek]")
{
    ap::test::TempDir dir;
    const auto samples = pattern(2 * 2000 * 3, 7);
    const auto expected = swapped(samples, 3);
    AiffFileSource src(dir.write("in.aiff", makeAiff({.sampleSize = 24}, samples)));
    std::vector<std::byte> buf(100 * 6);
    REQUIRE(src.seek(1234));
    CHECK(src.position() == 1234u);
    REQUIRE(src.read(buf) == 100u);
    CHECK(std::memcmp(buf.data(), expected.data() + 1234 * 6, buf.size()) == 0);
    REQUIRE(src.seek(1990));
    CHECK(src.read(buf) == 10u);
}

TEST_CASE("AIFF with fewer samples than COMM says ends where the data ends", "[engine][aiff]")
{
    ap::test::TempDir dir;
    auto file = makeAiff({.sampleSize = 16, .extraChunks = false}, pattern(2 * 100 * 2, 8));
    file.resize(file.size() - 2 * 2 * 10); // cut the last 10 frames
    AiffFileSource src(dir.write("cut.aiff", file));
    CHECK(src.totalFrames() == 90u);
}

TEST_CASE("AIFF files that cannot be played unchanged are refused", "[engine][aiff]")
{
    ap::test::TempDir dir;
    CHECK_THROWS_AS(AiffFileSource(dir.write("8.aiff", makeAiff({.sampleSize = 8}, pattern(20, 1)))), AiffFormatError);
    CHECK_THROWS_AS(AiffFileSource(dir.write("ulaw.aifc",
                                             makeAiff({.aifc = true, .compression = "ulaw", .sampleSize = 16},
                                                      pattern(40, 2)))),
                    AiffFormatError);
    CHECK_THROWS_AS(AiffFileSource(dir.write("f24.aifc", makeAiff({.aifc = true, .compression = "fl32", .sampleSize = 24},
                                                                    pattern(60, 3)))),
                    AiffFormatError);
    const std::vector<std::uint8_t> riff{'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'A', 'V', 'E'};
    CHECK_THROWS_AS(AiffFileSource(dir.write("riff.aiff", riff)), AiffFormatError);
    auto noSsnd = makeAiff({.sampleSize = 16, .extraChunks = false}, {});
    CHECK_NOTHROW(AiffFileSource(dir.write("empty.aiff", noSsnd))); // empty SSND: zero frames
}

TEST_CASE("readExtended80 decodes common sample rates", "[engine][aiff]")
{
    std::vector<std::uint8_t> v;
    ap::test::putExtended(v, 44100);
    CHECK(readExtended80(v.data()) == 44100.0);
    const std::uint8_t zero[10]{};
    CHECK(readExtended80(zero) == 0.0);
}
