#include <ap/library/legacy_text.h>

#include <windows.h>

namespace ap::library {

namespace {

bool isAscii(const Bytes& b)
{
    for (const auto c : b)
        if (c >= 0x80)
            return false;
    return true;
}

// Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF.
bool isUtf8(const Bytes& b)
{
    std::size_t i = 0;
    while (i < b.size())
    {
        const unsigned c = b[i];
        std::size_t n = 0;
        unsigned min = 0, cp = 0;
        if (c < 0x80)
        {
            ++i;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF)
            n = 1, min = 0x80, cp = c & 0x1F;
        else if (c >= 0xE0 && c <= 0xEF)
            n = 2, min = 0x800, cp = c & 0x0F;
        else if (c >= 0xF0 && c <= 0xF4)
            n = 3, min = 0x10000, cp = c & 0x07;
        else
            return false;
        if (i + n >= b.size())
            return false; // cut short
        for (std::size_t k = 1; k <= n; ++k)
        {
            const unsigned t = b[i + k];
            if ((t & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (t & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += n + 1;
    }
    return true;
}

std::wstring widen(const Bytes& b, unsigned codePage, DWORD flags)
{
    if (b.empty())
        return {};
    const auto* p = reinterpret_cast<const char*>(b.data());
    const int n = MultiByteToWideChar(codePage, flags, p, static_cast<int>(b.size()), nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(codePage, flags, p, static_cast<int>(b.size()), w.data(), n);
    return w;
}

std::string narrowUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// CP932 that Windows decodes without error and without the user-defined area
// (lead bytes F0-F9, which western letters like ö ó ñ fall into).
std::optional<std::wstring> decodeCp932(const Bytes& b)
{
    auto w = widen(b, 932, MB_ERR_INVALID_CHARS);
    if (w.empty() && !b.empty())
        return std::nullopt;
    for (const wchar_t c : w)
        if (c >= 0xE000 && c <= 0xF8FF)
            return std::nullopt;
    return w;
}

// Windows-1252, with the five bytes it leaves undefined kept as the ISO-8859-1
// code points (so nothing is lost).
std::string decode1252(const Bytes& b)
{
    static constexpr wchar_t k80[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    std::wstring w;
    w.reserve(b.size());
    for (const auto c : b)
        w.push_back(c >= 0x80 && c < 0xA0 ? k80[c - 0x80] : static_cast<wchar_t>(c));
    return narrowUtf8(w);
}

struct JapaneseEvidence
{
    bool strong = false;    // kana, Japanese punctuation, full-width forms, NEC symbols, or 3+ half-width katakana
    int level1Kanji = 0;    // JIS level 1 kanji (common ones)
};

// Walks bytes already known to be valid CP932.
JapaneseEvidence evidence(const Bytes& b)
{
    JapaneseEvidence e;
    int halfRun = 0;
    for (std::size_t i = 0; i < b.size(); ++i)
    {
        const unsigned c = b[i];
        if (c >= 0xA1 && c <= 0xDF)
        {
            if (++halfRun >= 3)
                e.strong = true;
            continue;
        }
        halfRun = 0;
        if (c < 0x80 || i + 1 >= b.size())
            continue;
        if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC))
        {
            if (c == 0x81 || c == 0x82 || c == 0x83 || c == 0x87)
                e.strong = true; // punctuation, full-width alphanumerics, hiragana, katakana, circled numbers
            else if (c >= 0x88 && c <= 0x98)
                ++e.level1Kanji;
            ++i; // the trail byte
        }
    }
    return e;
}

} // namespace

std::string_view charsetName(LegacyCharset c)
{
    switch (c)
    {
        case LegacyCharset::Windows1252: return "windows-1252";
        case LegacyCharset::ShiftJis: return "shift_jis";
        case LegacyCharset::Utf8: return "utf-8";
    }
    return "windows-1252";
}

std::optional<LegacyCharset> charsetFromName(std::string_view name)
{
    for (auto c : {LegacyCharset::Windows1252, LegacyCharset::ShiftJis, LegacyCharset::Utf8})
        if (charsetName(c) == name)
            return c;
    return std::nullopt;
}

std::optional<LegacyCharset> detectCharset(std::span<const Bytes> fields)
{
    bool any = false, allUtf8 = true;
    bool strong = false;
    int level1 = 0;
    for (const auto& f : fields)
    {
        if (isAscii(f))
            continue;
        any = true;
        allUtf8 = allUtf8 && isUtf8(f);
        if (decodeCp932(f))
        {
            const auto e = evidence(f);
            strong = strong || e.strong;
            level1 += e.level1Kanji;
        }
    }
    if (!any)
        return std::nullopt;
    if (allUtf8)
        return LegacyCharset::Utf8;
    if (strong || level1 >= 2)
        return LegacyCharset::ShiftJis;
    return LegacyCharset::Windows1252;
}

std::string decodeLegacy(const Bytes& bytes, LegacyCharset charset)
{
    if (isAscii(bytes))
        return std::string(bytes.begin(), bytes.end());
    switch (charset)
    {
        case LegacyCharset::Utf8:
            if (isUtf8(bytes))
                return std::string(bytes.begin(), bytes.end());
            break;
        case LegacyCharset::ShiftJis:
            if (const auto w = decodeCp932(bytes))
                return narrowUtf8(*w);
            break;
        case LegacyCharset::Windows1252:
            break;
    }
    return decode1252(bytes);
}

} // namespace ap::library
