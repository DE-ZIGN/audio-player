#pragma once

// Minimal WAV reader used only by tests. Written independently of the
// generator's writer so that the two cross-check each other.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace ap::test {

struct WavFile
{
    std::uint16_t formatTag = 0;     // as stored: 1 = PCM, 3 = IEEE float, 0xFFFE = extensible
    std::uint16_t sampleFormat = 0;  // effective: 1 = PCM, 3 = IEEE float
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t byteRate = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint16_t validBitsPerSample = 0; // extensible only; otherwise = bitsPerSample
    std::uint32_t channelMask = 0;        // extensible only
    bool hasFact = false;
    std::uint32_t factSampleLength = 0;
    std::vector<std::uint8_t> data;

    std::size_t frameCount() const;
    std::vector<std::int32_t> intSamples() const;   // PCM only, sign-extended
    std::vector<double> floatSamples() const;       // IEEE float only
};

// Throws std::runtime_error on malformed input.
WavFile parseWav(std::span<const std::uint8_t> bytes);
WavFile readWavFile(const std::filesystem::path& path);

} // namespace ap::test
