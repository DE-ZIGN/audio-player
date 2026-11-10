// Task 3-3 (docs/phase1-tasks.md): ISP信号で +3.01 dBTP ± 許容差
//
// The ISP test signal (CLAUDE.md chapter 5: fs/4, phase 45 degrees, 0 dBFS)
// is generated as a WAV file, read back and measured. Its steady part peaks at
// sqrt(2) times the sample peak: +3.01 dBTP. The signal starts and stops
// abruptly, and the band-limited waveform really overshoots there, so the
// whole file reads a little higher; the steady part is measured with the ends
// faded (tolerance +-0.02 dB).

#include <ap/engine/dsp/sample_convert.h>
#include <ap/engine/dsp/true_peak.h>
#include <ap/engine/wav_file_source.h>
#include <ap/signalgen/signals.h>
#include <ap/signalgen/wav_writer.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace ap::engine;
namespace sg = ap::signalgen;

TEST_CASE("ISP signal at 0 dBFS reads +3.01 dBTP", "[acceptance][truepeak]")
{
    const auto rate = GENERATE(44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u);
    const auto format = GENERATE(sg::SampleFormat::Int16, sg::SampleFormat::Int24, sg::SampleFormat::Int32);
    CAPTURE(rate, sg::formatName(format));

    ap::test::TempDir dir;
    const sg::Layout layout{rate, 2, rate / 2}; // 0.5 s
    const auto path = dir.path() / "isp.wav";
    sg::writeWav(path, sg::quantize(sg::ispSine(layout, 0.0), rate, 2, format));

    // The whole file, as the library will measure it.
    WavFileSource file(path);
    const auto whole = analyzePeaks(file);
    const double fullScale = std::pow(2.0, sg::bitsPerSample(format) - 1);
    const double samplePeakDb = 20 * std::log10(whole.samplePeakMax);
    CHECK(std::abs(whole.samplePeakMax - (fullScale - 1) / fullScale) < 1e-12); // the largest code
    CHECK(20 * std::log10(whole.truePeakMax) - samplePeakDb >= 3.0103 - 0.02);

    // The steady part: the same samples with 20 ms faded in and out.
    WavFileSource again(path);
    const auto f = again.format();
    std::vector<std::byte> bytes(static_cast<std::size_t>(again.totalFrames()) * f.bytesPerFrame());
    REQUIRE(again.read(bytes) == again.totalFrames());
    std::vector<double> x(static_cast<std::size_t>(again.totalFrames()) * f.channels);
    dsp::toDouble(bytes, f, x);
    const std::size_t frames = x.size() / 2;
    const std::size_t fade = rate / 50;
    for (std::size_t i = 0; i < fade; ++i)
    {
        const double g = 0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(i) / static_cast<double>(fade));
        for (std::size_t c = 0; c < 2; ++c)
        {
            x[i * 2 + c] *= g;
            x[(frames - 1 - i) * 2 + c] *= g;
        }
    }
    TruePeakMeter steady(2);
    steady.process(x);
    steady.finish();
    const double over = steady.truePeakDb() - samplePeakDb;
    CAPTURE(over, 20 * std::log10(whole.truePeakMax) - samplePeakDb);
    CHECK(std::abs(over - 3.0103) < 0.02);
}
