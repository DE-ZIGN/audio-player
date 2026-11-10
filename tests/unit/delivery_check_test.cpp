// Task 4-3: the bit-perfect self-check. What the output wrote into the
// driver's buffers is compared with the values read from the source; a
// difference takes BIT-PERFECT away.

#include <ap/engine/bit_perfect.h>
#include <ap/engine/output/asio_output.h>
#include <ap/engine/output/asio_sample_types.h>
#include <ap/engine/output/delivery_check.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>

#include "fake_asio_driver.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

using namespace ap::engine;
using ap::test::FakeAsioDriver;

namespace {

PcmFormat fmt(std::uint16_t container, std::uint16_t valid, std::uint16_t channels = 2,
              SampleType type = SampleType::Int)
{
    return PcmFormat{48000, channels, container, valid, type, 0};
}

std::vector<std::byte> pattern(std::size_t bytes, std::uint32_t seed)
{
    std::vector<std::byte> v(bytes);
    std::uint32_t x = seed;
    for (auto& b : v)
    {
        x = x * 1664525u + 1013904223u;
        b = static_cast<std::byte>(x >> 24);
    }
    return v;
}

// Packed 24-bit -> 24-bit in a 32-bit container (zero low byte), like repackSamples.
std::vector<std::byte> widen24(const std::vector<std::byte>& packed)
{
    std::vector<std::byte> out;
    for (std::size_t i = 0; i + 3 <= packed.size(); i += 3)
    {
        out.push_back(std::byte{0});
        out.insert(out.end(), packed.begin() + static_cast<std::ptrdiff_t>(i),
                   packed.begin() + static_cast<std::ptrdiff_t>(i + 3));
    }
    return out;
}

} // namespace

TEST_CASE("Self-check: the same bytes match", "[output][selfcheck]")
{
    DeliveryCheck c;
    const auto f = fmt(24, 24);
    const auto data = pattern(480 * f.bytesPerFrame(), 1);
    c.read(data, f, 480);
    c.deliveredInterleaved(data, f, 480);
    CHECK(c.result().matchedFrames == 480);
    CHECK(c.result().mismatchedFrames == 0);
}

TEST_CASE("Self-check: one changed sample is one mismatched frame", "[output][selfcheck]")
{
    DeliveryCheck c;
    const auto f = fmt(16, 16);
    const auto data = pattern(480 * f.bytesPerFrame(), 2);
    auto changed = data;
    changed[100 * f.bytesPerFrame() + 2] ^= std::byte{1}; // the lowest bit of frame 100, right channel
    c.read(data, f, 480);
    c.deliveredInterleaved(changed, f, 480);
    CHECK(c.result().matchedFrames == 479);
    CHECK(c.result().mismatchedFrames == 1);
}

TEST_CASE("Self-check: a lossless repack (24 in 32) matches, a lost low byte does not", "[output][selfcheck]")
{
    DeliveryCheck c;
    const auto packed = pattern(256 * 6, 3);
    const auto wide = widen24(packed);
    c.read(packed, fmt(24, 24), 256);
    c.deliveredInterleaved(wide, fmt(32, 24), 256);
    CHECK(c.result().mismatchedFrames == 0);

    // 24-bit cut to 16-bit: the values are not the same.
    std::vector<std::byte> cut;
    for (std::size_t i = 0; i + 3 <= packed.size(); i += 3)
        cut.insert(cut.end(), {packed[i + 1], packed[i + 2]});
    c.reset();
    c.read(packed, fmt(24, 24), 256);
    c.deliveredInterleaved(cut, fmt(16, 16), 256);
    CHECK(c.result().mismatchedFrames > 250); // only frames whose low bytes were zero can match
}

TEST_CASE("Self-check: ASIO per-channel buffers in each sample type", "[output][selfcheck][asio]")
{
    const std::size_t frames = 128;
    const auto packed = pattern(frames * 6, 4); // 24-bit stereo
    std::vector<std::vector<std::byte>> bufs(2);
    auto split = [&](long type) {
        const std::size_t bps = asio::bytesPerSample(type);
        for (auto& b : bufs)
            b.assign(frames * bps, std::byte{0x5A});
        std::vector<void*> ptrs{bufs[0].data(), bufs[1].data()};
        asio::deinterleave(type, type == asio::Int32LSB ? fmt(32, 24) : fmt(24, 24),
                           type == asio::Int32LSB ? std::span<const std::byte>(widen24(packed)) : packed, frames, ptrs);
        return ptrs;
    };

    for (long type : {static_cast<long>(asio::Int24LSB), static_cast<long>(asio::Int32LSB),
                      static_cast<long>(asio::Int32LSB24)})
    {
        CAPTURE(type);
        DeliveryCheck c;
        const auto ptrs = split(type);
        c.read(packed, fmt(24, 24), frames);
        c.deliveredAsio(type, ptrs, frames);
        CHECK(c.result().matchedFrames == frames);
        CHECK(c.result().mismatchedFrames == 0);

        // A changed bit in the driver's buffer of the left channel.
        static_cast<std::byte*>(ptrs[0])[5 * asio::bytesPerSample(type) + (type == asio::Int32LSB ? 1 : 0)] ^=
            std::byte{1};
        c.read(packed, fmt(24, 24), frames);
        c.deliveredAsio(type, ptrs, frames);
        CHECK(c.result().mismatchedFrames == 1);
    }
}

TEST_CASE("Self-check: frames not delivered or not read count as mismatched", "[output][selfcheck]")
{
    DeliveryCheck c;
    const auto f = fmt(16, 16);
    const auto data = pattern(100 * f.bytesPerFrame(), 5);
    c.read(data, f, 100);
    c.deliveredInterleaved(data, f, 60);
    CHECK(c.result().matchedFrames == 60);
    CHECK(c.result().mismatchedFrames == 40);

    c.reset();
    c.deliveredInterleaved(data, f, 100); // nothing was read for this period
    CHECK(c.result().matchedFrames == 0);
    CHECK(c.result().mismatchedFrames == 100);

    c.reset();
    c.read(data, f, 100);
    c.deliveredInterleaved(data, fmt(16, 16, 1), 100); // another channel layout
    CHECK(c.result().mismatchedFrames == 100);
}

TEST_CASE("Self-check through the player: dummy output, every frame matches", "[output][selfcheck][player]")
{
    ap::test::TempDir dir;
    const auto f = fmt(24, 24);
    const auto data = pattern(5000 * f.bytesPerFrame(), 6);
    for (bool repack : {false, true})
    {
        CAPTURE(repack);
        Player player([out = dir.path() / "o.raw", repack] {
            return std::make_unique<DummyOutput>(DummyOutput::Options{
                .path = out, .periodFrames = {480, 333}, .accepts = [repack](const PcmFormat& x) {
                    return !repack || x.containerBits == 32;
                }});
        });
        player.setQueue({QueueItem{"t", [f, data] { return std::make_unique<MemoryPcmSource>(f, data); }}});
        player.play();
        player.waitUntilIdle();
        const auto s = player.status();
        REQUIRE(s.state == PlayerState::Ended);
        REQUIRE(s.delivery.has_value());
        CHECK(s.delivery->matchedFrames == 5000);
        CHECK(s.delivery->mismatchedFrames == 0);
        REQUIRE(s.bitPerfect.has_value());
        CHECK(s.bitPerfect->bitPerfect);
        CHECK(s.bitPerfect->delivery.has_value());
    }
}

TEST_CASE("Self-check through the player: fake ASIO driver, every frame matches", "[output][selfcheck][asio]")
{
    for (long type : {static_cast<long>(asio::Int24LSB), static_cast<long>(asio::Int32LSB)})
    {
        CAPTURE(type);
        auto* fake = new FakeAsioDriver({.sampleType = type});
        const auto f = fmt(24, 24);
        const auto data = pattern(4800 * f.bytesPerFrame(), 7);
        {
            Player player([fake] {
                return std::make_unique<AsioOutput>("Fake ASIO", [fake]() -> IASIO* {
                    fake->AddRef();
                    return fake;
                });
            });
            player.setQueue({QueueItem{"t", [f, data] { return std::make_unique<MemoryPcmSource>(f, data); }}});
            player.play();
            player.waitUntilIdle();
            const auto s = player.status();
            REQUIRE(s.state == PlayerState::Ended);
            REQUIRE(s.delivery.has_value());
            CHECK(s.delivery->matchedFrames == 4800);
            CHECK(s.delivery->mismatchedFrames == 0);
        }
        fake->Release();
    }
}

TEST_CASE("Self-check: the processing path is not checked", "[output][selfcheck][player]")
{
    ap::test::TempDir dir;
    const auto f = PcmFormat{48000, 2, 32, 32, SampleType::Float, 0};
    std::vector<float> v(4000, 0.25f);
    std::vector<std::byte> b(v.size() * 4);
    std::memcpy(b.data(), v.data(), b.size());
    // An integer-only output: the float source is quantized on the processing path.
    Player player([out = dir.path() / "o.raw"] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{
            .path = out, .accepts = [](const PcmFormat& x) { return x.type == SampleType::Int; }});
    });
    player.setQueue({QueueItem{"t", [f, b] { return std::make_unique<MemoryPcmSource>(f, b); }}});
    player.play();
    player.waitUntilIdle();
    const auto s = player.status();
    REQUIRE(s.plan.has_value());
    CHECK(s.plan->processing);
    CHECK_FALSE(s.delivery.has_value());
}

TEST_CASE("BIT-PERFECT is taken away when the self-check finds a difference", "[path][bitperfect][selfcheck]")
{
    SignalPath path;
    path.source = fmt(16, 16);
    path.output.api = OutputApi::WasapiExclusive;
    path.output.format = path.source;

    const auto ok = judgeBitPerfect(path, {}, DeliveryResult{48000, 0});
    CHECK(ok.bitPerfect);
    CHECK_FALSE(ok.deliveryMismatch);

    const auto bad = judgeBitPerfect(path, {}, DeliveryResult{47999, 1});
    CHECK_FALSE(bad.bitPerfect);
    CHECK(bad.deliveryMismatch);
    REQUIRE(bad.delivery.has_value());
    CHECK(bad.delivery->mismatchedFrames == 1);
}
