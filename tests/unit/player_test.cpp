#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>
#include <ap/signalgen/signals.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

using namespace ap::engine;
using namespace std::chrono_literals;
namespace sg = ap::signalgen;

namespace {

constexpr std::uint32_t kRate = 48000;

PcmFormat format24()
{
    return PcmFormat{kRate, 2, 24, 24, SampleType::Int, 3};
}

std::vector<std::byte> makeData(std::size_t frames)
{
    const auto pcm = sg::bitPattern(sg::Layout{kRate, 2, frames}, sg::SampleFormat::Int24);
    std::vector<std::byte> bytes;
    bytes.reserve(pcm.intSamples.size() * 3);
    for (auto v : pcm.intSamples)
    {
        const auto u = static_cast<std::uint32_t>(v);
        bytes.push_back(static_cast<std::byte>(u));
        bytes.push_back(static_cast<std::byte>(u >> 8));
        bytes.push_back(static_cast<std::byte>(u >> 16));
    }
    return bytes;
}

std::vector<std::byte> readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<std::byte> out(raw.size());
    if (!raw.empty())
        std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

Player::OutputFactory dummyFactory(const std::filesystem::path& path, bool realtime,
                                   std::function<FormatCheck(const PcmFormat&)> check = {})
{
    return [=] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{
            .path = path, .periodFrames = {480}, .check = check, .paceInRealTime = realtime});
    };
}

std::vector<std::byte> slice(const std::vector<std::byte>& v, std::size_t from, std::size_t to)
{
    return {v.begin() + static_cast<std::ptrdiff_t>(from), v.begin() + static_cast<std::ptrdiff_t>(to)};
}

} // namespace

TEST_CASE("Player plays a loaded source to the end, bit-perfect", "[engine][player]")
{
    ap::test::TempDir dir;
    const auto data = makeData(10000);
    Player player(dummyFactory(dir.path() / "out.raw", false));

    CHECK(player.status().state == PlayerState::Empty);
    player.load(std::make_unique<MemoryPcmSource>(format24(), data));
    CHECK(player.status().state == PlayerState::Stopped);
    CHECK(player.status().totalFrames == 10000u);

    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(s.positionFrames == 10000u);
    REQUIRE(s.plan.has_value());
    CHECK(s.plan->repack == Repack::None);
    CHECK(readFile(dir.path() / "out.raw") == data);
}

TEST_CASE("Player starts from the seek position when stopped", "[engine][player]")
{
    ap::test::TempDir dir;
    const auto data = makeData(10000);
    Player player(dummyFactory(dir.path() / "out.raw", false));
    player.load(std::make_unique<MemoryPcmSource>(format24(), data));

    player.seek(2500);
    CHECK(player.status().positionFrames == 2500u);
    player.play();
    player.waitUntilIdle();

    CHECK(readFile(dir.path() / "out.raw") == slice(data, 2500 * 6, data.size()));
}

TEST_CASE("Player stop keeps the position", "[engine][player]")
{
    ap::test::TempDir dir;
    const auto data = makeData(kRate * 2); // 2 s, paced in real time
    Player player(dummyFactory(dir.path() / "out.raw", true));
    player.load(std::make_unique<MemoryPcmSource>(format24(), data));

    player.play();
    std::this_thread::sleep_for(200ms);
    CHECK(player.status().state == PlayerState::Playing);
    player.stop();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Stopped);
    CHECK(s.positionFrames > 0u);
    CHECK(s.positionFrames < kRate * 2u);

    const auto out = readFile(dir.path() / "out.raw");
    CHECK(out.size() == s.positionFrames * 6);
    CHECK(out == slice(data, 0, out.size()));
}

TEST_CASE("Player seek during playback jumps without changing sample values", "[engine][player]")
{
    ap::test::TempDir dir;
    const std::size_t total = kRate; // 1 s
    const auto data = makeData(total);
    Player player(dummyFactory(dir.path() / "out.raw", true));
    player.load(std::make_unique<MemoryPcmSource>(format24(), data));

    player.play();
    std::this_thread::sleep_for(100ms);
    player.seek(total / 2);
    player.waitUntilIdle();
    CHECK(player.status().state == PlayerState::Ended);

    // Output = data[0, k) followed by data[total/2, total).
    const auto out = readFile(dir.path() / "out.raw");
    const auto tail = slice(data, total / 2 * 6, data.size());
    REQUIRE(out.size() > tail.size());
    const std::size_t k = out.size() - tail.size();
    CHECK(k % 6 == 0);
    CHECK(slice(out, k, out.size()) == tail);
    CHECK(slice(out, 0, k) == slice(data, 0, k));
}

TEST_CASE("Player play after the end starts again from the beginning", "[engine][player]")
{
    ap::test::TempDir dir;
    const auto data = makeData(4000);
    Player player(dummyFactory(dir.path() / "out.raw", false));
    player.load(std::make_unique<MemoryPcmSource>(format24(), data));

    player.play();
    player.waitUntilIdle();
    REQUIRE(player.status().state == PlayerState::Ended);

    player.play();
    player.waitUntilIdle();
    CHECK(player.status().state == PlayerState::Ended);
    CHECK(readFile(dir.path() / "out.raw") == data);
}

TEST_CASE("Player reports an unsupported format as an error with its reason", "[engine][player]")
{
    ap::test::TempDir dir;
    Player player(dummyFactory(dir.path() / "out.raw", false, [](const PcmFormat&) {
        return FormatCheck::rejected(FormatRejection::SampleRate);
    }));
    player.load(std::make_unique<MemoryPcmSource>(format24(), makeData(1000)));

    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Error);
    CHECK(s.error.kind == PlayerErrorKind::FormatNotSupported);
    CHECK(s.error.formatCheck.reason == FormatRejection::SampleRate);
}

TEST_CASE("Player on a 96 kHz-only device: reason, accepted rates and lossless repack", "[engine][player][format]")
{
    // Same acceptance as the SSL 2 MKII with its control panel at 96 kHz.
    const auto ssl96 = [](const PcmFormat& f) {
        return f.sampleRate == 96000 && f.channels == 2 && f.type == SampleType::Int
            && ((f.containerBits == 16 && f.validBits == 16) || (f.containerBits == 32 && f.validBits == 24)
                || (f.containerBits == 32 && f.validBits == 32));
    };
    ap::test::TempDir dir;
    auto factory = [&](bool allowRepack) {
        (void)allowRepack;
        return [path = dir.path() / "out.raw", ssl96] {
            return std::make_unique<DummyOutput>(DummyOutput::Options{
                .path = path, .periodFrames = {480}, .check = {}, .paceInRealTime = false, .accepts = ssl96});
        };
    };

    SECTION("44.1 kHz file: refused, device takes 96 kHz")
    {
        Player player(factory(true));
        player.load(std::make_unique<MemoryPcmSource>(PcmFormat{44100, 2, 24, 24, SampleType::Int, 3}, makeData(1000)));
        player.play();
        player.waitUntilIdle();
        const auto s = player.status();
        CHECK(s.state == PlayerState::Error);
        CHECK(s.error.formatCheck.reason == FormatRejection::SampleRate);
        CHECK(s.error.formatCheck.acceptedRates == std::vector<std::uint32_t>{96000});
        CHECK_FALSE(std::filesystem::exists(dir.path() / "out.raw"));
    }

    SECTION("96 kHz packed 24-bit: sent in a 32-bit container, values unchanged")
    {
        Player player(factory(true));
        player.load(std::make_unique<MemoryPcmSource>(PcmFormat{96000, 2, 24, 24, SampleType::Int, 3}, makeData(1000)));
        player.play();
        player.waitUntilIdle();
        const auto s = player.status();
        CHECK(s.state == PlayerState::Ended);
        REQUIRE(s.plan.has_value());
        CHECK(s.plan->repack == Repack::WiderContainer);
    }

    SECTION("96 kHz packed 24-bit with repack disabled: refused as BitDepth")
    {
        Player player(factory(false), PlaybackOptions{.allowRepack = false});
        player.load(std::make_unique<MemoryPcmSource>(PcmFormat{96000, 2, 24, 24, SampleType::Int, 3}, makeData(1000)));
        player.play();
        player.waitUntilIdle();
        const auto s = player.status();
        CHECK(s.state == PlayerState::Error);
        CHECK(s.error.formatCheck.reason == FormatRejection::BitDepth);
        CHECK(s.error.formatCheck.acceptedRates == std::vector<std::uint32_t>{96000});
    }
}

TEST_CASE("Player load replaces the source and stops playback", "[engine][player]")
{
    ap::test::TempDir dir;
    Player player(dummyFactory(dir.path() / "out.raw", true));
    player.load(std::make_unique<MemoryPcmSource>(format24(), makeData(kRate)));
    player.play();
    std::this_thread::sleep_for(50ms);

    player.load(std::make_unique<MemoryPcmSource>(format24(), makeData(3000)));
    const auto s = player.status();
    CHECK(s.state == PlayerState::Stopped);
    CHECK(s.positionFrames == 0u);
    CHECK(s.totalFrames == 3000u);
}
