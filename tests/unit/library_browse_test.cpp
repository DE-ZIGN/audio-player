// Task 6-1: what the library screen asks for. Pages of tracks and albums,
// sorted and searched, with the shown values (edits included), the measured
// values and the plays; and the watched folders.

#include <ap/library/analysis.h>
#include <ap/library/browse.h>
#include <ap/library/history.h>
#include <ap/library/library_db.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using namespace ap::library;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;
using Strings = std::vector<std::string>;

namespace {

std::string u8(const char8_t* s)
{
    return std::string(reinterpret_cast<const char*>(s));
}

struct Lib
{
    LibraryDb lib = LibraryDb::openInMemory();
    std::int64_t root = lib.addRoot("D:/Music");

    std::int64_t add(const std::string& file, const std::string& title, const std::string& artist,
                     const std::string& album = {}, const std::string& trackNo = {}, const std::string& disc = {})
    {
        FileFacts f;
        f.path = fs::path("D:/Music") / fs::path(std::u8string(file.begin(), file.end()));
        f.size = 1;
        const auto t = lib.addTrack(f, root);
        auto set = [&](const char* field, const std::string& v) {
            if (!v.empty())
                lib.setValues(t, Layer::Tag, field, Strings{v});
        };
        set("title", title);
        set("artist", artist);
        set("album", album);
        set("tracknumber", trackNo);
        set("discnumber", disc);
        return t;
    }

    Strings titles(const TrackQuery& q)
    {
        Strings out;
        for (const auto& r : listTracks(lib, q).rows)
            out.push_back(r.title);
        return out;
    }
};

} // namespace

TEST_CASE("Browse: tracks sorted by artist, album, disc and track", "[library][browse]")
{
    Lib L;
    L.add("1.flac", "B-2", "Beta", "Second", "2");
    L.add("2.flac", "B-1", "Beta", "Second", "1/10");
    L.add("3.flac", "A-1", "alpha", "First", "1");
    L.add("4.flac", "D2-1", "Beta", "Second", "1", "2/2"); // disc 2
    TrackQuery q;
    CHECK(L.titles(q) == Strings{"A-1", "B-1", "B-2", "D2-1"});
    q.sort = TrackSort::Title;
    CHECK(L.titles(q) == Strings{"A-1", "B-1", "B-2", "D2-1"});
    q.descending = true;
    CHECK(L.titles(q) == Strings{"D2-1", "B-2", "B-1", "A-1"});
}

TEST_CASE("Browse: kana and width do not split the sort order", "[library][browse]")
{
    Lib L;
    L.add("1.flac", u8(u8"いろは"), "x");
    L.add("2.flac", u8(u8"アイウ"), "x");
    L.add("3.flac", u8(u8"ｳｴｵ"), "x");
    TrackQuery q;
    q.sort = TrackSort::Title;
    // あいう, いろは, うえお: katakana and half-width sort among the hiragana.
    CHECK(L.titles(q) == Strings{u8(u8"アイウ"), u8(u8"いろは"), u8(u8"ｳｴｵ")});
}

TEST_CASE("Browse: a page, and the total behind it", "[library][browse]")
{
    Lib L;
    for (int i = 0; i < 25; ++i)
        L.add(std::to_string(i) + ".flac", "Song " + std::string(i < 10 ? "0" : "") + std::to_string(i), "A");
    TrackQuery q;
    q.sort = TrackSort::Title;
    q.offset = 20;
    q.limit = 10;
    const auto page = listTracks(L.lib, q);
    CHECK(page.total == 25);
    REQUIRE(page.rows.size() == 5);
    CHECK(page.rows[0].title == "Song 20");
}

TEST_CASE("Browse: the search and an album narrow the tracks", "[library][browse]")
{
    Lib L;
    L.add("1.flac", u8(u8"夜明けのスキャット"), u8(u8"由紀さおり"), u8(u8"ベスト"), "2");
    L.add("2.flac", u8(u8"手紙"), u8(u8"由紀さおり"), u8(u8"ベスト"), "1");
    L.add("3.flac", u8(u8"スキャットマン"), "Scatman John", "Scat");
    TrackQuery q;
    q.search = u8(u8"すきゃっと");
    CHECK(listTracks(L.lib, q).total == 2);
    q.search = u8(u8"由紀");
    CHECK(listTracks(L.lib, q).total == 2);

    std::optional<std::string> key;
    for (const auto& r : listTracks(L.lib, TrackQuery{}).rows)
        if (r.album == u8(u8"ベスト"))
            key = r.albumKey;
    REQUIRE(key.has_value());
    TrackQuery album;
    album.albumKey = key;
    album.sort = TrackSort::Title; // ignored: an album is in track order
    CHECK(L.titles(album) == Strings{u8(u8"手紙"), u8(u8"夜明けのスキャット")});
}

TEST_CASE("Browse: what a row shows", "[library][browse]")
{
    Lib L;
    const auto t = L.add("01 untagged.wav", "", "");
    L.lib.database().exec("UPDATE tracks SET container = 'FLAC', sample_rate = 96000, bits = 24, frames = 96000 * 125");
    auto r = listTracks(L.lib, TrackQuery{}).rows.at(0);
    CHECK(r.title == "01 untagged"); // no tags: the file name
    CHECK(r.artist.empty());
    CHECK(r.container == "FLAC");
    CHECK(r.sampleRate == 96000u);
    CHECK(r.bits == 24);
    CHECK_THAT(*r.seconds, WithinAbs(125.0, 1e-9));
    CHECK(r.analysis == "pending");
    CHECK_FALSE(r.truePeakDb.has_value());

    // An edit shows; plays and rating; a measurement.
    L.lib.setValues(t, Layer::User, "title", Strings{"Edited"});
    L.lib.setValues(t, Layer::User, "artist", Strings{"A", "B"});
    PlayRecord p;
    p.sampleRate = 96000;
    p.playedFrames = 96000 * 100;
    p.trackFrames = 96000 * 125;
    recordPlay(L.lib, t, p);
    setRating(L.lib, t, 80);
    L.lib.database().exec("INSERT INTO track_analysis (track_id, analyzer_version, status, sample_peak, true_peak, "
                          "loudness_lufs, analyzed_at) VALUES (" + std::to_string(t) + ", " +
                          std::to_string(kAnalyzerVersion) + ", 'ok', 0.9, 1.0592537251772889, -9.5, 1)");
    r = listTracks(L.lib, TrackQuery{}).rows.at(0);
    CHECK(r.title == "Edited");
    CHECK(r.artist == "A / B");
    CHECK(r.plays == 1);
    CHECK(r.rating == 80);
    CHECK(r.analysis == "ok");
    CHECK_THAT(*r.truePeakDb, WithinAbs(0.5, 1e-9));
    CHECK(r.loudnessLufs == -9.5);
}

TEST_CASE("Browse: sorting by true peak puts the unmeasured last", "[library][browse]")
{
    Lib L;
    const auto a = L.add("a.flac", "a", "x");
    const auto b = L.add("b.flac", "b", "x");
    L.add("c.flac", "c", "x"); // not measured
    auto measure = [&](std::int64_t t, double peak) {
        L.lib.database().exec("INSERT INTO track_analysis (track_id, analyzer_version, status, sample_peak, true_peak, "
                              "analyzed_at) VALUES (" + std::to_string(t) + ", " + std::to_string(kAnalyzerVersion) +
                              ", 'ok', 0.5, " + std::to_string(peak) + ", 1)");
    };
    measure(a, 0.5);
    measure(b, 1.2);
    TrackQuery q;
    q.sort = TrackSort::TruePeak;
    q.descending = true;
    CHECK(L.titles(q) == Strings{"b", "a", "c"});
}

TEST_CASE("Browse: albums with their tracks, length, format and cover track", "[library][browse]")
{
    Lib L;
    const auto first = L.add("1.flac", "One", "Artist", "Album X", "1");
    L.add("2.flac", "Two", "Artist", "Album X", "2");
    L.add("3.flac", "Solo", "Other", "Album Y", "1");
    L.add("4.flac", "No album", "Other");
    L.lib.database().exec("UPDATE tracks SET container = 'FLAC', sample_rate = 44100, frames = 44100 * 60");
    L.lib.setValues(first, Layer::Tag, "date", Strings{"2024-05-01"});

    AlbumQuery q;
    const auto page = listAlbums(L.lib, q);
    CHECK(page.total == 2);
    REQUIRE(page.rows.size() == 2);
    const auto& x = page.rows[0];
    CHECK(x.album == "Album X");
    CHECK(x.artist == "Artist");
    CHECK(x.tracks == 2);
    CHECK(x.year == "2024");
    CHECK_THAT(*x.seconds, WithinAbs(120.0, 1e-9));
    CHECK(x.coverTrack == first);
    CHECK(x.container == "FLAC");
    CHECK(x.sampleRate == 44100u);

    q.search = "solo";
    CHECK(listAlbums(L.lib, q).total == 1);
    CHECK(listAlbums(L.lib, q).rows[0].album == "Album Y");
}

TEST_CASE("Browse: watched folders, and taking one out", "[library][browse]")
{
    Lib L;
    L.add("a.flac", "a", "x");
    L.add("b.flac", "b", "x");
    L.lib.database().exec("UPDATE files SET present = 0 WHERE path LIKE '%b.flac'");
    auto folders = listFolders(L.lib);
    REQUIRE(folders.size() == 1);
    CHECK(folders[0].tracks == 1);
    CHECK(folders[0].missing == 1);
    const auto counts = libraryCounts(L.lib);
    CHECK(counts.tracks == 1);
    CHECK(counts.missing == 1);
    CHECK(counts.toMeasure == 1);

    removeFolder(L.lib, folders[0].id);
    CHECK(listFolders(L.lib).empty());
    CHECK(listTracks(L.lib, TrackQuery{}).total == 0);
}
