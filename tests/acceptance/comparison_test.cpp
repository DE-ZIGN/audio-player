// Task 3-7 (docs/phase1-tasks.md): 原音／処理の切り替え — 切り替え前後の音量差テスト
// (quality-measures.md: 音作り側を原音に ±0.1 dB で合わせる).
//
// A steady multi-tone signal is played with a listening comparison: the
// processing chain is an EQ (a +6 dB peak and a -4 dB high shelf, with its
// automatic preamp) and the volume at -6 dB. The output alternates between
// the original and the processed side. The integrated loudness of each part,
// measured with libebur128 (independent of the comparison's own K-weighting),
// must agree within 0.1 dB.

#include <ap/engine/dsp/comparison.h>
#include <ap/engine/dsp/eq.h>
#include <ap/engine/dsp/limiter.h>
#include <ap/engine/dsp/loudness.h>
#include <ap/engine/dsp/volume.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <numbers>

using namespace ap::engine;

namespace {

// Calls `at` once when reading passes `frame`.
class TriggerSource final : public PcmSource
{
public:
    TriggerSource(std::unique_ptr<PcmSource> inner, std::vector<std::pair<std::uint64_t, std::function<void()>>> at)
        : inner_(std::move(inner)), at_(std::move(at))
    {
    }
    const PcmFormat& format() const noexcept override { return inner_->format(); }
    std::uint64_t totalFrames() const noexcept override { return inner_->totalFrames(); }
    std::size_t read(std::span<std::byte> dst) override
    {
        const auto n = inner_->read(dst);
        for (auto& [frame, fn] : at_)
            if (fn && inner_->position() >= frame)
            {
                fn();
                fn = nullptr;
            }
        return n;
    }
    bool seek(std::uint64_t f) override { return inner_->seek(f); }
    std::uint64_t position() const noexcept override { return inner_->position(); }

private:
    std::unique_ptr<PcmSource> inner_;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> at_;
};

// 40 tones from 60 Hz to 14 kHz with fixed phases, each a whole number of
// cycles per second: the signal repeats every second, so any whole seconds of
// it have the same loudness.
std::vector<float> multitone(std::uint32_t rate, std::size_t seconds)
{
    std::vector<float> v(static_cast<std::size_t>(rate) * seconds * 2);
    std::uint64_t x = 5;
    std::vector<std::pair<double, double>> tones;
    for (int k = 0; k < 40; ++k)
    {
        const double f = std::round(60.0 * std::pow(14000.0 / 60.0, k / 39.0));
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        tones.emplace_back(f, 2 * std::numbers::pi * static_cast<double>(x >> 11) * 0x1.0p-53);
    }
    for (std::size_t i = 0; i < v.size() / 2; ++i)
    {
        double s = 0;
        for (const auto& [f, ph] : tones)
            s += std::sin(2 * std::numbers::pi * f * static_cast<double>(i) / rate + ph);
        v[2 * i] = static_cast<float>(0.012 * s);
        v[2 * i + 1] = static_cast<float>(0.010 * s);
    }
    return v;
}

double loudnessOf(const std::vector<float>& v, std::size_t fromFrame, std::size_t frames, std::uint32_t rate)
{
    LoudnessMeter m(rate, 2);
    std::vector<double> d(v.begin() + static_cast<std::ptrdiff_t>(fromFrame * 2),
                          v.begin() + static_cast<std::ptrdiff_t>((fromFrame + frames) * 2));
    m.process(d);
    return m.integratedLufs();
}

} // namespace

TEST_CASE("Comparison: the processed side is matched to the original within 0.1 dB", "[acceptance][comparison]")
{
    const auto rate = GENERATE(44100u, 48000u, 96000u);
    CAPTURE(rate);
    const std::size_t seconds = 12;
    const auto in = multitone(rate, seconds);

    auto eq = std::make_shared<EqStage>();
    eq->setBands({{EqFilterType::Peak, 2500, 6.0, 1.0}, {EqFilterType::HighShelf, 8000, -4.0, 0.7}});
    auto volume = std::make_shared<VolumeStage>();
    volume->setGainDb(-6.0);
    auto limiter = std::make_shared<SafetyLimiter>();
    PlaybackOptions options;
    options.processing =
        std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{eq, volume, limiter});
    options.processing->setComparing(true);
    auto& ab = options.processing->comparison();
    ab.select(ComparisonSide::Original);

    ap::test::TempDir dir;
    Player player([out = dir.path() / "o.raw"] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{
            .path = out, .periodFrames = {512}, .accepts = [](const PcmFormat& f) { return f.type == SampleType::Float; }});
    }, options);
    // Original for 0-4 s, processed for 4-8 s, original again for 8-12 s.
    auto* abp = &ab;
    auto src = std::make_unique<TriggerSource>(
        std::make_unique<MemoryPcmSource>(PcmFormat{rate, 2, 32, 32, SampleType::Float, 0x3},
                                          [&] {
                                              std::vector<std::byte> b(in.size() * 4);
                                              std::memcpy(b.data(), in.data(), b.size());
                                              return b;
                                          }()),
        std::vector<std::pair<std::uint64_t, std::function<void()>>>{
            {static_cast<std::uint64_t>(rate) * 4, [abp] { abp->select(ComparisonSide::Processed); }},
            {static_cast<std::uint64_t>(rate) * 8, [abp] { abp->select(ComparisonSide::Original); }}});
    player.load(std::move(src));
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.path.has_value());
    CHECK(s.path->comparison);
    std::ifstream f(dir.path() / "o.raw", std::ios::binary);
    std::vector<char> raw{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    std::vector<float> out(raw.size() / 4);
    std::memcpy(out.data(), raw.data(), out.size() * 4);
    REQUIRE(out.size() == in.size());

    // Whole seconds well inside each part (the switch happens within one period of its time).
    const double original = loudnessOf(out, rate * 1, rate * 2, rate);
    const double processed = loudnessOf(out, rate * 5, rate * 2, rate);
    const double originalAgain = loudnessOf(out, rate * 9, rate * 2, rate);
    CAPTURE(original, processed, originalAgain, ab.matchDb());
    CHECK(std::abs(processed - original) <= 0.1);
    CHECK(std::abs(ab.levelDifferenceDb()) <= 0.1); // what the comparison itself reports
    CHECK(std::abs(originalAgain - original) < 1e-6);
    // The original side keeps its values.
    CHECK(std::memcmp(out.data() + rate * 2, in.data() + rate * 2, static_cast<std::size_t>(rate) * 2 * 4) == 0);
    // The processed side really is different (the EQ is there), though as loud.
    double diff = 0;
    for (std::size_t i = rate * 5 * 2; i < rate * 7 * 2; ++i)
        diff = std::max(diff, static_cast<double>(std::abs(out[i] - in[i])));
    CHECK(diff > 0.01);
}
