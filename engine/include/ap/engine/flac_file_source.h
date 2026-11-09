#pragma once

// FLAC files decoded with libFLAC into integer PCM. Sample values are not
// changed: a sample of N bits goes into the smallest 16/24/32-bit container
// that holds it, left-justified (as in WAV and WASAPI), with validBits = N.
//
// The MD5 that FLAC stores for its samples (STREAMINFO) is checked when the
// file is decoded from the first sample to the last without a seek
// elsewhere. The result is reported by integrity(); decode errors in the
// file (lost sync, CRC mismatch, ...) are counted there too.

#include <ap/engine/md5.h>
#include <ap/engine/pcm_source.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ap::engine {

class FlacFormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class FlacFileSource final : public PcmSource
{
public:
    // Throws FlacFormatError (message in English, for logs).
    explicit FlacFileSource(const std::filesystem::path& path);
    ~FlacFileSource() override;
    FlacFileSource(const FlacFileSource&) = delete;
    FlacFileSource& operator=(const FlacFileSource&) = delete;

    const PcmFormat& format() const noexcept override { return format_; }
    std::uint64_t totalFrames() const noexcept override { return totalFrames_; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t frame) override;
    std::uint64_t position() const noexcept override { return position_; }
    std::string containerName() const override { return "FLAC"; }
    IntegrityCheck integrity() const override;

    // Bits per sample as stored in the file (= format().validBits).
    unsigned bitsPerSample() const noexcept { return bitsPerSample_; }

    struct Decoder; // libFLAC callbacks need access

private:
    void finishVerification();

    std::unique_ptr<Decoder> d_;
    PcmFormat format_;
    unsigned bitsPerSample_ = 0;
    std::uint64_t totalFrames_ = 0;
    std::uint64_t position_ = 0;

    std::vector<std::byte> pending_; // decoded frames not yet read
    std::size_t pendingOffset_ = 0;  // bytes of pending_ already read
    bool endOfStream_ = false;

    // Verification: MD5 over the samples in FLAC's byte layout, in decode order.
    Md5Digest storedMd5_{};
    std::unique_ptr<Md5> md5_; // set while a check from the start is running
    std::uint64_t md5Frames_ = 0;
    std::vector<std::byte> md5Scratch_;
    std::atomic<IntegrityState> state_{IntegrityState::NotAvailable};
    std::atomic<std::uint32_t> decodeErrors_{0};

    friend struct Decoder;
};

} // namespace ap::engine
