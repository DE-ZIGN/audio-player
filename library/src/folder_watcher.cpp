#include <ap/library/folder_watcher.h>

#include <windows.h>

#include <system_error>
#include <vector>

namespace ap::library {

FolderWatcher::FolderWatcher(std::filesystem::path folder, std::function<void(const WatchEvent&)> onEvent)
    : FolderWatcher(std::move(folder), std::move(onEvent), Options{})
{
}

FolderWatcher::FolderWatcher(std::filesystem::path folder, std::function<void(const WatchEvent&)> onEvent,
                             Options options)
    : folder_(std::move(folder)), onEvent_(std::move(onEvent)), options_(options)
{
    // Others may go on reading, writing, renaming and deleting in the folder.
    HANDLE dir = CreateFileW(folder_.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (dir == INVALID_HANDLE_VALUE)
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "cannot watch the folder");
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop)
    {
        const auto e = GetLastError();
        CloseHandle(dir);
        throw std::system_error(static_cast<int>(e), std::system_category(), "CreateEvent");
    }
    dir_ = dir;
    stop_ = stop;
    thread_ = std::thread([this] { run(); });
}

FolderWatcher::~FolderWatcher()
{
    SetEvent(static_cast<HANDLE>(stop_));
    if (thread_.joinable())
        thread_.join();
    CloseHandle(static_cast<HANDLE>(dir_));
    CloseHandle(static_cast<HANDLE>(stop_));
}

void FolderWatcher::run()
{
    const HANDLE dir = static_cast<HANDLE>(dir_);
    const HANDLE stop = static_cast<HANDLE>(stop_);
    // DWORD-aligned, as ReadDirectoryChangesW needs.
    std::vector<DWORD> buffer((options_.bufferBytes + sizeof(DWORD) - 1) / sizeof(DWORD));
    const DWORD bytes = static_cast<DWORD>(buffer.size() * sizeof(DWORD));
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent)
    {
        onEvent_({WatchEvent::Kind::Failed, folder_, "CreateEvent failed"});
        return;
    }
    constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE
                            | FILE_NOTIFY_CHANGE_LAST_WRITE;
    auto fail = [&](DWORD error, const char* what) {
        onEvent_({WatchEvent::Kind::Failed, folder_,
                  std::string(what) + ": " + std::system_category().message(static_cast<int>(error))});
    };

    for (;;)
    {
        ResetEvent(ov.hEvent);
        if (!ReadDirectoryChangesW(dir, buffer.data(), bytes, TRUE, kFilter, nullptr, &ov, nullptr))
        {
            fail(GetLastError(), "ReadDirectoryChangesW");
            break;
        }
        const HANDLE waits[] = {stop, ov.hEvent};
        const DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0)
        {
            CancelIoEx(dir, &ov);
            DWORD ignored = 0;
            GetOverlappedResult(dir, &ov, &ignored, TRUE);
            break;
        }
        DWORD got = 0;
        if (!GetOverlappedResult(dir, &ov, &got, FALSE))
        {
            const DWORD e = GetLastError();
            if (e == ERROR_NOTIFY_ENUM_DIR)
            {
                onEvent_({WatchEvent::Kind::Overflow, folder_, {}});
                continue;
            }
            fail(e, "watching the folder");
            break;
        }
        if (got == 0)
        {
            // More changes than the buffer holds: which ones is not known.
            onEvent_({WatchEvent::Kind::Overflow, folder_, {}});
        }
        else
        {
            const auto* p = reinterpret_cast<const std::byte*>(buffer.data());
            for (;;)
            {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(p);
                const std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
                onEvent_({WatchEvent::Kind::Changed, folder_ / name, {}});
                if (info->NextEntryOffset == 0)
                    break;
                p += info->NextEntryOffset;
            }
        }
        if (options_.pauseAfterRead.count() > 0
            && WaitForSingleObject(stop, static_cast<DWORD>(options_.pauseAfterRead.count())) == WAIT_OBJECT_0)
            break;
    }
    CloseHandle(ov.hEvent);
}

} // namespace ap::library
