#pragma once

// Watching a folder while the application runs (task 5-2): ReadDirectoryChangesW
// on the folder and everything under it, on a thread of its own.
//
// It only reports where something changed; LibraryMonitor compares those
// places with the database. When more changes arrive than the buffer holds,
// Windows cannot say which: that is reported as an overflow, and the whole
// folder is listed again.

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

namespace ap::library {

struct WatchEvent
{
    enum class Kind
    {
        Changed,  // `path` (a file or a folder) was added, removed, renamed or written
        Overflow, // changes were lost: list the whole folder again
        Failed,   // the watch stopped (the folder was removed, the drive went away); `detail` says why
    };
    Kind kind = Kind::Changed;
    std::filesystem::path path;
    std::string detail;
};

class FolderWatcher
{
public:
    struct Options
    {
        // 64 KB: the largest buffer that works over the network (SMB).
        std::size_t bufferBytes = 64 * 1024;
        // Tests only: wait this long after each read before asking for the next
        // one, so changes pile up (to make an overflow happen).
        std::chrono::milliseconds pauseAfterRead{0};
    };

    // Starts watching. Throws std::system_error when the folder cannot be opened.
    // `onEvent` is called on the watcher's thread.
    FolderWatcher(std::filesystem::path folder, std::function<void(const WatchEvent&)> onEvent);
    FolderWatcher(std::filesystem::path folder, std::function<void(const WatchEvent&)> onEvent, Options options);
    ~FolderWatcher(); // stops and waits for the thread
    FolderWatcher(const FolderWatcher&) = delete;
    FolderWatcher& operator=(const FolderWatcher&) = delete;

    const std::filesystem::path& folder() const noexcept { return folder_; }

private:
    void run();

    std::filesystem::path folder_;
    std::function<void(const WatchEvent&)> onEvent_;
    Options options_;
    void* dir_ = nullptr;  // HANDLE of the folder
    void* stop_ = nullptr; // HANDLE of an event
    std::thread thread_;
};

} // namespace ap::library
