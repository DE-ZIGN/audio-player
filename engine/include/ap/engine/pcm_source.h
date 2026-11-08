#pragma once

#include <ap/engine/pcm_format.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ap::engine {

// Read-side dropout counters (see PrefetchingSource).
struct SourceHealth
{
    std::uint32_t starvations = 0;    // reads that could not be filled in time
    std::uint64_t starvedFrames = 0;  // frames replaced by silence because of that
    std::uint64_t seekFillFrames = 0; // silence while refilling after a seek (not a dropout)
};

// Set when the PCM stream carries DSD packed as DoP (DSD over PCM).
struct DopInfo
{
    std::uint32_t dsdRate = 0;   // e.g. 2822400 for DSD64
    std::string container;       // "DSF" or "DFF"
};

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

    // Moves the read position to `frame` (clamped to totalFrames()).
    // Returns false if the source cannot seek.
    virtual bool seek(std::uint64_t frame) = 0;

    // Current read position in frames.
    virtual std::uint64_t position() const noexcept = 0;

    // Dropout counters; plain sources never starve.
    virtual SourceHealth health() const noexcept { return {}; }

    // Blocks until data at the current position can be read without filling
    // (e.g. after a seek while not playing). Not for the playback thread.
    virtual void waitUntilReady() {}

    // Set when the samples are DoP; such streams must never be processed.
    virtual std::optional<DopInfo> dop() const { return std::nullopt; }

    // The file type the samples are read from ("WAV", "DSF", "DFF"); empty
    // for sources that are not read from a file.
    virtual std::string containerName() const { return {}; }
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
    bool seek(std::uint64_t frame) override;
    std::uint64_t position() const noexcept override;

    const std::vector<std::byte>& data() const noexcept { return data_; }

private:
    PcmFormat format_;
    std::vector<std::byte> data_;
    std::size_t position_ = 0;
};

} // namespace ap::engine
