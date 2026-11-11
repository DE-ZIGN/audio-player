// Task 4-4: the meters on what goes to the output. The playback thread only
// copies the bytes it hands the output; the meter's own thread measures them.
// Through the player, the meter must read what the whole-file analysis
// (tasks 3-3, 3-4) reads for the same values, keep each track's values to
// that track (gapless boundaries included), and go on with a track when the
// output is reopened in the middle of it.

#include <ap/engine/dsp/loudness.h>
#include <ap/engine/dsp/volume.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/output/output_meter.h>
#include <ap/engine/player.h>

#include "temp_dir.h"
#include "trigger_source.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

using namespace ap::engine;
using Catch::Matchers::WithinAbs;

namespace {

constexpr std::uint32_t kRate = 48000;

PcmFormat float32(std::uint32_t rate = kRate, std::uint16_t channels = 2)
{
    return PcmFormat{rate, channels, 32, 32, SampleType::Float, 0};
}

PcmFormat int16()
{
    return PcmFormat{kRate, 2, 16, 16, SampleType::Int, 0};
}

// A 1 kHz stereo tone at `dbfs`, as float32 bytes.
std::vector<std::byte> toneFloat(double seconds, double dbfs, std::uint32_t rate = kRate, std::uint16_t channels = 2)
{
    const auto frames = static_cast<std::size_t>(seconds * rate);
    const double a = std::pow(10.0, dbfs / 20.0);
    std::vector<float> v(frames * channels);
    for (std::size_t i = 0; i < frames; ++i)
        for (std::uint16_t c = 0; c < channels; ++c)
            v[i * channels + c] = static_cast<float>(a * std::sin(2 * std::numbers::pi * 1000.0 * i / rate));
    std::vector<std::byte> b(v.size() * 4);
    std::memcpy(b.data(), v.data(), b.size());
    return b;
}

// The ISP pattern (+, +, -, -) at full scale in 16 bits, with a 1 kHz tone
// under it for loudness: inter-sample peaks well above the sample peak.
std::vector<std::byte> ispInt16(std::size_t frames)
{
    std::vector<std::int16_t> v(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double isp = (i % 4 < 2) ? 0.7 : -0.7;
        const double tone = 0.25 * std::sin(2 * std::numbers::pi * 1000.0 * i / kRate);
        v[i * 2] = v[i * 2 + 1] = static_cast<std::int16_t>(std::lround((isp + tone) * 32767.0));
    }
    std::vector<std::byte> b(v.size() * 2);
    std::memcpy(b.data(), v.data(), b.size());
    return b;
}

TrackAnalysis analyze(const PcmFormat& f, const std::vector<std::byte>& data)
{
    MemoryPcmSource src(f, data);
    return analyzeTrack(src);
}

// Hands `data` to the meter in periods of `period` frames, as an output session would.
void deliverAll(OutputMeter& m, const PcmFormat& f, const std::vector<std::byte>& data, std::size_t period,
                bool flushEach = true)
{
    const std::size_t bpf = f.bytesPerFrame();
    for (std::size_t off = 0; off < data.size(); off += period * bpf)
    {
        const std::size_t n = std::min(period * bpf, data.size() - off);
        m.delivered(std::span(data).subspan(off, n), n / bpf);
        if (flushEach)
            m.flush();
    }
}

Player::OutputFactory dummy(const std::filesystem::path& p,
                            std::function<bool(const PcmFormat&)> accepts = {})
{
    return [p, accepts] {
        return std::make_unique<DummyOutput>(
            DummyOutput::Options{.path = p, .periodFrames = {480, 333}, .accepts = accepts});
    };
}

// The dummy output is not paced by a device and runs far faster than real
// time: the meter waits for its thread instead of skipping data.
PlaybackOptions waitingMeter(PlaybackOptions o = {})
{
    o.meter = std::make_shared<OutputMeter>(true);
    return o;
}

QueueItem item(const PcmFormat& f, std::vector<std::byte> data)
{
    return QueueItem{"t", [f, data] { return std::make_unique<MemoryPcmSource>(f, data); }};
}

} // namespace

TEST_CASE("Meter: a track that starts within a period starts its values at that frame", "[meter][output]")
{
    const auto f = float32();
    const auto a = toneFloat(3.0, -20.0);
    const auto b = toneFloat(4.0, -30.0);
    std::vector<std::byte> both = a;
    both.insert(both.end(), b.begin(), b.end());

    OutputMeter m;
    m.beginSession(f, true, 0);
    m.trackStarts(0, 0);
    m.trackStarts(3 * kRate, 1); // known ahead of time, as on the processing path
    deliverAll(m, f, both, 441); // the boundary falls inside a period
    m.endSession();

    const auto s = m.status();
    CHECK(s.state == MeterState::Idle);
    CHECK(s.queueIndex == 1);
    CHECK(s.droppedFrames == 0);
    REQUIRE(s.reading.has_value());
    CHECK(s.reading->trackFrames == 4 * kRate);
    // The second track's integrated loudness is exactly the analysis of that track alone.
    CHECK(s.reading->integratedLufs == analyze(f, b).integratedLufs);
}

TEST_CASE("Meter: data the meter thread cannot keep up with is counted, not waited for", "[meter][output]")
{
    // 30 s of 8-channel 384 kHz handed over at once: far more than the ring
    // (1 s) and faster than the interpolator runs.
    const auto f = PcmFormat{384000, 8, 32, 32, SampleType::Int, 0};
    const std::size_t total = 2800 * 4096; // about 30 s
    std::vector<std::byte> period(4096 * f.bytesPerFrame(), std::byte{0x11});

    OutputMeter m;
    m.beginSession(f, true, 0);
    m.trackStarts(0, 0);
    for (std::size_t done = 0; done < total; done += 4096)
        m.delivered(period, 4096);
    m.endSession();

    const auto s = m.status();
    REQUIRE(s.reading.has_value());
    CHECK(s.droppedFrames > 0);
    CHECK(s.reading->trackFrames + s.droppedFrames == total); // every frame measured or counted
}

TEST_CASE("Meter: DoP is not measured", "[meter][output]")
{
    OutputMeter m;
    const auto f = PcmFormat{176400, 2, 24, 24, SampleType::Int, 0};
    m.beginSession(f, false, 0);
    CHECK(m.status().state == MeterState::NotPcm);
    m.trackStarts(0, 0);
    std::vector<std::byte> data(1000 * f.bytesPerFrame(), std::byte{0x05});
    m.delivered(data, 1000);
    m.flush();
    CHECK_FALSE(m.status().reading.has_value());
    m.endSession();
    CHECK(m.status().state == MeterState::Idle);
}

TEST_CASE("Meter: a session that resumes a track goes on with it; one from the start begins again", "[meter][output]")
{
    const auto f = float32();
    const auto two = toneFloat(2.0, -20.0);
    OutputMeter m;

    m.beginSession(f, true, 0);
    m.trackStarts(0, 0);
    deliverAll(m, f, two, 480);
    m.endSession();

    m.beginSession(f, true, 2 * kRate); // resumed where it stopped
    m.trackStarts(0, 0);
    deliverAll(m, f, two, 480);
    m.endSession();
    CHECK(m.status().reading->trackFrames == 4 * kRate);

    m.beginSession(f, true, 0); // the same track again from its beginning
    m.trackStarts(0, 0);
    deliverAll(m, f, toneFloat(1.0, -20.0), 480);
    m.endSession();
    CHECK(m.status().reading->trackFrames == kRate);
}

TEST_CASE("Meter through the player: the direct path reads what the analysis reads", "[meter][player]")
{
    ap::test::TempDir dir;
    const auto data = ispInt16(3 * kRate);
    const auto expected = analyze(int16(), data);
    REQUIRE(expected.peaks.truePeakMax > expected.peaks.samplePeakMax * 1.2); // real inter-sample peaks

    Player player(dummy(dir.path() / "o.raw"), waitingMeter());
    player.setQueue({item(int16(), data)});
    player.play();
    player.waitUntilIdle();
    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.path.has_value());
    CHECK_FALSE(s.path->processing);
    CHECK(s.meter.state == MeterState::Idle);
    CHECK(s.meter.droppedFrames == 0);
    REQUIRE(s.meter.reading.has_value());
    const auto& r = *s.meter.reading;
    CHECK(r.trackFrames == 3 * kRate);
    for (std::uint16_t c = 0; c < 2; ++c)
        CHECK(r.truePeakMax[c] == expected.peaks.truePeak[c]);
    CHECK(r.integratedLufs == expected.integratedLufs);
}

TEST_CASE("Meter through the player: the processing path is measured after processing", "[meter][player]")
{
    ap::test::TempDir dir;
    const auto data = ispInt16(3 * kRate);
    const auto expected = analyze(int16(), data);

    auto vol = std::make_shared<VolumeStage>();
    vol->setGainDb(-6.0);
    PlaybackOptions o;
    o.processing = std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{vol});
    Player player(dummy(dir.path() / "o.raw"), waitingMeter(o));
    player.setQueue({item(int16(), data)});
    player.play();
    player.waitUntilIdle();
    const auto s = player.status();
    REQUIRE(s.path.has_value());
    REQUIRE(s.path->processing);
    REQUIRE(s.meter.reading.has_value());
    const auto& r = *s.meter.reading;
    // 32-bit output with TPDF dither: within a hair of exactly 6 dB lower.
    CHECK_THAT(20 * std::log10(r.truePeakMax[0] / expected.peaks.truePeak[0]), WithinAbs(-6.0, 0.001));
    CHECK_THAT(r.integratedLufs - expected.integratedLufs, WithinAbs(-6.0, 0.001));
}

TEST_CASE("Meter through the player: gapless tracks each get their own values", "[meter][player]")
{
    ap::test::TempDir dir;
    const auto f = float32();
    const auto a = toneFloat(2.0, -12.0);
    const auto b = toneFloat(3.3, -30.0); // not a whole number of periods
    Player player(dummy(dir.path() / "o.raw"), waitingMeter());
    player.setQueue({item(f, a), item(f, b)});
    player.play();
    player.waitUntilIdle();
    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.transitions.size() == 1);
    REQUIRE(s.transitions[0].kind == TransitionKind::Gapless);
    REQUIRE(s.meter.reading.has_value());
    CHECK(s.meter.queueIndex == 1);
    CHECK(s.meter.reading->trackFrames == b.size() / f.bytesPerFrame());
    CHECK(s.meter.reading->integratedLufs == analyze(f, b).integratedLufs);
    CHECK_THAT(20 * std::log10(s.meter.reading->truePeakMax[0]), WithinAbs(-30.0, 0.05));
}

TEST_CASE("Meter through the player: reopening mid-track for processing goes on with the track", "[meter][player]")
{
    ap::test::TempDir dir;
    const auto data = ispInt16(2 * kRate);
    auto vol = std::make_shared<VolumeStage>();
    PlaybackOptions o;
    o.processing = std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{vol});
    Player player(dummy(dir.path() / "o.raw"), waitingMeter(o));
    player.setQueue({QueueItem{"t", [data, vol] {
        return std::make_unique<ap::test::TriggerSource>(
            std::make_unique<MemoryPcmSource>(int16(), data),
            ap::test::TriggerSource::Triggers{{kRate / 2, [vol] { vol->setGainDb(-6.0); }}});
    }}});
    player.play();
    player.waitUntilIdle();
    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.transitions.size() == 1);
    REQUIRE(s.transitions[0].kind == TransitionKind::ProcessingChanged);
    REQUIRE(s.meter.reading.has_value());
    CHECK(s.meter.reading->trackFrames == 2 * kRate); // both sessions, one track
}

TEST_CASE("Meter through the player: a new queue forgets the values", "[meter][player]")
{
    ap::test::TempDir dir;
    Player player(dummy(dir.path() / "o.raw"), waitingMeter());
    player.setQueue({item(float32(), toneFloat(0.5, -20.0))});
    player.play();
    player.waitUntilIdle();
    REQUIRE(player.status().meter.reading.has_value());
    player.setQueue({item(float32(), toneFloat(0.5, -20.0))});
    CHECK_FALSE(player.status().meter.reading.has_value());
    CHECK(player.status().meter.state == MeterState::Idle);
}
