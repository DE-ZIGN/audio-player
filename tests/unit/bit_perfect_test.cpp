// Task 4-2: the BIT-PERFECT judgement in each mode (各モードで表示が正しい),
// from what actually happened on the path.

#include <ap/engine/bit_perfect.h>
#include <ap/engine/dsp/eq.h>
#include <ap/engine/dsp/isp_safe.h>
#include <ap/engine/dsp/limiter.h>
#include <ap/engine/dsp/replay_gain.h>
#include <ap/engine/dsp/volume.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

using namespace ap::engine;

namespace {

PcmFormat int16()
{
    return PcmFormat{48000, 2, 16, 16, SampleType::Int, 0};
}

std::vector<std::byte> tone16(std::size_t frames)
{
    std::vector<std::int16_t> v(frames * 2);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<std::int16_t>(8000 * std::sin(0.03 * static_cast<double>(i)));
    std::vector<std::byte> b(v.size() * 2);
    std::memcpy(b.data(), v.data(), b.size());
    return b;
}

struct Rig
{
    std::shared_ptr<ReplayGainStage> rg = std::make_shared<ReplayGainStage>();
    std::shared_ptr<EqStage> eq = std::make_shared<EqStage>();
    std::shared_ptr<VolumeStage> volume = std::make_shared<VolumeStage>();
    std::shared_ptr<IspSafeStage> isp = std::make_shared<IspSafeStage>();
    std::shared_ptr<SafetyLimiter> limiter = std::make_shared<SafetyLimiter>();
    std::shared_ptr<ProcessingChain> chain =
        std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{rg, eq, volume, isp, limiter});
};

// Plays `source` on a dummy output that accepts `accepts`, and returns the status at the end.
PlayerStatus playOn(Rig& rig, std::unique_ptr<PcmSource> source, std::function<bool(const PcmFormat&)> accepts = {},
                    std::optional<ReplayGainInfo> rg = std::nullopt, bool allowDop = false)
{
    ap::test::TempDir dir;
    PlaybackOptions options;
    options.allowDop = allowDop;
    options.processing = rig.chain;
    Player player([out = dir.path() / "o.raw", accepts] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out, .periodFrames = {480}, .accepts = accepts});
    }, options);
    auto holder = std::make_shared<std::unique_ptr<PcmSource>>(std::move(source));
    player.setQueue({QueueItem{"t", [holder] { return std::move(*holder); }, rg}});
    player.play();
    player.waitUntilIdle();
    return player.status();
}

} // namespace

TEST_CASE("BIT-PERFECT: the direct path, unchanged", "[path][bitperfect]")
{
    Rig rig;
    const auto s = playOn(rig, std::make_unique<MemoryPcmSource>(int16(), tone16(2000)));
    REQUIRE(s.bitPerfect.has_value());
    CHECK(s.bitPerfect->bitPerfect);
    CHECK_FALSE(s.bitPerfect->repacked);
    CHECK(s.bitPerfect->dummyOutput);
    CHECK(s.bitPerfect->stages.empty());
    CHECK(s.stages.empty());
}

TEST_CASE("BIT-PERFECT: a lossless container repack keeps it (values unchanged)", "[path][bitperfect]")
{
    Rig rig;
    const auto s = playOn(rig, std::make_unique<MemoryPcmSource>(int16(), tone16(2000)),
                          [](const PcmFormat& f) { return f.containerBits == 32 && f.type == SampleType::Int; });
    REQUIRE(s.plan.has_value());
    CAPTURE(static_cast<int>(s.plan->repack), s.plan->output.containerBits, s.plan->output.validBits, s.plan->processing,
            s.path->output.format.containerBits, static_cast<int>(s.state));
    REQUIRE(s.bitPerfect.has_value());
    CHECK(s.bitPerfect->bitPerfect);
    CHECK(s.bitPerfect->repacked);
}

TEST_CASE("BIT-PERFECT: DoP carries the DSD as it is", "[path][bitperfect][dop]")
{
    class DopMemory final : public PcmSource
    {
    public:
        explicit DopMemory(std::vector<std::byte> d) : m_(PcmFormat{176400, 2, 24, 24, SampleType::Int, 0}, std::move(d)) {}
        const PcmFormat& format() const noexcept override { return m_.format(); }
        std::uint64_t totalFrames() const noexcept override { return m_.totalFrames(); }
        std::size_t read(std::span<std::byte> dst) override { return m_.read(dst); }
        bool seek(std::uint64_t f) override { return m_.seek(f); }
        std::uint64_t position() const noexcept override { return m_.position(); }
        std::optional<DopInfo> dop() const override { return DopInfo{2822400, "DSF"}; }

    private:
        MemoryPcmSource m_;
    };
    Rig rig;
    rig.volume->setGainDb(-6.0); // never applied to DoP
    const auto s = playOn(rig, std::make_unique<DopMemory>(std::vector<std::byte>(3000 * 6, std::byte{0x69})), {},
                          std::nullopt, true);
    REQUIRE(s.bitPerfect.has_value());
    CHECK(s.bitPerfect->bitPerfect);
    CHECK(s.bitPerfect->dop);
}

TEST_CASE("BIT-PERFECT: a float source on a float output passes its decoded values", "[path][bitperfect]")
{
    Rig rig;
    std::vector<float> v(4000, 0.25f);
    std::vector<std::byte> b(v.size() * 4);
    std::memcpy(b.data(), v.data(), b.size());
    const auto s = playOn(rig, std::make_unique<MemoryPcmSource>(PcmFormat{48000, 2, 32, 32, SampleType::Float, 0}, b));
    REQUIRE(s.bitPerfect.has_value());
    CHECK(s.bitPerfect->bitPerfect);
    CHECK(s.bitPerfect->floatSource);
}

TEST_CASE("Not BIT-PERFECT: the digital volume, with its value", "[path][bitperfect]")
{
    Rig rig;
    rig.volume->setGainDb(-6.0);
    const auto s = playOn(rig, std::make_unique<MemoryPcmSource>(int16(), tone16(2000)));
    REQUIRE(s.bitPerfect.has_value());
    CHECK_FALSE(s.bitPerfect->bitPerfect);
    REQUIRE(s.bitPerfect->stages.size() == 2);
    CHECK(s.bitPerfect->stages[0] == StageState{"volume", -6.0});
    CHECK(s.bitPerfect->stages[1].name == "limiter");
    CHECK(s.bitPerfect->dither);
}

TEST_CASE("Not BIT-PERFECT: ReplayGain, EQ and ISP-safe with their values", "[path][bitperfect]")
{
    Rig rig;
    rig.rg->setMode(ReplayGainMode::Track);
    rig.eq->setBands({{EqFilterType::Peak, 1000, 4.0, 1.0}});
    rig.isp->setMode(IspSafeMode::Measured);
    const auto s = playOn(rig, std::make_unique<MemoryPcmSource>(int16(), tone16(4000)), {},
                          ReplayGainInfo{-3.0, std::pow(10.0, 1.0 / 20), std::nullopt, std::nullopt});
    REQUIRE(s.bitPerfect.has_value());
    CHECK_FALSE(s.bitPerfect->bitPerfect);
    // ISP-safe does nothing here (+1 dBTP - 3 dB is below -0.5 dBTP), so it is not listed.
    REQUIRE(s.stages.size() == 3);
    CHECK(s.stages[0].name == "replaygain");
    CHECK(std::abs(*s.stages[0].gainDb - -3.0) < 1e-9);
    CHECK(s.stages[1].name == "eq");
    CHECK(std::abs(*s.stages[1].gainDb - -4.0) < 0.01); // the automatic preamp
    CHECK(s.stages[2].name == "limiter");
}

TEST_CASE("Not BIT-PERFECT: a float source quantized for an integer-only output", "[path][bitperfect]")
{
    Rig rig;
    std::vector<float> v(4000, 0.25f);
    std::vector<std::byte> b(v.size() * 4);
    std::memcpy(b.data(), v.data(), b.size());
    const auto s = playOn(rig, std::make_unique<MemoryPcmSource>(PcmFormat{48000, 2, 32, 32, SampleType::Float, 0}, b),
                          [](const PcmFormat& f) { return f.type == SampleType::Int; });
    REQUIRE(s.bitPerfect.has_value());
    CHECK_FALSE(s.bitPerfect->bitPerfect);
    CHECK(s.bitPerfect->quantized);
    CHECK(s.bitPerfect->dither);
}

TEST_CASE("Not BIT-PERFECT: the listening comparison", "[path][bitperfect]")
{
    Rig rig;
    rig.chain->setComparing(true);
    const auto s = playOn(rig, std::make_unique<MemoryPcmSource>(int16(), tone16(2000)));
    REQUIRE(s.bitPerfect.has_value());
    CHECK_FALSE(s.bitPerfect->bitPerfect);
    CHECK(s.bitPerfect->comparison);
    CHECK(s.bitPerfect->stages.back().name == "comparison");
}

TEST_CASE("BIT-PERFECT with a note: the device's own volume changes the signal after the driver", "[path][bitperfect]")
{
    SignalPath p;
    p.source = int16();
    p.output.api = OutputApi::WasapiExclusive;
    p.output.endpointVolume = EndpointVolumeInfo{true, 0.37f, false, true};
    auto v = judgeBitPerfect(p, {});
    CHECK(v.bitPerfect);
    REQUIRE(v.deviceVolume.has_value());
    CHECK(std::abs(*v.deviceVolume - 0.37) < 1e-6);

    p.output.endpointVolume = EndpointVolumeInfo{true, 1.0f, false, true};
    CHECK_FALSE(judgeBitPerfect(p, {}).deviceVolume.has_value());
    p.output.endpointVolume = EndpointVolumeInfo{true, 0.5f, false, false}; // not in hardware: no effect in exclusive mode
    CHECK_FALSE(judgeBitPerfect(p, {}).deviceVolume.has_value());
}
