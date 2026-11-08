#include <ap/signalgen/signals.h>
#include <ap/signalgen/wav_writer.h>

#include "wav_reader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <vector>

using namespace ap::signalgen;

namespace {

PcmData makeTestPcm(SampleFormat format, std::uint16_t channels, std::size_t frames)
{
    const Layout layout{88200, channels, frames};
    if (isInteger(format))
        return bitPattern(layout, format);
    return quantize(sine(layout, 997.0, -3.0, 0.3), layout.sampleRate, channels, format);
}

std::uint32_t readLe32(const std::vector<std::uint8_t>& b, std::size_t offset)
{
    return static_cast<std::uint32_t>(b[offset]) | (static_cast<std::uint32_t>(b[offset + 1]) << 8)
         | (static_cast<std::uint32_t>(b[offset + 2]) << 16) | (static_cast<std::uint32_t>(b[offset + 3]) << 24);
}

} // namespace

TEST_CASE("WAV round-trip preserves every sample and header field", "[signalgen][wav]")
{
    const auto format = GENERATE(SampleFormat::Int16, SampleFormat::Int24, SampleFormat::Int32,
                                 SampleFormat::Float32, SampleFormat::Float64);
    const auto channels = GENERATE(std::uint16_t{1}, std::uint16_t{2}, std::uint16_t{6});
    CAPTURE(formatName(format), channels);

    const auto pcm = makeTestPcm(format, channels, 1500);
    const auto bytes = encodeWav(pcm);
    const auto wav = ap::test::parseWav(bytes);

    const int bits = bitsPerSample(format);
    const bool plainPcm = format == SampleFormat::Int16 && channels <= 2;

    CHECK(wav.formatTag == (plainPcm ? 1 : 0xFFFE));
    CHECK(wav.sampleFormat == (isInteger(format) ? 1 : 3));
    CHECK(wav.channels == channels);
    CHECK(wav.sampleRate == 88200u);
    CHECK(wav.bitsPerSample == bits);
    CHECK(wav.validBitsPerSample == bits);
    CHECK(wav.blockAlign == channels * bits / 8);
    CHECK(wav.byteRate == 88200u * wav.blockAlign);
    CHECK(wav.frameCount() == 1500);
    if (!plainPcm)
        CHECK(wav.channelMask == (channels == 1 ? 0x4u : channels == 2 ? 0x3u : 0u));
    CHECK(wav.hasFact == !isInteger(format));
    if (wav.hasFact)
        CHECK(wav.factSampleLength == 1500u);

    // RIFF size field matches the file.
    CHECK(readLe32(bytes, 4) + 8 == bytes.size());

    if (isInteger(format))
    {
        CHECK(wav.intSamples() == pcm.intSamples);
    }
    else if (format == SampleFormat::Float64)
    {
        CHECK(wav.floatSamples() == pcm.floatSamples);
    }
    else
    {
        const auto back = wav.floatSamples();
        REQUIRE(back.size() == pcm.floatSamples.size());
        for (std::size_t i = 0; i < back.size(); ++i)
            REQUIRE(back[i] == static_cast<double>(static_cast<float>(pcm.floatSamples[i])));
    }
}

TEST_CASE("WAV data chunk with odd size is padded", "[signalgen][wav]")
{
    const auto pcm = makeTestPcm(SampleFormat::Int24, 1, 3); // 9 data bytes
    const auto bytes = encodeWav(pcm);
    CHECK(bytes.size() % 2 == 0);
    CHECK(readLe32(bytes, 4) + 8 == bytes.size());

    const auto wav = ap::test::parseWav(bytes);
    CHECK(wav.data.size() == 9);
    CHECK(wav.intSamples() == pcm.intSamples);
}
