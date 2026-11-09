#pragma once

// MP3 (MPEG-1/2/2.5 Layer III, also Layer I/II) decoded with libmpg123 (LGPL,
// a DLL) to 32-bit floating point, as CLAUDE.md requires for lossy formats:
// no rounding to integers here, values above full scale are kept (no clip).
// The decoder computes in 32-bit float (mpg123's portable C decoder, the same
// result on every CPU).
//
// Gapless: the encoder delay and padding recorded in the LAME / Xing tag are
// removed, so the source holds exactly the samples that were encoded. The
// length comes from scanning the whole file when it is opened. Seeks are
// sample-exact (mpg123 decodes the frames a seek target depends on first).
//
// Integer-only outputs cannot take this format unchanged; the processing path
// (M3) quantizes it with dither. Until then such outputs refuse it with the
// reason "float not supported".

#include <ap/engine/pcm_source.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>

struct mpg123_handle_struct;

namespace ap::engine {

class Mp3FormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class Mp3FileSource final : public PcmSource
{
public:
    // Throws Mp3FormatError (message in English, for logs).
    explicit Mp3FileSource(const std::filesystem::path& path);
    ~Mp3FileSource() override;
    Mp3FileSource(const Mp3FileSource&) = delete;
    Mp3FileSource& operator=(const Mp3FileSource&) = delete;

    const PcmFormat& format() const noexcept override { return format_; }
    std::uint64_t totalFrames() const noexcept override { return totalFrames_; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t frame) override;
    std::uint64_t position() const noexcept override { return position_; }
    std::string containerName() const override { return "MP3"; }

    // True if the file carries encoder delay / padding (LAME or Xing tag) that was removed.
    bool gaplessInfo() const noexcept { return gaplessInfo_; }

private:
    mpg123_handle_struct* h_ = nullptr;
    PcmFormat format_;
    std::uint64_t totalFrames_ = 0;
    std::uint64_t position_ = 0;
    bool gaplessInfo_ = false;
};

} // namespace ap::engine
