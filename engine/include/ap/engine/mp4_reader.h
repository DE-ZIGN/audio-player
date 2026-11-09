#pragma once

// MPEG-4 (.m4a / .mp4) audio track reader: finds the first sound track and
// returns where each encoded packet is, how many sample frames it decodes
// to, and the codec's configuration. Only the boxes needed for that are read;
// every size and count is checked against the box and the file that holds it.

#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ap::engine {

class Mp4FormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct Mp4Packet
{
    std::uint64_t offset = 0; // in the file
    std::uint32_t size = 0;   // bytes
    std::uint32_t frames = 0; // sample frames it decodes to (from the time-to-sample table)
};

struct Mp4AudioTrack
{
    std::string codec;                  // sample entry type: "alac", "mp4a", ...
    std::uint32_t timescale = 0;        // media timescale (ticks per second)
    std::uint16_t channels = 0;         // from the sample entry
    std::uint16_t sampleSize = 0;       // bits, from the sample entry
    std::uint32_t sampleRate = 0;       // from the sample entry (16.16, integer part)
    std::vector<std::uint8_t> config;   // codec configuration box payload ('alac': ALACSpecificConfig..., 'mp4a': esds)
    std::vector<Mp4Packet> packets;
    std::uint64_t totalFrames = 0;      // sum of packet frames
    // Edit list (first non-empty edit): frames to drop at the start and the
    // number of frames to play. Without an edit list: 0 and totalFrames.
    std::uint64_t editStart = 0;
    std::uint64_t editFrames = 0;
    bool hasEditList = false;

    // iTunes gapless information (udta/meta/ilst "----" item "iTunSMPB"):
    // encoder priming, padding and the original length, in output samples.
    struct ITunSmpb
    {
        std::uint64_t priming = 0;
        std::uint64_t padding = 0;
        std::uint64_t originalFrames = 0;
    };
    std::optional<ITunSmpb> itunSmpb;
};

// Parses the text of an iTunSMPB value (" 00000000 00000840 000001CA 00000000003F31F6 ...").
// Returns nothing if it is malformed.
std::optional<Mp4AudioTrack::ITunSmpb> parseITunSmpb(std::string_view text);

// Throws Mp4FormatError.
Mp4AudioTrack readMp4AudioTrack(const std::filesystem::path& path);

// True if the file starts like an ISO base media file ("ftyp" box first).
bool looksLikeMp4(const std::filesystem::path& path);

} // namespace ap::engine
