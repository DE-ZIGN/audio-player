#pragma once

// Album covers for the library screen (task 6-1). Music files are only read.
//
// A track's cover is the picture embedded in its file (the front cover if
// it says which, else the first), or else an image in its folder (cover,
// folder, front, album, albumart; .jpg, .jpeg or .png). For the screen it is
// scaled down once and kept as a JPEG in a cache folder; the cache file's name
// changes when the music file or the folder image changes, so a stale
// thumbnail is never shown. Scaling uses the Windows Imaging Component (WIC).

#include <ap/library/library_db.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ap::library {

struct Cover
{
    std::vector<unsigned char> bytes; // as stored (JPEG, PNG, ...)
    std::string source;               // "embedded" or the image file's path (UTF-8)
};

// Empty when the file has no picture and its folder has no cover image.
std::optional<Cover> findCover(const std::filesystem::path& musicFile);

class ImageError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct Thumbnail
{
    std::vector<unsigned char> jpeg;
    int width = 0;
    int height = 0;
};

// Scales an image (anything WIC decodes) to fit in maxSide x maxSide (never
// up), as a JPEG. Throws ImageError when the image cannot be decoded.
Thumbnail makeThumbnail(std::span<const unsigned char> image, int maxSide);

// The size of an image, for tests and checks. Throws ImageError.
std::pair<int, int> imageSize(std::span<const unsigned char> image);

inline constexpr int kCoverThumbnailSide = 320;

// The cached thumbnail of a track's cover in `cacheFolder`, made when it is
// missing. Empty when the track has no cover (that is remembered too, with an
// empty marker file, until the music file or its folder changes).
std::optional<std::filesystem::path> coverThumbnail(LibraryDb& db, std::int64_t track,
                                                    const std::filesystem::path& cacheFolder,
                                                    int maxSide = kCoverThumbnailSide);

} // namespace ap::library
