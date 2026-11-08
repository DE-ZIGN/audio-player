// Task 1-6: the playback path is recorded from facts (what each stage did),
// so the path display (M4) can show it and judge bit-perfect from it.

#include <ap/engine/audio_file.h>
#include <ap/engine/output/asio_output.h>
#include <ap/engine/output/asio_sample_types.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/output/wasapi_exclusive_output.h>
#include <ap/engine/player.h>
#include <ap/engine/prefetching_source.h>
#include <ap/signalgen/dsd.h>
#include <ap/signalgen/signals.h>
#include <ap/signalgen/wav_writer.h>

#include "fake_asio_driver.h"
#include "temp_dir.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ap::engine;
namespace sg = ap::signalgen;
using ap::test::FakeAsioDriver;

namespace {

PcmFormat fmt(std::uint32_t rate, std::uint16_t container, std::uint16_t valid)
{
    return PcmFormat{rate, 2, container, valid, SampleType::Int, 0};
}

// Same layout, ignoring the speaker mask (files may carry one).
bool same(const PcmFormat& a, const PcmFormat& b)
{
    return a.sampleRate == b.sampleRate && a.channels == b.channels && a.containerBits == b.containerBits
        && a.validBits == b.validBits && a.type == b.type;
}

std::vector<std::byte> zeros(const PcmFormat& f, std::size_t frames)
{
    return std::vector<std::byte>(frames * f.bytesPerFrame(), std::byte{0});
}

std::filesystem::path writeWav24(const std::filesystem::path& dir, std::uint32_t rate)
{
    const sg::Layout layout{rate, 2, rate / 10};
    const auto path = dir / "tone.wav";
    sg::writeWav(path, sg::quantize(sg::sine(layout, 1000.0, -40.0), rate, 2, sg::SampleFormat::Int24));
    return path;
}

std::filesystem::path writeDsf(const std::filesystem::path& dir)
{
    const auto channels = sg::dsdSine(2822400, 2, 4096, 1000.0, -20.0);
    const auto path = dir / "tone.dsf";
    sg::writeBytes(path, sg::encodeDsf(channels, 2822400));
    return path;
}

QueueItem fileTrack(const std::filesystem::path& path)
{
    return {path.filename().string(), [path] { return std::make_unique<PrefetchingSource>(openAudioFile(path)); }};
}

Player::OutputFactory dummyOn(const std::filesystem::path& out, std::function<bool(const PcmFormat&)> accepts = {})
{
    return [out, accepts] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out, .periodFrames = {480}, .accepts = accepts});
    };
}

struct FakeHolder
{
    FakeAsioDriver* fake;
    explicit FakeHolder(FakeAsioDriver::Config c) : fake(new FakeAsioDriver(std::move(c))) {}
    ~FakeHolder() { fake->Release(); }
};

Player::OutputFactory asioOn(FakeAsioDriver* fake)
{
    return [fake] {
        return std::make_unique<AsioOutput>("ASIO: Fake", [fake]() -> IASIO* {
            fake->AddRef();
            return fake;
        });
    };
}

} // namespace

TEST_CASE("Dummy output reports its path only while open", "[engine][path]")
{
    ap::test::TempDir dir;
    DummyOutput out({.path = dir.path() / "out.raw", .periodFrames = {480}});
    CHECK_FALSE(out.pathInfo().has_value());

    const auto f = fmt(48000, 24, 24);
    out.open(f);
    const auto p = out.pathInfo();
    REQUIRE(p);
    CHECK(p->api == OutputApi::Dummy);
    CHECK(p->deviceName == out.deviceName());
    CHECK(p->format == f);
    CHECK(p->periodFrames == 480u);
    CHECK(p->periodMilliseconds == Catch::Approx(10.0));
    CHECK(p->layout == SampleLayout::Interleaved);
    CHECK_FALSE(p->asio.has_value());
    CHECK_FALSE(p->endpointVolume.has_value());

    out.close();
    CHECK_FALSE(out.pathInfo().has_value());
}

TEST_CASE("WASAPI path info is built from what the device reported", "[engine][path]")
{
    const auto f = fmt(96000, 32, 24);
    const EndpointVolumeInfo volume{.available = true, .scalar = 0.5f, .muted = false, .hardwareVolume = true};
    const auto p = makeWasapiPathInfo("SSL 2 MKII", f, 960, volume);
    CHECK(p.api == OutputApi::WasapiExclusive);
    CHECK(p.deviceName == "SSL 2 MKII");
    CHECK(p.format == f);
    CHECK(p.periodFrames == 960u);
    CHECK(p.periodMilliseconds == Catch::Approx(10.0));
    CHECK(p.eventDriven);
    CHECK(p.layout == SampleLayout::Interleaved);
    REQUIRE(p.endpointVolume);
    CHECK(p.endpointVolume->hardwareVolume);
    CHECK(p.endpointVolume->scalar == Catch::Approx(0.5f));

    // No volume control on the endpoint: nothing is claimed about it.
    CHECK_FALSE(makeWasapiPathInfo("x", f, 960, EndpointVolumeInfo{}).endpointVolume.has_value());
}

TEST_CASE("Path of a WAV file played unchanged", "[engine][path]")
{
    ap::test::TempDir dir;
    const auto wav = writeWav24(dir.path(), 48000);
    Player player(dummyOn(dir.path() / "out.raw"));
    player.setQueue({fileTrack(wav)});
    CHECK_FALSE(player.status().path.has_value()); // nothing has been opened yet
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.path);
    CHECK(s.path->container == "WAV");
    CHECK(same(s.path->source, fmt(48000, 24, 24)));
    CHECK_FALSE(s.path->dop.has_value());
    CHECK_FALSE(s.path->processing);
    CHECK(s.path->repack == Repack::None);
    CHECK(s.path->output.api == OutputApi::Dummy);
    CHECK(same(s.path->output.format, fmt(48000, 24, 24)));
}

TEST_CASE("Path records a container repack", "[engine][path]")
{
    ap::test::TempDir dir;
    const auto wav = writeWav24(dir.path(), 48000);
    Player player(dummyOn(dir.path() / "out.raw", [](const PcmFormat& f) { return f.containerBits == 32; }));
    player.setQueue({fileTrack(wav)});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.path);
    CHECK(same(s.path->source, fmt(48000, 24, 24)));
    CHECK(s.path->repack == Repack::WiderContainer);
    CHECK(same(s.path->output.format, fmt(48000, 32, 24)));
}

TEST_CASE("Path of a DSF file sent as DoP", "[engine][path][dop]")
{
    ap::test::TempDir dir;
    const auto dsf = writeDsf(dir.path());
    Player player(dummyOn(dir.path() / "out.raw"), PlaybackOptions{.allowDop = true});
    player.setQueue({fileTrack(dsf)});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.path);
    CHECK(s.path->container == "DSF");
    REQUIRE(s.path->dop);
    CHECK(s.path->dop->dsdRate == 2822400u);
    CHECK(same(s.path->source, fmt(176400, 24, 24)));
    CHECK_FALSE(s.path->processing);
}

TEST_CASE("ASIO path: right-aligned sample type, driver and rate change are recorded", "[engine][path][asio]")
{
    FakeHolder h({.sampleType = asio::Int32LSB24, .rates = {44100, 48000}, .initialRate = 48000, .bufferSize = 441});
    Player player(asioOn(h.fake));
    const auto f = fmt(44100, 24, 24);
    player.setQueue({{"a", [f] { return std::make_unique<MemoryPcmSource>(f, zeros(f, 4410)); }}});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.path);
    const auto& o = s.path->output;
    CHECK(o.api == OutputApi::Asio);
    CHECK(o.deviceName == "ASIO: Fake");
    CHECK(s.path->repack == Repack::None);
    CHECK(o.format == f);
    CHECK(o.layout == SampleLayout::PerChannelRightAligned); // 24 bits in the low bytes of 32, sign-extended
    CHECK(o.periodFrames == 441u);
    CHECK(o.periodMilliseconds == Catch::Approx(10.0));
    CHECK_FALSE(o.endpointVolume.has_value());
    REQUIRE(o.asio);
    CHECK(o.asio->driverName == "Fake ASIO");
    CHECK(o.asio->driverVersion == 1);
    CHECK(o.asio->sampleType == asio::Int32LSB24);
    CHECK(o.asio->sampleTypeName == "Int32LSB24");
    CHECK(o.asio->rateChanged);
    CHECK(o.asio->rateBefore == 48000.0);
}

TEST_CASE("ASIO path: same-width sample type, rate already right", "[engine][path][asio]")
{
    FakeHolder h({.sampleType = asio::Int32LSB, .initialRate = 48000});
    Player player(asioOn(h.fake));
    const auto f = fmt(48000, 24, 24);
    player.setQueue({{"a", [f] { return std::make_unique<MemoryPcmSource>(f, zeros(f, 4800)); }}});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.path);
    CHECK(s.path->repack == Repack::WiderContainer);
    CHECK(s.path->output.layout == SampleLayout::PerChannel);
    REQUIRE(s.path->output.asio);
    CHECK(s.path->output.asio->sampleTypeName == "Int32LSB");
    CHECK_FALSE(s.path->output.asio->rateChanged);
}

TEST_CASE("Path is cleared when a new queue is set", "[engine][path]")
{
    ap::test::TempDir dir;
    const auto wav = writeWav24(dir.path(), 48000);
    Player player(dummyOn(dir.path() / "out.raw"));
    player.setQueue({fileTrack(wav)});
    player.play();
    player.waitUntilIdle();
    REQUIRE(player.status().path);

    player.setQueue({fileTrack(wav)});
    CHECK_FALSE(player.status().path.has_value());
}
