#pragma once

#include <ap/engine/pcm_format.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Lossless container repacking for integer PCM.
//
// Some devices accept 24-bit audio only in a 32-bit container. Moving a
// sample into a larger container shifts it left by whole bytes and fills the
// new low bytes with zeros. The value relative to full scale does not change
// and the original bytes can be recovered exactly, so this is not a sample
// format conversion. It is still reported to the user as part of the path.

namespace ap::engine {

enum class Repack
{
    None,           // bytes are sent as read
    WiderContainer, // same valid bits, larger container (24 -> 24-in-32)
    ZeroPadded,     // more valid bits declared, new low bits are zero (16 -> 24)
};

// Alternative device formats for src, most preferred first. src itself is not
// included. Empty for float formats.
std::vector<PcmFormat> repackCandidates(const PcmFormat& src);

// Repack kind needed to go from src to dst (None if equal). dst must be src
// or one of repackCandidates(src).
Repack classifyRepack(const PcmFormat& src, const PcmFormat& dst);

// Copies `samples` samples, left-justifying each into the wider slot.
// dstBytesPerSample must be >= srcBytesPerSample.
void repackSamples(std::span<const std::byte> src, std::uint32_t srcBytesPerSample,
                   std::span<std::byte> dst, std::uint32_t dstBytesPerSample, std::size_t samples);

} // namespace ap::engine
