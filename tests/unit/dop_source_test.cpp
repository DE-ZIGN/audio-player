// Task 1-5 completion criterion: DoP markers and ordering.

#include <ap/engine/dop_source.h>

#include "dsd_test_files.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstring>
#include <vector>

using namespace ap::engine;

namespace {

std::vector<std::vector<std::uint8_t>> channels(std::size_t n, std::uint16_t count = 2)
{
    std::vector<std::vector<std::uint8_t>> ch(count, std::vector<std::uint8_t>(n));
    for (std::size_t c = 0; c < count; ++c)
        for (std::size_t i = 0; i < n; ++i)
            ch[c][i] = static_cast<std::uint8_t>(i * (5 + 2 * c) + 17 * c + 1);
    return ch;
}

std::unique_ptr<DsdSource> memory(const std::vector<std::vector<std::uint8_t>>& ch, std::uint32_t rate = 2822400)
{
    const auto bytes = ap::test::interleave(ch);
    std::vector<std::byte> v(bytes.size());
    std::memcpy(v.data(), bytes.data(), bytes.size());
    return std::make_unique<MemoryDsdSource>(
        DsdFormat{rate, static_cast<std::uint16_t>(ch.size()), 0, DsdContainer::Memory}, std::move(v));
}

std::vector<std::uint8_t> readAll(PcmSource& s, std::size_t chunk)
{
    std::vector<std::uint8_t> out;
    std::vector<std::byte> buf(chunk * s.format().bytesPerFrame());
    while (const auto n = s.read(buf))
    {
        const auto* p = reinterpret_cast<const std::uint8_t*>(buf.data());
        out.insert(out.end(), p, p + n * s.format().bytesPerFrame());
        if (n < chunk)
            break;
    }
    return out;
}

// Checks every DoP frame against the DSD bytes (padding with 0x69 beyond the end).
void checkDop(const std::vector<std::uint8_t>& pcm, const std::vector<std::vector<std::uint8_t>>& ch,
              std::uint64_t firstFrame = 0)
{
    const std::size_t nch = ch.size();
    const std::size_t frames = pcm.size() / (3 * nch);
    auto dsdByte = [&](std::size_t c, std::size_t k) -> std::uint8_t { return k < ch[c].size() ? ch[c][k] : 0x69; };
    for (std::size_t i = 0; i < frames; ++i)
    {
        const std::uint64_t f = firstFrame + i;
        const std::uint8_t marker = (f % 2 == 0) ? 0x05 : 0xFA;
        for (std::size_t c = 0; c < nch; ++c)
        {
            const std::uint8_t* s = &pcm[(i * nch + c) * 3]; // little-endian 24-bit
            INFO("frame " << f << " channel " << c);
            REQUIRE(s[2] == marker);                    // top byte: marker
            REQUIRE(s[1] == dsdByte(c, 2 * f));         // earlier 8 DSD bits
            REQUIRE(s[0] == dsdByte(c, 2 * f + 1));     // later 8 DSD bits
        }
    }
}

} // namespace

TEST_CASE("DoP: PCM format follows the DSD rate", "[engine][dop]")
{
    const auto rate = GENERATE(2822400u, 5644800u, 11289600u, 3072000u);
    DopSource dop(memory(channels(100), rate));
    CHECK(dop.format().sampleRate == rate / 16);
    CHECK(dop.format().containerBits == 24);
    CHECK(dop.format().validBits == 24);
    CHECK(dop.format().type == SampleType::Int);
    CHECK(dop.format().channels == 2);
    REQUIRE(dop.dop().has_value());
    CHECK(dop.dop()->dsdRate == rate);
}

TEST_CASE("DoP: markers alternate per frame and match across channels; DSD bits keep their order",
          "[engine][dop]")
{
    const auto nch = GENERATE(std::uint16_t{1}, std::uint16_t{2}, std::uint16_t{6});
    const auto ch = channels(4000, nch);
    DopSource dop(memory(ch));
    CHECK(dop.totalFrames() == 2000u);
    const auto pcm = readAll(dop, 441);
    CHECK(pcm.size() == 2000u * 3 * nch);
    checkDop(pcm, ch);
}

TEST_CASE("DoP: odd lengths are padded with DSD silence to an even number of frames", "[engine][dop]")
{
    const auto bytes = GENERATE(std::size_t{5}, std::size_t{6}, std::size_t{7}, std::size_t{8}, std::size_t{1});
    const auto ch = channels(bytes);
    DopSource dop(memory(ch));
    CHECK(dop.totalFrames() % 2 == 0);
    CHECK(dop.totalFrames() * 2 >= bytes);
    const auto pcm = readAll(dop, 3);
    CHECK(pcm.size() == dop.totalFrames() * 6);
    checkDop(pcm, ch);
}

TEST_CASE("DoP: two tracks joined directly keep the marker sequence", "[engine][dop]")
{
    // A has an odd number of DSD byte pairs; without padding B would start with the same marker as A's last frame.
    const auto a = channels(10);
    const auto b = channels(8);
    DopSource da(memory(a));
    DopSource db(memory(b));
    auto pa = readAll(da, 64);
    const auto pb = readAll(db, 64);
    // Frame markers across the join.
    const std::size_t lastA = pa.size() / 6 - 1;
    CHECK(pa[lastA * 6 + 2] == kDopMarkerOdd);
    CHECK(pb[2] == kDopMarkerEven);
}

TEST_CASE("DoP: seek keeps data and marker phase", "[engine][dop]")
{
    const auto ch = channels(8000);
    DopSource dop(memory(ch));
    REQUIRE(dop.seek(1001));
    CHECK(dop.position() == 1001u);
    std::vector<std::byte> buf(10 * 6);
    REQUIRE(dop.read(buf) == 10);
    std::vector<std::uint8_t> pcm(buf.size());
    std::memcpy(pcm.data(), buf.data(), buf.size());
    checkDop(pcm, ch, 1001);
}

TEST_CASE("DoP: DSF and DFF with the same bits give identical DoP", "[engine][dop]")
{
    const auto ch = channels(9000);
    ap::test::TempDir dir;
    DopSource fromDsf(std::make_unique<DsfFileSource>(dir.write("a.dsf", ap::test::makeDsf(ch, 2822400))));
    DopSource fromDff(std::make_unique<DffFileSource>(dir.write("a.dff", ap::test::makeDff(ch, 2822400))));
    const auto x = readAll(fromDsf, 500);
    const auto y = readAll(fromDff, 500);
    CHECK(x == y);
    checkDop(x, ch);
    CHECK(fromDsf.dop()->container == "DSF");
    CHECK(fromDff.dop()->container == "DFF");
}
