#pragma once

// FLAC files for tests and device checks, encoded with libFLAC.

#include <ap/signalgen/pcm.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ap::signalgen {

struct FlacOptions
{
    bool storeMd5 = true;           // the MD5 of the samples in STREAMINFO (all zeros when false)
    unsigned compressionLevel = 5;  // 0..8
    unsigned seekPointSeconds = 1;  // a seek table entry every N seconds (0 = no seek table)
};

// Encodes interleaved integer samples (each within `bitsPerSample` bits,
// right-aligned, 4..32 bits) and writes a .flac file. The encoder verifies
// its own output. Throws std::runtime_error on failure.
void writeFlac(const std::filesystem::path& path, std::uint32_t sampleRate, std::uint16_t channels,
               unsigned bitsPerSample, const std::vector<std::int32_t>& interleaved, const FlacOptions& options = {});

// Integer PcmData (16, 24 or 32 bit). Throws std::invalid_argument for float data.
void writeFlac(const std::filesystem::path& path, const PcmData& pcm, const FlacOptions& options = {});

} // namespace ap::signalgen
