#pragma once

// Impulse responses for the space stage (task 9-6): the built-in rooms,
// synthesized by algorithm, and the user's IR files.
//
// The built-in rooms (until IRs measured in real halls are chosen by
// listening, docs/ir-candidates.md, they are all synthesized):
//   - early reflections: the image-source method in a rectangular room (the
//     source in front of the listener, two receivers 18 cm apart), each
//     reflection attenuated by the walls and by distance;
//   - late reverberation: noise (fixed seed, a different one per channel) in
//     four bands, each decaying at its own reverberation time (RT60), faded in
//     where the reflections become dense, at the level where the early part
//     ends.
// No direct sound: the dry signal is the direct sound. Deterministic: the
// same room at the same rate is the same IR every time.
//
// Nothing here is claimed to sound like a particular hall; the rooms' numbers
// (size, RT60) are what is said.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ap::engine {

enum class SpaceRoom
{
    Off,
    Club,
    Hall,
    Arena,
};

// "off", "club", "hall", "arena": in the settings file, the status and the page.
const char* spaceRoomKey(SpaceRoom room) noexcept;
std::optional<SpaceRoom> spaceRoomFrom(std::string_view key) noexcept;

struct ImpulseResponse
{
    double sampleRate = 48000.0;
    std::vector<std::vector<double>> channels; // 1 (mono) or 2 (left, right)

    std::size_t frames() const noexcept { return channels.empty() ? 0 : channels.front().size(); }
    double seconds() const noexcept { return static_cast<double>(frames()) / sampleRate; }
};

// The built-in room's design numbers, for the display and the tests.
struct RoomDesign
{
    double length, width, height;  // m
    double rt60[4];                // s, for the bands below 250 Hz, 250 Hz-2 kHz, 2-6 kHz, above 6 kHz
    double mixingSeconds;          // where the late reverberation takes over
    double seconds;                // the IR's length
};
RoomDesign roomDesign(SpaceRoom room) noexcept; // Club, Hall or Arena

// A built-in room at `sampleRate`, stereo. Room must not be Off.
ImpulseResponse synthesizeRoom(SpaceRoom room, double sampleRate);

// Windowed-sinc resampling (Kaiser window, 32 zero crossings), for IRs; not
// for the playback thread.
std::vector<double> resampleIr(const std::vector<double>& x, double fromRate, double toRate);

// The longest IR used (longer files are cut, with a 10 ms fade-out).
inline constexpr double kMaxIrSeconds = 5.0;

struct LoadedIr
{
    ImpulseResponse ir;
    std::uint16_t fileChannels = 0; // as in the file (only the first two are used)
    bool truncated = false;         // longer than kMaxIrSeconds
    std::size_t trimmedFrames = 0;  // near-silence removed from the start
};

// Reads an IR file (WAV, FLAC, AIFF and the other PCM formats the player
// reads): mono or stereo (of more channels, the first two), any rate; the
// silence before the first arrival is removed (below -80 dB of the peak).
// Throws std::runtime_error with a message for the user (Japanese).
LoadedIr loadImpulseResponse(const std::filesystem::path& path);

} // namespace ap::engine
