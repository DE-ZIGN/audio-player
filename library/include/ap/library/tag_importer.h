#pragma once

// Reads the tags of the files the scan marked (files.needs_read) into the
// tag layer of their tracks (task 5-3). The edits and the external layer are
// not touched; a value the user edited still shows over the new tag.

#include <ap/library/library_db.h>
#include <ap/library/tag_reader.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace ap::library {

struct ImportReport
{
    std::size_t read = 0;
    std::size_t failed = 0;    // could not be read (recorded in files.read_error)
    std::size_t shiftJis = 0;  // decoded as Shift_JIS (detected or chosen)
    std::vector<std::string> errors; // "path: reason", English
};

class TagImporter
{
public:
    explicit TagImporter(LibraryDb& db) : db_(db) {}

    // Reads up to `limit` marked files that are present.
    ImportReport readPending(std::size_t limit = std::numeric_limits<std::size_t>::max());

private:
    LibraryDb& db_;
};

// The user's choice of character set for a file's undeclared text (none:
// detect). The file is marked to be read again.
void setTagCharset(LibraryDb& db, std::int64_t track, std::optional<LegacyCharset> charset);

} // namespace ap::library
