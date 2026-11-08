// Task 1-5: DSF / DFF readers deliver the DSD bits unchanged in the canonical
// layout (bytes interleaved by channel, earliest bit in the MSB).

#include <ap/engine/dsd_source.h>

#include "dsd_test_files.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstring>
#include <vector>

using namespace ap::engine;
using ap::test::interleave;
using ap::test::makeDff;
using ap::test::makeDsf;

namespace {

std::vector<std::vector<std::uint8_t>> stereo(std::size_t n)
{
    std::vector<std::uint8_t> l(n), r(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        l[i] = static_cast<std::uint8_t>(i * 7 + 1);
        r[i] = static_cast<std::uint8_t>(0x80 + i * 3);
    }
    return {l, r};
}

std::vector<std::uint8_t> readAll(DsdSource& s, std::size_t chunkFrames)
{
    std::vector<std::uint8_t> out;
    std::vector<std::byte> buf(chunkFrames * s.format().channels);
    while (const auto n = s.read(buf))
    {
        const auto* p = reinterpret_cast<const std::uint8_t*>(buf.data());
        out.insert(out.end(), p, p + n * s.format().channels);
    }
    return out;
}

} // namespace

TEST_CASE("DFF: interleaved DSD bytes are read unchanged", "[engine][dsd]")
{
    const auto ch = stereo(10001);
    ap::test::TempDir dir;
    DffFileSource src(dir.write("a.dff", makeDff(ch, 2822400)));
    CHECK(src.format().dsdRate == 2822400u);
    CHECK(src.format().channels == 2);
    CHECK(src.format().container == DsdContainer::Dff);
    CHECK(src.totalBytes() == 10001u);
    CHECK(readAll(src, 333) == interleave(ch));
}

TEST_CASE("DSF (1 bit, LSB first): bits are reordered to MSB first, nothing else changes", "[engine][dsd]")
{
    const auto blockSize = GENERATE(4096u, 100u);
    const auto ch = stereo(10001); // more than two blocks, last block partly padding
    ap::test::TempDir dir;
    DsfFileSource src(dir.write("a.dsf", makeDsf(ch, 5644800, 1, blockSize)));
    CHECK(src.format().dsdRate == 5644800u);
    CHECK(src.format().container == DsdContainer::Dsf);
    CHECK(src.totalBytes() == 10001u);
    CHECK(readAll(src, 777) == interleave(ch));
}

TEST_CASE("DSF bit order matches the specification", "[engine][dsd]")
{
    // With 1 bit per sample the first sample is the LSB of the stored byte.
    // Stored 0x01 (first sample = 1) is canonical 0x80; stored 0x0F is 0xF0.
    std::vector<std::vector<std::uint8_t>> canonical{{0x80, 0xF0, 0x01}, {0x00, 0xFF, 0x69}};
    const auto file = makeDsf(canonical, 2822400);
    // makeDsf stores reversed bytes; check the raw file holds the spec's values.
    const std::size_t dataStart = 28 + 52 + 12;
    CHECK(file[dataStart + 0] == 0x01);
    CHECK(file[dataStart + 1] == 0x0F);
    CHECK(file[dataStart + 2] == 0x80);

    ap::test::TempDir dir;
    DsfFileSource src(dir.write("a.dsf", file));
    CHECK(readAll(src, 16) == interleave(canonical));
}

TEST_CASE("DSF with 8 bits per sample (MSB first) is not reordered", "[engine][dsd]")
{
    const auto ch = stereo(5000);
    ap::test::TempDir dir;
    DsfFileSource src(dir.write("a.dsf", makeDsf(ch, 2822400, 8)));
    CHECK(readAll(src, 1000) == interleave(ch));
}

TEST_CASE("DSD seek lands on the right byte frame", "[engine][dsd]")
{
    const auto ch = stereo(12000);
    const auto all = interleave(ch);
    ap::test::TempDir dir;
    DsfFileSource dsf(dir.write("a.dsf", makeDsf(ch, 2822400)));
    DffFileSource dff(dir.write("a.dff", makeDff(ch, 2822400)));
    for (DsdSource* s : {static_cast<DsdSource*>(&dsf), static_cast<DsdSource*>(&dff)})
    {
        REQUIRE(s->seek(9000)); // in the third DSF block group
        CHECK(s->position() == 9000u);
        std::vector<std::byte> buf(50 * 2);
        REQUIRE(s->read(buf) == 50);
        CHECK(std::memcmp(buf.data(), all.data() + 9000 * 2, buf.size()) == 0);
    }
}

TEST_CASE("DST-compressed DFF and non-DSD files are refused", "[engine][dsd]")
{
    ap::test::TempDir dir;
    CHECK_THROWS_AS(DffFileSource(dir.write("a.dff", makeDff(stereo(100), 2822400, "DST "))), DsdFormatError);
    const std::vector<std::uint8_t> junk(64, 0x20);
    CHECK_THROWS_AS(DsfFileSource(dir.write("b.dsf", junk)), DsdFormatError);
    CHECK_THROWS_AS(openDsdFile(dir.write("c.wav", junk)), DsdFormatError);
    CHECK_FALSE(looksLikeDsdFile(dir.path() / "c.wav"));
    CHECK(looksLikeDsdFile(dir.write("d.dsf", makeDsf(stereo(10), 2822400))));
    CHECK(looksLikeDsdFile(dir.write("e.dff", makeDff(stereo(10), 2822400))));
}

TEST_CASE("DSD rate names", "[engine][dsd]")
{
    CHECK(dsdRateName(2822400) == "DSD64");
    CHECK(dsdRateName(5644800) == "DSD128");
    CHECK(dsdRateName(11289600) == "DSD256");
    CHECK(dsdRateName(3072000) == "DSD64 (48 kHz family)");
}
