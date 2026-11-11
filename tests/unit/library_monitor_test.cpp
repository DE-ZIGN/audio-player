// Task 5-2: watching the folders while running. Changes reported by
// ReadDirectoryChangesW (real ones, in a temporary folder) bring the database
// in line: added, changed, deleted, renamed. When the buffer overflows, the
// whole folder is listed again and nothing is lost.

#include <ap/library/folder_watcher.h>
#include <ap/library/library_db.h>
#include <ap/library/library_monitor.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace ap::library;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

fs::path put(const fs::path& p, std::size_t bytes = 100)
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << std::string(bytes, 'x');
    return p;
}

std::int64_t scalar(const fs::path& db, const std::string& sql)
{
    auto lib = LibraryDb::open(db); // a second connection, as the screen would have
    auto st = lib.database().prepare(sql);
    REQUIRE(st.step());
    return st.integer(0);
}

// Waits up to 10 s for `pred` (Windows reports changes a little later).
bool eventually(const std::function<bool()>& pred)
{
    for (auto until = std::chrono::steady_clock::now() + 10s; std::chrono::steady_clock::now() < until;
         std::this_thread::sleep_for(50ms))
        if (pred())
            return true;
    return pred();
}

struct Fixture
{
    ap::test::TempDir dir;
    fs::path root = dir.path() / "Music";
    fs::path db = dir.path() / "library.db";

    Fixture()
    {
        fs::create_directories(root);
        auto lib = LibraryDb::open(db);
        lib.addRoot(root);
    }

    std::int64_t present() const
    {
        return scalar(db, "SELECT COUNT(*) FROM files WHERE present = 1");
    }
};

} // namespace

TEST_CASE("Watcher: reports where something changed", "[library][watch]")
{
    ap::test::TempDir dir;
    std::mutex m;
    std::vector<WatchEvent> events;
    {
        FolderWatcher w(dir.path(), [&](const WatchEvent& e) {
            std::lock_guard lock(m);
            events.push_back(e);
        });
        put(dir.path() / "sub" / "a.flac");
        CHECK(eventually([&] {
            std::lock_guard lock(m);
            return std::any_of(events.begin(), events.end(), [&](const WatchEvent& e) {
                return e.kind == WatchEvent::Kind::Changed && e.path == dir.path() / "sub" / "a.flac";
            });
        }));
    }
    CHECK_THROWS_AS(FolderWatcher(dir.path() / "no such folder", [](const WatchEvent&) {}), std::system_error);
}

TEST_CASE("Monitor: the startup scan finds what is already there", "[library][watch]")
{
    Fixture f;
    put(f.root / "a.flac");
    put(f.root / "Album" / "b.mp3");
    LibraryMonitor monitor(f.db);
    REQUIRE(monitor.waitUntilIdle(10s));
    const auto s = monitor.stats();
    CHECK(s.startupScans == 1);
    CHECK(s.total.added.size() == 2);
    CHECK(s.errors.empty());
    CHECK(f.present() == 2);
}

TEST_CASE("Monitor: added, changed, deleted and renamed files while running", "[library][watch]")
{
    Fixture f;
    const auto a = put(f.root / "a.flac", 100);
    LibraryMonitor monitor(f.db);
    REQUIRE(monitor.waitUntilIdle(10s));

    // Added: a whole album folder copied in.
    for (int i = 1; i <= 5; ++i)
        put(f.root / "Album" / ("0" + std::to_string(i) + ".flac"));
    put(f.root / "Album" / "cover.jpg");
    CHECK(eventually([&] { return f.present() == 6; }));

    // Changed: to be read again.
    {
        auto lib = LibraryDb::open(f.db);
        lib.database().exec("UPDATE files SET needs_read = 0");
    }
    put(a, 150);
    CHECK(eventually([&] { return scalar(f.db, "SELECT COUNT(*) FROM files WHERE needs_read = 1") == 1; }));

    // Renamed folder: the same tracks at their new paths.
    const auto tracksBefore = scalar(f.db, "SELECT COUNT(*) FROM tracks");
    fs::rename(f.root / "Album", f.root / "Album (2026)");
    CHECK(eventually([&] {
        return scalar(f.db, "SELECT COUNT(*) FROM files WHERE present = 1 AND path LIKE '%Album (2026)%'") == 5;
    }));
    CHECK(scalar(f.db, "SELECT COUNT(*) FROM tracks") == tracksBefore);

    // Deleted: kept as missing.
    fs::remove(a);
    CHECK(eventually([&] { return f.present() == 5; }));
    CHECK(scalar(f.db, "SELECT COUNT(*) FROM files WHERE present = 0") == 1);
    REQUIRE(monitor.waitUntilIdle(10s));
    CHECK(monitor.stats().errors.empty());
}

TEST_CASE("Monitor: after an overflow the whole folder is listed again", "[library][watch]")
{
    Fixture f;
    LibraryMonitor::Options o;
    o.watcher.bufferBytes = 512;     // room for a few changes only
    o.watcher.pauseAfterRead = 300ms; // and slow to ask for more
    LibraryMonitor monitor(f.db, o);
    REQUIRE(monitor.waitUntilIdle(10s));

    constexpr int kFiles = 300;
    for (int i = 0; i < kFiles; ++i)
        put(f.root / "Many" / ("track " + std::to_string(i) + ".flac"));
    CHECK(eventually([&] { return f.present() == kFiles; }));
    CHECK(eventually([&] { return monitor.stats().overflowRescans >= 1; }));
    CHECK(monitor.stats().errors.empty());
}

TEST_CASE("Monitor: when the watched folder is deleted, its files are missing", "[library][watch]")
{
    Fixture f;
    put(f.root / "a.flac");
    put(f.root / "b.flac");
    LibraryMonitor monitor(f.db);
    REQUIRE(monitor.waitUntilIdle(10s));
    REQUIRE(f.present() == 2);

    fs::remove_all(f.root);
    CHECK(eventually([&] { return f.present() == 0; }));
    CHECK(scalar(f.db, "SELECT COUNT(*) FROM tracks") == 2); // kept, as missing
}
