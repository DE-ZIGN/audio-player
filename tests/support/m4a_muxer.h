#pragma once

// A minimal MPEG-4 audio muxer for tests: writes given packets with a given
// sample entry, optionally with an edit list and an iTunes gapless tag
// (iTunSMPB), so the reader's handling of both can be checked.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ap::test {

struct M4aTrack
{
    std::uint32_t timescale = 0;           // media timescale (= movie timescale here)
    std::vector<std::uint8_t> sampleEntry; // the whole sample entry box, e.g. 'mp4a' with its 'esds'
    std::vector<std::vector<std::uint8_t>> packets;
    std::vector<std::uint32_t> packetFrames; // per packet, in the timescale
    std::uint64_t editStart = 0;  // edit list: media time to start at...
    std::uint64_t editFrames = 0; // ...and duration (0 = no edit list)
    std::string itunSmpb;         // iTunSMPB text (empty = none)
};

void writeM4a(const std::filesystem::path& path, const M4aTrack& track);

// Copies the packets and sample entry of an AAC .m4a into a new file with the
// given edit list and/or iTunSMPB (the original file's own boxes are dropped).
void rewriteAacM4a(const std::filesystem::path& from, const std::filesystem::path& to, std::uint64_t editStart,
                   std::uint64_t editFrames, const std::string& itunSmpb);

// " 00000000 PPPPPPPP QQQQQQQQ LLLLLLLLLLLLLLLL" (priming, padding, length), as iTunes writes it.
std::string makeITunSmpb(std::uint64_t priming, std::uint64_t padding, std::uint64_t length);

} // namespace ap::test
