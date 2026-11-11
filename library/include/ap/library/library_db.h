#pragma once

// The library database (task 5-1, CLAUDE.md chapter 7).
//
// - Music files are only read. What the application learns or the user
//   changes is kept here, never written back to the files (except when the
//   user explicitly asks, later).
// - Displayed values come in three layers, per field: the user's edit >
//   an external database (MusicBrainz, later) > the file's tag. The highest
//   layer that has the field wins; a user edit can also clear a field.
// - A track (a piece of audio) is separate from the file it is in, so its play
//   history, rating and edits survive renames and moves (task 5-5).
// - Measured values (True Peak, loudness) and, in phase 2, file provenance
//   findings (quality-measures.md 32: reported with their evidence, never
//   fixed automatically) are kept per track.
//
// The schema itself is in library_schema.cpp (migration 1).

#include <ap/library/migration.h>
#include <ap/library/sqlite.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ap::library {

// The library's migrations, oldest first.
std::span<const Migration> libraryMigrations();
int librarySchemaVersion();

enum class Layer : int
{
    Tag = 0,      // read from the file
    External = 1, // from an external database
    User = 2,     // edited by the user
};

// Where a file is and how to recognise it again.
struct FileFacts
{
    std::filesystem::path path;
    std::uint64_t size = 0;
    std::int64_t modifiedAt = 0; // unix ms
    // NTFS identity: the volume serial and the 128-bit file ID survive renames
    // and moves within the volume. Not available on some drives (NAS).
    std::optional<std::uint64_t> volumeSerial;
    std::optional<std::array<std::byte, 16>> ntfsFileId;
};

class LibraryDb
{
public:
    // Opens or creates the database and brings it up to date. An existing
    // database is copied to "<file>.v<old version>.bak" before it is changed.
    // Throws NewerSchemaError when it was made by a newer application.
    static LibraryDb open(const std::filesystem::path& file);
    static LibraryDb openInMemory();

    Database& database() noexcept { return db_; }
    int schemaVersion() { return db_.userVersion(); }
    // What open() did to bring the database up to date.
    const MigrationResult& migration() const noexcept { return migration_; }

    std::int64_t addRoot(const std::filesystem::path& folder);
    // A file and the track it holds (the whole file). Returns the track id.
    std::int64_t addTrack(const FileFacts& file, std::optional<std::int64_t> rootId = {});

    // Replaces one layer's values of one field (several values: e.g. two
    // artists, in order). An empty list removes that layer's values.
    void setValues(std::int64_t track, Layer layer, std::string_view field, std::span<const std::string> values,
                   std::string_view source = {});
    // The user's edit: the field shows nothing, whatever the lower layers have.
    void clearByUser(std::int64_t track, std::string_view field);
    // Takes one layer's values away (e.g. undo the user's edit).
    void removeValues(std::int64_t track, Layer layer, std::string_view field);

    // What is shown, and which layer it comes from (none: the field is empty).
    std::vector<std::string> values(std::int64_t track, std::string_view field);
    std::optional<Layer> shownLayer(std::int64_t track, std::string_view field);

private:
    LibraryDb(Database db, MigrationResult m) : db_(std::move(db)), migration_(std::move(m)) {}

    Database db_;
    MigrationResult migration_;
};

std::int64_t nowUnixMs();

} // namespace ap::library
