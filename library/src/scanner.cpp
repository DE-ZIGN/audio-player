#include <ap/library/scanner.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace ap::library {

namespace fs = std::filesystem;

namespace {

// std::filesystem's clock on MSVC counts 100 ns since 1601, as FILETIME does.
static_assert(std::is_same_v<fs::file_time_type::period, std::ratio<1, 10'000'000>>);
constexpr std::int64_t kFileTimeToUnix = 116'444'736'000'000'000; // 100 ns from 1601 to 1970

std::int64_t unixMsFromTicks(std::int64_t ticks)
{
    return (ticks - kFileTimeToUnix) / 10'000;
}

std::string utf8(const fs::path& p)
{
    const auto u8 = p.lexically_normal().u8string();
    return std::string(u8.begin(), u8.end());
}

fs::path fromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

// SQLite's NOCASE folds ASCII only; so does this, so the two agree.
std::string foldCase(std::string s)
{
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string withoutTrailingSeparator(std::string s)
{
    while (s.size() > 3 && (s.back() == '\\' || s.back() == '/'))
        s.pop_back();
    return s;
}

struct DiskFile
{
    fs::path path;
    std::string key; // UTF-8, case folded
    std::uint64_t size = 0;
    std::int64_t modifiedAt = 0;
};

struct Row
{
    std::int64_t id = 0;
    std::string path;
    std::int64_t size = 0;
    std::int64_t modifiedAt = 0;
    bool present = true;
};

} // namespace

bool isMusicFile(const fs::path& p)
{
    static constexpr std::array<std::string_view, 9> kExtensions = {".wav", ".flac", ".aif", ".aiff", ".aifc",
                                                                     ".m4a", ".mp3", ".dsf", ".dff"};
    const auto ext = foldCase(utf8(p.extension()));
    return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
}

std::optional<FileFacts> statFile(const fs::path& p)
{
    // No access to the contents is needed for the attributes and the identity;
    // everyone else may go on reading, writing and deleting.
    HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return std::nullopt;
    FileFacts f;
    f.path = p;
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok = GetFileInformationByHandle(h, &info) != 0;
    if (ok)
    {
        f.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        const auto ticks = (static_cast<std::int64_t>(info.ftLastWriteTime.dwHighDateTime) << 32)
                         | info.ftLastWriteTime.dwLowDateTime;
        f.modifiedAt = unixMsFromTicks(ticks);
        FILE_ID_INFO id{};
        if (GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof id)) // not on FAT/exFAT
        {
            f.volumeSerial = id.VolumeSerialNumber;
            std::array<std::byte, 16> bytes{};
            std::memcpy(bytes.data(), id.FileId.Identifier, bytes.size());
            f.ntfsFileId = bytes;
        }
    }
    CloseHandle(h);
    if (!ok)
        return std::nullopt;
    return f;
}

void ScanReport::append(const ScanReport& o)
{
    added.insert(added.end(), o.added.begin(), o.added.end());
    changed.insert(changed.end(), o.changed.begin(), o.changed.end());
    missing.insert(missing.end(), o.missing.begin(), o.missing.end());
    returned.insert(returned.end(), o.returned.begin(), o.returned.end());
    moved.insert(moved.end(), o.moved.begin(), o.moved.end());
    unchanged += o.unchanged;
    unreadable += o.unreadable;
}

std::optional<fs::path> rootPath(LibraryDb& db, std::int64_t rootId)
{
    auto st = db.database().prepare("SELECT path FROM roots WHERE id = ?");
    st.bind(1, rootId);
    if (!st.step())
        return std::nullopt;
    return fromUtf8(st.text(0));
}

ScanReport Scanner::scanRoot(std::int64_t rootId)
{
    const auto root = rootPath(db_, rootId);
    if (!root)
        throw std::invalid_argument("no such watched folder");
    return reconcile(rootId, *root);
}

ScanReport Scanner::scanPath(std::int64_t rootId, const fs::path& path)
{
    return reconcile(rootId, path);
}

ScanReport Scanner::reconcile(std::int64_t rootId, const fs::path& path)
{
    const auto root = rootPath(db_, rootId);
    if (!root)
        throw std::invalid_argument("no such watched folder");
    const std::string rootStr = withoutTrailingSeparator(utf8(*root));
    const std::string target = withoutTrailingSeparator(utf8(path));
    const std::string rootKey = foldCase(rootStr);
    const std::string targetKey = foldCase(target);
    const std::string rootPrefix = rootKey.ends_with('\\') ? rootKey : rootKey + '\\';
    if (targetKey != rootKey && !targetKey.starts_with(rootPrefix))
        throw std::invalid_argument("the path is not in the watched folder");

    ScanReport report;

    // What is on disk now under `path` (directory entries: no file is opened).
    std::unordered_map<std::string, DiskFile> disk;
    auto take = [&](const fs::directory_entry& e) {
        std::error_code ec;
        if (!e.is_regular_file(ec) || !isMusicFile(e.path()))
            return;
        const auto size = e.file_size(ec);
        if (ec)
        {
            ++report.unreadable;
            return;
        }
        const auto time = e.last_write_time(ec);
        if (ec)
        {
            ++report.unreadable;
            return;
        }
        DiskFile f{e.path().lexically_normal(), {}, size, unixMsFromTicks(time.time_since_epoch().count())};
        f.key = foldCase(utf8(f.path));
        disk.emplace(f.key, std::move(f));
    };
    std::error_code ec;
    const fs::path here = fromUtf8(target);
    const auto status = fs::status(here, ec);
    if (fs::is_directory(status))
    {
        for (fs::recursive_directory_iterator it(here, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
            take(*it);
        if (ec)
            ++report.unreadable; // the listing stopped early: what was not reached is left as it was
    }
    else if (fs::is_regular_file(status))
    {
        take(fs::directory_entry(here, ec));
    }

    auto& db = db_.database();
    Database::Transaction tx(db);
    const auto now = nowUnixMs();

    // What the database has under `path` (present or missing).
    const std::string prefix = target.ends_with('\\') ? target : target + '\\';
    const std::string upper = prefix.substr(0, prefix.size() - 1) + ']'; // ']' follows '\' in code order
    auto rows = db.prepare("SELECT id, path, size, modified_at, present FROM files "
                           "WHERE path = ?1 OR (path >= ?2 AND path < ?3)");
    rows.bind(1, target).bind(2, prefix).bind(3, upper);
    std::vector<Row> known;
    while (rows.step())
        known.push_back(Row{rows.integer(0), rows.text(1), rows.integer(2), rows.integer(3), rows.integer(4) != 0});

    std::unordered_map<std::int64_t, std::size_t> missingNow; // row id -> index in report.missing
    auto update = db.prepare("UPDATE files SET size = ?2, modified_at = ?3, present = 1, last_seen_at = ?4, "
                             "needs_read = CASE WHEN ?5 THEN 1 ELSE needs_read END WHERE id = ?1");
    auto markMissing = db.prepare("UPDATE files SET present = 0 WHERE id = ?");
    for (const auto& r : known)
    {
        const auto it = disk.find(foldCase(r.path));
        if (it == disk.end())
        {
            if (r.present)
            {
                markMissing.reset();
                markMissing.bind(1, r.id).run();
                missingNow.emplace(r.id, report.missing.size());
                report.missing.push_back(fromUtf8(r.path));
            }
            continue;
        }
        const auto& f = it->second;
        const bool changed = static_cast<std::int64_t>(f.size) != r.size || f.modifiedAt != r.modifiedAt;
        if (changed || !r.present)
        {
            update.reset();
            update.bind(1, r.id)
                .bind(2, static_cast<std::int64_t>(f.size))
                .bind(3, f.modifiedAt)
                .bind(4, now)
                .bind(5, static_cast<std::int64_t>(changed))
                .run();
            if (!r.present)
                report.returned.push_back(f.path);
            else
                report.changed.push_back(f.path);
        }
        else
        {
            ++report.unchanged;
        }
        disk.erase(it);
    }

    // The rest is new here: moved from elsewhere (the same NTFS identity), or added.
    auto byIdentity = db.prepare("SELECT id, path, size, modified_at FROM files WHERE volume_serial = ? AND ntfs_file_id = ?");
    auto move = db.prepare("UPDATE files SET path = ?2, root_id = ?3, size = ?4, modified_at = ?5, present = 1, "
                           "last_seen_at = ?6, needs_read = CASE WHEN ?7 THEN 1 ELSE needs_read END WHERE id = ?1");
    std::vector<const DiskFile*> fresh;
    for (const auto& [key, f] : disk)
        fresh.push_back(&f);
    std::sort(fresh.begin(), fresh.end(), [](const DiskFile* a, const DiskFile* b) { return a->key < b->key; });
    for (const DiskFile* f : fresh)
    {
        auto facts = statFile(f->path);
        if (!facts)
        {
            ++report.unreadable;
            continue;
        }
        if (facts->volumeSerial && facts->ntfsFileId)
        {
            byIdentity.reset();
            byIdentity.bind(1, static_cast<std::int64_t>(*facts->volumeSerial))
                .bind(2, std::span<const std::byte>(*facts->ntfsFileId));
            if (byIdentity.step())
            {
                const auto id = byIdentity.integer(0);
                const auto oldPath = fromUtf8(byIdentity.text(1));
                const bool changed = static_cast<std::int64_t>(facts->size) != byIdentity.integer(2)
                                  || facts->modifiedAt != byIdentity.integer(3);
                const auto still = statFile(oldPath);
                const bool hardLink = still && still->volumeSerial == facts->volumeSerial
                                   && still->ntfsFileId == facts->ntfsFileId;
                if (!hardLink)
                {
                    move.reset();
                    move.bind(1, id)
                        .bind(2, utf8(f->path))
                        .bind(3, rootId)
                        .bind(4, static_cast<std::int64_t>(facts->size))
                        .bind(5, facts->modifiedAt)
                        .bind(6, now)
                        .bind(7, static_cast<std::int64_t>(changed))
                        .run();
                    {
                        auto tracks = db.prepare("SELECT id FROM tracks WHERE file_id = ?");
                        tracks.bind(1, id);
                        while (tracks.step())
                            db_.refreshSearch(tracks.integer(0)); // found by its new file name
                    }
                    if (const auto m = missingNow.find(id); m != missingNow.end())
                        report.missing[m->second].clear(); // not gone: moved (removed below)
                    report.moved.emplace_back(oldPath, f->path);
                    continue;
                }
                // Another name of the same file (a hard link): a file of its own
                // here, without the identity the first name holds.
                facts->volumeSerial.reset();
                facts->ntfsFileId.reset();
            }
        }
        db_.addTrack(*facts, rootId);
        report.added.push_back(f->path);
    }
    std::erase_if(report.missing, [](const fs::path& p) { return p.empty(); });
    tx.commit();
    return report;
}

} // namespace ap::library
