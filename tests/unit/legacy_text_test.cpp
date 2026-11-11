// Task 5-3: undeclared text in old tags (ID3v1, ID3v2 "ISO-8859-1" frames,
// RIFF INFO). Shift_JIS written on Japanese Windows must come out as
// Japanese, and western text must not be mistaken for it.

#include <ap/library/legacy_text.h>

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <string>
#include <vector>

using namespace ap::library;

namespace {

std::string u8(const char8_t* s)
{
    return std::string(reinterpret_cast<const char*>(s));
}

// Encodes UTF-8 text in a Windows code page (932 = Shift_JIS, 1252 = western).
Bytes encode(const std::string& utf8, unsigned codePage)
{
    const int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(wn), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), wn);
    const int n = WideCharToMultiByte(codePage, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
    Bytes b(static_cast<std::size_t>(n));
    WideCharToMultiByte(codePage, 0, w.data(), wn, reinterpret_cast<char*>(b.data()), n, nullptr, nullptr);
    return b;
}

Bytes raw(const std::string& s)
{
    return Bytes(s.begin(), s.end());
}

std::optional<LegacyCharset> detectOne(const Bytes& b)
{
    const std::vector<Bytes> fields{b};
    return detectCharset(fields);
}

} // namespace

TEST_CASE("Legacy text: Shift_JIS from Japanese Windows is read as Japanese", "[library][charset]")
{
    for (const auto* text : {u8"夜明けのスキャット", u8"由紀さおり", u8"千本桜", u8"ベスト・アルバム「2026」", u8"テスト",
                             u8"①～⑩　ライブ" /* CP932 0x8160 is U+FF5E, as Windows maps it */, u8"ＡＢＣ　全角"})
    {
        const auto t = u8(text);
        CAPTURE(t);
        const auto b = encode(t, 932);
        CHECK(detectOne(b) == LegacyCharset::ShiftJis);
        CHECK(decodeLegacy(b, LegacyCharset::ShiftJis) == t);
    }
}

TEST_CASE("Legacy text: half-width katakana", "[library][charset]")
{
    const auto t = u8(u8"ﾃｽﾄ ｿﾝｸﾞ");
    const auto b = encode(t, 932);
    CHECK(detectOne(b) == LegacyCharset::ShiftJis);
    CHECK(decodeLegacy(b, LegacyCharset::ShiftJis) == t);
}

TEST_CASE("Legacy text: western text is not mistaken for Shift_JIS", "[library][charset]")
{
    // Several of these are valid CP932 by accident (Hélène, Don't, Ça, Åsa).
    for (const auto* text : {u8"Café", u8"Björk", u8"Sigur Rós", u8"Mötley Crüe", u8"Hélène", u8"Don’t Stop Me Now",
                             u8"Ça va", u8"Señor", u8"Für Elise", u8"Åsa", u8"€uro – “quoted”"})
    {
        const auto t = u8(text);
        CAPTURE(t);
        const auto b = encode(t, 1252);
        CHECK(detectOne(b) == LegacyCharset::Windows1252);
        CHECK(decodeLegacy(b, LegacyCharset::Windows1252) == t);
    }
}

TEST_CASE("Legacy text: UTF-8 written into an ISO-8859-1 field", "[library][charset]")
{
    for (const auto* text : {u8"Café", u8"夜明けのスキャット"})
    {
        const auto t = u8(text);
        CAPTURE(t);
        CHECK(detectOne(raw(t)) == LegacyCharset::Utf8);
        CHECK(decodeLegacy(raw(t), LegacyCharset::Utf8) == t);
    }
}

TEST_CASE("Legacy text: ASCII needs no decision", "[library][charset]")
{
    const std::vector<Bytes> fields{raw("Hello"), raw("World 2026")};
    CHECK_FALSE(detectCharset(fields).has_value());
    CHECK(decodeLegacy(raw("Hello"), LegacyCharset::ShiftJis) == "Hello");
}

TEST_CASE("Legacy text: the fields of one file are judged together", "[library][charset]")
{
    // A lone half-width katakana is not enough on its own (its byte B1 is "±"
    // in Windows-1252), but the file's other field is clearly Japanese.
    const auto lone = encode(u8(u8"ｱ"), 932);
    CHECK(detectOne(lone) == LegacyCharset::Windows1252);
    const std::vector<Bytes> fields{lone, encode(u8(u8"ひらがな"), 932), raw("ASCII")};
    CHECK(detectCharset(fields) == LegacyCharset::ShiftJis);
    CHECK(decodeLegacy(lone, LegacyCharset::ShiftJis) == u8(u8"ｱ"));

    // One level 1 kanji alone is not enough; two in the file are.
    const auto one = encode(u8(u8"雪"), 932);
    CHECK(detectOne(one) == LegacyCharset::Windows1252);
    const std::vector<Bytes> two{one, encode(u8(u8"空"), 932)};
    CHECK(detectCharset(two) == LegacyCharset::ShiftJis);
}

TEST_CASE("Legacy text: a field the file's character set cannot decode is decoded on its own", "[library][charset]")
{
    const auto jp = encode(u8(u8"日本語のタイトル"), 932);
    const auto west = encode(u8(u8"Café"), 1252); // E9 at the end: not valid Shift_JIS
    const std::vector<Bytes> fields{jp, west};
    CHECK(detectCharset(fields) == LegacyCharset::ShiftJis);
    CHECK(decodeLegacy(west, LegacyCharset::ShiftJis) == u8(u8"Café")); // falls back for this field
}

TEST_CASE("Legacy text: names of the character sets", "[library][charset]")
{
    for (auto c : {LegacyCharset::Windows1252, LegacyCharset::ShiftJis, LegacyCharset::Utf8})
        CHECK(charsetFromName(charsetName(c)) == c);
    CHECK_FALSE(charsetFromName("euc-jp").has_value());
}
