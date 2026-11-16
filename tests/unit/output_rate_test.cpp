// Task 10-2: which rate the processing path sends (音作りモード only): the
// track's (default), a fixed rate, at least a rate, or an integer multiple;
// and, when the DAC refuses it, a rate it takes (the track's family first).

#include <ap/engine/output/dummy_output.h>
#include <ap/engine/output_rate.h>
#include <ap/engine/playback.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <vector>

using namespace ap::engine;

namespace {

PcmFormat pcm(std::uint32_t rate, std::uint16_t bits = 24)
{
    PcmFormat f;
    f.sampleRate = rate;
    f.channels = 2;
    f.validBits = bits;
    f.containerBits = bits == 24 ? 24 : bits;
    f.type = SampleType::Int;
    return f;
}

std::uint32_t first(std::uint32_t source, OutputRateChoice c)
{
    return outputRateCandidates(source, c).front();
}

} // namespace

TEST_CASE("Output rate: the choices give the rate wanted first", "[output-rate]")
{
    CHECK(first(44100, {}) == 44100); // the track's rate by default
    CHECK(first(96000, {OutputRateMode::Track}) == 96000);
    // Fixed: every track at that rate, higher ones lowered.
    for (const std::uint32_t src : {44100u, 48000u, 96000u, 192000u, 384000u})
        CHECK(first(src, {OutputRateMode::Fixed, 96000}) == 96000);
    // At least: only lower tracks raised.
    CHECK(first(44100, {OutputRateMode::AtLeast, 96000}) == 96000);
    CHECK(first(48000, {OutputRateMode::AtLeast, 96000}) == 96000);
    CHECK(first(96000, {OutputRateMode::AtLeast, 96000}) == 96000);
    CHECK(first(192000, {OutputRateMode::AtLeast, 96000}) == 192000);
    // Integer multiples: the family stays.
    CHECK(first(44100, {OutputRateMode::Multiple, 0, 2}) == 88200);
    CHECK(first(48000, {OutputRateMode::Multiple, 0, 2}) == 96000);
    CHECK(first(96000, {OutputRateMode::Multiple, 0, 2}) == 192000);
    CHECK(first(44100, {OutputRateMode::Multiple, 0, 8}) == 352800);
}

TEST_CASE("Output rate: when the DAC refuses, the next choices", "[output-rate]")
{
    // Multiples: the largest the DAC takes, then the track's own rate.
    const auto m = outputRateCandidates(44100, {OutputRateMode::Multiple, 0, 8});
    REQUIRE(m.size() >= 4);
    CHECK(std::vector<std::uint32_t>(m.begin(), m.begin() + 4) == std::vector<std::uint32_t>{352800, 176400, 88200, 44100});
    // Then rates of the track's family (higher first), then the other family.
    const auto t = outputRateCandidates(44100, {});
    const auto at = [&](std::uint32_t r) { return std::find(t.begin(), t.end(), r) - t.begin(); };
    CHECK(at(44100) == 0);
    CHECK(at(88200) < at(176400));
    CHECK(at(176400) < at(22050));  // the family, higher before lower
    CHECK(at(22050) < at(48000));   // then the other family
    CHECK(at(48000) < at(96000));
    // No rate twice.
    CHECK(std::set<std::uint32_t>(t.begin(), t.end()).size() == t.size());
}

TEST_CASE("Output rate: settings keys", "[output-rate][settings]")
{
    for (const auto m : {OutputRateMode::Track, OutputRateMode::Fixed, OutputRateMode::AtLeast, OutputRateMode::Multiple})
        CHECK(outputRateModeFrom(outputRateModeKey(m)) == m);
    CHECK_FALSE(outputRateModeFrom("bogus"));
    CHECK(isOutputRateChoiceRate(96000));
    CHECK_FALSE(isOutputRateChoiceRate(12345));
    CHECK(isOutputRateMultiple(4));
    CHECK_FALSE(isOutputRateMultiple(3));
}

TEST_CASE("Output rate: the processing path takes the first rate the output accepts", "[output-rate][playback]")
{
    ap::test::TempDir dir;
    // A DAC with 44.1, 48, 96 and 192 kHz only.
    const std::set<std::uint32_t> rates{44100, 48000, 96000, 192000};
    DummyOutput out({.path = dir.path() / "out.raw", .accepts = [&](const PcmFormat& f) {
                         return rates.contains(f.sampleRate) && f.type == SampleType::Int && f.containerBits == 32 &&
                                f.validBits == 32;
                     }});
    struct Case
    {
        std::uint32_t source;
        OutputRateChoice choice;
        std::uint32_t expected;
        std::optional<Resampling::Reason> reason;
    };
    for (const auto& c : {
             Case{44100, {}, 44100, std::nullopt},
             Case{44100, {OutputRateMode::Fixed, 96000}, 96000, Resampling::Reason::Setting},
             Case{192000, {OutputRateMode::Fixed, 96000}, 96000, Resampling::Reason::Setting},
             Case{44100, {OutputRateMode::Multiple, 0, 2}, 44100, Resampling::Reason::Unsupported}, // 88.2 refused
             Case{48000, {OutputRateMode::Multiple, 0, 4}, 192000, Resampling::Reason::Setting},
             Case{48000, {OutputRateMode::Multiple, 0, 8}, 192000, Resampling::Reason::Unsupported}, // 384 refused
             Case{88200, {}, 44100, Resampling::Reason::Unsupported},  // the track's rate refused: its family
             Case{352800, {}, 44100, Resampling::Reason::Unsupported}, // 176.4 refused too
             Case{32000, {}, 44100, Resampling::Reason::Unsupported},  // no family member: the other, nearest higher first
         })
    {
        CAPTURE(c.source, outputRateModeKey(c.choice.mode), c.choice.rate, c.choice.multiple);
        const auto n = negotiateProcessing(pcm(c.source), out, outputRateCandidates(c.source, c.choice));
        REQUIRE(n.plan);
        CHECK(n.plan->output.sampleRate == c.expected);
        CHECK(n.plan->source.sampleRate == c.source);
        if (c.expected == c.source)
            CHECK_FALSE(n.plan->resample);
        else
        {
            REQUIRE(n.plan->resample);
            CHECK(n.plan->resample->from == c.source);
            CHECK(n.plan->resample->to == c.expected);
            CHECK(n.plan->resample->reason == c.reason);
        }
    }
}
