#pragma once

// Mapping between this project's interleaved PCM layouts and ASIO's
// per-channel sample types. Only value-preserving mappings exist here:
// copying, left-justifying into a wider slot (as for WASAPI containers) or
// sign-extending into a right-aligned 32-bit slot. Integer to float (or the
// other way) is a conversion and is never offered.
//
// Sample type codes are the ASIOSampleType values from asio.h, so this
// header does not need the SDK.

#include <ap/engine/pcm_format.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ap::engine::asio {

enum SampleTypeCode : long
{
    Int16LSB = 16,
    Int24LSB = 17,
    Int32LSB = 18,
    Float32LSB = 19,
    Float64LSB = 20,
    Int32LSB16 = 24, // 16 bits right-aligned in 32
    Int32LSB18 = 25,
    Int32LSB20 = 26,
    Int32LSB24 = 27, // 24 bits right-aligned in 32
};

// Bytes per sample in the driver's buffers; 0 if the type is not supported here.
std::size_t bytesPerSample(long sampleType) noexcept;

// True if interleaved PCM in `format` can be written to channels of
// `sampleType` without changing any sample value.
bool accepts(long sampleType, const PcmFormat& format) noexcept;

// Writes `frames` frames of interleaved `src` (layout `format`, which must be
// accepted) into the per-channel buffers `channels` (one pointer per channel,
// each with room for `frames` samples of `sampleType`).
void deinterleave(long sampleType, const PcmFormat& format, std::span<const std::byte> src, std::size_t frames,
                  std::span<void* const> channels);

// True for the types that hold a narrower sample in the low bytes of a 32-bit
// slot (Int32LSB16, Int32LSB24): samples are sign-extended into them.
bool rightAligned(long sampleType) noexcept;

// "Int32LSB" etc., for logs and the UI.
std::string sampleTypeName(long sampleType);

} // namespace ap::engine::asio
