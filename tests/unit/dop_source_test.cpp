// Task 1-5 completion criterion: DoP markers and ordering.

#include <ap/engine/audio_file.h>
#include <ap/engine/dop_source.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>
#include <ap/engine/prefetching_source.h>

#include "dsd_test_files.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstring>
#include <fstream>
#include <iterator>
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
// `firstFrame` = frame of the track the data starts at; `markerPhase` = frame
// count the marker sequence continues from (defaults to firstFrame).
void checkDop(const std::vector<std::uint8_t>& pcm, const std::vector<std::vector<std::uint8_t>>& ch,
              std::uint64_t firstFrame = 0, std::uint64_t markerPhase = UINT64_MAX)
{
    if (markerPhase == UINT64_MAX)
        markerPhase = firstFrame;
    const std::size_t nch = ch.size();
    const std::size_t frames = pcm.size() / (3 * nch);
    auto dsdByte = [&](std::size_t c, std::size_t k) -> std::uint8_t { return k < ch[c].size() ? ch[c][k] : 0x69; };
    for (std::size_t i = 0; i < frames; ++i)
    {
        const std::uint64_t f = firstFrame + i;
        const std::uint8_t marker = ((markerPhase + i) % 2 == 0) ? 0x05 : 0xFA;
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

TEST_CASE("DoP through Player: markers continue across a gapless join, also in 32-bit slots", "[engine][dop][player]")
{
    const bool only32 = GENERATE(false, true);
    CAPTURE(only32);
    const auto a = channels(1001); // odd number of DSD bytes
    const auto b = channels(777);
    ap::test::TempDir dir;
    const auto fa = dir.write("a.dsf", ap::test::makeDsf(a, 2822400));
    const auto fb = dir.write("b.dff", ap::test::makeDff(b, 2822400));

    Player player(
        [path = dir.path() / "out.raw", only32] {
            DummyOutput::Options o{.path = path, .periodFrames = {64, 7}};
            if (only32)
                o.accepts = [](const PcmFormat& f) { return f.containerBits == 32; };
            return std::make_unique<DummyOutput>(o);
        },
        PlaybackOptions{.allowDop = true});
    auto open = [](std::filesystem::path p) {
        return [p] { return std::make_unique<PrefetchingSource>(openAudioFile(p)); };
    };
    player.setQueue({{"a.dsf", open(fa)}, {"b.dff", open(fb)}});
    REQUIRE(player.status().dop.has_value());
    CHECK(player.status().dop->dsdRate == 2822400u);
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.transitions.size() == 1);
    CHECK(s.transitions[0].kind == TransitionKind::Gapless);
    CHECK(s.plan->output.sampleRate == 176400u);

    std::ifstream in(dir.path() / "out.raw", std::ios::binary);
    std::vector<std::uint8_t> out{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const std::size_t bps = only32 ? 4 : 3;
    // Repack to 32-bit slots only prepends a zero byte; the 24-bit DoP word is unchanged.
    std::vector<std::uint8_t> dop24;
    for (std::size_t i = 0; i < out.size(); i += bps)
    {
        if (only32)
            REQUIRE(out[i] == 0);
        dop24.insert(dop24.end(), out.begin() + static_cast<std::ptrdiff_t>(i + bps - 3),
                     out.begin() + static_cast<std::ptrdiff_t>(i + bps));
    }
    // Expected: track A padded to an even frame count, then track B, markers alternating throughout.
    const std::size_t framesA = ((a[0].size() + 1) / 2 + 1) / 2 * 2;
    const std::vector<std::uint8_t> partA(dop24.begin(), dop24.begin() + static_cast<std::ptrdiff_t>(framesA * 6));
    const std::vector<std::uint8_t> partB(dop24.begin() + static_cast<std::ptrdiff_t>(framesA * 6), dop24.end());
    checkDop(partA, a);
    checkDop(partB, b, 0, framesA); // marker phase continues from track A
}

TEST_CASE("DoP is refused unless enabled for the output", "[engine][dop][player]")
{
    ap::test::TempDir dir;
    const auto f = dir.write("a.dsf", ap::test::makeDsf(channels(1000), 2822400));
    auto factory = [path = dir.path() / "out.raw"] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{.path = path, .periodFrames = {64}});
    };

    SECTION("default: refused, nothing sent")
    {
        Player player(factory);
        player.setQueue({{"a.dsf", [f] { return openAudioFile(f); }}});
        player.play();
        player.waitUntilIdle();
        const auto s = player.status();
        CHECK(s.state == PlayerState::Error);
        CHECK(s.error.kind == PlayerErrorKind::DopNotAllowed);
        CHECK_FALSE(std::filesystem::exists(dir.path() / "out.raw"));
    }
    SECTION("enabled: played")
    {
        Player player(factory, PlaybackOptions{.allowDop = true});
        player.setQueue({{"a.dsf", [f] { return openAudioFile(f); }}});
        player.play();
        player.waitUntilIdle();
        CHECK(player.status().state == PlayerState::Ended);
    }
}

TEST_CASE("A PCM track followed by a DoP track of the same PCM format is not joined", "[engine][dop][player]")
{
    ap::test::TempDir dir;
    const auto dsf = dir.write("b.dsf", ap::test::makeDsf(channels(1000), 2822400));
    const PcmFormat pcm176{176400, 2, 24, 24, SampleType::Int, 3};
    std::vector<std::byte> pcmData(500 * 6, std::byte{0x11});

    Player player([path = dir.path() / "out.raw"] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{.path = path, .periodFrames = {64}});
    });
    player.setQueue({{"a.wav", [pcm176, pcmData] { return std::make_unique<MemoryPcmSource>(pcm176, pcmData); }},
                     {"b.dsf", [dsf] { return openAudioFile(dsf); }}});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    // The DoP track must go through the DoP check, so the join is a reopen, which here is refused.
    CHECK(s.state == PlayerState::Error);
    CHECK(s.error.kind == PlayerErrorKind::DopNotAllowed);
    CHECK(s.queueIndex == 1);
    REQUIRE(s.transitions.empty());
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
