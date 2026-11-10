// Task 3-5: the ISP-safe gain and the safety limiter.

#include <ap/engine/dsp/isp_safe.h>
#include <ap/engine/dsp/limiter.h>
#include <ap/engine/dsp/true_peak.h>
#include <ap/engine/dsp/volume.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/playback.h>
#include <ap/engine/player.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>

using namespace ap::engine;

namespace {

constexpr std::uint32_t kRate = 48000;

double fromDb(double db)
{
    return std::pow(10.0, db / 20.0);
}

double db(double x)
{
    return 20.0 * std::log10(x);
}

TrackInfo withPeak(double peakDb, std::optional<double> albumPeakDb = std::nullopt)
{
    ReplayGainInfo rg{0.0, fromDb(peakDb), std::nullopt, std::nullopt};
    if (albumPeakDb)
    {
        rg.albumGainDb = 0.0;
        rg.albumPeak = fromDb(*albumPeakDb);
    }
    return TrackInfo{0, rg};
}

PcmFormat float32(std::uint16_t channels = 2, std::uint32_t rate = kRate)
{
    return PcmFormat{rate, channels, 32, 32, SampleType::Float, 0};
}

std::vector<std::byte> bytesOf(const std::vector<float>& v)
{
    std::vector<std::byte> b(v.size() * 4);
    std::memcpy(b.data(), v.data(), b.size());
    return b;
}

std::vector<float> readFloats(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<float> v(raw.size() / 4);
    std::memcpy(v.data(), raw.data(), v.size() * 4);
    return v;
}

double truePeakOf(const std::vector<float>& v, std::uint16_t channels)
{
    std::vector<double> d(v.begin(), v.end());
    TruePeakMeter m(channels);
    m.process(d);
    m.finish();
    return m.truePeak();
}

// Active but changes nothing: makes play() take the processing path (the
// limiter alone never does, by design).
class Forced final : public ProcessingStage
{
public:
    std::string name() const override { return "forced"; }
    void prepare(std::uint32_t, std::uint16_t) override {}
    bool isIdentity() const noexcept override { return false; }
    void process(std::span<double>, std::uint16_t) noexcept override {}
};

// Plays float samples through `stages` (after Forced) into a float-only dummy
// output and returns what came out.
std::vector<float> playThrough(const std::vector<float>& in, std::uint16_t channels,
                               std::vector<std::shared_ptr<ProcessingStage>> stages, const TrackInfo& track,
                               std::vector<std::size_t> periods = {480})
{
    ap::test::TempDir dir;
    MemoryPcmSource src(float32(channels), bytesOf(in));
    DummyOutput out({.path = dir.path() / "o.raw",
                     .periodFrames = periods,
                     .accepts = [](const PcmFormat& f) { return f.type == SampleType::Float; }});
    PlaybackOptions options;
    stages.insert(stages.begin(), std::make_shared<Forced>());
    options.processing = std::make_shared<ProcessingChain>(std::move(stages));
    options.processing->trackStarted(track);
    play(src, out, options);
    return readFloats(dir.path() / "o.raw");
}

std::vector<float> ispSignal(std::size_t frames, double samplePeakDb)
{
    // fs/4, 45 degrees: +A, +A, -A, -A, ... (true peak = A + 3.01 dB)
    std::vector<float> v;
    const auto a = static_cast<float>(fromDb(samplePeakDb));
    for (std::size_t i = 0; i < frames; ++i)
    {
        const float x = (i % 4 < 2) ? a : -a;
        v.insert(v.end(), {x, x});
    }
    return v;
}

} // namespace

TEST_CASE("ISP-safe: off is identity", "[dsp][isp]")
{
    IspSafeStage s;
    s.trackStarted(withPeak(3.0));
    CHECK(s.isIdentity());
}

TEST_CASE("ISP-safe measured: lowers by the excess over -0.5 dBTP", "[dsp][isp]")
{
    IspSafeStage s;
    s.setMode(IspSafeMode::Measured);
    s.trackStarted(withPeak(2.96));
    CHECK(std::abs(s.gainDb() - -3.46) < 1e-9);
    s.trackStarted(withPeak(-0.2));
    CHECK(std::abs(s.gainDb() - -0.3) < 1e-9);
    s.trackStarted(withPeak(-1.0));
    CHECK(s.gainDb() == 0.0);
    s.prepare(kRate, 2);
    CHECK(s.isIdentity());
}

TEST_CASE("ISP-safe measured: earlier gains are taken into account", "[dsp][isp]")
{
    IspSafeStage s;
    s.setMode(IspSafeMode::Measured);
    s.trackStarted(withPeak(2.96));
    s.setUpstreamGain(fromDb(-6.0)); // the volume already lowered it below the target
    CHECK(s.gainDb() == 0.0);
    s.setUpstreamGain(fromDb(-2.0));
    CHECK(std::abs(s.gainDb() - -1.46) < 1e-9);
    s.setUpstreamGain(fromDb(+2.0)); // ReplayGain lifted it
    CHECK(std::abs(s.gainDb() - -5.46) < 1e-9);
}

TEST_CASE("ISP-safe measured: the album peak in album mode; fixed -3 dB without a peak", "[dsp][isp]")
{
    IspSafeStage s;
    s.setMode(IspSafeMode::Measured);
    s.setUseAlbumPeak(true);
    s.trackStarted(withPeak(-2.0, 1.0));
    CHECK(std::abs(s.gainDb() - -1.5) < 1e-9);
    s.trackStarted(TrackInfo{1, std::nullopt});
    CHECK(std::abs(s.gainDb() - -3.0) < 1e-9);
}

TEST_CASE("ISP-safe fixed: always -3 dB", "[dsp][isp]")
{
    IspSafeStage s;
    s.setMode(IspSafeMode::Fixed);
    s.trackStarted(withPeak(-20.0));
    CHECK(std::abs(s.gainDb() - -3.0) < 1e-9);
    CHECK_FALSE(s.isIdentity());
}

TEST_CASE("ISP-safe in the chain: the volume before it reduces its reduction", "[dsp][isp]")
{
    auto volume = std::make_shared<VolumeStage>();
    auto isp = std::make_shared<IspSafeStage>();
    isp->setMode(IspSafeMode::Measured);
    ProcessingChain chain({volume, isp});
    chain.trackStarted(withPeak(1.5));
    CHECK_FALSE(chain.bypassed());
    CHECK(std::abs(isp->gainDb() - -2.0) < 1e-9);
    volume->setGainDb(-3.0);
    CHECK(chain.bypassed() == false); // the volume itself is active
    CHECK(isp->gainDb() == 0.0);      // 1.5 - 3 = -1.5 dBTP: nothing left to do
}

TEST_CASE("Limiter: below the ceiling the samples pass exactly, length and timing kept", "[dsp][limiter]")
{
    const auto periods = GENERATE(std::vector<std::size_t>{480}, std::vector<std::size_t>{7, 4096, 100});
    std::vector<float> in(20011 * 2);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float>(0.5 * std::sin(0.013 * static_cast<double>(i)));
    auto limiter = std::make_shared<SafetyLimiter>();
    const auto out = playThrough(in, 2, {limiter}, TrackInfo{}, periods);
    REQUIRE(out.size() == in.size());
    CHECK(std::memcmp(out.data(), in.data(), in.size() * 4) == 0);
    CHECK(limiter->stats().events == 0);
}

TEST_CASE("Limiter: keeps the true peak at the ceiling", "[dsp][limiter]")
{
    const auto signal = GENERATE(std::string("isp"), std::string("square"), std::string("impulses"));
    CAPTURE(signal);
    std::vector<float> in;
    if (signal == "isp")
        in = ispSignal(48000, 0.0); // +3.01 dBTP
    else if (signal == "square")
    {
        // Full-scale jumps between neighbours, as in a real track (+2.96 dBTP).
        for (std::size_t i = 0; i < 48000; ++i)
        {
            const float x = (i / 3) % 2 ? 0.985f : -0.985f;
            in.insert(in.end(), {x, x * 0.9f});
        }
    }
    else
    {
        in.assign(48000 * 2, 0.0f);
        for (std::size_t i = 1000; i < 48000; i += 4000)
        {
            in[i * 2] = 1.0f;
            in[i * 2 + 2] = 1.0f; // two full-scale samples in a row overshoot between them
        }
    }
    auto limiter = std::make_shared<SafetyLimiter>();
    const auto out = playThrough(in, 2, {limiter}, TrackInfo{});
    REQUIRE(out.size() == in.size());
    const double tp = db(truePeakOf(out, 2));
    CAPTURE(db(truePeakOf(in, 2)), tp);
    CHECK(tp <= SafetyLimiter::kCeilingDbtp + 0.05);
    const auto st = limiter->stats();
    CHECK(st.events >= 1);
    CHECK(st.maxReductionDb < 0.0);
}

TEST_CASE("Limiter: counts each time it acts", "[dsp][limiter]")
{
    std::vector<float> in(96000 * 2, 0.0f);
    for (const std::size_t at : {10000u, 40000u, 70000u})
        for (std::size_t i = at; i < at + 2000; ++i)
        {
            const float x = (i % 4 < 2) ? 1.0f : -1.0f;
            in[i * 2] = in[i * 2 + 1] = x;
        }
    auto limiter = std::make_shared<SafetyLimiter>();
    playThrough(in, 2, {limiter}, TrackInfo{});
    CHECK(limiter->stats().events == 3);
    CHECK(limiter->stats().limitedFrames > 6000);
}

TEST_CASE("Limiter: never calls for processing by itself", "[dsp][limiter]")
{
    ProcessingChain chain({std::make_shared<SafetyLimiter>()});
    CHECK(chain.bypassed());
    CHECK(chain.activeStageNames() == std::vector<std::string>{"limiter"});
}

TEST_CASE("ISP-safe fixed -3 dB leaves +0.01 dBTP of the ISP signal for the limiter", "[dsp][isp][limiter]")
{
    auto isp = std::make_shared<IspSafeStage>();
    isp->setMode(IspSafeMode::Fixed);
    auto limiter = std::make_shared<SafetyLimiter>();
    const auto out = playThrough(ispSignal(48000, 0.0), 2, {isp, limiter}, TrackInfo{});
    CHECK(db(truePeakOf(out, 2)) <= SafetyLimiter::kCeilingDbtp + 0.05);
    CHECK(limiter->stats().events >= 1);
}

TEST_CASE("Player: a direct track over 0 dBTP reports its true peak (ISP warning)", "[dsp][isp][player]")
{
    ap::test::TempDir dir;
    auto isp = std::make_shared<IspSafeStage>();
    PlaybackOptions options;
    options.processing = std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{isp});
    Player player([out = dir.path() / "o.raw"] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out, .periodFrames = {480}});
    }, options);
    const auto in = ispSignal(4800, 0.0);
    QueueItem item{"isp", [in] { return std::make_unique<MemoryPcmSource>(float32(), bytesOf(in)); },
                   ReplayGainInfo{0.0, fromDb(3.1), std::nullopt, std::nullopt}};
    player.setQueue({item});
    REQUIRE(player.status().trackTruePeak.has_value());
    CHECK(std::abs(db(*player.status().trackTruePeak) - 3.1) < 1e-9);
    player.play();
    player.waitUntilIdle();
    REQUIRE(player.status().path.has_value());
    CHECK_FALSE(player.status().path->processing); // ISP-safe is off: bit-perfect, with the warning to show
}
