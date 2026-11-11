// Task 5-1: the library schema. Displayed values in three layers per field
// (user edit > external database > file tag), tracks separate from their
// files, and what a track keeps (analysis, provenance findings, plays).

#include <ap/library/library_db.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ap::library;
using Strings = std::vector<std::string>;

namespace {

FileFacts file(const std::string& name)
{
    FileFacts f;
    f.path = std::filesystem::path("D:/Music") / name;
    f.size = 1000;
    f.modifiedAt = 1'700'000'000'000;
    return f;
}

std::int64_t count(LibraryDb& lib, const std::string& sql)
{
    auto st = lib.database().prepare(sql);
    REQUIRE(st.step());
    return st.integer(0);
}

} // namespace

TEST_CASE("Layers: the tag is shown when nothing else has the field", "[library][layers]")
{
    auto lib = LibraryDb::openInMemory();
    const auto t = lib.addTrack(file("a.flac"));
    lib.setValues(t, Layer::Tag, "title", Strings{"夜明け"}, "vorbis");
    CHECK(lib.values(t, "title") == Strings{"夜明け"});
    CHECK(lib.shownLayer(t, "title") == Layer::Tag);
    CHECK(lib.values(t, "artist").empty());
    CHECK_FALSE(lib.shownLayer(t, "artist").has_value());
}

TEST_CASE("Layers: user edit > external database > tag, field by field", "[library][layers]")
{
    auto lib = LibraryDb::openInMemory();
    const auto t = lib.addTrack(file("a.flac"));
    lib.setValues(t, Layer::Tag, "title", Strings{"yoake"});
    lib.setValues(t, Layer::Tag, "artist", Strings{"Tag Artist"});
    lib.setValues(t, Layer::External, "title", Strings{"夜明け"}, "musicbrainz");
    CHECK(lib.values(t, "title") == Strings{"夜明け"});
    CHECK(lib.values(t, "artist") == Strings{"Tag Artist"}); // other fields keep their own layer

    lib.setValues(t, Layer::User, "title", Strings{"夜明け（ライブ）"});
    CHECK(lib.values(t, "title") == Strings{"夜明け（ライブ）"});
    CHECK(lib.shownLayer(t, "title") == Layer::User);

    // Undoing the user's edit shows the external value again; nothing below was lost.
    lib.removeValues(t, Layer::User, "title");
    CHECK(lib.values(t, "title") == Strings{"夜明け"});
    lib.removeValues(t, Layer::External, "title");
    CHECK(lib.values(t, "title") == Strings{"yoake"});
}

TEST_CASE("Layers: the user can clear a field the tag has", "[library][layers]")
{
    auto lib = LibraryDb::openInMemory();
    const auto t = lib.addTrack(file("a.flac"));
    lib.setValues(t, Layer::Tag, "comment", Strings{"ripped by XYZ"});
    lib.clearByUser(t, "comment");
    CHECK(lib.values(t, "comment").empty());
    CHECK(lib.shownLayer(t, "comment") == Layer::User); // empty because the user said so
    lib.removeValues(t, Layer::User, "comment");
    CHECK(lib.values(t, "comment") == Strings{"ripped by XYZ"});
}

TEST_CASE("Layers: several values keep their order; setting a layer replaces it", "[library][layers]")
{
    auto lib = LibraryDb::openInMemory();
    const auto t = lib.addTrack(file("a.flac"));
    lib.setValues(t, Layer::Tag, "artist", Strings{"B", "A", "C"});
    CHECK(lib.values(t, "artist") == Strings{"B", "A", "C"});
    lib.setValues(t, Layer::Tag, "artist", Strings{"D"}); // the file's tag was read again
    CHECK(lib.values(t, "artist") == Strings{"D"});
    lib.setValues(t, Layer::Tag, "artist", Strings{});
    CHECK(lib.values(t, "artist").empty());
}

TEST_CASE("Files: a path and an NTFS identity belong to one file", "[library][schema]")
{
    auto lib = LibraryDb::openInMemory();
    auto a = file("a.flac");
    a.volumeSerial = 0x1234;
    a.ntfsFileId = std::array<std::byte, 16>{std::byte{1}};
    lib.addTrack(a);

    auto samePathOtherCase = file("A.FLAC"); // NTFS paths do not tell case apart
    CHECK_THROWS_AS(lib.addTrack(samePathOtherCase), SqliteError);

    auto sameIdentity = file("moved.flac");
    sameIdentity.volumeSerial = 0x1234;
    sameIdentity.ntfsFileId = a.ntfsFileId;
    CHECK_THROWS_AS(lib.addTrack(sameIdentity), SqliteError);

    auto otherVolume = sameIdentity;
    otherVolume.volumeSerial = 0x9999; // the same file ID on another volume is another file
    CHECK_NOTHROW(lib.addTrack(otherVolume));
}

TEST_CASE("Tracks: a missing file keeps its track, history and rating", "[library][schema]")
{
    auto lib = LibraryDb::openInMemory();
    const auto t = lib.addTrack(file("a.flac"));
    auto& db = lib.database();
    db.exec("UPDATE tracks SET rating = 80");
    db.exec("INSERT INTO plays (track_id, started_at, played_frames, completed) VALUES (" + std::to_string(t)
            + ", 1, 100, 1)");
    db.exec("UPDATE files SET present = 0"); // the drive is not connected
    CHECK(count(lib, "SELECT rating FROM tracks") == 80);
    CHECK(count(lib, "SELECT COUNT(*) FROM plays") == 1);

    // Moved to another drive: the track is attached to the new file, the old row goes.
    const auto found = lib.addTrack(file("elsewhere.flac")); // found by the scan as a new file
    const auto newFile = count(lib, "SELECT file_id FROM tracks WHERE id = " + std::to_string(found));
    db.exec("DELETE FROM tracks WHERE id = " + std::to_string(found));
    db.exec("UPDATE tracks SET file_id = " + std::to_string(newFile) + " WHERE id = " + std::to_string(t));
    db.exec("DELETE FROM files WHERE present = 0");
    CHECK(count(lib, "SELECT COUNT(*) FROM plays WHERE track_id = " + std::to_string(t)) == 1);
    CHECK(count(lib, "SELECT rating FROM tracks WHERE id = " + std::to_string(t)) == 80);
}

TEST_CASE("Tracks: removing a file from the library removes what hangs on its track", "[library][schema]")
{
    auto lib = LibraryDb::openInMemory();
    const auto t = lib.addTrack(file("a.flac"));
    const auto id = std::to_string(t);
    auto& db = lib.database();
    lib.setValues(t, Layer::Tag, "title", Strings{"x"});
    db.exec("INSERT INTO plays (track_id, started_at, played_frames, completed) VALUES (" + id + ", 1, 100, 1)");
    db.exec("INSERT INTO track_analysis (track_id, analyzer_version, sample_peak, true_peak, analyzed_at) VALUES (" + id
            + ", 1, 0.9, 1.4, 1)");
    db.exec("INSERT INTO provenance_findings (track_id, check_name, result, evidence, checker_version, checked_at) "
            "VALUES (" + id + ", 'upsampled', 'inconclusive', '{\"cutoffHz\": 21000}', 1, 1)");
    db.exec("DELETE FROM files");
    for (const char* t2 : {"tracks", "track_fields", "plays", "track_analysis", "provenance_findings"})
    {
        CAPTURE(t2);
        CHECK(count(lib, std::string("SELECT COUNT(*) FROM ") + t2) == 0);
    }
}

TEST_CASE("Schema: values the columns would not make sense with are refused", "[library][schema]")
{
    auto lib = LibraryDb::openInMemory();
    const auto id = std::to_string(lib.addTrack(file("a.flac")));
    auto& db = lib.database();
    CHECK_THROWS_AS(db.exec("UPDATE tracks SET rating = 101"), SqliteError);
    // Only the user layer may hold "cleared" (no value).
    CHECK_THROWS_AS(db.exec("INSERT INTO track_fields (track_id, field, layer, idx, value, updated_at) VALUES (" + id
                            + ", 'title', 0, 0, NULL, 1)"),
                    SqliteError);
    // A provenance finding is one of three results, with evidence as JSON.
    CHECK_THROWS_AS(db.exec("INSERT INTO provenance_findings (track_id, check_name, result, checker_version, checked_at) "
                            "VALUES (" + id + ", 'polarity', 'fixed', 1, 1)"),
                    SqliteError);
    CHECK_THROWS_AS(db.exec("INSERT INTO provenance_findings (track_id, check_name, result, evidence, checker_version, "
                            "checked_at) VALUES (" + id + ", 'polarity', 'found', 'not json', 1, 1)"),
                    SqliteError);
    // A track belongs to a file that exists.
    CHECK_THROWS_AS(db.exec("INSERT INTO tracks (file_id, added_at) VALUES (999, 1)"), SqliteError);
}
