#pragma once

// Text in tag fields that declare no character set (task 5-3, CLAUDE.md
// chapter 7): ID3v1, ID3v2 frames marked ISO-8859-1, RIFF INFO chunks. The
// specs say ISO-8859-1, but the bytes are whatever the writing program's
// system code page was: Shift_JIS (CP932) on Japanese Windows, Windows-1252
// in western Europe, sometimes UTF-8.
//
// One file is written by one program, so the fields of a file are judged
// together and decoded with one character set:
//   - UTF-8:     every non-ASCII field is valid UTF-8 (with multi-byte sequences)
//   - Shift_JIS: every non-ASCII field is valid CP932 (no user-defined area),
//                and there is Japanese in it: kana or Japanese punctuation, a
//                run of 3 or more half-width katakana, or 2 or more JIS level 1
//                kanji (common ones) in all. Western text can decode to
//                valid CP932 by accident ("Hélène" makes rare level 2 kanji,
//                "Don't" with a curly apostrophe one level 1 kanji), so that
//                alone is not taken as Japanese.
//   - otherwise Windows-1252 (ISO-8859-1 plus the curly quotes, the euro, ...)
// A field that the file's character set cannot decode is decoded on its own.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ap::library {

enum class LegacyCharset
{
    Windows1252,
    ShiftJis, // CP932: Shift_JIS with the NEC and IBM extensions Windows uses
    Utf8,
};

std::string_view charsetName(LegacyCharset c); // "windows-1252", "shift_jis", "utf-8"
std::optional<LegacyCharset> charsetFromName(std::string_view name);

using Bytes = std::vector<unsigned char>;

// The character set for a file's undeclared fields. Empty when every field is
// ASCII (nothing to decide).
std::optional<LegacyCharset> detectCharset(std::span<const Bytes> fields);

// Decodes one field to UTF-8. Bytes the character set cannot decode fall back
// to Windows-1252 for the whole field.
std::string decodeLegacy(const Bytes& bytes, LegacyCharset charset);

} // namespace ap::library
