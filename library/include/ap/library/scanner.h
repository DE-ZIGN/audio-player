#pragma once

// Comparing watched folders with the library database (task 5-2).
//
// A scan lists the music files under a folder (the extensions the engine
// opens) and brings the database in line with them:
//   - added:    a file the database does not have (a new track, to be read)
//   - changed:  its size or last write time differs (to be read again)
//   - missing:  in the database, no longer on disk. The row is kept (marked
//               not present), so the track's edits, rating and plays stay.
//   - returned: marked missing before, found again at the same place
//   - moved:    found at a new path with the NTFS identity (volume serial and
//               file ID) of a known file: renamed or moved within the volume.
//               The same track, at its new path.
// The files themselves are only read (their directory entries, and their
// identity for files the database does not know by path).
//
// "To be read" is files.needs_read; reading the tags is task 5-3.

#include <ap/library/library_db.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ap::library {

// The extensions the engine opens (engine/src/audio_file.cpp), lower case.
bool isMusicFile(const std::filesystem::path& p);

// Size, last write time and NTFS identity of one file (as FileFacts).
// Empty when it cannot be read (gone, no access).
std::optional<FileFacts> statFile(const std::filesystem::path& p);

struct ScanReport
{
    std::vector<std::filesystem::path> added;
    std::vector<std::filesystem::path> changed;
    std::vector<std::filesystem::path> missing;
    std::vector<std::filesystem::path> returned;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved; // from, to
    std::size_t unchanged = 0;
    std::size_t unreadable = 0; // listed but could not be looked at (no access); left as they were

    bool empty() const noexcept
    {
        return added.empty() && changed.empty() && missing.empty() && returned.empty() && moved.empty();
    }
    void append(const ScanReport& other);
};

class Scanner
{
public:
    explicit Scanner(LibraryDb& db) : db_(db) {}

    // Everything under the root folder (the startup scan, and after an overflow).
    ScanReport scanRoot(std::int64_t rootId);

    // One file or folder under the root, whatever it is now: a file, a folder
    // (everything under it), or nothing (what the database had there is missing).
    ScanReport scanPath(std::int64_t rootId, const std::filesystem::path& path);

private:
    ScanReport reconcile(std::int64_t rootId, const std::filesystem::path& path);

    LibraryDb& db_;
};

// The root folder's path as stored.
std::optional<std::filesystem::path> rootPath(LibraryDb& db, std::int64_t rootId);

} // namespace ap::library
