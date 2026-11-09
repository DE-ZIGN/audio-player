#pragma once

#include <ap/engine/pcm_source.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace ap::engine {

class WavFormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Streams the data chunk of a RIFF/WAVE file (also RF64 / BW64 for files
// over 4 GiB) without touching sample values.
// Supports WAVE_FORMAT_PCM (8-bit excluded), WAVE_FORMAT_IEEE_FLOAT and
// WAVE_FORMAT_EXTENSIBLE with the PCM or IEEE_FLOAT subformat. Unknown chunks
// are skipped; chunk padding is honoured.
// Throws WavFormatError (message in English, for logs) on unsupported or
// malformed files.
class WavFileSource final : public PcmSource
{
public:
    explicit WavFileSource(const std::filesystem::path& path);

    const PcmFormat& format() const noexcept override { return format_; }
    std::uint64_t totalFrames() const noexcept override { return totalFrames_; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t frame) override;
    std::uint64_t position() const noexcept override { return framesRead_; }
    std::string containerName() const override { return "WAV"; }

    // Byte offset of the first sample in the file (for diagnostics).
    std::uint64_t dataOffset() const noexcept { return dataOffset_; }

private:
    std::ifstream file_;
    PcmFormat format_;
    std::uint64_t dataOffset_ = 0;
    std::uint64_t totalFrames_ = 0;
    std::uint64_t framesRead_ = 0;
};

} // namespace ap::engine
