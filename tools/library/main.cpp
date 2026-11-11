// ap_lib: tries the library database on real folders (tasks 5-2, 5-3).
//
//   ap_lib scan FOLDER --db FILE     add FOLDER as a watched folder (once), compare it
//                                    with the database, read the tags of what changed
//   ap_lib show --db FILE [--charset shift_jis|windows-1252|utf-8]
//                                    list the tracks with their shown values
//   ap_lib find --db FILE WORDS...   search (task 5-4)
//   ap_lib analyze --db FILE         measure True Peak and loudness of what waits (task 5-6)
//
// The music files are only read.

#include <ap/library/analysis.h>
#include <ap/library/history.h>
#include <ap/library/library_db.h>
#include <ap/library/scanner.h>
#include <ap/library/search.h>
#include <ap/library/tag_importer.h>

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <vector>

using namespace ap::library;
namespace fs = std::filesystem;

namespace {

constexpr const char* kUsage =
    "使い方:\n"
    "  ap_lib scan FOLDER --db FILE   FOLDER を監視フォルダーとして DB に加え（初回だけ）、DB と比べ、\n"
    "                                 新しいファイルと変わったファイルのタグを読む（音楽ファイルは読むだけ）\n"
    "  ap_lib show --db FILE [--charset shift_jis|windows-1252|utf-8]\n"
    "                                 曲の一覧（表示する値と、タグの文字コードの判断）\n"
    "  ap_lib find --db FILE 言葉...   検索（かな・全角半角・大文字小文字・アクセント記号の違いを吸収。\n"
    "                                 1〜2 文字の言葉も探せる。言葉はすべて含むものを探す）\n"
    "  ap_lib analyze --db FILE       測っていない曲の True Peak とラウドネスを測る（アルバムはまとめて）\n";

void print(const std::string& s)
{
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

std::string utf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string utf8(const fs::path& p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string joined(const std::vector<std::string>& v)
{
    std::string s;
    for (const auto& x : v)
        s += (s.empty() ? "" : " / ") + x;
    return s.empty() ? "—" : s;
}

std::int64_t rootFor(LibraryDb& lib, const fs::path& folder)
{
    auto st = lib.database().prepare("SELECT id FROM roots WHERE path = ?");
    st.bind(1, utf8(folder.lexically_normal()));
    if (st.step())
        return st.integer(0);
    return lib.addRoot(folder);
}

int scan(const fs::path& folder, const fs::path& db)
{
    if (!fs::is_directory(folder))
    {
        print("フォルダーがありません: " + utf8(folder) + "\n");
        return 2;
    }
    auto lib = LibraryDb::open(db);
    const auto root = rootFor(lib, fs::absolute(folder));
    const auto t0 = GetTickCount64();
    const auto r = Scanner(lib).scanRoot(root);
    const auto t1 = GetTickCount64();
    auto tags = TagImporter(lib).readPending();
    // Moved from another drive: the old track takes the new file (task 5-5).
    const auto relinked = relinkMovedTracks(lib);
    if (!relinked.empty())
    {
        const auto again = TagImporter(lib).readPending();
        tags.read += again.read;
        tags.failed += again.failed;
    }
    const auto t2 = GetTickCount64();
    for (const auto& m : relinked)
        print("  別のドライブへの移動（同じ曲として付け直した）: " + utf8(m.from) + " → " + utf8(m.to) + "\n");

    print(std::format("比較（{:.2f} 秒）: 追加 {}、変更 {}、見つからない {}、戻った {}、移動・改名 {}、変化なし {}、読めない {}\n",
                      (t1 - t0) / 1000.0, r.added.size(), r.changed.size(), r.missing.size(), r.returned.size(),
                      r.moved.size(), r.unchanged, r.unreadable));
    print(std::format("タグ（{:.2f} 秒）: 読んだ {}（うち Shift_JIS と判断 {}）、読めなかった {}\n", (t2 - t1) / 1000.0, tags.read,
                      tags.shiftJis, tags.failed));
    for (const auto& e : tags.errors)
        print("  読めなかった: " + e + "\n");
    for (const auto& m : r.moved)
        print("  移動・改名: " + utf8(m.first) + " → " + utf8(m.second) + "\n");
    for (const auto& m : r.missing)
        print("  見つからない: " + utf8(m) + "\n");
    return 0;
}

int show(const fs::path& db, const std::optional<std::string>& charset)
{
    auto lib = LibraryDb::open(db);
    std::string sql = "SELECT tracks.id, files.path, files.tag_charset, files.tag_charset_override, files.present, "
                      "tracks.container, tracks.sample_rate, tracks.bits, tracks.frames, tracks.dsd_rate, "
                      "a.status, a.true_peak, a.loudness_lufs, a.error "
                      "FROM tracks JOIN files ON files.id = tracks.file_id "
                      "LEFT JOIN track_analysis AS a ON a.track_id = tracks.id";
    if (charset)
        sql += " WHERE files.tag_charset = ?";
    sql += " ORDER BY files.path";
    auto st = lib.database().prepare(sql);
    if (charset)
        st.bind(1, *charset);
    std::size_t n = 0;
    while (st.step())
    {
        const auto id = st.integer(0);
        const auto cs = st.isNull(2) ? std::string("—") : st.text(2);
        const auto ov = st.isNull(3) ? std::string() : "（利用者の指定: " + st.text(3) + "）";
        print(std::format("{}{}\n  文字コード: {}{}\n  曲名: {}\n  アーティスト: {}\n  アルバム: {}\n", st.text(1),
                          st.integer(4) ? "" : "（見つからない）", cs, ov, joined(lib.values(id, "title")),
                          joined(lib.values(id, "artist")), joined(lib.values(id, "album"))));
        // Format and length (task 5-6): the headers' values, the decoder's once measured.
        std::string format = st.isNull(5) ? std::string("—") : st.text(5);
        double seconds = 0.0;
        if (!st.isNull(9))
        {
            format += std::format(" DSD{}", st.integer(9) / 44100);
            seconds = st.isNull(8) ? 0.0 : static_cast<double>(st.integer(8)) / static_cast<double>(st.integer(9));
        }
        else if (!st.isNull(6))
        {
            format += std::format(" {:g} kHz", st.integer(6) / 1000.0);
            if (!st.isNull(7))
                format += std::format(" / {}bit", st.integer(7));
            seconds = st.isNull(8) ? 0.0 : static_cast<double>(st.integer(8)) / static_cast<double>(st.integer(6));
        }
        const auto length = std::format("{}:{:04.1f}", static_cast<int>(seconds) / 60, std::fmod(seconds, 60.0));
        std::string measured = "測っていない";
        if (!st.isNull(10))
        {
            const auto status = st.text(10);
            if (status == "ok")
                measured = std::format("True Peak {:+.2f} dBTP{}、ラウドネス {}", 20.0 * std::log10(st.real(11)),
                                       st.real(11) > 1.0 ? "（0 dBTP 超え）" : "",
                                       st.isNull(12) ? std::string("無音") : std::format("{:.2f} LUFS", st.real(12)));
            else if (status == "not_pcm")
                measured = "DSD（PCM ではないので測らない）";
            else
                measured = "デコードできなかった（" + st.text(13) + "）";
        }
        print(std::format("  形式: {}、長さ {}\n  測定: {}\n", format, length, measured));
        ++n;
    }
    print(std::format("{} 曲\n", n));
    return 0;
}

int find(const fs::path& db, const std::string& query)
{
    auto lib = LibraryDb::open(db);
    const auto t0 = GetTickCount64();
    const auto ids = searchTracks(lib, query);
    const auto t1 = GetTickCount64();
    print(std::format("「{}」（検索の形：「{}」）: {} 曲（{} ms）\n", query, normalizeForSearch(query), ids.size(), t1 - t0));
    for (const auto id : ids)
    {
        auto st = lib.database().prepare(
            "SELECT files.path FROM tracks JOIN files ON files.id = tracks.file_id WHERE tracks.id = ?");
        st.bind(1, id);
        st.step();
        print(std::format("  {} ／ {} ／ {}\n    {}\n", joined(lib.values(id, "title")), joined(lib.values(id, "artist")),
                          joined(lib.values(id, "album")), st.text(0)));
    }
    return 0;
}

int analyze(const fs::path& db)
{
    auto lib = LibraryDb::open(db);
    print(std::format("測っていない曲: {}\n", pendingAnalysis(lib)));
    const auto t0 = GetTickCount64();
    const auto r = analyzePending(lib);
    const auto t1 = GetTickCount64();
    print(std::format("測った曲 {}、アルバム {}、DSD（測らない）{}、デコードできなかった {}（{:.2f} 秒）\n", r.tracks, r.albums,
                      r.notPcm, r.failed, (t1 - t0) / 1000.0));
    for (const auto& e : r.errors)
        print("  デコードできなかった: " + e + "\n");
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::wstring> args(argv + 1, argv + argc);
    std::optional<fs::path> db;
    std::optional<std::string> charset;
    std::vector<std::wstring> rest;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == L"--db" && i + 1 < args.size())
            db = fs::path(args[++i]);
        else if (args[i] == L"--charset" && i + 1 < args.size())
            charset = utf8(args[++i]);
        else
            rest.push_back(args[i]);
    }
    try
    {
        if (db && rest.size() == 2 && rest[0] == L"scan")
            return scan(fs::path(rest[1]), *db);
        if (db && rest.size() == 1 && rest[0] == L"show")
            return show(*db, charset);
        if (db && rest.size() == 1 && rest[0] == L"analyze")
            return analyze(*db);
        if (db && rest.size() >= 2 && rest[0] == L"find")
        {
            std::wstring q;
            for (std::size_t i = 1; i < rest.size(); ++i)
                q += (i > 1 ? L" " : L"") + rest[i];
            return find(*db, utf8(q));
        }
    }
    catch (const std::exception& e)
    {
        print(std::string("エラー: ") + e.what() + "\n");
        return 1;
    }
    print(kUsage);
    return 2;
}
