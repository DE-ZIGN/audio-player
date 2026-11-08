#pragma once

#include <cstdint>
#include <string>

namespace ap::engine {

enum class SampleType
{
    Int,
    Float,
};

// Layout of interleaved little-endian PCM as it travels from source to device.
struct PcmFormat
{
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::uint16_t containerBits = 0; // bits per sample slot: 16, 24, 32 (64 for double)
    std::uint16_t validBits = 0;     // meaningful bits, left-justified in the container
    SampleType type = SampleType::Int;
    std::uint32_t channelMask = 0;   // WAVE_FORMAT_EXTENSIBLE speaker mask; 0 = unspecified

    std::uint32_t bytesPerSample() const noexcept { return containerBits / 8u; }
    std::uint32_t bytesPerFrame() const noexcept { return bytesPerSample() * channels; }

    // True when the format has sensible, supported field values.
    bool isValid() const noexcept;

    bool operator==(const PcmFormat&) const = default;
};

// Human-readable summary in Japanese, e.g. "44.1 kHz / 24bit（32bit コンテナ）/ 2ch".
std::string describe(const PcmFormat& format);

} // namespace ap::engine
