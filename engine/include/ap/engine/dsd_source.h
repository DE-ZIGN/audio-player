#pragma once

// DSD (1-bit) sources. All readers deliver the same canonical layout:
// bytes interleaved by channel (ch0, ch1, ..., ch0, ch1, ...), each byte
// holding 8 consecutive DSD bits with the earliest bit in the MSB (the DFF
// layout). Converting DSF (LSB-first bytes in 4096-byte blocks per channel)
// to this layout only reorders bits; nothing is lost or changed.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ap::engine {

class DsdFormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

enum class DsdContainer
{
    Dsf,
    Dff,
    Memory,
};

struct DsdFormat
{
    std::uint32_t dsdRate = 0; // bits per second per channel, e.g. 2822400 (DSD64)
    std::uint16_t channels = 0;
    std::uint32_t channelMask = 0;
    DsdContainer container = DsdContainer::Memory;

    bool operator==(const DsdFormat&) const = default;
};

// "DSD64" for 2822400, "DSD128" for 5644800, ... (also the 48 kHz family).
std::string dsdRateName(std::uint32_t dsdRate);

class DsdSource
{
public:
    virtual ~DsdSource() = default;
    virtual const DsdFormat& format() const noexcept = 0;
    // Bytes per channel (8 DSD bits each).
    virtual std::uint64_t totalBytes() const noexcept = 0;
    // Reads up to dst.size() / channels byte frames; returns byte frames read (0 at the end).
    virtual std::size_t read(std::span<std::byte> dst) = 0;
    virtual bool seek(std::uint64_t byteFrame) = 0;
    virtual std::uint64_t position() const noexcept = 0;
};

// Canonical-layout bytes held in memory (tests, generators).
class MemoryDsdSource final : public DsdSource
{
public:
    MemoryDsdSource(DsdFormat format, std::vector<std::byte> interleaved);
    const DsdFormat& format() const noexcept override { return format_; }
    std::uint64_t totalBytes() const noexcept override { return data_.size() / format_.channels; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t byteFrame) override;
    std::uint64_t position() const noexcept override { return position_; }

private:
    DsdFormat format_;
    std::vector<std::byte> data_;
    std::uint64_t position_ = 0;
};

// Sony DSF (DSD Stream File). Supports 1 bit per sample (LSB first) and
// 8 bits per sample (MSB first), any block size.
class DsfFileSource final : public DsdSource
{
public:
    explicit DsfFileSource(const std::filesystem::path& path); // throws DsdFormatError
    const DsdFormat& format() const noexcept override { return format_; }
    std::uint64_t totalBytes() const noexcept override { return totalBytes_; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t byteFrame) override;
    std::uint64_t position() const noexcept override { return position_; }

private:
    void loadGroup(std::uint64_t group);

    std::ifstream file_;
    DsdFormat format_;
    std::uint64_t dataOffset_ = 0;
    std::uint64_t totalBytes_ = 0;
    std::uint32_t blockSize_ = 4096;
    bool lsbFirst_ = true;
    std::uint64_t position_ = 0;
    std::uint64_t loadedGroup_ = UINT64_MAX;
    std::vector<std::byte> group_; // one block per channel
};

// Philips DSDIFF (.dff), uncompressed DSD only (DST is refused).
class DffFileSource final : public DsdSource
{
public:
    explicit DffFileSource(const std::filesystem::path& path); // throws DsdFormatError
    const DsdFormat& format() const noexcept override { return format_; }
    std::uint64_t totalBytes() const noexcept override { return totalBytes_; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t byteFrame) override;
    std::uint64_t position() const noexcept override { return position_; }

private:
    std::ifstream file_;
    DsdFormat format_;
    std::uint64_t dataOffset_ = 0;
    std::uint64_t totalBytes_ = 0;
    std::uint64_t position_ = 0;
};

// Opens .dsf or .dff by content. Throws DsdFormatError.
std::unique_ptr<DsdSource> openDsdFile(const std::filesystem::path& path);

// True if the file starts like a DSF or DFF file.
bool looksLikeDsdFile(const std::filesystem::path& path);

} // namespace ap::engine
