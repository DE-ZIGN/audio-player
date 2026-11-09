#pragma once

// Writes ALAC .m4a files the way Apple's tools do: Apple's reference encoder
// (test-only copy in third_party/alac) and a minimal MPEG-4 muxer. The last
// packet holds only the remaining frames (a "partial frame"), and the sample
// table records its real length, so the file keeps its exact length.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace ap::test {

struct AppleAlacOptions
{
    std::uint32_t frameSize = 4096;
    // Edit list: drop `editStart` frames and play `editFrames` (0 = no edit list).
    std::uint64_t editStart = 0;
    std::uint64_t editFrames = 0;
};

// `pcm`: interleaved little-endian integer samples, 2 bytes for 16-bit,
// 3 bytes for 20-bit (left-justified, low 4 bits zero) and 24-bit, 4 bytes
// for 32-bit. Throws std::runtime_error.
void writeAppleAlacM4a(const std::filesystem::path& path, std::uint32_t sampleRate, std::uint16_t channels,
                       std::uint16_t bitDepth, const std::vector<std::byte>& pcm, const AppleAlacOptions& options = {});

} // namespace ap::test
