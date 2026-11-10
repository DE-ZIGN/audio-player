// CLAUDE.md chapter 5:
//   ISPセーフ：出力が −0.5 dBTP 以下。テスト信号は fs/4・位相 45度・0 dBFS のサイン波（= +3.01 dBTP）
//
// The ISP signal is written as a WAV, measured as the library will measure it
// at import (the whole file's true peak), and played through the processing
// chain (ReplayGain off, volume 100%, ISP-safe measured, safety limiter) into
// a 32-bit integer output. The output's true peak must be at or below
// -0.5 dBTP, by the ISP-safe gain alone (the limiter does not act).

#include <ap/engine/dsp/isp_safe.h>
#include <ap/engine/dsp/limiter.h>
#include <ap/engine/dsp/replay_gain.h>
#include <ap/engine/dsp/sample_convert.h>
#include <ap/engine/dsp/true_peak.h>
#include <ap/engine/dsp/volume.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>
#include <ap/engine/wav_file_source.h>
#include <ap/signalgen/signals.h>
#include <ap/signalgen/wav_writer.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <fstream>
#include <iterator>
#include <vector>

using namespace ap::engine;
namespace sg = ap::signalgen;

TEST_CASE("ISP-safe: the ISP signal at 0 dBFS comes out at or below -0.5 dBTP", "[acceptance][isp]")
{
    const auto rate = GENERATE(44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u);
    const auto format = GENERATE(sg::SampleFormat::Int16, sg::SampleFormat::Int24, sg::SampleFormat::Int32);
    CAPTURE(rate, sg::formatName(format));

    ap::test::TempDir dir;
    const auto wav = dir.path() / "isp.wav";
    sg::writeWav(wav, sg::quantize(sg::ispSine(sg::Layout{rate, 2, rate / 2}, 0.0), rate, 2, format));

    // At import: the whole file's true peak.
    WavFileSource measured(wav);
    const auto peaks = analyzePeaks(measured);
    REQUIRE(20 * std::log10(peaks.truePeakMax) > 3.0);

    auto replayGain = std::make_shared<ReplayGainStage>();
    auto volume = std::make_shared<VolumeStage>();
    auto isp = std::make_shared<IspSafeStage>();
    isp->setMode(IspSafeMode::Measured);
    auto limiter = std::make_shared<SafetyLimiter>();
    PlaybackOptions options;
    options.processing = std::make_shared<ProcessingChain>(
        std::vector<std::shared_ptr<ProcessingStage>>{replayGain, volume, isp, limiter});

    const auto out = dir.path() / "out.raw";
    Player player([out] { return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out, .periodFrames = {1000}}); },
                  options);
    QueueItem item{"isp", [wav] { return std::make_unique<WavFileSource>(wav); },
                   ReplayGainInfo{0.0, peaks.truePeakMax, std::nullopt, std::nullopt}};
    player.setQueue({item});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.path.has_value());
    CHECK(s.path->processing);
    CHECK(s.path->processingStages == std::vector<std::string>{"isp-safe", "limiter"});
    REQUIRE(s.path->output.format.containerBits == 32);

    std::ifstream in(out, std::ios::binary);
    std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(raw.size());
    std::memcpy(bytes.data(), raw.data(), raw.size());
    REQUIRE(bytes.size() == static_cast<std::size_t>(rate / 2) * 2 * 4); // every frame, no more
    std::vector<double> x(bytes.size() / 4);
    dsp::toDouble(bytes, s.path->output.format, x);
    TruePeakMeter meter(2);
    meter.process(x);
    meter.finish();
    CAPTURE(meter.truePeakDb(), isp->gainDb());
    CHECK(meter.truePeakDb() <= -0.5 + 1e-6);
    CHECK(meter.truePeakDb() > -0.6); // lowered by the excess, not more
    CHECK(limiter->stats().events == 0);
}
