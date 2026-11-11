#include <ap/library/library_db.h>

#include <chrono>

namespace ap::library {

namespace {

std::string utf8(const std::filesystem::path& p)
{
    const auto u8 = p.lexically_normal().u8string(); // on Windows, "/" becomes "\"
    return std::string(u8.begin(), u8.end());
}

// Field names are stored in lower case (ASCII).
std::string fieldName(std::string_view field)
{
    std::string f(field);
    for (auto& c : f)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return f;
}

} // namespace

std::int64_t nowUnixMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

LibraryDb LibraryDb::open(const std::filesystem::path& file)
{
    auto db = Database::open(file);
    const int current = db.userVersion();
    auto backup = file;
    backup += ".v" + std::to_string(current) + ".bak";
    auto m = migrate(db, libraryMigrations(), backup);
    // Readers (the screen) do not wait for the writer (the scan), and a crash
    // does not leave a half-written database.
    db.exec("PRAGMA journal_mode = WAL;");
    return LibraryDb(std::move(db), std::move(m));
}

LibraryDb LibraryDb::openInMemory()
{
    auto db = Database::openInMemory();
    auto m = migrate(db, libraryMigrations());
    return LibraryDb(std::move(db), std::move(m));
}

std::int64_t LibraryDb::addRoot(const std::filesystem::path& folder)
{
    auto st = db_.prepare("INSERT INTO roots (path, added_at) VALUES (?, ?)");
    st.bind(1, utf8(folder)).bind(2, nowUnixMs()).run();
    return db_.lastInsertRowId();
}

std::int64_t LibraryDb::addTrack(const FileFacts& f, std::optional<std::int64_t> rootId)
{
    const auto now = nowUnixMs();
    Database::Transaction tx(db_);
    auto file = db_.prepare("INSERT INTO files (root_id, path, volume_serial, ntfs_file_id, size, modified_at, "
                            "present, last_seen_at) VALUES (?, ?, ?, ?, ?, ?, 1, ?)");
    file.bind(1, rootId).bind(2, utf8(f.path));
    if (f.volumeSerial && f.ntfsFileId)
        file.bind(3, static_cast<std::int64_t>(*f.volumeSerial)).bind(4, std::span<const std::byte>(*f.ntfsFileId));
    else
        file.bindNull(3).bindNull(4);
    file.bind(5, static_cast<std::int64_t>(f.size)).bind(6, f.modifiedAt).bind(7, now).run();
    const auto fileId = db_.lastInsertRowId();

    auto track = db_.prepare("INSERT INTO tracks (file_id, added_at) VALUES (?, ?)");
    track.bind(1, fileId).bind(2, now).run();
    const auto trackId = db_.lastInsertRowId();
    tx.commit();
    return trackId;
}

void LibraryDb::setValues(std::int64_t track, Layer layer, std::string_view field, std::span<const std::string> values,
                          std::string_view source)
{
    const auto name = fieldName(field);
    const auto now = nowUnixMs();
    Database::Transaction tx(db_);
    db_.prepare("DELETE FROM track_fields WHERE track_id = ? AND layer = ? AND field = ?")
        .bind(1, track)
        .bind(2, static_cast<std::int64_t>(layer))
        .bind(3, name)
        .run();
    auto ins = db_.prepare(
        "INSERT INTO track_fields (track_id, field, layer, idx, value, source, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        ins.reset();
        ins.bind(1, track).bind(2, name).bind(3, static_cast<std::int64_t>(layer)).bind(4, static_cast<std::int64_t>(i));
        ins.bind(5, std::string_view(values[i]));
        if (source.empty())
            ins.bindNull(6);
        else
            ins.bind(6, source);
        ins.bind(7, now).run();
    }
    tx.commit();
}

void LibraryDb::clearByUser(std::int64_t track, std::string_view field)
{
    const auto name = fieldName(field);
    Database::Transaction tx(db_);
    db_.prepare("DELETE FROM track_fields WHERE track_id = ? AND layer = 2 AND field = ?").bind(1, track).bind(2, name).run();
    db_.prepare("INSERT INTO track_fields (track_id, field, layer, idx, value, updated_at) VALUES (?, ?, 2, 0, NULL, ?)")
        .bind(1, track)
        .bind(2, name)
        .bind(3, nowUnixMs())
        .run();
    tx.commit();
}

void LibraryDb::removeValues(std::int64_t track, Layer layer, std::string_view field)
{
    db_.prepare("DELETE FROM track_fields WHERE track_id = ? AND layer = ? AND field = ?")
        .bind(1, track)
        .bind(2, static_cast<std::int64_t>(layer))
        .bind(3, fieldName(field))
        .run();
}

std::vector<std::string> LibraryDb::values(std::int64_t track, std::string_view field)
{
    auto st = db_.prepare("SELECT value FROM track_values WHERE track_id = ? AND field = ? ORDER BY idx");
    st.bind(1, track).bind(2, fieldName(field));
    std::vector<std::string> out;
    while (st.step())
        out.push_back(st.text(0));
    return out;
}

std::optional<Layer> LibraryDb::shownLayer(std::int64_t track, std::string_view field)
{
    auto st = db_.prepare("SELECT MAX(layer) FROM track_fields WHERE track_id = ? AND field = ?");
    st.bind(1, track).bind(2, fieldName(field));
    if (!st.step() || st.isNull(0))
        return std::nullopt;
    return static_cast<Layer>(st.integer(0));
}

} // namespace ap::library
