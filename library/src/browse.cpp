#include <ap/library/browse.h>

#include <ap/library/analysis.h>
#include <ap/library/search.h>

#include <cmath>

namespace ap::library {

namespace fs = std::filesystem;

namespace {

fs::path fromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

double toDb(double linear)
{
    return 20.0 * std::log10(linear);
}

// "x" -> "x DESC" for every key, except the "(... IS NULL)" ones that keep
// values that are not there (not measured, unknown length) last either way.
std::string orderBy(const std::vector<std::string>& keys, bool descending)
{
    std::string s;
    for (const auto& k : keys)
    {
        const bool nullsLast = k.ends_with("IS NULL)");
        s += (s.empty() ? "" : ", ") + k + (descending && !nullsLast ? " DESC" : "");
    }
    return s;
}

void bindAll(Statement& st, int& i, const std::vector<std::string>& binds)
{
    for (const auto& b : binds)
        st.bind(i++, std::string_view(b));
}

} // namespace

TrackPage listTracks(LibraryDb& lib, const TrackQuery& q)
{
    std::string where = "1";
    std::vector<std::string> binds;
    const auto filter = searchFilter(q.search);
    if (filter)
    {
        where += " AND t.id IN (SELECT rowid FROM track_search WHERE " + filter->where + ")";
        binds = filter->binds;
    }
    if (q.albumKey)
    {
        where += " AND d.album_key = ?";
        binds.push_back(*q.albumKey);
    }
    const std::string from = " FROM tracks AS t JOIN track_display AS d ON d.track_id = t.id "
                             "JOIN files AS f ON f.id = t.file_id "
                             "LEFT JOIN track_analysis AS a ON a.track_id = t.id WHERE ";
    const std::string seconds = "(t.frames * 1.0 / COALESCE(t.sample_rate, t.dsd_rate))";
    const std::string plays = "(SELECT COALESCE(SUM(counted), 0) FROM plays AS p WHERE p.track_id = t.id)";

    std::vector<std::string> keys;
    if (q.albumKey)
        keys = {"(d.disc_no IS NULL)", "d.disc_no", "(d.track_no IS NULL)", "d.track_no", "d.title_sort"};
    else
        switch (q.sort)
        {
            case TrackSort::Artist:
                keys = {"d.artist_sort", "d.album_sort", "d.disc_no", "d.track_no", "d.title_sort"};
                break;
            case TrackSort::Album:
                keys = {"d.album_sort", "d.disc_no", "d.track_no", "d.title_sort"};
                break;
            case TrackSort::Title: keys = {"d.title_sort", "d.artist_sort"}; break;
            case TrackSort::Length: keys = {"(t.frames IS NULL)", seconds, "d.title_sort"}; break;
            case TrackSort::Format: keys = {"(t.container IS NULL)", "t.container", "t.sample_rate", "t.bits", "d.title_sort"}; break;
            case TrackSort::Rate:
                keys = {"(COALESCE(t.sample_rate, t.dsd_rate) IS NULL)", "COALESCE(t.sample_rate, t.dsd_rate)", "t.bits",
                        "d.title_sort"};
                break;
            case TrackSort::TruePeak: keys = {"(a.true_peak IS NULL)", "a.true_peak", "d.title_sort"}; break;
            case TrackSort::Plays: keys = {plays, "d.title_sort"}; break;
            case TrackSort::Added: keys = {"t.added_at", "t.id"}; break;
        }

    TrackPage page;
    {
        auto st = lib.database().prepare("SELECT COUNT(*)" + from + where);
        int i = 1;
        bindAll(st, i, binds);
        st.step();
        page.total = static_cast<std::size_t>(st.integer(0));
    }
    auto st = lib.database().prepare(
        "SELECT t.id, d.title, d.artist, d.album, d.album_key, d.track_no, d.disc_no, t.frames, t.sample_rate, t.dsd_rate, "
        "t.bits, t.container, a.status, a.true_peak, a.loudness_lufs, a.fingerprint IS NOT t.fingerprint, "
        "a.analyzer_version, " + plays + ", t.rating, f.present, f.path" + from + where + " ORDER BY "
        + orderBy(keys, q.descending) + " LIMIT ? OFFSET ?");
    int i = 1;
    bindAll(st, i, binds);
    st.bind(i++, static_cast<std::int64_t>(q.limit)).bind(i, static_cast<std::int64_t>(q.offset));
    while (st.step())
    {
        TrackRow r;
        r.id = st.integer(0);
        r.title = st.text(1);
        r.artist = st.text(2);
        r.album = st.text(3);
        if (!st.isNull(4))
            r.albumKey = st.text(4);
        if (!st.isNull(5))
            r.trackNo = st.integer(5);
        if (!st.isNull(6))
            r.discNo = st.integer(6);
        if (!st.isNull(8))
            r.sampleRate = static_cast<std::uint32_t>(st.integer(8));
        if (!st.isNull(9))
            r.dsdRate = static_cast<std::uint32_t>(st.integer(9));
        const auto rate = r.sampleRate ? r.sampleRate : r.dsdRate;
        if (!st.isNull(7) && rate && *rate > 0)
            r.seconds = static_cast<double>(st.integer(7)) / *rate;
        if (!st.isNull(10))
            r.bits = static_cast<std::uint16_t>(st.integer(10));
        r.container = st.text(11);
        const bool stale = st.isNull(12) || st.integer(15) != 0 || st.integer(16) < kAnalyzerVersion;
        r.analysis = stale ? "pending" : st.text(12);
        if (!stale && r.analysis == "ok")
        {
            r.truePeakDb = toDb(st.real(13));
            if (!st.isNull(14))
                r.loudnessLufs = st.real(14);
        }
        r.plays = st.integer(17);
        if (!st.isNull(18))
            r.rating = static_cast<int>(st.integer(18));
        r.present = st.integer(19) != 0;
        r.path = fromUtf8(st.text(20));
        page.rows.push_back(std::move(r));
    }
    return page;
}

AlbumPage listAlbums(LibraryDb& lib, const AlbumQuery& q)
{
    std::string where = "d.album_key IS NOT NULL";
    std::vector<std::string> binds;
    if (const auto filter = searchFilter(q.search))
    {
        where += " AND d.track_id IN (SELECT rowid FROM track_search WHERE " + filter->where + ")";
        binds = filter->binds;
    }
    const std::string from = " FROM track_display AS d JOIN tracks AS t ON t.id = d.track_id "
                             "JOIN files AS f ON f.id = t.file_id AND f.present = 1 WHERE " + where;
    std::vector<std::string> keys;
    switch (q.sort)
    {
        case AlbumSort::Artist: keys = {"MIN(d.album_sort)"}; break;
        case AlbumSort::Album: keys = {"MIN(d.album) COLLATE NOCASE", "MIN(d.album_sort)"}; break;
        case AlbumSort::Year: keys = {"(MAX(d.year) IS NULL)", "MAX(d.year)", "MIN(d.album_sort)"}; break;
        case AlbumSort::Added: keys = {"MAX(t.added_at)", "MIN(d.album_sort)"}; break;
    }

    AlbumPage page;
    {
        auto st = lib.database().prepare("SELECT COUNT(DISTINCT d.album_key)" + from);
        int i = 1;
        bindAll(st, i, binds);
        st.step();
        page.total = static_cast<std::size_t>(st.integer(0));
    }
    auto st = lib.database().prepare(
        "SELECT d.album_key, MIN(d.album), MIN(d.album_artist), MAX(d.year), COUNT(*), "
        "CASE WHEN COUNT(t.frames) = COUNT(*) THEN SUM(t.frames * 1.0 / COALESCE(t.sample_rate, t.dsd_rate)) END, "
        "(SELECT d2.track_id FROM track_display AS d2 JOIN tracks AS t2 ON t2.id = d2.track_id "
        " JOIN files AS f2 ON f2.id = t2.file_id AND f2.present = 1 WHERE d2.album_key = d.album_key "
        " ORDER BY d2.disc_no IS NULL, d2.disc_no, d2.track_no IS NULL, d2.track_no, d2.title_sort LIMIT 1), "
        "(SELECT true_peak FROM album_analysis WHERE album_key = d.album_key), "
        "CASE WHEN COUNT(DISTINCT t.container) = 1 AND COUNT(t.container) = COUNT(*) THEN MIN(t.container) END, "
        "CASE WHEN COUNT(DISTINCT t.sample_rate) = 1 AND COUNT(t.sample_rate) = COUNT(*) THEN MIN(t.sample_rate) END"
        + from + " GROUP BY d.album_key ORDER BY " + orderBy(keys, q.descending) + " LIMIT ? OFFSET ?");
    int i = 1;
    bindAll(st, i, binds);
    st.bind(i++, static_cast<std::int64_t>(q.limit)).bind(i, static_cast<std::int64_t>(q.offset));
    while (st.step())
    {
        AlbumRow r;
        r.key = st.text(0);
        r.album = st.text(1);
        r.artist = st.text(2);
        if (!st.isNull(3))
            r.year = st.text(3);
        r.tracks = static_cast<std::size_t>(st.integer(4));
        if (!st.isNull(5))
            r.seconds = st.real(5);
        r.coverTrack = st.integer(6);
        if (!st.isNull(7))
            r.truePeakDb = toDb(st.real(7));
        if (!st.isNull(8))
            r.container = st.text(8);
        if (!st.isNull(9))
            r.sampleRate = static_cast<std::uint32_t>(st.integer(9));
        page.rows.push_back(std::move(r));
    }
    return page;
}

std::vector<FolderRow> listFolders(LibraryDb& lib)
{
    std::vector<FolderRow> out;
    auto st = lib.database().prepare(
        "SELECT r.id, r.path, COALESCE(SUM(f.present), 0), COALESCE(SUM(1 - f.present), 0) FROM roots AS r "
        "LEFT JOIN files AS f ON f.root_id = r.id GROUP BY r.id ORDER BY r.path");
    while (st.step())
        out.push_back({st.integer(0), fromUtf8(st.text(1)), static_cast<std::size_t>(st.integer(2)),
                       static_cast<std::size_t>(st.integer(3))});
    return out;
}

void removeFolder(LibraryDb& lib, std::int64_t folderId)
{
    auto& db = lib.database();
    Database::Transaction tx(db);
    // Tracks go with their files (cascade), and with them their search and display rows.
    db.prepare("DELETE FROM files WHERE root_id = ?").bind(1, folderId).run();
    db.prepare("DELETE FROM roots WHERE id = ?").bind(1, folderId).run();
    tx.commit();
}

LibraryCounts libraryCounts(LibraryDb& lib)
{
    LibraryCounts c;
    auto st = lib.database().prepare(
        "SELECT (SELECT COUNT(*) FROM files WHERE present = 1), (SELECT COUNT(*) FROM files WHERE present = 0), "
        "(SELECT COUNT(DISTINCT d.album_key) FROM track_display AS d JOIN tracks AS t ON t.id = d.track_id "
        " JOIN files AS f ON f.id = t.file_id AND f.present = 1 WHERE d.album_key IS NOT NULL), "
        "(SELECT COUNT(*) FROM tracks AS t JOIN files AS f ON f.id = t.file_id AND f.present = 1 "
        " LEFT JOIN track_analysis AS a ON a.track_id = t.id "
        " WHERE a.track_id IS NULL OR a.analyzer_version < ? OR a.fingerprint IS NOT t.fingerprint)");
    st.bind(1, static_cast<std::int64_t>(kAnalyzerVersion));
    st.step();
    c.tracks = static_cast<std::size_t>(st.integer(0));
    c.missing = static_cast<std::size_t>(st.integer(1));
    c.albums = static_cast<std::size_t>(st.integer(2));
    c.toMeasure = static_cast<std::size_t>(st.integer(3));
    return c;
}

} // namespace ap::library
