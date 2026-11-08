#include <ap/signalgen/pcm.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>
#include <vector>

using namespace ap::signalgen;

TEST_CASE("Integer code ranges", "[signalgen][pcm]")
{
    CHECK(maxCode(SampleFormat::Int16) == 32767);
    CHECK(minCode(SampleFormat::Int16) == -32768);
    CHECK(maxCode(SampleFormat::Int24) == 8388607);
    CHECK(minCode(SampleFormat::Int24) == -8388608);
    CHECK(maxCode(SampleFormat::Int32) == std::numeric_limits<std::int32_t>::max());
    CHECK(minCode(SampleFormat::Int32) == std::numeric_limits<std::int32_t>::min());
}

TEST_CASE("Format names round-trip through parseFormat", "[signalgen][pcm]")
{
    for (auto f : {SampleFormat::Int16, SampleFormat::Int24, SampleFormat::Int32,
                   SampleFormat::Float32, SampleFormat::Float64})
    {
        const auto parsed = parseFormat(formatName(f));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == f);
    }
    CHECK(parseFormat("24") == SampleFormat::Int24);
    CHECK(parseFormat("float64") == SampleFormat::Float64);
    CHECK_FALSE(parseFormat("8").has_value());
}

TEST_CASE("quantize rounds half away from zero and clamps", "[signalgen][pcm]")
{
    const double halfLsb = 0.5 / 32767.0;
    const std::vector<double> in{0.0, 1.0, -1.0, 0.5, 1.5, -1.5, halfLsb, -halfLsb};
    const auto pcm = quantize(in, 48000, 1, SampleFormat::Int16);

    REQUIRE(pcm.intSamples.size() == in.size());
    CHECK(pcm.intSamples[0] == 0);
    CHECK(pcm.intSamples[1] == 32767);
    CHECK(pcm.intSamples[2] == -32767);
    CHECK(pcm.intSamples[3] == 16384); // 16383.5 -> 16384
    CHECK(pcm.intSamples[4] == 32767); // clamped
    CHECK(pcm.intSamples[5] == -32768); // clamped
    CHECK(pcm.intSamples[6] == 1);
    CHECK(pcm.intSamples[7] == -1);
    CHECK(pcm.frameCount() == in.size());
}

TEST_CASE("quantize maps full scale for 24 and 32 bit", "[signalgen][pcm]")
{
    const std::vector<double> in{1.0, -1.0};
    CHECK(quantize(in, 48000, 2, SampleFormat::Int24).intSamples
          == std::vector<std::int32_t>{8388607, -8388607});
    CHECK(quantize(in, 48000, 2, SampleFormat::Int32).intSamples
          == std::vector<std::int32_t>{2147483647, -2147483647});
}

TEST_CASE("quantize passes float formats through unchanged", "[signalgen][pcm]")
{
    const std::vector<double> in{0.1, -0.25, 1.0 / 3.0, 1.5};
    const auto pcm = quantize(in, 96000, 2, SampleFormat::Float64);
    CHECK(pcm.floatSamples == in);
    CHECK(pcm.intSamples.empty());
    CHECK(pcm.frameCount() == 2);
}

TEST_CASE("quantize rejects bad input", "[signalgen][pcm]")
{
    CHECK_THROWS_AS(quantize({0.0, 0.0, 0.0}, 48000, 2, SampleFormat::Int16), std::invalid_argument);
    CHECK_THROWS_AS(quantize({0.0}, 48000, 0, SampleFormat::Int16), std::invalid_argument);
    CHECK_THROWS_AS(quantize({std::numeric_limits<double>::quiet_NaN()}, 48000, 1, SampleFormat::Int16),
                    std::invalid_argument);
}
