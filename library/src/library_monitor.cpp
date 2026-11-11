#include <ap/library/library_monitor.h>

#include <algorithm>
#include <map>
#include <memory>
#include <wchar.h>

namespace ap::library {

namespace fs = std::filesystem;

namespace {

// Whether `p` is `dir` or inside it (case folded the way NTFS compares names).
bool within(const std::wstring& p, const std::wstring& dir)
{
    if (p.size() < dir.size() || _wcsnicmp(p.c_str(), dir.c_str(), dir.size()) != 0)
        return false;
    return p.size() == dir.size() || p[dir.size()] == L'\\';
}

} // namespace

LibraryMonitor::LibraryMonitor(fs::path databaseFile) : LibraryMonitor(std::move(databaseFile), Options{}) {}

LibraryMonitor::LibraryMonitor(fs::path databaseFile, Options options)
    : databaseFile_(std::move(databaseFile)), options_(options)
{
    thread_ = std::thread([this] { run(); });
}

LibraryMonitor::~LibraryMonitor()
{
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void LibraryMonitor::post(std::int64_t rootId, const WatchEvent& e)
{
    {
        std::lock_guard lock(mutex_);
        pending_.push_back({rootId, e});
        lastEvent_ = std::chrono::steady_clock::now();
    }
    wake_.notify_all();
}

bool LibraryMonitor::waitUntilIdle(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(mutex_);
    return idle_.wait_for(lock, timeout, [this] { return !busy_ && pending_.empty(); });
}

MonitorStats LibraryMonitor::stats() const
{
    std::lock_guard lock(mutex_);
    return stats_;
}

void LibraryMonitor::run()
{
    std::vector<std::unique_ptr<FolderWatcher>> watchers;
    try
    {
        auto lib = LibraryDb::open(databaseFile_);
        Scanner scanner(lib);

        std::vector<std::pair<std::int64_t, fs::path>> roots;
        {
            auto st = lib.database().prepare("SELECT id FROM roots ORDER BY id");
            while (st.step())
                if (const auto p = rootPath(lib, st.integer(0)))
                    roots.emplace_back(st.integer(0), *p);
        }

        // Watch first, then scan: a change during the scan is not lost.
        for (const auto& [id, path] : roots)
        {
            try
            {
                watchers.push_back(std::make_unique<FolderWatcher>(
                    path, [this, id](const WatchEvent& e) { post(id, e); }, options_.watcher));
            }
            catch (const std::exception& e)
            {
                std::lock_guard lock(mutex_);
                ++stats_.watchFailures;
                stats_.errors.push_back(e.what());
            }
        }
        for (const auto& [id, path] : roots)
        {
            const auto r = scanner.scanRoot(id); // a missing folder: everything in it is missing
            std::lock_guard lock(mutex_);
            ++stats_.startupScans;
            stats_.total.append(r);
        }

        for (;;)
        {
            std::vector<Pending> batch;
            {
                std::unique_lock lock(mutex_);
                busy_ = false;
                if (pending_.empty())
                    idle_.notify_all();
                wake_.wait(lock, [this] { return stop_ || !pending_.empty(); });
                if (stop_)
                    return;
                // Wait until the folder has been quiet for a moment.
                while (!stop_)
                {
                    const auto due = lastEvent_ + options_.settle;
                    if (std::chrono::steady_clock::now() >= due)
                        break;
                    wake_.wait_until(lock, due);
                }
                if (stop_)
                    return;
                batch.swap(pending_);
                busy_ = true;
            }

            // Per folder: everything again, or each changed place once (a place
            // inside another one in the list is covered by it).
            std::map<std::int64_t, bool> whole;
            std::map<std::int64_t, std::vector<std::wstring>> places;
            for (const auto& p : batch)
            {
                if (p.event.kind == WatchEvent::Kind::Changed)
                {
                    places[p.rootId].push_back(p.event.path.lexically_normal().wstring());
                    continue;
                }
                whole[p.rootId] = true;
                std::lock_guard lock(mutex_);
                if (p.event.kind == WatchEvent::Kind::Overflow)
                    ++stats_.overflowRescans;
                else
                {
                    ++stats_.watchFailures;
                    stats_.errors.push_back(p.event.detail);
                }
            }
            ScanReport report;
            std::size_t scans = 0;
            std::vector<std::string> errors;
            // One failed comparison (e.g. the database was busy) is recorded; the
            // monitor goes on. The next startup scan catches up.
            auto attempt = [&](auto&& scan) {
                try
                {
                    report.append(scan());
                }
                catch (const std::exception& e)
                {
                    errors.push_back(e.what());
                }
            };
            for (const auto& [id, isWhole] : whole)
            {
                attempt([&] { return scanner.scanRoot(id); });
                places.erase(id);
            }
            for (auto& [id, list] : places)
            {
                // Shortest first, so a folder comes before what is inside it.
                std::sort(list.begin(), list.end(),
                          [](const std::wstring& a, const std::wstring& b) { return a.size() < b.size(); });
                std::vector<std::wstring> chosen;
                for (const auto& p : list)
                    if (std::none_of(chosen.begin(), chosen.end(), [&](const std::wstring& c) { return within(p, c); }))
                        chosen.push_back(p);
                for (const auto& p : chosen)
                {
                    attempt([&] { return scanner.scanPath(id, fs::path(p)); });
                    ++scans;
                }
            }
            std::lock_guard lock(mutex_);
            stats_.pathScans += scans;
            stats_.total.append(report);
            stats_.errors.insert(stats_.errors.end(), errors.begin(), errors.end());
        }
    }
    catch (const std::exception& e)
    {
        std::lock_guard lock(mutex_);
        stats_.errors.push_back(e.what());
        busy_ = false;
        pending_.clear();
        idle_.notify_all();
    }
    // The watchers stop (and join) before this thread ends.
}

} // namespace ap::library
