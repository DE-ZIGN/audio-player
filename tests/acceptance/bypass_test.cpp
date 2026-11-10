// CLAUDE.md chapter 5:
//   EQ 全 0 dB・音作りオフ：バイパスが成立し、原音モードと出力が一致する
//
// Task 3-1 builds the processing path without real stages yet, so "processing
// off" is a chain whose stages are all identity. The output of that chain must
// be the output of the direct path (原音モード), byte for byte, and the path
// must be recorded as not processed. As a check of the conversions alone, the
// same input forced through the processing path into its own format must also
// come out unchanged.

#include <ap/engine/dsp/processing.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/playback.h>
#include <ap/engine/sha256.h>
#include <ap/engine/wav_file_source.h>
#include <ap/signalgen/signals.h>
#include <ap/signalgen/wav_writer.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace ap::engine;
namespace sg = ap::signalgen;

namespace {

// A stage at its neutral setting (like EQ at 0 dB everywhere).
class NeutralStage final : public ProcessingStage
{
public:
    std::string name() const override { return "neutral"; }
    void prepare(std::uint32_t, std::uint16_t) override {}
    bool isIdentity() const noexcept override { return true; }
    void process(std::span<double> samples, std::uint16_t) noexcept override
    {
        for (auto& s : samples)
            s *= 0.5; // never called while identity; would show in the output if it were
    }
};

// Active but changes nothing: forces the processing path.
class ForcedStage final : public ProcessingStage
{
public:
    std::string name() const override { return "forced"; }
    void prepare(std::uint32_t, std::uint16_t) override {}
    bool isIdentity() const noexcept override { return false; }
    void process(std::span<double>, std::uint16_t) noexcept override {}
};

std::vector<std::uint8_t> makeWav(const std::string& signal, std::uint32_t rate, sg::SampleFormat format)
{
    const sg::Layout layout{rate, 2, rate / 4}; // 0.25 s
    if (signal == "bitpattern")
        return sg::encodeWav(sg::bitPattern(layout, format));
    if (signal == "isp")
        return sg::encodeWav(sg::quantize(sg::ispSine(layout, 0.0), rate, 2, format));
    return sg::encodeWav(sg::quantize(sg::sine(layout, 1000.0, -90.0), rate, 2, format));
}

std::string playToHash(const std::filesystem::path& in, const std::filesystem::path& out,
                       std::shared_ptr<ProcessingChain> chain, PlaybackPlan& plan,
                       std::function<bool(const PcmFormat&)> accepts = {})
{
    WavFileSource source(in);
    DummyOutput output({.path = out, .periodFrames = {441, 4096, 7, 1000}, .accepts = accepts});
    PlaybackOptions options;
    options.processing = std::move(chain);
    plan = play(source, output, options).plan;
    return toHex(sha256File(out));
}

} // namespace

TEST_CASE("Processing off: bypass holds and the output equals the direct path", "[acceptance][bypass]")
{
    const auto rate = GENERATE(44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u);
    const auto format = GENERATE(sg::SampleFormat::Int16, sg::SampleFormat::Int24, sg::SampleFormat::Int32);
    const auto signal = GENERATE(std::string("bitpattern"), std::string("isp"), std::string("sine"));
    CAPTURE(rate, sg::formatName(format), signal);

    ap::test::TempDir dir;
    const auto in = dir.write("in.wav", makeWav(signal, rate, format));

    PlaybackPlan direct, neutral, forced;
    const auto directHash = playToHash(in, dir.path() / "direct.raw", nullptr, direct);
    const auto neutralHash = playToHash(
        in, dir.path() / "neutral.raw",
        std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{
            std::make_shared<NeutralStage>(), std::make_shared<NeutralStage>()}),
        neutral);

    CHECK_FALSE(neutral.processing);
    CHECK(neutral.output == direct.output);
    CHECK(neutralHash == directHash);

    // The processing path itself, into the source format: int -> double -> int is exact.
    const PcmFormat sourceFormat = direct.source;
    const auto forcedHash = playToHash(
        in, dir.path() / "forced.raw",
        std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{std::make_shared<ForcedStage>()}),
        forced, [sourceFormat](const PcmFormat& f) { return f == sourceFormat; });
    CHECK(forced.processing);
    CHECK(forcedHash == directHash);
}
