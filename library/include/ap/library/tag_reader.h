#pragma once

// Reading the tags of a music file with TagLib (task 5-3). Only reads.
//
// Field names are TagLib's unified ones in lower case (title, artist, album,
// albumartist, composer, tracknumber, ...), the same names as the library's
// track_fields. Text in fields that declare no character set (ID3v1, ID3v2
// "ISO-8859-1" frames, RIFF INFO) is decoded by the file's detected character
// set (legacy_text.h), or by the user's choice for that file.

#include <ap/library/legacy_text.h>

#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ap::library {

struct TagReadResult
{
    std::map<std::string, std::vector<std::string>> fields; // UTF-8 values, in order
    std::string source; // where the tags were: "mpeg", "flac", "mp4", "wav", "aiff", "dsf", "dsdiff"
    // How text without a declared character set was decoded; empty when the
    // tags had no such text beyond ASCII.
    std::optional<LegacyCharset> legacyCharset;
    bool legacyOverridden = false; // the user's choice rather than detection
};

class TagReadError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Throws TagReadError when the file cannot be opened or is not a format TagLib reads.
TagReadResult readTags(const std::filesystem::path& file, std::optional<LegacyCharset> charsetOverride = {});

} // namespace ap::library
