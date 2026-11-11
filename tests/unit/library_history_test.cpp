// Task 5-5: following moves, plays and ratings (完了条件：移動後も履歴が残る).
//
// A file moved to another drive gets a new file ID; the track is found again
// by its fingerprint, whichever of the two places is scanned first, and keeps
// its plays, rating and edits. Copies, tracks that already have something of
// their own, and ambiguous fingerprints are not joined.

#include <ap/library/history.h>
#include <ap/library/library_db.h>
#include <ap/library/library_monitor.h>
#include <ap/library/scanner.h>
#include <ap/library/tag_importer.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace ap::library;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using Strings = std::vector<std::string>;

namespace {

using Buf = std::vector<unsigned char>;

fs::path write(const fs::path& p, const Buf& b)
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return p;
}

Buf bytes(const std::string& s, std::size_t repeat = 1)
{
    Buf b;
    for (std::size_t i = 0; i < repeat; ++i)
        b.insert(b.end(), s.begin(), s.end());
    return b;
}

void le32(Buf& b, std::uint32_t v)
{
    for (int s = 0; s <= 24; s += 8)
        b.push_back(static_cast<unsigned char>(v >> s));
}

// A FLAC file with no audio frames: STREAMINFO (with the given MD5) and Vorbis comments.
Buf flac(const std::array<unsigned char, 16>& md5, const std::vector<std::string>& comments)
{
    Buf b{'f', 'L', 'a', 'C', 0x00, 0, 0, 34, 0x10, 0x00, 0x10, 0x00, 0, 0, 0, 0, 0, 0};
    const std::uint64_t packed = (std::uint64_t{44100} << 44) | (std::uint64_t{1} << 41) | (std::uint64_t{15} << 36);
    for (int s = 56; s >= 0; s -= 8)
        b.push_back(static_cast<unsigned char>(packed >> s));
    b.insert(b.end(), md5.begin(), md5.end());
    Buf vc;
    le32(vc, 4);
    vc.insert(vc.end(), {'t', 'e', 's', 't'});
    le32(vc, static_cast<std::uint32_t>(comments.size()));
    for (const auto& c : comments)
    {
        le32(vc, static_cast<std::uint32_t>(c.size()));
        vc.insert(vc.end(), c.begin(), c.end());
    }
    b.push_back(0x84);
    b.push_back(static_cast<unsigned char>(vc.size() >> 16));
    b.push_back(static_cast<unsigned char>(vc.size() >> 8));
    b.push_back(static_cast<unsigned char>(vc.size()));
    b.insert(b.end(), vc.begin(), vc.end());
    return b;
}

constexpr std::array<unsigned char, 16> kMd5{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

std::int64_t scalar(LibraryDb& lib, const std::string& sql)
{
    auto st = lib.database().prepare(sql);
    REQUIRE(st.step());
    return st.integer(0);
}

// Two watched folders, as if on two drives.
struct TwoDrives
{
    ap::test::TempDir dir;
    fs::path a = dir.path() / "DriveA";
    fs::path b = dir.path() / "DriveB";
    LibraryDb lib = LibraryDb::openInMemory();
    std::int64_t rootA = 0, rootB = 0;

    TwoDrives()
    {
        fs::create_directories(a);
        fs::create_directories(b);
        rootA = lib.addRoot(a);
        rootB = lib.addRoot(b);
    }

    // What the monitor does after comparing a place: read tags, then relink.
    std::vector<Relink> scan(std::int64_t root)
    {
        Scanner(lib).scanRoot(root);
        TagImporter(lib).readPending();
        return relinkMovedTracks(lib);
    }

    std::int64_t trackAt(const fs::path& p)
    {
        auto st = lib.database().prepare(
            "SELECT tracks.id FROM tracks JOIN files ON files.id = tracks.file_id WHERE files.path = ? AND present = 1");
        const auto u = p.lexically_normal().u8string();
        st.bind(1, std::string_view(reinterpret_cast<const char*>(u.data()), u.size()));
        return st.step() ? st.integer(0) : 0;
    }

    // A track with a history: a play, a rating and an edit.
    void giveHistory(std::int64_t t)
    {
        PlayRecord p;
        p.startedAt = 1'700'000'000'000;
        p.playedFrames = 44100 * 300;
        p.trackFrames = 44100 * 300;
        p.sampleRate = 44100;
        p.reachedEnd = true;
        recordPlay(lib, t, p);
        setRating(lib, t, 80);
        lib.setValues(t, Layer::User, "title", Strings{"edited"});
    }

    void checkHistory(std::int64_t t)
    {
        const auto s = trackStats(lib, t);
        CHECK(s.playCount == 1);
        CHECK(s.rating == 80);
        CHECK(lib.values(t, "title") == Strings{"edited"});
    }
};

// Moves a file the way a move between drives does: a copy and a delete (a new file ID).
void moveAcrossDrives(const fs::path& from, const fs::path& to)
{
    fs::create_directories(to.parent_path());
    fs::copy_file(from, to);
    fs::remove(from);
}

} // namespace

TEST_CASE("Fingerprint: FLAC by its audio MD5, others by size, head and tail", "[library][history]")
{
    ap::test::TempDir dir;
    const auto f1 = write(dir.path() / "a.flac", flac(kMd5, {"TITLE=One"}));
    const auto f2 = write(dir.path() / "b.flac", flac(kMd5, {"TITLE=Edited tags", "ARTIST=Someone"}));
    const auto fp = fingerprintOf(f1);
    REQUIRE(fp.has_value());
    CHECK(fp->starts_with("flac-md5:0102030405060708090a0b0c0d0e0f10"));
    CHECK(fingerprintOf(f2) == fp); // the same audio with other tags

    const auto noMd5 = write(dir.path() / "c.flac", flac({}, {"TITLE=One"})); // encoder left it zero
    CHECK(fingerprintOf(noMd5)->starts_with("sha256-ht:"));

    // Large files: only the head and the tail are read, with the size.
    const auto big1 = write(dir.path() / "big1.wav", bytes("0123456789abcdef", 100000)); // 1.6 MB
    auto middle = bytes("0123456789abcdef", 100000);
    middle[800000] = 'X'; // a difference in the middle is not seen
    const auto big2 = write(dir.path() / "big2.wav", middle);
    auto tail = bytes("0123456789abcdef", 100000);
    tail.back() = 'X';
    const auto big3 = write(dir.path() / "big3.wav", tail);
    CHECK(fingerprintOf(big1)->starts_with("sha256-ht:"));
    CHECK(fingerprintOf(big2) == fingerprintOf(big1));
    CHECK(fingerprintOf(big3) != fingerprintOf(big1));
    CHECK_FALSE(fingerprintOf(dir.path() / "missing.wav").has_value());
}

TEST_CASE("Moves: to another drive, the track keeps its history (new place scanned last)", "[library][history]")
{
    TwoDrives d;
    const auto src = write(d.a / "Album" / "01.wav", bytes("track one"));
    write(d.a / "Album" / "02.wav", bytes("track two"));
    d.scan(d.rootA);
    const auto t = d.trackAt(src);
    REQUIRE(t != 0);
    d.giveHistory(t);

    const auto dst = d.b / "Album" / "01.wav";
    moveAcrossDrives(src, dst);
    CHECK(d.scan(d.rootA).empty()); // the old place: missing, the new one not seen yet
    const auto relinked = d.scan(d.rootB);
    REQUIRE(relinked.size() == 1);
    CHECK(relinked[0].track == t);
    CHECK(relinked[0].to == dst);
    CHECK(d.trackAt(dst) == t);
    d.checkHistory(t);
    CHECK(scalar(d.lib, "SELECT COUNT(*) FROM tracks") == 2);
    CHECK(scalar(d.lib, "SELECT COUNT(*) FROM files") == 2); // the old file row is gone
}

TEST_CASE("Moves: the same when the new place is scanned first", "[library][history]")
{
    TwoDrives d;
    const auto src = write(d.a / "01.wav", bytes("track one"));
    d.scan(d.rootA);
    const auto t = d.trackAt(src);
    d.giveHistory(t);

    const auto dst = d.b / "01.wav";
    moveAcrossDrives(src, dst);
    CHECK(d.scan(d.rootB).empty()); // a new file; the old one is not known to be gone yet
    CHECK(d.trackAt(dst) != t);
    const auto relinked = d.scan(d.rootA); // now the old one is missing: the two are joined
    REQUIRE(relinked.size() == 1);
    CHECK(d.trackAt(dst) == t);
    d.checkHistory(t);
    CHECK(scalar(d.lib, "SELECT COUNT(*) FROM tracks") == 1);
}

TEST_CASE("Moves: FLAC whose tags were edited on the way is still the same track", "[library][history]")
{
    TwoDrives d;
    const auto src = write(d.a / "01.flac", flac(kMd5, {"TITLE=Old"}));
    d.scan(d.rootA);
    const auto t = d.trackAt(src);
    d.giveHistory(t);

    fs::remove(src);
    const auto dst = write(d.b / "01.flac", flac(kMd5, {"TITLE=New", "ARTIST=Tagged elsewhere"}));
    d.scan(d.rootA);
    REQUIRE(d.scan(d.rootB).size() == 1);
    CHECK(d.trackAt(dst) == t);
    TagImporter(d.lib).readPending(); // the new file's tags are read into the track
    CHECK(d.lib.values(t, "artist") == Strings{"Tagged elsewhere"});
    CHECK(d.lib.values(t, "title") == Strings{"edited"}); // the user's edit stays on top
}

TEST_CASE("Moves: copies, tracks with their own history and ambiguous ones are not joined", "[library][history]")
{
    SECTION("a copy: the original is still there")
    {
        TwoDrives d;
        const auto src = write(d.a / "01.wav", bytes("track one"));
        d.scan(d.rootA);
        fs::copy_file(src, d.b / "01.wav");
        d.scan(d.rootB);
        CHECK(d.scan(d.rootA).empty());
        CHECK(scalar(d.lib, "SELECT COUNT(*) FROM tracks") == 2);
    }
    SECTION("the new track already has a rating")
    {
        TwoDrives d;
        const auto src = write(d.a / "01.wav", bytes("track one"));
        d.scan(d.rootA);
        moveAcrossDrives(src, d.b / "01.wav");
        d.scan(d.rootB);
        setRating(d.lib, d.trackAt(d.b / "01.wav"), 20);
        CHECK(d.scan(d.rootA).empty());
        CHECK(scalar(d.lib, "SELECT COUNT(*) FROM tracks") == 2);
    }
    SECTION("two missing tracks with the same fingerprint")
    {
        TwoDrives d;
        write(d.a / "x" / "01.wav", bytes("same"));
        write(d.a / "y" / "01.wav", bytes("same"));
        d.scan(d.rootA);
        fs::remove_all(d.a / "y");
        moveAcrossDrives(d.a / "x" / "01.wav", d.b / "01.wav");
        d.scan(d.rootB);
        CHECK(d.scan(d.rootA).empty()); // which one moved cannot be told
        CHECK(scalar(d.lib, "SELECT COUNT(*) FROM tracks") == 3);
    }
}

TEST_CASE("Moves: a rename on the same drive keeps the history too", "[library][history]")
{
    TwoDrives d;
    const auto src = write(d.a / "01.wav", bytes("track one"));
    d.scan(d.rootA);
    const auto t = d.trackAt(src);
    d.giveHistory(t);
    fs::rename(src, d.a / "01 renamed.wav"); // the NTFS file ID stays (task 5-2)
    d.scan(d.rootA);
    CHECK(d.trackAt(d.a / "01 renamed.wav") == t);
    d.checkHistory(t);
}

TEST_CASE("Moves: the monitor follows a move between two watched folders", "[library][history][watch]")
{
    ap::test::TempDir dir;
    const auto a = dir.path() / "DriveA";
    const auto b = dir.path() / "DriveB";
    const auto db = dir.path() / "library.db";
    const auto src = write(a / "01.wav", bytes("track one"));
    fs::create_directories(b);
    std::int64_t t = 0;
    {
        auto lib = LibraryDb::open(db);
        lib.addRoot(a);
        lib.addRoot(b);
    }
    {
        LibraryMonitor monitor(db);
        REQUIRE(monitor.waitUntilIdle(10s));
        {
            auto lib = LibraryDb::open(db);
            auto st = lib.database().prepare("SELECT id FROM tracks");
            REQUIRE(st.step());
            t = st.integer(0);
            setRating(lib, t, 60);
        }
        moveAcrossDrives(src, b / "01.wav");
        bool moved = false;
        for (auto until = std::chrono::steady_clock::now() + 10s; !moved && std::chrono::steady_clock::now() < until;)
        {
            std::this_thread::sleep_for(100ms);
            auto lib = LibraryDb::open(db);
            moved = scalar(lib, "SELECT COUNT(*) FROM tracks") == 1
                 && scalar(lib, "SELECT COUNT(*) FROM files WHERE present = 1 AND path LIKE '%DriveB%'") == 1;
        }
        CHECK(moved);
    }
    auto lib = LibraryDb::open(db);
    CHECK(trackStats(lib, t).rating == 60);
}

TEST_CASE("Plays: half the track or 4 minutes count as a play", "[library][history]")
{
    auto lib = LibraryDb::openInMemory();
    FileFacts f;
    f.path = "D:/Music/a.flac";
    const auto t = lib.addTrack(f);
    auto play = [&](std::uint64_t seconds, std::uint64_t length, std::int64_t at) {
        PlayRecord p;
        p.startedAt = at;
        p.sampleRate = 48000;
        p.playedFrames = seconds * 48000;
        p.trackFrames = length * 48000;
        return recordPlay(lib, t, p);
    };
    CHECK(play(100, 200, 1000));      // half
    CHECK_FALSE(play(99, 200, 2000));  // less than half
    CHECK(play(240, 600, 3000));      // 4 minutes of a long track
    CHECK_FALSE(play(239, 600, 4000));
    CHECK(play(240, 0, 5000));        // length unknown: 4 minutes
    CHECK_FALSE(play(200, 0, 6000));

    const auto s = trackStats(lib, t);
    CHECK(s.playCount == 3);
    CHECK(s.lastPlayedAt == 6000); // the last play, counted or not
    CHECK(scalar(lib, "SELECT COUNT(*) FROM plays") == 6); // every play is kept
}

TEST_CASE("Ratings: 0 to 100, or none", "[library][history]")
{
    auto lib = LibraryDb::openInMemory();
    FileFacts f;
    f.path = "D:/Music/a.flac";
    const auto t = lib.addTrack(f);
    CHECK_FALSE(trackStats(lib, t).rating.has_value());
    setRating(lib, t, 100);
    CHECK(trackStats(lib, t).rating == 100);
    setRating(lib, t, 0);
    CHECK(trackStats(lib, t).rating == 0);
    setRating(lib, t, std::nullopt);
    CHECK_FALSE(trackStats(lib, t).rating.has_value());
    CHECK_THROWS_AS(setRating(lib, t, 101), std::out_of_range);
    CHECK_THROWS_AS(setRating(lib, t, -1), std::out_of_range);
}

// --- Task 6-2: what the player reports, turned into plays ---------------------

namespace {

struct Feed
{
    PlayCounter counter;
    std::int64_t now = 1'000'000;
    std::vector<std::pair<std::int64_t, PlayRecord>> plays;

    // Steps of 100 ms at 48 kHz.
    void step(std::int64_t track, std::uint64_t position, bool playing = true, std::uint64_t total = 48000 * 10)
    {
        auto out = counter.observe({track, position, total, 48000, playing, now});
        plays.insert(plays.end(), out.begin(), out.end());
        now += 100;
    }
    void playThrough(std::int64_t track, std::uint64_t from, std::uint64_t to, std::uint64_t total = 48000 * 10)
    {
        for (std::uint64_t p = from; p <= to; p += 4800)
            step(track, p, true, total);
    }
};

} // namespace

TEST_CASE("Play counter: a track heard to its end, then the next one", "[library][history][plays]")
{
    Feed f;
    f.playThrough(1, 0, 48000 * 10 - 4800); // up to 0.1 s before the end
    f.step(2, 2400);                        // gapless: the next track has begun
    REQUIRE(f.plays.size() == 1);
    CHECK(f.plays[0].first == 1);
    CHECK(f.plays[0].second.playedFrames == 48000u * 10);
    CHECK(f.plays[0].second.reachedEnd);
    CHECK(f.plays[0].second.trackFrames == 48000u * 10);
    CHECK(f.plays[0].second.startedAt == 1'000'000);

    f.playThrough(2, 7200, 48000 * 3);
    const auto last = f.counter.finish(); // the app closes
    REQUIRE(last.size() == 1);
    CHECK(last[0].first == 2);
    CHECK(last[0].second.playedFrames == 141600u); // from 0 (the boundary) to the last look (2.95 s)
    CHECK_FALSE(last[0].second.reachedEnd);
}

TEST_CASE("Play counter: a seek is not heard", "[library][history][plays]")
{
    Feed f;
    f.playThrough(1, 0, 48000 * 2);
    f.step(1, 48000 * 8); // jumped ahead
    f.playThrough(1, 48000 * 8 + 4800, 48000 * 9);
    f.step(1, 48000 * 1); // jumped back
    f.playThrough(1, 48000 * 1 + 4800, 48000 * 2);
    const auto p = f.counter.finish();
    REQUIRE(p.size() == 1);
    CHECK(p[0].second.playedFrames == 48000u * 4); // 2 s + 1 s + 1 s
}

TEST_CASE("Play counter: a pause is the same play; nothing heard is no play", "[library][history][plays]")
{
    Feed f;
    f.playThrough(1, 0, 48000 * 2);
    for (int i = 0; i < 20; ++i)
        f.step(1, 48000 * 2, false); // paused for 2 s
    f.playThrough(1, 48000 * 2 + 4800, 48000 * 3);
    f.step(5, 0, false); // another track chosen, not started
    REQUIRE(f.plays.size() == 1);
    CHECK(f.plays[0].second.playedFrames == 48000u * 3);
    CHECK(f.counter.finish().empty()); // track 5 was never heard
}

TEST_CASE("Play counter: tracks not from the library are not counted", "[library][history][plays]")
{
    Feed f;
    f.playThrough(0, 0, 48000 * 5); // 0: opened as a file
    CHECK(f.counter.finish().empty());
}
