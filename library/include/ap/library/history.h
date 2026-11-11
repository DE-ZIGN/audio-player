#pragma once

// Following files that moved, and what the user did with a track (task 5-5).
//
// Moves. Within one NTFS volume a moved file keeps its file ID, and the scan
// follows it (task 5-2). To another drive, a NAS, or on a FAT/exFAT drive it
// does not: the scan sees one file missing and a new one added. Each track
// therefore gets a fingerprint when its tags are read:
//   - FLAC: the MD5 of the audio that the file's STREAMINFO holds (it does not
//     change when the tags are edited) — "flac-md5:<hex>"
//   - otherwise: SHA-256 of the size, the first 256 KB and the last 256 KB
//     (reading whole files over a network would take too long) — "sha256-ht:<hex>"
// relinkMovedTracks() then gives a missing track the new file when exactly one
// new track has the same fingerprint, and that new track has nothing of its
// own yet (no plays, rating or edits). The order the two places were scanned
// in does not matter. A copy (the original still there) is not a move.
//
// Plays. Every play is recorded; it counts as a play when at least half of the
// track or 4 minutes were heard (the usual rule).

#include <ap/library/library_db.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ap::library {

// The fingerprint of a music file (see above). Empty when it cannot be read.
std::optional<std::string> fingerprintOf(const std::filesystem::path& file);

struct Relink
{
    std::int64_t track = 0; // the track that keeps its history
    std::filesystem::path from, to;
};

// Gives missing tracks the new files they moved to (see above).
std::vector<Relink> relinkMovedTracks(LibraryDb& db);

inline constexpr double kPlayCountsAfterSeconds = 240.0;

struct PlayRecord
{
    std::int64_t startedAt = 0;    // unix ms
    std::uint64_t playedFrames = 0; // heard (a seek does not add what was skipped)
    std::uint64_t trackFrames = 0;  // the track's length; 0: unknown (then only 4 minutes count)
    std::uint32_t sampleRate = 0;
    bool reachedEnd = false;        // played up to the end
};

// Records one play; returns whether it counts as a play.
bool recordPlay(LibraryDb& db, std::int64_t track, const PlayRecord& play);

// Turns what the player reports, looked at a few times a second, into plays
// (task 6-2). Only what was heard counts: the position must move on while
// playing, and by no more than the time that passed (a seek, forward or back,
// is not heard). A pause and a resume of the same track are one play. When
// the next track starts (gaplessly or not), the end of the previous one is
// counted up to its end if the time allows it, and that play is given out.
// Track 0 means a track that is not in the library (not counted).
class PlayCounter
{
public:
    struct Observation
    {
        std::int64_t track = 0;          // library track id; 0: none
        std::uint64_t position = 0;      // frames into the track
        std::uint64_t totalFrames = 0;
        std::uint32_t sampleRate = 0;
        bool playing = false;
        std::int64_t now = 0;            // unix ms
    };

    // The plays that ended with this observation (the previous track's).
    std::vector<std::pair<std::int64_t, PlayRecord>> observe(const Observation& o);
    // The play going on now, if anything of it was heard (the app closes, the queue is replaced).
    std::vector<std::pair<std::int64_t, PlayRecord>> finish();

private:
    std::optional<Observation> last_;
    PlayRecord current_;
};

struct TrackStats
{
    std::int64_t playCount = 0;            // plays that count
    std::optional<std::int64_t> lastPlayedAt; // unix ms, of any play
    std::optional<int> rating;             // 0..100
};

TrackStats trackStats(LibraryDb& db, std::int64_t track);

// 0..100, or none (not rated). Throws std::out_of_range otherwise.
void setRating(LibraryDb& db, std::int64_t track, std::optional<int> rating);

} // namespace ap::library
