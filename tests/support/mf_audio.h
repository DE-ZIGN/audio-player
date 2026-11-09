#pragma once

// Windows Media Foundation helpers for tests: an implementation independent
// of the player's own decoders, used to make test files and to cross-check
// decoded output.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ap::test {

struct MfPcm
{
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0; // container bits of the PCM bytes (16, 24, 32)
    std::vector<std::byte> data;     // interleaved little-endian integer PCM
};

// Encodes integer PCM to Apple Lossless in an MPEG-4 (.m4a) file with the
// Microsoft ALAC encoder. Throws std::runtime_error (with the HRESULT) when
// Media Foundation refuses the format.
void mfWriteAlacM4a(const std::filesystem::path& path, const MfPcm& pcm);

// AAC (192 kbit/s) in .m4a with the Microsoft AAC encoder: 16-bit, 44.1/48 kHz input.
void mfWriteAacM4a(const std::filesystem::path& path, const MfPcm& pcm);

// Decodes a file with Media Foundation's source reader to integer PCM of
// `bitsPerSample` container bits. Throws std::runtime_error.
MfPcm mfDecode(const std::filesystem::path& path, std::uint16_t bitsPerSample);

} // namespace ap::test
