#pragma once

#include <ap/signalgen/pcm.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ap::signalgen {

// Encodes PCM as a RIFF/WAVE file image (little-endian).
//  - 16-bit integer with 1 or 2 channels: WAVE_FORMAT_PCM, 16-byte fmt chunk.
//  - Everything else: WAVE_FORMAT_EXTENSIBLE with the PCM or IEEE_FLOAT
//    subformat, valid bits = container bits, channel mask for mono/stereo
//    (0 = unspecified for other counts).
//  - Float formats also get a fact chunk (sample frames).
//  - The data chunk is padded to an even size.
// Throws std::length_error if the file would exceed the 4 GiB RIFF limit.
std::vector<std::uint8_t> encodeWav(const PcmData& pcm);

// encodeWav() and write to disk. Throws std::runtime_error on I/O failure.
void writeWav(const std::filesystem::path& path, const PcmData& pcm);

} // namespace ap::signalgen
