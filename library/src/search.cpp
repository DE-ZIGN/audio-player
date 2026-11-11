#include <ap/library/search.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>

namespace ap::library {

namespace {

std::wstring widen(std::string_view s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring normalize(NORM_FORM form, const std::wstring& w)
{
    if (w.empty())
        return w;
    int n = NormalizeString(form, w.data(), static_cast<int>(w.size()), nullptr, 0);
    for (int attempt = 0; attempt < 3 && n > 0; ++attempt)
    {
        std::wstring out(static_cast<std::size_t>(n), L'\0');
        const int got = NormalizeString(form, w.data(), static_cast<int>(w.size()), out.data(), n);
        if (got > 0)
        {
            out.resize(static_cast<std::size_t>(got));
            return out;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            break;
        n = -got; // the estimate it gives back
    }
    return w; // not valid UTF-16 (a lone surrogate): left as it is
}

std::wstring lower(const std::wstring& w)
{
    if (w.empty())
        return w;
    const int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                nullptr, nullptr, 0);
    if (n <= 0)
        return w;
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr,
                  nullptr, 0);
    return out;
}

std::size_t codePoints(std::string_view s)
{
    std::size_t n = 0;
    for (const char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            ++n;
    return n;
}

// Field groups: a word must be found within one of them.
struct Group
{
    const char* column;
    std::vector<std::string_view> fields;
};

const std::array<Group, 4>& groups()
{
    static const std::array<Group, 4> g = {{
        {"title", {"title", "subtitle"}},
        {"artists",
         {"artist", "albumartist", "artists", "composer", "conductor", "ensemble", "performer", "lyricist", "arranger",
          "remixer"}},
        {"album", {"album", "work", "movementname", "grouping"}},
        {"other", {"genre"}},
    }};
    return g;
}

// Between values in one column: never in a query, so no match spans two values.
constexpr char kSeparator = '\x1F';

} // namespace

std::string normalizeForSearch(std::string_view text)
{
    std::wstring w = normalize(NormalizationKC, widen(text)); // widths, compatibility forms
    w = lower(w);
    w = normalize(NormalizationD, w);
    std::wstring plain;
    plain.reserve(w.size());
    for (const wchar_t c : w)
        if (c < 0x0300 || c > 0x036F) // combining accents (not the kana voicing marks)
            plain.push_back(c);
    w = normalize(NormalizationC, plain);

    std::wstring out;
    out.reserve(w.size());
    bool space = false;
    for (wchar_t c : w)
    {
        if (std::iswspace(c))
        {
            space = !out.empty();
            continue;
        }
        if (space)
            out.push_back(L' ');
        space = false;
        if (c >= 0x30A1 && c <= 0x30F6) // katakana to hiragana
            c = static_cast<wchar_t>(c - 0x60);
        out.push_back(c);
    }
    return narrow(out);
}

void LibraryDb::refreshSearch(std::int64_t track)
{
    auto& db = db_;
    std::string file;
    {
        auto st = db.prepare("SELECT files.path FROM tracks JOIN files ON files.id = tracks.file_id WHERE tracks.id = ?");
        st.bind(1, track);
        if (!st.step())
        {
            db.prepare("DELETE FROM track_search WHERE rowid = ?").bind(1, track).run();
            return; // no such track
        }
        const auto path = st.text(0);
        const auto slash = path.find_last_of("\\/");
        file = path.substr(slash == std::string::npos ? 0 : slash + 1);
    }
    std::array<std::string, 4> text;
    auto values = db.prepare("SELECT field, value FROM track_values WHERE track_id = ? ORDER BY field, idx");
    values.bind(1, track);
    while (values.step())
    {
        const auto field = values.text(0);
        for (std::size_t g = 0; g < groups().size(); ++g)
            if (std::find(groups()[g].fields.begin(), groups()[g].fields.end(), field) != groups()[g].fields.end())
            {
                if (!text[g].empty())
                    text[g].push_back(kSeparator);
                text[g] += normalizeForSearch(values.text(1));
            }
    }
    Database::Transaction tx(db);
    db.prepare("DELETE FROM track_search WHERE rowid = ?").bind(1, track).run();
    db.prepare("INSERT INTO track_search (rowid, title, artists, album, other, file) VALUES (?, ?, ?, ?, ?, ?)")
        .bind(1, track)
        .bind(2, text[0])
        .bind(3, text[1])
        .bind(4, text[2])
        .bind(5, text[3])
        .bind(6, normalizeForSearch(file))
        .run();
    tx.commit();
}

void LibraryDb::ensureSearchIndex()
{
    auto st = db_.prepare("SELECT value FROM library_meta WHERE key = 'search_index'");
    if (st.step() && st.text(0) == std::to_string(kSearchIndexVersion))
        return;
    Database::Transaction tx(db_);
    db_.exec("DELETE FROM track_search");
    std::vector<std::int64_t> tracks;
    {
        auto all = db_.prepare("SELECT id FROM tracks");
        while (all.step())
            tracks.push_back(all.integer(0));
    }
    for (const auto t : tracks)
        refreshSearch(t);
    db_.prepare("INSERT OR REPLACE INTO library_meta (key, value) VALUES ('search_index', ?)")
        .bind(1, std::to_string(kSearchIndexVersion))
        .run();
    tx.commit();
}

std::vector<std::int64_t> searchTracks(LibraryDb& lib, std::string_view query, std::size_t limit)
{
    std::vector<std::string> longWords, shortWords;
    {
        const auto q = normalizeForSearch(query);
        std::size_t i = 0;
        while (i < q.size())
        {
            const auto end = q.find(' ', i);
            const auto word = q.substr(i, end == std::string::npos ? std::string::npos : end - i);
            if (!word.empty())
                (codePoints(word) >= 3 ? longWords : shortWords).push_back(word);
            if (end == std::string::npos)
                break;
            i = end + 1;
        }
    }
    if (longWords.empty() && shortWords.empty())
        return {};

    // Words of 3+ characters through the trigram index, each as a quoted
    // phrase (FTS5 syntax characters inside are plain text).
    std::string match;
    for (const auto& w : longWords)
    {
        std::string phrase = "\"";
        for (const char c : w)
        {
            phrase.push_back(c);
            if (c == '"')
                phrase.push_back('"');
        }
        phrase += '"';
        match += (match.empty() ? "" : " ") + phrase;
    }

    std::string sql = "SELECT rowid FROM track_search WHERE ";
    int param = 1;
    std::vector<std::string> binds;
    if (!match.empty())
    {
        sql += "track_search MATCH ?" + std::to_string(param++);
        binds.push_back(match);
    }
    for (const auto& w : shortWords)
    {
        // Shorter words are looked for in the indexed text, column by column.
        const auto p = "?" + std::to_string(param++);
        sql += binds.empty() ? "" : " AND ";
        sql += "(instr(title, " + p + ") > 0 OR instr(artists, " + p + ") > 0 OR instr(album, " + p
             + ") > 0 OR instr(other, " + p + ") > 0 OR instr(file, " + p + ") > 0)";
        binds.push_back(w);
    }
    sql += match.empty() ? " ORDER BY rowid" : " ORDER BY rank";
    sql += " LIMIT ?" + std::to_string(param);

    auto st = lib.database().prepare(sql);
    for (std::size_t i = 0; i < binds.size(); ++i)
        st.bind(static_cast<int>(i + 1), std::string_view(binds[i]));
    st.bind(param, static_cast<std::int64_t>(std::min<std::size_t>(limit, INT64_MAX)));
    std::vector<std::int64_t> ids;
    while (st.step())
        ids.push_back(st.integer(0));
    return ids;
}

} // namespace ap::library
