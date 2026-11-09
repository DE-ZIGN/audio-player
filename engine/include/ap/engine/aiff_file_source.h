#pragma once

// AIFF and AIFC (uncompressed) files. AIFF stores samples big-endian; they
// are delivered little-endian (the byte order is reversed per sample, values
// do not change). Samples stay left-justified in their container, as AIFF
// stores them. Supported AIFC types: NONE, twos, in24, in32 (big-endian
// integer), sowt, 23ni, 42ni (little-endian integer), fl32/FL32, fl64/FL64
// (big-endian float). Compressed AIFC and 8-bit files are refused.

#include <ap/engine/pcm_source.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace ap::engine {

class AiffFormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class AiffFileSource final : public PcmSource
{
public:
    // Throws AiffFormatError (message in English, for logs).
    explicit AiffFileSource(const std::filesystem::path& path);

    const PcmFormat& format() const noexcept override { return format_; }
    std::uint64_t totalFrames() const noexcept override { return totalFrames_; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t frame) override;
    std::uint64_t position() const noexcept override { return framesRead_; }
    std::string containerName() const override { return aifc_ ? "AIFC" : "AIFF"; }

    // True when the file stores samples big-endian (they are byte-swapped on read).
    bool bigEndian() const noexcept { return bigEndian_; }

private:
    std::ifstream file_;
    PcmFormat format_;
    bool aifc_ = false;
    bool bigEndian_ = true;
    std::uint64_t dataOffset_ = 0;
    std::uint64_t totalFrames_ = 0;
    std::uint64_t framesRead_ = 0;
};

// Converts an IEEE 754 80-bit extended number (AIFF's sample-rate field, big-endian).
double readExtended80(const std::uint8_t* bytes) noexcept;

} // namespace ap::engine
