// Task 3-4 (docs/phase1-tasks.md): ReplayGain 2.0 — 既知の音源で値が一致
//
// The known sources are the EBU Tech 3341 minimum requirements test signals
// for integrated loudness (cases 1-5), generated here: 1 kHz
// sine waves in phase on both channels, levels in dBFS per channel. Each must
// read the stated loudness within +-0.1 LU. ReplayGain 2.0 is then
// -18 LUFS minus the integrated loudness.

#include <ap/engine/dsp/loudness.h>
#include <ap/engine/dsp/replay_gain.h>
#include <ap/engine/pcm_source.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <numbers>
#include <utility>
#include <vector>

using namespace ap::engine;

namespace {

constexpr std::uint32_t kRate = 48000;

// Concatenated 1 kHz stereo tones: (level dBFS, seconds), as 32-bit float PCM.
MemoryPcmSource tones(const std::vector<std::pair<double, double>>& parts, std::uint32_t rate = kRate)
{
    std::vector<float> v;
    std::size_t n = 0;
    for (const auto& [level, seconds] : parts)
    {
        const double a = std::pow(10.0, level / 20.0);
        const auto frames = static_cast<std::size_t>(std::llround(seconds * rate));
        for (std::size_t i = 0; i < frames; ++i, ++n)
        {
            const auto s = static_cast<float>(a * std::sin(2 * std::numbers::pi * 1000.0 * static_cast<double>(n) / rate));
            v.push_back(s);
            v.push_back(s);
        }
    }
    std::vector<std::byte> bytes(v.size() * 4);
    std::memcpy(bytes.data(), v.data(), bytes.size());
    return MemoryPcmSource(PcmFormat{rate, 2, 32, 32, SampleType::Float, 0x3}, std::move(bytes));
}

} // namespace

TEST_CASE("EBU Tech 3341 case 1: 1 kHz at -23 dBFS reads -23.0 LUFS", "[acceptance][loudness]")
{
    auto src = tones({{-23.0, 20.0}});
    const auto a = analyzeTrack(src);
    CHECK(std::abs(a.integratedLufs - -23.0) <= 0.1);
    CHECK(std::abs(replayGainDb(a.integratedLufs) - 5.0) <= 0.1); // -18 - (-23)
}

TEST_CASE("EBU Tech 3341 case 2: 1 kHz at -33 dBFS reads -33.0 LUFS", "[acceptance][loudness]")
{
    auto src = tones({{-33.0, 20.0}});
    CHECK(std::abs(analyzeTrack(src).integratedLufs - -33.0) <= 0.1);
}

TEST_CASE("EBU Tech 3341 case 3: -36/-23/-36 dBFS (10/60/10 s) reads -23.0 LUFS (relative gate)", "[acceptance][loudness]")
{
    auto src = tones({{-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}});
    CHECK(std::abs(analyzeTrack(src).integratedLufs - -23.0) <= 0.1);
}

TEST_CASE("EBU Tech 3341 case 4: -72/-36/-23/-36/-72 dBFS reads -23.0 LUFS (absolute gate)", "[acceptance][loudness]")
{
    auto src = tones({{-72.0, 10.0}, {-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}, {-72.0, 10.0}});
    CHECK(std::abs(analyzeTrack(src).integratedLufs - -23.0) <= 0.1);
}

TEST_CASE("EBU Tech 3341 case 5: -26/-20/-26 dBFS (20/20.1/20 s) reads -23.0 LUFS", "[acceptance][loudness]")
{
    auto src = tones({{-26.0, 20.0}, {-20.0, 20.1}, {-26.0, 20.0}});
    CHECK(std::abs(analyzeTrack(src).integratedLufs - -23.0) <= 0.1);
}

TEST_CASE("Loudness at 44.1 kHz: 1 kHz at -23 dBFS reads -23.0 LUFS", "[acceptance][loudness]")
{
    auto src = tones({{-23.0, 20.0}}, 44100);
    CHECK(std::abs(analyzeTrack(src).integratedLufs - -23.0) <= 0.1);
}
