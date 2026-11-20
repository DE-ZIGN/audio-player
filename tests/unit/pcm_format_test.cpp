// Task 11-3 (M11): describe() is for technical details (exception texts,
// logs), in English; the page writes the formats it shows itself (messages.ts).

#include <ap/engine/pcm_format.h>

#include <catch2/catch_test_macros.hpp>

using namespace ap::engine;

TEST_CASE("A format described in English, for technical details", "[format]")
{
    const auto f = [](std::uint32_t rate, std::uint16_t container, std::uint16_t valid, SampleType type = SampleType::Int) {
        return PcmFormat{rate, 2, container, valid, type};
    };
    CHECK(describe(f(44100, 16, 16)) == "44.1 kHz / 16-bit / 2ch");
    CHECK(describe(f(96000, 32, 24)) == "96 kHz / 24-bit in 32-bit container / 2ch");
    CHECK(describe(f(48000, 32, 32, SampleType::Float)) == "48 kHz / 32-bit float / 2ch");
    CHECK(describe(PcmFormat{352800, 1, 64, 64, SampleType::Float}) == "352.8 kHz / 64-bit float / 1ch");
}
