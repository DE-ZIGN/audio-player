#pragma once

#include <ap/engine/pcm_format.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ap::engine {

// A source of interleaved PCM in its original byte layout. Sources never
// convert sample values: the bytes they deliver are the bytes of the file.
class PcmSource
{
public:
    virtual ~PcmSource() = default;

    virtual const PcmFormat& format() const noexcept = 0;
    virtual std::uint64_t totalFrames() const noexcept = 0;

    // Copies up to dst.size() / bytesPerFrame() frames into dst and returns
    // the number of frames copied. Returns 0 at end of stream.
    virtual std::size_t read(std::span<std::byte> dst) = 0;
};

// Holds all PCM bytes in memory.
class MemoryPcmSource final : public PcmSource
{
public:
    MemoryPcmSource(PcmFormat format, std::vector<std::byte> data);

    // Reads everything remaining from another source.
    static MemoryPcmSource loadAll(PcmSource& source);

    const PcmFormat& format() const noexcept override { return format_; }
    std::uint64_t totalFrames() const noexcept override;
    std::size_t read(std::span<std::byte> dst) override;

    const std::vector<std::byte>& data() const noexcept { return data_; }

private:
    PcmFormat format_;
    std::vector<std::byte> data_;
    std::size_t position_ = 0;
};

} // namespace ap::engine
