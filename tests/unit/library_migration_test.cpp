// Task 5-1: how a library database is brought up to date (schema and migrations).
//
// - A new database gets every step; an up-to-date one is left alone.
// - Each step is one transaction: a failing step leaves the database exactly
//   at the version before it.
// - An existing database is copied before it is changed.
// - A database from a newer application is not touched.

#include <ap/library/library_db.h>
#include <ap/library/migration.h>
#include <ap/library/sqlite.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ap::library;

namespace {

const Migration kV1{1, "people", "CREATE TABLE people (id INTEGER PRIMARY KEY, name TEXT NOT NULL);"};
const Migration kV2{2, "people.age", "ALTER TABLE people ADD COLUMN age INTEGER;"};
const Migration kV3Broken{3, "broken", "CREATE TABLE pets (id INTEGER PRIMARY KEY); THIS IS NOT SQL;"};

std::vector<char> readAll(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool hasTable(Database& db, const std::string& name)
{
    auto st = db.prepare("SELECT 1 FROM sqlite_schema WHERE type = 'table' AND name = ?");
    st.bind(1, name);
    return st.step();
}

} // namespace

TEST_CASE("Migration: a new database gets every step", "[library][migration]")
{
    auto db = Database::openInMemory();
    const Migration steps[] = {kV1, kV2};
    const auto r = migrate(db, steps);
    CHECK(r.fromVersion == 0);
    CHECK(r.toVersion == 2);
    CHECK_FALSE(r.backup.has_value()); // nothing to keep in a new database
    CHECK(db.userVersion() == 2);
    db.exec("INSERT INTO people (name, age) VALUES ('a', 3)");
}

TEST_CASE("Migration: an up-to-date database is left alone", "[library][migration]")
{
    ap::test::TempDir dir;
    const auto file = dir.path() / "lib.db";
    const Migration steps[] = {kV1, kV2};
    {
        auto db = Database::open(file);
        migrate(db, steps);
    }
    auto db = Database::open(file);
    const auto r = migrate(db, steps, dir.path() / "backup.db");
    CHECK(r.fromVersion == 2);
    CHECK(r.toVersion == 2);
    CHECK_FALSE(r.backup.has_value());
    CHECK_FALSE(std::filesystem::exists(dir.path() / "backup.db"));
}

TEST_CASE("Migration: an existing database keeps its data and is copied first", "[library][migration]")
{
    ap::test::TempDir dir;
    const auto file = dir.path() / "lib.db";
    {
        auto db = Database::open(file);
        const Migration v1[] = {kV1};
        migrate(db, v1);
        db.exec("INSERT INTO people (name) VALUES ('Hanako')");
    }
    auto db = Database::open(file);
    const Migration steps[] = {kV1, kV2};
    const auto r = migrate(db, steps, dir.path() / "lib.db.v1.bak");
    CHECK(r.fromVersion == 1);
    CHECK(r.toVersion == 2);
    REQUIRE(r.backup.has_value());
    CHECK(*r.backup == dir.path() / "lib.db.v1.bak");

    auto st = db.prepare("SELECT name, age FROM people");
    REQUIRE(st.step());
    CHECK(st.text(0) == "Hanako");
    CHECK(st.isNull(1));

    // The backup is the database as it was: version 1, no new column.
    auto old = Database::open(*r.backup);
    CHECK(old.userVersion() == 1);
    CHECK_THROWS_AS(old.exec("SELECT age FROM people"), SqliteError);
    auto names = old.prepare("SELECT name FROM people");
    REQUIRE(names.step());
    CHECK(names.text(0) == "Hanako");
}

TEST_CASE("Migration: a failing step changes nothing", "[library][migration]")
{
    ap::test::TempDir dir;
    const auto file = dir.path() / "lib.db";
    {
        auto db = Database::open(file);
        const Migration v1[] = {kV1};
        migrate(db, v1);
        db.exec("INSERT INTO people (name) VALUES ('Taro')");
    }
    auto db = Database::open(file);
    const Migration steps[] = {kV1, kV2, kV3Broken};
    try
    {
        migrate(db, steps, dir.path() / "lib.db.v1.bak");
        FAIL("expected MigrationError");
    }
    catch (const MigrationError& e)
    {
        CHECK(e.failedVersion() == 3);
        CHECK(e.reachedVersion() == 2); // step 2 completed, step 3 rolled back
    }
    CHECK(db.userVersion() == 2);
    CHECK_FALSE(hasTable(db, "pets")); // the first statement of the broken step did not stay
    CHECK(std::filesystem::exists(dir.path() / "lib.db.v1.bak"));
}

TEST_CASE("Migration: a database from a newer application is not touched", "[library][migration]")
{
    ap::test::TempDir dir;
    const auto file = dir.path() / "lib.db";
    {
        auto db = Database::open(file);
        db.exec("CREATE TABLE future (x); PRAGMA user_version = 9;");
    }
    const auto before = readAll(file);
    {
        auto db = Database::open(file);
        const Migration steps[] = {kV1, kV2};
        try
        {
            migrate(db, steps, dir.path() / "backup.db");
            FAIL("expected NewerSchemaError");
        }
        catch (const NewerSchemaError& e)
        {
            CHECK(e.found() == 9);
            CHECK(e.supported() == 2);
        }
    }
    CHECK(readAll(file) == before);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "backup.db"));
}

TEST_CASE("Migration: steps must be numbered 1, 2, ... in order", "[library][migration]")
{
    auto db = Database::openInMemory();
    const Migration gap[] = {kV1, Migration{3, "skips 2", "SELECT 1;"}};
    CHECK_THROWS_AS(migrate(db, gap), std::invalid_argument);
    CHECK(db.userVersion() == 0);
}

TEST_CASE("Library: a new database is created at the current schema version", "[library][migration]")
{
    ap::test::TempDir dir;
    const auto file = dir.path() / "library.db";
    {
        auto lib = LibraryDb::open(file);
        CHECK(lib.schemaVersion() == librarySchemaVersion());
        CHECK(lib.migration().fromVersion == 0);
        CHECK_FALSE(lib.migration().backup.has_value());
        for (const char* t : {"roots", "files", "tracks", "track_fields", "track_analysis", "album_analysis",
                              "provenance_findings", "plays"})
        {
            CAPTURE(t);
            CHECK(hasTable(lib.database(), t));
        }
        // Foreign keys are enforced and the schema holds together.
        auto fk = lib.database().prepare("PRAGMA foreign_keys");
        REQUIRE(fk.step());
        CHECK(fk.integer(0) == 1);
        auto check = lib.database().prepare("PRAGMA foreign_key_check");
        CHECK_FALSE(check.step());
    }
    auto again = LibraryDb::open(file);
    CHECK(again.migration().fromVersion == librarySchemaVersion());
    CHECK(again.migration().toVersion == librarySchemaVersion());
}

TEST_CASE("Library: refuses a database from a newer application", "[library][migration]")
{
    ap::test::TempDir dir;
    const auto file = dir.path() / "library.db";
    {
        auto db = Database::open(file);
        db.exec("PRAGMA user_version = " + std::to_string(librarySchemaVersion() + 1));
    }
    CHECK_THROWS_AS(LibraryDb::open(file), NewerSchemaError);
}
