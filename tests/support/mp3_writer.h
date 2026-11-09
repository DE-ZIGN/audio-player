#pragma once

// MP3 test files encoded with LAME (test-only). With the LAME tag, the file
// records the encoder delay and the padding, so a gapless decoder can return
// exactly the samples that were encoded.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ap::test {

struct Mp3Options
{
    int bitrateKbps = 320; // CBR bit rate
    bool vbr = false;      // VBR (quality below) instead of CBR
    float vbrQuality = 0.0f;
    bool lameTag = true;   // write the Xing/LAME tag (delay, padding, length)
};

struct Mp3Info
{
    int encoderDelay = 0;   // samples LAME put before the signal
    int encoderPadding = 0; // samples after it, to fill the last frame
};

// `interleaved`: float samples (1.0 = full scale). Throws std::runtime_error.
Mp3Info writeMp3(const std::filesystem::path& path, std::uint32_t sampleRate, std::uint16_t channels,
                 const std::vector<float>& interleaved, const Mp3Options& options = {});

} // namespace ap::test
