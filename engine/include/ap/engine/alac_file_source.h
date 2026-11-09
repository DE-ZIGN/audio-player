#pragma once

// Apple Lossless (ALAC) in an MPEG-4 file (.m4a), decoded with Apple's
// reference decoder (third_party/alac, hardened: see VENDORED.md) into
// integer PCM. Sample values are not changed: 16/24/32-bit samples as they
// are, 20-bit samples left-justified in a 24-bit container (validBits = 20).
// ALAC carries no checksum; packets the decoder rejects are replaced by
// silence of the same length and counted in integrity().decodeErrors.

#include <ap/engine/mp4_reader.h>
#include <ap/engine/pcm_source.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

class ALACDecoder;

namespace ap::engine {

class AlacFormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class AlacFileSource final : public PcmSource
{
public:
    // Throws AlacFormatError or Mp4FormatError (messages in English, for logs).
    explicit AlacFileSource(const std::filesystem::path& path);
    ~AlacFileSource() override;
    AlacFileSource(const AlacFileSource&) = delete;
    AlacFileSource& operator=(const AlacFileSource&) = delete;

    const PcmFormat& format() const noexcept override { return format_; }
    std::uint64_t totalFrames() const noexcept override { return totalFrames_; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t frame) override;
    std::uint64_t position() const noexcept override { return position_; }
    std::string containerName() const override { return "ALAC"; }
    IntegrityCheck integrity() const override;

    // Bytes of zero padding after each packet given to the decoder (see VENDORED.md).
    static constexpr std::size_t kInputPadding = 256;

private:
    bool decodePacket(std::size_t index); // fills pending_ with the packet's frames

    std::ifstream file_;
    Mp4AudioTrack track_;
    std::unique_ptr<ALACDecoder> decoder_;
    PcmFormat format_;
    unsigned decodedBytesPerSample_ = 0; // as the decoder writes them: 2, 3, 3, 4
    std::uint32_t frameLength_ = 0;

    std::vector<std::uint64_t> packetStart_; // first frame of each packet (before the edit)
    std::uint64_t totalFrames_ = 0;          // after the edit list
    std::uint64_t position_ = 0;             // in played frames (after the edit)
    std::size_t nextPacket_ = 0;

    std::vector<std::uint8_t> input_;   // packet + padding
    std::vector<std::byte> decoded_;    // one packet as decoded
    std::vector<std::byte> pending_;    // frames not yet read
    std::size_t pendingOffset_ = 0;

    std::atomic<std::uint32_t> decodeErrors_{0};
};

} // namespace ap::engine
