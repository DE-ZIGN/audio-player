#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ap::signalgen {

enum class SampleFormat
{
    Int16,
    Int24,
    Int32,
    Float32,
    Float64,
};

bool isInteger(SampleFormat format) noexcept;

// Container size in bits: 16, 24, 32, 32, 64.
int bitsPerSample(SampleFormat format) noexcept;

// "16bit", "24bit", "32bit", "f32", "f64" (used in file names).
std::string_view formatName(SampleFormat format) noexcept;

// Accepts "16", "16bit", "24", "24bit", "32", "32bit", "f32", "float32", "f64", "float64".
std::optional<SampleFormat> parseFormat(std::string_view text) noexcept;

// Largest positive integer code, 2^(N-1) - 1. Zero for float formats.
std::int32_t maxCode(SampleFormat format) noexcept;

// Most negative integer code, -2^(N-1). Zero for float formats.
std::int32_t minCode(SampleFormat format) noexcept;

struct PcmData
{
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    SampleFormat format = SampleFormat::Int16;
    std::vector<std::int32_t> intSamples; // interleaved; used when isInteger(format)
    std::vector<double> floatSamples;     // interleaved; used otherwise

    std::size_t frameCount() const noexcept;
};

// Converts normalized samples (1.0 = full scale) to PCM.
//  - Integer: code = round-half-away-from-zero(x * maxCode), clamped to
//    [minCode, maxCode]. No dither: the output is fully deterministic.
//  - Float: values are copied unchanged (Float32 is narrowed when written).
// Throws std::invalid_argument on a channel/size mismatch or non-finite input.
PcmData quantize(const std::vector<double>& normalized,
                 std::uint32_t sampleRate,
                 std::uint16_t channels,
                 SampleFormat format);

} // namespace ap::signalgen
