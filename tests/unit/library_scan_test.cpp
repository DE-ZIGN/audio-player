// Task 5-2: the startup scan. Files under a watched folder are compared with
// the database: added, changed, missing, returned, moved (renamed within the
// volume: the same track). The files are only read.

#include <ap/library/library_db.h>
#include <ap/library/scanner.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>
#include <string>
#include <vector>

using namespace ap::library;
namespace fs = std::filesystem;

namespace {

fs::path put(const fs::path& p, std::size_t bytes = 100, char fill = 'x')
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << std::string(bytes, fill);
    return p;
}

std::int64_t scalar(LibraryDb& lib, const std::string& sql)
{
    auto st = lib.database().prepare(sql);
    REQUIRE(st.step());
    return st.integer(0);
}

std::int64_t trackAt(LibraryDb& lib, const fs::path& p)
{
    auto st = lib.database().prepare(
        "SELECT tracks.id FROM tracks JOIN files ON files.id = tracks.file_id WHERE files.path = ?");
    const auto u8 = p.lexically_normal().u8string();
    st.bind(1, std::string_view(reinterpret_cast<const char*>(u8.data()), u8.size()));
    REQUIRE(st.step());
    return st.integer(0);
}

struct Fixture
{
    ap::test::TempDir dir;
    fs::path root = dir.path() / "Music";
    LibraryDb lib = LibraryDb::openInMemory();
    std::int64_t rootId = 0;
    Scanner scanner{lib};

    Fixture()
    {
        fs::create_directories(root);
        rootId = lib.addRoot(root);
    }
};

} // namespace

TEST_CASE("Scan: music files are added, other files are not", "[library][scan]")
{
    Fixture f;
    put(f.root / "a.flac");
    put(f.root / "Album" / fs::path(u8"01 夜明け.FLAC")); // Japanese names, upper-case extension
    put(f.root / "Album" / "02.mp3");
    put(f.root / "Album" / "cover.jpg");
    put(f.root / "Album" / "notes.txt");
    put(f.root / "Album" / "Disc 2" / "01.dsf");

    const auto r = f.scanner.scanRoot(f.rootId);
    CHECK(r.added.size() == 4);
    CHECK(r.changed.empty());
    CHECK(r.missing.empty());
    CHECK(scalar(f.lib, "SELECT COUNT(*) FROM tracks") == 4);
    CHECK(scalar(f.lib, "SELECT COUNT(*) FROM files WHERE needs_read = 1") == 4); // tags to be read (5-3)
    CHECK(scalar(f.lib, "SELECT COUNT(*) FROM files WHERE root_id = " + std::to_string(f.rootId)) == 4);
}

TEST_CASE("Scan: a second scan with nothing changed changes nothing", "[library][scan]")
{
    Fixture f;
    put(f.root / "a.flac");
    put(f.root / "b.wav");
    f.scanner.scanRoot(f.rootId);
    f.lib.database().exec("UPDATE files SET needs_read = 0"); // as if the tags were read
    const auto r = f.scanner.scanRoot(f.rootId);
    CHECK(r.empty());
    CHECK(r.unchanged == 2);
    CHECK(scalar(f.lib, "SELECT COUNT(*) FROM files WHERE needs_read = 1") == 0);
}

TEST_CASE("Scan: a changed size or write time is to be read again", "[library][scan]")
{
    Fixture f;
    const auto a = put(f.root / "a.flac", 100);
    const auto b = put(f.root / "b.flac", 100);
    f.scanner.scanRoot(f.rootId);
    f.lib.database().exec("UPDATE files SET needs_read = 0");
    const auto track = trackAt(f.lib, a);

    put(a, 120);                                                    // a tag was edited and grew
    fs::last_write_time(b, fs::last_write_time(b) + std::chrono::hours(1)); // same size, newer
    const auto r = f.scanner.scanRoot(f.rootId);
    CHECK(r.changed.size() == 2);
    CHECK(r.added.empty());
    CHECK(trackAt(f.lib, a) == track); // the same track
    CHECK(scalar(f.lib, "SELECT size FROM files WHERE path LIKE '%a.flac'") == 120);
    CHECK(scalar(f.lib, "SELECT COUNT(*) FROM files WHERE needs_read = 1") == 2);
}

TEST_CASE("Scan: a deleted file is kept as missing, and returns when it is back", "[library][scan]")
{
    Fixture f;
    const auto a = put(f.root / "a.flac");
    f.scanner.scanRoot(f.rootId);
    const auto track = trackAt(f.lib, a);
    f.lib.database().exec("UPDATE tracks SET rating = 60");

    fs::remove(a);
    auto r = f.scanner.scanRoot(f.rootId);
    CHECK(r.missing.size() == 1);
    CHECK(scalar(f.lib, "SELECT present FROM files") == 0);
    CHECK(scalar(f.lib, "SELECT rating FROM tracks") == 60); // kept

    r = f.scanner.scanRoot(f.rootId); // still gone: reported once
    CHECK(r.empty());

    put(a);
    r = f.scanner.scanRoot(f.rootId);
    CHECK(r.returned.size() == 1);
    CHECK(trackAt(f.lib, a) == track);
    CHECK(scalar(f.lib, "SELECT present FROM files") == 1);
}

TEST_CASE("Scan: a renamed or moved file stays the same track", "[library][scan]")
{
    Fixture f;
    const auto a = put(f.root / "a.flac");
    const auto b = put(f.root / "Old Folder" / "b.flac");
    f.scanner.scanRoot(f.rootId);
    const auto ta = trackAt(f.lib, a);
    const auto tb = trackAt(f.lib, b);

    const auto a2 = f.root / "A renamed.flac";
    fs::rename(a, a2);
    fs::rename(f.root / "Old Folder", f.root / "New Folder"); // a folder rename moves what is in it
    const auto r = f.scanner.scanRoot(f.rootId);
    CHECK(r.moved.size() == 2);
    CHECK(r.added.empty());
    CHECK(r.missing.empty());
    CHECK(trackAt(f.lib, a2) == ta);
    CHECK(trackAt(f.lib, f.root / "New Folder" / "b.flac") == tb);
    CHECK(scalar(f.lib, "SELECT COUNT(*) FROM files") == 2);
}

TEST_CASE("Scan: one path at a time (a file, a folder, or what is gone)", "[library][scan]")
{
    Fixture f;
    put(f.root / "x" / "1.flac");
    put(f.root / "x" / "2.flac");
    put(f.root / "y.flac");
    f.scanner.scanRoot(f.rootId);

    put(f.root / "x" / "3.flac");
    put(f.root / "z.flac");
    auto r = f.scanner.scanPath(f.rootId, f.root / "x"); // only the folder x
    CHECK(r.added.size() == 1);
    CHECK(r.unchanged == 2);

    r = f.scanner.scanPath(f.rootId, f.root / "z.flac"); // one file
    CHECK(r.added.size() == 1);

    fs::remove_all(f.root / "x");
    r = f.scanner.scanPath(f.rootId, f.root / "x"); // the folder is gone: what was in it is missing
    CHECK(r.missing.size() == 3);
    CHECK(scalar(f.lib, "SELECT COUNT(*) FROM files WHERE present = 1") == 2); // y and z

    // A folder whose name starts like another is not inside it.
    put(f.root / "x2" / "4.flac");
    r = f.scanner.scanPath(f.rootId, f.root / "x");
    CHECK(r.empty());
}

TEST_CASE("Scan: a path outside the root is refused", "[library][scan]")
{
    Fixture f;
    CHECK_THROWS_AS(f.scanner.scanPath(f.rootId, f.dir.path() / "elsewhere.flac"), std::invalid_argument);
}

TEST_CASE("Scan: the NTFS identity of a file is read", "[library][scan]")
{
    ap::test::TempDir dir;
    const auto p = put(dir.path() / "a.flac", 1234);
    const auto facts = statFile(p);
    REQUIRE(facts.has_value());
    CHECK(facts->size == 1234);
    CHECK(facts->modifiedAt > 0);
    // The temp folder is on NTFS on the development machines (FAT drives have no file ID).
    CHECK(facts->volumeSerial.has_value());
    CHECK(facts->ntfsFileId.has_value());
    CHECK_FALSE(statFile(dir.path() / "missing.flac").has_value());
}
