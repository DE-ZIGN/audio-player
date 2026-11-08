// Task 1-4: ASIO output, tested against a fake driver ("ダミーの ASIO 環境").
// Checks that every sample value reaches the driver unchanged in its sample
// type, that the host sets the rate per track, that unsupported formats are
// refused with a reason, and that dropouts and driver resets are reported.

#include <ap/engine/output/asio_output.h>
#include <ap/engine/output/asio_sample_types.h>
#include <ap/engine/player.h>

#include "fake_asio_driver.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace ap::engine;
using namespace std::chrono_literals;
using ap::test::FakeAsioDriver;

namespace {

std::vector<std::byte> pattern24(std::size_t frames, std::uint32_t seed, std::uint16_t channels = 2)
{
    std::vector<std::byte> v(frames * channels * 3);
    std::uint32_t x = seed;
    for (auto& b : v)
    {
        x = x * 1664525u + 1013904223u;
        b = static_cast<std::byte>((x >> 24) | 1u);
    }
    return v;
}

std::vector<std::byte> pattern16(std::size_t frames, std::uint32_t seed)
{
    auto v = pattern24(frames, seed);
    v.resize(frames * 2 * 2);
    return v;
}

PcmFormat fmt(std::uint32_t rate, std::uint16_t container, std::uint16_t valid, std::uint16_t channels = 2)
{
    return PcmFormat{rate, channels, container, valid, SampleType::Int, 0};
}

// One channel of interleaved data.
std::vector<std::byte> channelOf(const std::vector<std::byte>& interleaved, std::size_t bps, std::size_t channels,
                                 std::size_t c)
{
    std::vector<std::byte> out;
    for (std::size_t i = c * bps; i + bps <= interleaved.size(); i += bps * channels)
        out.insert(out.end(), interleaved.begin() + static_cast<std::ptrdiff_t>(i),
                   interleaved.begin() + static_cast<std::ptrdiff_t>(i + bps));
    return out;
}

bool allZero(const std::vector<std::byte>& v, std::size_t from)
{
    for (std::size_t i = from; i < v.size(); ++i)
        if (v[i] != std::byte{0})
            return false;
    return true;
}

// Player whose output is an AsioOutput on `fake` (one reference is shared with the test).
Player::OutputFactory asioOn(FakeAsioDriver* fake)
{
    return [fake] {
        return std::make_unique<AsioOutput>("Fake ASIO", [fake]() -> IASIO* {
            fake->AddRef();
            return fake;
        });
    };
}

QueueItem track(const std::string& name, PcmFormat f, std::vector<std::byte> data)
{
    return {name, [f, data] { return std::make_unique<MemoryPcmSource>(f, data); }};
}

struct FakeHolder
{
    FakeAsioDriver* fake;
    explicit FakeHolder(FakeAsioDriver::Config c) : fake(new FakeAsioDriver(std::move(c))) {}
    ~FakeHolder() { fake->Release(); }
};

} // namespace

TEST_CASE("ASIO Int24LSB: packed 24-bit reaches each channel byte for byte", "[engine][asio]")
{
    FakeHolder h({.sampleType = asio::Int24LSB});
    const auto data = pattern24(4800, 1);
    Player player(asioOn(h.fake));
    player.setQueue({track("a", fmt(48000, 24, 24), data)});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.plan);
    CHECK(s.plan->repack == Repack::None);

    const auto cap = h.fake->captured();
    for (std::size_t c = 0; c < 2; ++c)
    {
        const auto expected = channelOf(data, 3, 2, c);
        REQUIRE(cap[c].size() >= expected.size());
        CHECK(std::memcmp(cap[c].data(), expected.data(), expected.size()) == 0);
        CHECK(allZero(cap[c], expected.size())); // then silence while draining
    }
}

TEST_CASE("ASIO Int32LSB: 24-bit goes into 32-bit slots with a zero low byte", "[engine][asio]")
{
    FakeHolder h({.sampleType = asio::Int32LSB});
    const auto data = pattern24(4800, 2);
    Player player(asioOn(h.fake));
    player.setQueue({track("a", fmt(48000, 24, 24), data)});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    CHECK(s.plan->repack == Repack::WiderContainer);
    CHECK(s.plan->output.containerBits == 32);

    const auto cap = h.fake->captured();
    for (std::size_t c = 0; c < 2; ++c)
    {
        const auto expected = channelOf(data, 3, 2, c);
        std::vector<std::byte> unpacked;
        for (std::size_t i = 0; i < expected.size() / 3; ++i)
        {
            CHECK(cap[c][i * 4] == std::byte{0});
            unpacked.insert(unpacked.end(), cap[c].begin() + static_cast<std::ptrdiff_t>(i * 4 + 1),
                            cap[c].begin() + static_cast<std::ptrdiff_t>(i * 4 + 4));
        }
        CHECK(unpacked == expected);
    }
}

TEST_CASE("ASIO Int16LSB: 16-bit unchanged", "[engine][asio]")
{
    FakeHolder h({.sampleType = asio::Int16LSB});
    const auto data = pattern16(4800, 3);
    Player player(asioOn(h.fake));
    player.setQueue({track("a", fmt(44100, 16, 16), data)});
    player.play();
    player.waitUntilIdle();
    REQUIRE(player.status().state == PlayerState::Ended);

    const auto cap = h.fake->captured();
    const auto expected = channelOf(data, 2, 2, 1);
    CHECK(std::memcmp(cap[1].data(), expected.data(), expected.size()) == 0);
}

TEST_CASE("ASIO float driver: integer files are refused, not converted", "[engine][asio]")
{
    FakeHolder h({.sampleType = asio::Float32LSB});
    Player player(asioOn(h.fake));
    player.setQueue({track("a", fmt(48000, 24, 24), pattern24(480, 4))});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Error);
    CHECK(s.error.kind == PlayerErrorKind::FormatNotSupported);
    CHECK(s.error.formatCheck.reason == FormatRejection::SampleType);
    CHECK(h.fake->createBuffersCalls() == 0);
}

TEST_CASE("ASIO sets the driver to each track's rate", "[engine][asio][rate]")
{
    FakeHolder h({.sampleType = asio::Int32LSB, .rates = {44100, 48000, 96000}, .initialRate = 48000});
    Player player(asioOn(h.fake));
    player.setQueue({track("a", fmt(44100, 24, 24), pattern24(2000, 5)), track("b", fmt(96000, 24, 24), pattern24(2000, 6))});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(h.fake->rateHistory() == std::vector<double>{44100, 96000});
    REQUIRE(s.transitions.size() == 1);
    CHECK(s.transitions[0].kind == TransitionKind::Reopened);
}

TEST_CASE("ASIO: a rate the driver cannot run at is refused with the accepted rates", "[engine][asio][rate]")
{
    FakeHolder h({.sampleType = asio::Int32LSB, .rates = {44100, 48000}, .initialRate = 48000});
    Player player(asioOn(h.fake));
    player.setQueue({track("a", fmt(96000, 24, 24), pattern24(480, 7))});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Error);
    CHECK(s.error.formatCheck.reason == FormatRejection::SampleRate);
    CHECK(s.error.formatCheck.acceptedRates == std::vector<std::uint32_t>{44100, 48000});
    CHECK(h.fake->rateHistory().empty());
}

TEST_CASE("ASIO: same-format tracks play gaplessly in one session", "[engine][asio][gapless]")
{
    FakeHolder h({.sampleType = asio::Int24LSB});
    const auto a = pattern24(1500, 8);
    const auto b = pattern24(2100, 9);
    Player player(asioOn(h.fake));
    player.setQueue({track("a", fmt(48000, 24, 24), a), track("b", fmt(48000, 24, 24), b)});
    player.play();
    player.waitUntilIdle();

    REQUIRE(player.status().state == PlayerState::Ended);
    CHECK(h.fake->createBuffersCalls() == 1);
    std::vector<std::byte> ab = a;
    ab.insert(ab.end(), b.begin(), b.end());
    const auto cap = h.fake->captured();
    const auto expected = channelOf(ab, 3, 2, 0);
    CHECK(std::memcmp(cap[0].data(), expected.data(), expected.size()) == 0);
}

TEST_CASE("ASIO: a slow buffer switch is recorded as a late buffer", "[engine][asio][dropout]")
{
    FakeHolder h({.sampleType = asio::Int24LSB, .bufferSize = 480, .realtime = true});
    // Read on the driver's callback thread stalls once for 60 ms (6 buffer periods).
    class Stall final : public PcmSource
    {
    public:
        explicit Stall(std::vector<std::byte> d) : inner_(fmt(48000, 24, 24), std::move(d)) {}
        const PcmFormat& format() const noexcept override { return inner_.format(); }
        std::uint64_t totalFrames() const noexcept override { return inner_.totalFrames(); }
        std::uint64_t position() const noexcept override { return inner_.position(); }
        bool seek(std::uint64_t f) override { return inner_.seek(f); }
        std::size_t read(std::span<std::byte> dst) override
        {
            if (!done_ && inner_.position() > 4800)
            {
                done_ = true;
                std::this_thread::sleep_for(60ms);
            }
            return inner_.read(dst);
        }

    private:
        MemoryPcmSource inner_;
        bool done_ = false;
    };
    const auto data = pattern24(14400, 10); // 0.3 s
    Player player(asioOn(h.fake));
    player.setQueue({{"a", [data] { return std::make_unique<Stall>(data); }}});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(s.dropouts.lateBuffers >= 1u);
}

TEST_CASE("ASIO: an overload reported by the driver is counted", "[engine][asio][dropout]")
{
    FakeHolder h({.sampleType = asio::Int24LSB, .realtime = true});
    Player player(asioOn(h.fake));
    player.setQueue({track("a", fmt(48000, 24, 24), pattern24(9600, 11))});
    player.play();
    std::this_thread::sleep_for(50ms);
    h.fake->reportOverload();
    player.waitUntilIdle();
    CHECK(player.status().dropouts.lateBuffers >= 1u);
}

TEST_CASE("ASIO: a reset request from the driver stops playback with a reason", "[engine][asio]")
{
    FakeHolder h({.sampleType = asio::Int24LSB, .realtime = true});
    Player player(asioOn(h.fake));
    player.setQueue({track("a", fmt(48000, 24, 24), pattern24(48000, 12))}); // 1 s
    player.play();
    std::this_thread::sleep_for(100ms);
    h.fake->requestReset();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Error);
    CHECK(s.error.kind == PlayerErrorKind::Output);
    CHECK(s.error.outputError == OutputError::DeviceInvalidated);
    CHECK_FALSE(h.fake->running());
}

TEST_CASE("ASIO: stop ends playback and keeps the position", "[engine][asio]")
{
    FakeHolder h({.sampleType = asio::Int24LSB, .realtime = true});
    Player player(asioOn(h.fake));
    player.setQueue({track("a", fmt(48000, 24, 24), pattern24(48000, 13))});
    player.play();
    std::this_thread::sleep_for(150ms);
    player.stop();
    const auto s = player.status();
    CHECK(s.state == PlayerState::Stopped);
    CHECK(s.positionFrames > 0u);
    CHECK(s.positionFrames < 48000u);
    CHECK_FALSE(h.fake->running());
}

TEST_CASE("ASIO driver details are read at load", "[engine][asio]")
{
    FakeHolder h({.outputs = 8, .sampleType = asio::Int32LSB, .initialRate = 44100, .bufferSize = 256});
    AsioOutput out("Fake ASIO", [f = h.fake]() -> IASIO* {
        f->AddRef();
        return f;
    });
    const auto d = out.details();
    CHECK(d.driverName == "Fake ASIO");
    CHECK(d.outputChannels == 8);
    CHECK(d.sampleType == asio::Int32LSB);
    CHECK(d.preferredBufferSize == 256);
    CHECK(d.currentSampleRate == 44100.0);
    CHECK(out.checkFormat(fmt(44100, 32, 24)).supported);
    CHECK(out.checkFormat(fmt(44100, 24, 24)).reason == FormatRejection::BitDepth); // repack needed
}
