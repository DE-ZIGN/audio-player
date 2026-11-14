#pragma once

// Choosing the tube type by genre (task 9-7).
//
// The order: an album's choice (the user's, per album) > the genre table > the
// manual type (none chosen here). Off: always the manual type.
//
// The table: genre -> type, edited by the user; the default is a starting
// point, not a claim about what suits a genre (CLAUDE.md chapter 1). A track's
// genre tag values are split at ";", "/" and ","; each part is compared with
// the table ignoring case, full/half width and surrounding spaces. An exact
// match anywhere comes first (in the tag's order); then a table genre inside a
// part ("Vocal Jazz" has "jazz"), in the table's order.
//
// The stage then switches type at the track's first sample, with its usual
// 20 ms crossfade. The types are normalised to unity gain for small signals,
// so the level stays (measured in tests/unit/tube_auto_test.cpp).

#include <ap/engine/dsp/tube.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ap::engine {

struct TubeGenreRule
{
    std::string genre;
    TubeType type{};

    bool operator==(const TubeGenreRule&) const = default;
};

struct TubeAlbumChoice
{
    std::string albumKey; // as the library groups albums (album artist, 0x1F, album)
    std::string album;    // the album's name, for the page
    TubeType type{};

    bool operator==(const TubeAlbumChoice&) const = default;
};

inline constexpr std::size_t kMaxTubeGenreRules = 200;
inline constexpr std::size_t kMaxTubeAlbumChoices = 2000;
inline constexpr std::size_t kMaxTubeGenreLength = 100;

std::vector<TubeGenreRule> defaultTubeGenreRules();

struct TubeAutoSettings
{
    bool enabled = false;
    std::vector<TubeGenreRule> rules = defaultTubeGenreRules(); // in the user's order
    std::vector<TubeAlbumChoice> albums;

    bool operator==(const TubeAutoSettings&) const = default;
};

enum class TubeChoiceReason
{
    Album,
    Genre,
};

struct TubeChoice
{
    TubeType type{};
    TubeChoiceReason reason{};
    std::string matched; // the genre value as the tag has it, or the album's name
};

// None: the manual type plays.
std::optional<TubeChoice> chooseTubeType(const TubeAutoSettings& settings, std::span<const std::string> genres,
                                         std::string_view albumKey);

// How genres are compared: ASCII lower case, full-width ASCII as half-width,
// runs of spaces as one, no spaces at the ends.
std::string normalizedGenre(std::string_view genre);

} // namespace ap::engine
