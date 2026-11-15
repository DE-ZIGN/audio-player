#pragma once

// Impulse responses for the space stage (task 9-6). Each SPACE button plays one of:
//   - measured: an IR measured in a real hall, bundled (HALL only: St Paul's
//     Hall, University of Huddersfield, from the MAIR library by Hyunkook Lee
//     and Connor Millns, CC BY 4.0; chosen by listening, docs/ir-candidates.md);
//   - synthesized: made here by algorithm (every room);
//   - the user's file.
// The synthesized rooms:
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
#include <memory>
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

// Which IR a room plays.
enum class SpaceIrSource
{
    Measured,    // the bundled measured IR (only where hasMeasuredIr)
    Synthesized, // made by algorithm
    File,        // the user's file
};
// "measured", "synth", "file".
const char* spaceIrSourceKey(SpaceIrSource source) noexcept;
std::optional<SpaceIrSource> spaceIrSourceFrom(std::string_view key) noexcept;
bool hasMeasuredIr(SpaceRoom room) noexcept;                 // HALL
SpaceIrSource defaultSpaceIrSource(SpaceRoom room) noexcept; // measured where there is one, else synthesized

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

// The bundled measured IR of a room (read once from the engine's own data;
// thread-safe), or nullptr where there is none. HALL: St Paul's Hall (MAIR,
// AB pair, source S01 at the front centre of the stage; 44.1 kHz stereo,
// 32-bit float, cut 1 ms before the direct sound).
std::shared_ptr<const ImpulseResponse> measuredRoom(SpaceRoom room);

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
