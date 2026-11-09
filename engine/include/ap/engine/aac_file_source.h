#pragma once

// AAC in an MPEG-4 file (.m4a), decoded by Windows' AAC decoder (Media
// Foundation; CLAUDE.md: no AAC decoder is bundled) to 32-bit floating point,
// as for every lossy format. The container is read by mp4_reader; each packet
// is handed to the decoder directly, so trimming and seeking are decided here:
//
// - Gapless: the encoder's priming at the start and the padding at the end
//   are removed. The edit list (elst) is used when present, otherwise the
//   iTunes tag iTunSMPB. Without either, nothing tells what to remove and the
//   whole decoded stream is played.
// - Seeks: AAC frames overlap, so decoding restarts two packets before the
//   target and the output before the target is dropped.

#include <ap/engine/mp4_reader.h>
#include <ap/engine/pcm_source.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ap::engine {

class AacFormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class AacFileSource final : public PcmSource
{
public:
    // Throws AacFormatError or Mp4FormatError (messages in English, for logs).
    explicit AacFileSource(const std::filesystem::path& path);
    ~AacFileSource() override;
    AacFileSource(const AacFileSource&) = delete;
    AacFileSource& operator=(const AacFileSource&) = delete;

    const PcmFormat& format() const noexcept override { return format_; }
    std::uint64_t totalFrames() const noexcept override { return totalFrames_; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t frame) override;
    std::uint64_t position() const noexcept override { return position_; }
    std::string containerName() const override { return "AAC"; }
    IntegrityCheck integrity() const override;

    // Where the trimming came from: "elst", "iTunSMPB" or "" (none).
    const char* gaplessSource() const noexcept { return gaplessSource_; }

    struct Decoder; // the Media Foundation transform and its state

private:
    void startDecodingAt(std::size_t packet); // flushes the decoder and positions the reader
    bool fillPending();                       // decodes the next packet into pending_

    std::ifstream file_;
    Mp4AudioTrack track_;
    std::unique_ptr<Decoder> d_;
    PcmFormat format_;
    std::vector<std::uint64_t> packetStart_; // first output frame of each packet (decoded stream)
    std::uint64_t streamStart_ = 0;          // output frames dropped at the start (priming)
    std::uint64_t totalFrames_ = 0;          // after trimming
    std::uint64_t position_ = 0;             // in played frames
    std::size_t nextPacket_ = 0;
    std::uint64_t decodedFrame_ = 0;         // stream frame of the first frame in pending_
    bool inputEnded_ = false;
    std::vector<float> pending_;
    std::size_t pendingOffset_ = 0;          // floats already taken
    const char* gaplessSource_ = "";
    std::atomic<std::uint32_t> decodeErrors_{0};
};

} // namespace ap::engine
