#pragma once

// Japanese-aware substring search over the library (task 5-4, CLAUDE.md
// chapter 7).
//
// Text is put in one form before it is indexed and before it is looked for,
// so these find each other:
//   - Unicode NFKC: full-width ASCII to half-width (ＡＢＣ → ABC), half-width
//     katakana to full-width (ｶﾞ → ガ), ① → 1
//   - lower case, accents removed (Björk → bjork)
//   - katakana to hiragana (スキャット → すきゃっと)
//   - runs of white space to one space
//
// The index is SQLite FTS5 with the trigram tokenizer: a word of 3 or more
// characters is found through it. Shorter words (one kanji is a common
// Japanese search) cannot be, so they are looked for in the indexed text
// directly; when the search also has a longer word, the index narrows the
// tracks first. All words must be found (AND), each within one field group:
// title; artists (artist, album artist, composer, conductor, ensemble, ...);
// album (album, work, movement); other (genre); and the file name, so a track
// without tags can be found.

#include <ap/library/library_db.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ap::library {

// The form text is indexed and searched in. Input and output are UTF-8.
std::string normalizeForSearch(std::string_view text);

// Changes whenever normalizeForSearch() or the field groups change: the index
// is then built again when the database is opened.
inline constexpr int kSearchIndexVersion = 1;

// Track ids, best matches first (words of 3+ characters), at most `limit`.
std::vector<std::int64_t> searchTracks(LibraryDb& db, std::string_view query, std::size_t limit = 1000);

} // namespace ap::library
