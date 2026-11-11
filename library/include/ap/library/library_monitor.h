#pragma once

// Keeps the library database in line with the watched folders (task 5-2,
// CLAUDE.md chapter 7): a scan of each folder at start (the startup diff
// scan), then ReadDirectoryChangesW while running.
//
// Changes are collected until the folder has been quiet for a moment (a copy
// of an album is many events), then only the places that changed are
// compared again (Scanner::scanPath). After an overflow (changes lost), or
// when the watch fails (the folder or the drive went away), the whole folder
// is scanned again.
//
// Runs on a thread of its own with its own connection to the database.

#include <ap/library/folder_watcher.h>
#include <ap/library/scanner.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ap::library {

struct MonitorStats
{
    std::size_t startupScans = 0;    // whole folders scanned at start
    std::size_t pathScans = 0;       // places compared again after changes
    std::size_t overflowRescans = 0; // whole folders scanned again after an overflow
    std::size_t watchFailures = 0;   // folders whose watch stopped (scanned again, no longer watched)
    std::vector<std::string> errors; // English, for logs
    ScanReport total;                // everything found since the start
};

class LibraryMonitor
{
public:
    struct Options
    {
        std::chrono::milliseconds settle{300}; // quiet time before the changes are compared
        FolderWatcher::Options watcher;
    };

    // Opens `databaseFile` (it must exist with its watched folders), scans and watches.
    explicit LibraryMonitor(std::filesystem::path databaseFile);
    LibraryMonitor(std::filesystem::path databaseFile, Options options);
    ~LibraryMonitor();
    LibraryMonitor(const LibraryMonitor&) = delete;
    LibraryMonitor& operator=(const LibraryMonitor&) = delete;

    // Until the startup scans are done and nothing is waiting to be compared.
    // Changes Windows has not reported yet are not known here. false: timed out.
    bool waitUntilIdle(std::chrono::milliseconds timeout);

    MonitorStats stats() const;

private:
    struct Pending
    {
        std::int64_t rootId = 0;
        WatchEvent event;
    };

    void run();
    void post(std::int64_t rootId, const WatchEvent& e);

    std::filesystem::path databaseFile_;
    Options options_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::vector<Pending> pending_;
    std::chrono::steady_clock::time_point lastEvent_;
    bool busy_ = true; // starting, or comparing
    bool stop_ = false;
    MonitorStats stats_;
    std::thread thread_;
};

} // namespace ap::library
