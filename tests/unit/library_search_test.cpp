// Task 5-4: Japanese substring search (完了条件：検索テスト).
//
// Hiragana, katakana and half-width katakana find each other, as do full-
// and half-width letters, upper and lower case, and accented letters and
// their plain forms. Words shorter than 3 characters (one kanji) are found
// too. Words must all be found, each within one field group, and the index
// follows the user's edits, tag reads, moves and deletions.

#include <ap/library/library_db.h>
#include <ap/library/scanner.h>
#include <ap/library/search.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>
#include <vector>

using namespace ap::library;
namespace fs = std::filesystem;
using Strings = std::vector<std::string>;
using Ids = std::vector<std::int64_t>;

namespace {

std::string u8(const char8_t* s)
{
    return std::string(reinterpret_cast<const char*>(s));
}

Ids sorted(Ids v)
{
    std::sort(v.begin(), v.end());
    return v;
}

struct Library
{
    LibraryDb lib = LibraryDb::openInMemory();

    std::int64_t track(const std::string& file, const std::string& title, const std::string& artist = {},
                       const std::string& album = {})
    {
        FileFacts f;
        f.path = fs::path("D:/Music") / fs::path(std::u8string(file.begin(), file.end()));
        f.size = 1;
        const auto t = lib.addTrack(f);
        if (!title.empty())
            lib.setValues(t, Layer::Tag, "title", Strings{title});
        if (!artist.empty())
            lib.setValues(t, Layer::Tag, "artist", Strings{artist});
        if (!album.empty())
            lib.setValues(t, Layer::Tag, "album", Strings{album});
        return t;
    }

    Ids find(const std::string& q) { return sorted(searchTracks(lib, q)); }
};

} // namespace

TEST_CASE("Search form: width, case, accents and kana are put in one form", "[library][search]")
{
    CHECK(normalizeForSearch(u8(u8"ＡＢＣ　１２３")) == "abc 123");
    CHECK(normalizeForSearch(u8(u8"ｶﾀｶﾅ")) == u8(u8"かたかな"));
    CHECK(normalizeForSearch(u8(u8"カタカナ")) == u8(u8"かたかな"));
    CHECK(normalizeForSearch(u8(u8"ｶﾞｷﾞｸﾞ")) == u8(u8"がぎぐ")); // half-width voiced marks join their kana
    CHECK(normalizeForSearch(u8(u8"ヴァイオリン")) == u8(u8"ゔぁいおりん"));
    CHECK(normalizeForSearch(u8(u8"Björk Guðmundsdóttir")) == "bjork guðmundsdottir");
    CHECK(normalizeForSearch(u8(u8"Beyoncé")) == "beyonce");
    CHECK(normalizeForSearch(u8(u8"①")) == "1");
    CHECK(normalizeForSearch(u8(u8"  夜明け  の\tスキャット ")) == u8(u8"夜明け の すきゃっと"));
    CHECK(normalizeForSearch(u8(u8"漢字はそのまま")) == u8(u8"漢字はそのまま"));
    CHECK(normalizeForSearch("") == "");
}

TEST_CASE("Search: kana of any kind and width find each other", "[library][search]")
{
    Library L;
    const auto a = L.track("a.flac", u8(u8"夜明けのスキャット"), u8(u8"由紀さおり"));
    const auto b = L.track("b.flac", u8(u8"スキャットマン"), "Scatman John");
    L.track("c.flac", u8(u8"別の曲"), u8(u8"別の人"));
    CHECK(L.find(u8(u8"スキャット")) == Ids{a, b});
    CHECK(L.find(u8(u8"すきゃっと")) == Ids{a, b});
    CHECK(L.find(u8(u8"ｽｷｬｯﾄ")) == Ids{a, b});
    CHECK(L.find(u8(u8"さおり")) == Ids{a});
    CHECK(L.find(u8(u8"サオリ")) == Ids{a});
}

TEST_CASE("Search: full-width letters, case and accents", "[library][search]")
{
    Library L;
    const auto a = L.track("a.flac", "Hyperballad", u8(u8"Björk"));
    const auto b = L.track("b.flac", u8(u8"ＡＢＣ　ソング"));
    CHECK(L.find("bjork") == Ids{a});
    CHECK(L.find(u8(u8"BJÖRK")) == Ids{a});
    CHECK(L.find("HYPER") == Ids{a});
    CHECK(L.find("abc") == Ids{b});
    CHECK(L.find(u8(u8"ＡＢＣ")) == Ids{b});
}

TEST_CASE("Search: words shorter than three characters", "[library][search]")
{
    Library L;
    const auto a = L.track("a.flac", u8(u8"愛の讃歌"));
    const auto b = L.track("b.flac", u8(u8"夜明けの愛"));
    const auto c = L.track("c.flac", u8(u8"夜明け"));
    CHECK(L.find(u8(u8"愛")) == Ids{a, b});          // one kanji
    CHECK(L.find(u8(u8"明け")) == Ids{b, c});        // two characters
    CHECK(L.find(u8(u8"夜明け 愛")) == Ids{b});      // a long word narrows, the short one filters
    CHECK(L.find(u8(u8"愛 讃")) == Ids{a});          // only short words
    CHECK(L.find("x").empty());
}

TEST_CASE("Search: every word must be found, each within one field group", "[library][search]")
{
    Library L;
    const auto a = L.track("a.flac", u8(u8"夜明けのスキャット"), u8(u8"由紀さおり"), u8(u8"ベスト"));
    L.track("b.flac", u8(u8"夜明けの歌"), u8(u8"別の人"));
    CHECK(L.find(u8(u8"夜明け 由紀")) == Ids{a});     // title and artist
    CHECK(L.find(u8(u8"由紀 べすと")) == Ids{a});     // artist and album
    CHECK(L.find(u8(u8"夜明け スキャットマン")).empty());
    // The end of the title and the start of the artist do not join up.
    CHECK(L.find(u8(u8"ット由紀")).empty());
    CHECK(L.find(u8(u8"ト由")).empty());
}

TEST_CASE("Search: other artist fields, the work, the genre and the file name", "[library][search]")
{
    Library L;
    const auto a = L.track("Symphony_No5.flac", "I. Allegro con brio");
    L.lib.setValues(a, Layer::Tag, "composer", Strings{"Ludwig van Beethoven"});
    L.lib.setValues(a, Layer::Tag, "conductor", Strings{u8(u8"小澤征爾")});
    L.lib.setValues(a, Layer::Tag, "work", Strings{"Symphony No. 5"});
    L.lib.setValues(a, Layer::Tag, "genre", Strings{"Classical"});
    const auto untagged = L.track(u8(u8"録音 2026-11-11.wav"), "");
    CHECK(L.find("beethoven") == Ids{a});
    CHECK(L.find(u8(u8"小澤")) == Ids{a});
    CHECK(L.find("symphony no. 5") == Ids{a});
    CHECK(L.find("classical") == Ids{a});
    CHECK(L.find(u8(u8"録音")) == Ids{untagged}); // no tags: found by its file name
    CHECK(L.find("2026-11-11") == Ids{untagged});
}

TEST_CASE("Search: the index follows edits, clears and removals", "[library][search]")
{
    Library L;
    const auto a = L.track("a.flac", u8(u8"旧い題名"));
    L.lib.setValues(a, Layer::User, "title", Strings{u8(u8"新しい題名")});
    CHECK(L.find(u8(u8"新しい")) == Ids{a});
    CHECK(L.find(u8(u8"旧い")).empty()); // what is shown is what is found
    L.lib.removeValues(a, Layer::User, "title");
    CHECK(L.find(u8(u8"旧い")) == Ids{a});
    L.lib.clearByUser(a, "title");
    CHECK(L.find(u8(u8"旧い")).empty());
    L.lib.database().exec("DELETE FROM files"); // removed from the library
    L.lib.removeValues(a, Layer::User, "title");
    CHECK(L.find("a.flac").empty());
}

TEST_CASE("Search: characters with a meaning to SQL or FTS5 are just characters", "[library][search]")
{
    Library L;
    const auto a = L.track("a.flac", R"(He said "hi" 100% a_b (c) * x OR y)");
    for (const char* q : {R"("hi")", "100%", "a_b", "(c)", "*", "OR", R"(")", "x OR y"})
    {
        CAPTURE(q);
        CHECK(L.find(q) == Ids{a});
    }
    for (const char* q : {R"("bye")", "NEAR(", "'", "%%", "   "}) // not there, and no error
    {
        CAPTURE(q);
        CHECK(L.find(q).empty());
    }
}

TEST_CASE("Search: files moved by the scan are found by their new name", "[library][search]")
{
    ap::test::TempDir dir;
    const auto root = dir.path() / "Music";
    fs::create_directories(root);
    std::ofstream(root / "old name.flac") << "x";
    auto lib = LibraryDb::openInMemory();
    const auto rootId = lib.addRoot(root);
    Scanner(lib).scanRoot(rootId);
    CHECK(searchTracks(lib, "old name").size() == 1);
    fs::rename(root / "old name.flac", root / "new name.flac");
    Scanner(lib).scanRoot(rootId);
    CHECK(searchTracks(lib, "new name").size() == 1);
    CHECK(searchTracks(lib, "old name").empty());
}

TEST_CASE("Search: an existing library is indexed when the database is opened", "[library][search]")
{
    ap::test::TempDir dir;
    const auto file = dir.path() / "library.db";
    {
        auto lib = LibraryDb::open(file);
        FileFacts f;
        f.path = "D:/Music/a.flac";
        const auto t = lib.addTrack(f);
        lib.setValues(t, Layer::Tag, "title", Strings{u8(u8"夜明け")});
        // As if the index were from another version of the search form.
        lib.database().exec("DELETE FROM track_search; UPDATE library_meta SET value = '0' WHERE key = 'search_index'");
    }
    auto lib = LibraryDb::open(file);
    CHECK(searchTracks(lib, u8(u8"夜明け")).size() == 1); // (kanji are not found by their reading)
}

TEST_CASE("Search: many tracks", "[library][search]")
{
    Library L;
    {
        Database::Transaction tx(L.lib.database());
        for (int i = 0; i < 20000; ++i)
            L.track("t" + std::to_string(i) + ".flac", u8(u8"曲名") + std::to_string(i),
                    i % 100 == 0 ? u8(u8"由紀さおり") : "Artist " + std::to_string(i % 37));
        tx.commit();
    }
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    CHECK(searchTracks(L.lib, u8(u8"由紀さおり"), 100000).size() == 200);
    const auto longMs = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    t0 = clock::now();
    CHECK(searchTracks(L.lib, u8(u8"由紀"), 100000).size() == 200);
    const auto shortMs = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    INFO("3+ characters: " << longMs << " ms, 2 characters: " << shortMs << " ms");
    CHECK(longMs < 1000);
    CHECK(shortMs < 1000);
    WARN("20000 tracks: 3+ characters " << longMs << " ms, 2 characters " << shortMs << " ms");
}
