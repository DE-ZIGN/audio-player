#pragma once

// What the library screen asks for (task 6-1): pages of tracks and albums,
// sorted and searched, and the watched folders. Reads the shown values kept
// in track_display (search.cpp keeps them with the search index) and the
// measured values (task 5-6).

#include <ap/library/library_db.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ap::library {

struct TrackRow
{
    std::int64_t id = 0;
    std::string title; // the file name when there is no tag
    std::string artist;
    std::string album;
    std::optional<std::string> albumKey;
    std::optional<std::int64_t> trackNo, discNo;
    std::optional<double> seconds;
    std::string container; // "FLAC", "MP3", ... (empty: not read yet)
    std::optional<std::uint32_t> sampleRate;
    std::optional<std::uint32_t> dsdRate;
    std::optional<std::uint16_t> bits;
    // The measurement: "ok" (truePeakDb set), "pending" (not measured yet),
    // "not_pcm" (DSD), "failed" (could not be decoded).
    std::string analysis;
    std::optional<double> truePeakDb;
    std::optional<double> loudnessLufs;
    std::int64_t plays = 0;
    std::optional<int> rating;
    bool present = true;
    std::filesystem::path path;
};

enum class TrackSort
{
    Artist, // artist, album, disc, track
    Album,  // album, disc, track
    Title,
    Length,
    Format,
    Rate,
    TruePeak,
    Plays,
    Added,  // when added (the screen asks for descending: newest first)
};

struct TrackQuery
{
    std::string search;     // search.h; empty: all
    std::optional<std::string> albumKey; // only this album, in disc and track order
    TrackSort sort = TrackSort::Artist;
    bool descending = false;
    std::size_t offset = 0;
    std::size_t limit = 200;
};

struct TrackPage
{
    std::vector<TrackRow> rows;
    std::size_t total = 0; // rows matching, before offset and limit
};

TrackPage listTracks(LibraryDb& db, const TrackQuery& query);

struct AlbumRow
{
    std::string key;
    std::string album;
    std::string artist; // the album artist (or the first track's artist)
    std::optional<std::string> year;
    std::size_t tracks = 0;
    std::optional<double> seconds;   // all tracks, when every length is known
    std::int64_t coverTrack = 0;     // the first track, whose file (or folder) gives the cover
    std::optional<double> truePeakDb; // the album's (measured)
    std::string container;           // when all tracks share one ("FLAC"); else empty
    std::optional<std::uint32_t> sampleRate; // when all tracks share one
};

enum class AlbumSort
{
    Artist, // artist, album
    Album,
    Year,
    Added, // when added (descending: newest first)
};

struct AlbumQuery
{
    std::string search; // albums with a matching track
    AlbumSort sort = AlbumSort::Artist;
    bool descending = false;
    std::size_t offset = 0;
    std::size_t limit = 200;
};

struct AlbumPage
{
    std::vector<AlbumRow> rows;
    std::size_t total = 0;
};

AlbumPage listAlbums(LibraryDb& db, const AlbumQuery& query);

struct FolderRow
{
    std::int64_t id = 0;
    std::filesystem::path path;
    std::size_t tracks = 0;  // present
    std::size_t missing = 0; // not found
};

std::vector<FolderRow> listFolders(LibraryDb& db);

// Takes a watched folder out of the library with its tracks (and their
// history). The files are not touched.
void removeFolder(LibraryDb& db, std::int64_t folderId);

struct LibraryCounts
{
    std::size_t tracks = 0;   // present
    std::size_t missing = 0;
    std::size_t albums = 0;
    std::size_t toMeasure = 0; // waiting for the analysis
};

LibraryCounts libraryCounts(LibraryDb& db);

} // namespace ap::library
