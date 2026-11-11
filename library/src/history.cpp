#include <ap/library/history.h>

#include <flacfile.h>
#include <flacproperties.h>

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace ap::library {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kPiece = 256 * 1024;

std::string hex(const unsigned char* p, std::size_t n)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i)
    {
        s.push_back(digits[p[i] >> 4]);
        s.push_back(digits[p[i] & 15]);
    }
    return s;
}

std::optional<std::string> flacMd5(const fs::path& file)
{
    TagLib::FLAC::File f(file.c_str(), true, TagLib::AudioProperties::Fast);
    if (!f.isValid() || !f.audioProperties())
        return std::nullopt;
    const auto sig = f.audioProperties()->signature();
    if (sig.size() != 16)
        return std::nullopt;
    bool zero = true;
    for (const char c : sig)
        zero = zero && c == 0;
    if (zero)
        return std::nullopt; // the encoder did not compute it
    return "flac-md5:" + hex(reinterpret_cast<const unsigned char*>(sig.data()), sig.size());
}

std::optional<std::string> headTail(const fs::path& file)
{
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return std::nullopt;
    LARGE_INTEGER size{};
    std::optional<std::string> result;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (GetFileSizeEx(h, &size) && BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0
        && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0)
    {
        bool ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(&size.QuadPart), sizeof size.QuadPart, 0) == 0;
        std::vector<unsigned char> buf(kPiece);
        auto readAt = [&](std::int64_t offset, std::size_t n) {
            LARGE_INTEGER pos{};
            pos.QuadPart = offset;
            DWORD got = 0;
            if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN) || !ReadFile(h, buf.data(), static_cast<DWORD>(n), &got, nullptr)
                || got != n)
                return false;
            return BCryptHashData(hash, buf.data(), got, 0) == 0;
        };
        const auto total = size.QuadPart;
        // The first and the last piece; a small file is read once, whole.
        const std::size_t head = static_cast<std::size_t>(std::min<std::int64_t>(total, kPiece));
        ok = ok && readAt(0, head);
        if (total > static_cast<std::int64_t>(kPiece))
        {
            const auto tailStart = std::max<std::int64_t>(kPiece, total - static_cast<std::int64_t>(kPiece));
            ok = ok && readAt(tailStart, static_cast<std::size_t>(total - tailStart));
        }
        unsigned char digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof digest, 0) == 0)
            result = "sha256-ht:" + hex(digest, sizeof digest);
    }
    if (hash)
        BCryptDestroyHash(hash);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(h);
    return result;
}

fs::path fromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

} // namespace

std::optional<std::string> fingerprintOf(const fs::path& file)
{
    auto ext = file.extension().wstring();
    for (auto& c : ext)
        c = static_cast<wchar_t>(towlower(c));
    if (ext == L".flac")
        if (auto md5 = flacMd5(file))
            return md5;
    return headTail(file);
}

std::vector<Relink> relinkMovedTracks(LibraryDb& lib)
{
    auto& db = lib.database();
    // Missing tracks and present ones with the same fingerprint.
    struct Pair
    {
        std::int64_t oldTrack, oldFile, newTrack, newFile;
        std::string oldPath, newPath;
    };
    std::vector<Pair> pairs;
    {
        auto st = db.prepare(
            "SELECT m.id, m.file_id, n.id, n.file_id, mf.path, nf.path FROM tracks AS m "
            "JOIN files AS mf ON mf.id = m.file_id AND mf.present = 0 "
            "JOIN tracks AS n ON n.fingerprint = m.fingerprint AND n.id <> m.id "
            "JOIN files AS nf ON nf.id = n.file_id AND nf.present = 1 "
            "WHERE m.fingerprint IS NOT NULL");
        while (st.step())
            pairs.push_back({st.integer(0), st.integer(1), st.integer(2), st.integer(3), st.text(4), st.text(5)});
    }
    // Only one to one: a missing track with exactly one candidate, which is the
    // candidate of no other missing track.
    std::map<std::int64_t, int> perOld, perNew;
    for (const auto& p : pairs)
    {
        ++perOld[p.oldTrack];
        ++perNew[p.newTrack];
    }
    auto hasOwn = [&](std::int64_t track) {
        auto st = db.prepare("SELECT (SELECT COUNT(*) FROM plays WHERE track_id = ?1) "
                             "+ (SELECT COUNT(*) FROM tracks WHERE id = ?1 AND rating IS NOT NULL) "
                             "+ (SELECT COUNT(*) FROM track_fields WHERE track_id = ?1 AND layer > 0)");
        st.bind(1, track);
        st.step();
        return st.integer(0) > 0;
    };

    std::vector<Relink> done;
    for (const auto& p : pairs)
    {
        if (perOld[p.oldTrack] != 1 || perNew[p.newTrack] != 1 || hasOwn(p.newTrack))
            continue;
        Database::Transaction tx(db);
        // The new track was only its file's tags: it goes, the old one takes the file.
        db.prepare("DELETE FROM tracks WHERE id = ?").bind(1, p.newTrack).run();
        db.prepare("UPDATE tracks SET file_id = ? WHERE id = ?").bind(1, p.newFile).bind(2, p.oldTrack).run();
        db.prepare("DELETE FROM files WHERE id = ?").bind(1, p.oldFile).run();
        // Its tags may have been edited on the way: read them into the old track.
        db.prepare("UPDATE files SET needs_read = 1 WHERE id = ?").bind(1, p.newFile).run();
        lib.refreshSearch(p.oldTrack);
        tx.commit();
        done.push_back({p.oldTrack, fromUtf8(p.oldPath), fromUtf8(p.newPath)});
    }
    return done;
}

std::vector<std::pair<std::int64_t, PlayRecord>> PlayCounter::finish()
{
    std::vector<std::pair<std::int64_t, PlayRecord>> out;
    if (last_ && last_->track != 0 && current_.playedFrames > 0)
        out.emplace_back(last_->track, current_);
    last_.reset();
    current_ = {};
    return out;
}

std::vector<std::pair<std::int64_t, PlayRecord>> PlayCounter::observe(const Observation& o)
{
    // How far playing can move the position in the time since the last look,
    // with room for the timer's unevenness.
    auto reach = [&](std::uint32_t rate) -> std::uint64_t {
        if (!last_ || rate == 0)
            return 0;
        const auto ms = std::max<std::int64_t>(0, o.now - last_->now);
        return static_cast<std::uint64_t>(ms * 1.5 * rate / 1000.0 + rate * 0.25);
    };

    std::vector<std::pair<std::int64_t, PlayRecord>> out;
    if (last_ && o.track != last_->track)
    {
        // The previous track ended here: its last part counts if the time allows.
        if (last_->playing && o.playing && last_->totalFrames > last_->position
            && last_->totalFrames - last_->position <= reach(last_->sampleRate))
        {
            current_.playedFrames += last_->totalFrames - last_->position;
            current_.reachedEnd = true;
        }
        if (last_->track != 0 && current_.playedFrames > 0)
            out.emplace_back(last_->track, current_);
        // The new track may have begun a little before this look (gaplessly).
        const bool continued = last_->playing && o.playing && o.position <= reach(o.sampleRate);
        current_ = {};
        current_.startedAt = o.now;
        current_.playedFrames = continued ? o.position : 0;
    }
    else if (!last_)
    {
        current_ = {};
        current_.startedAt = o.now;
    }
    else if (o.playing && o.position >= last_->position && o.position - last_->position <= reach(o.sampleRate))
    {
        current_.playedFrames += o.position - last_->position; // heard (not a seek)
        if (o.totalFrames > 0 && o.position >= o.totalFrames)
            current_.reachedEnd = true;
    }
    current_.trackFrames = o.totalFrames;
    current_.sampleRate = o.sampleRate;
    last_ = o;
    return out;
}

bool recordPlay(LibraryDb& lib, std::int64_t track, const PlayRecord& p)
{
    const double seconds = p.sampleRate ? static_cast<double>(p.playedFrames) / p.sampleRate : 0.0;
    const bool counts = (p.trackFrames > 0 && p.playedFrames * 2 >= p.trackFrames) || seconds >= kPlayCountsAfterSeconds;
    lib.database()
        .prepare("INSERT INTO plays (track_id, started_at, played_frames, completed, counted) VALUES (?, ?, ?, ?, ?)")
        .bind(1, track)
        .bind(2, p.startedAt)
        .bind(3, static_cast<std::int64_t>(p.playedFrames))
        .bind(4, static_cast<std::int64_t>(p.reachedEnd))
        .bind(5, static_cast<std::int64_t>(counts))
        .run();
    return counts;
}

TrackStats trackStats(LibraryDb& lib, std::int64_t track)
{
    TrackStats s;
    auto st = lib.database().prepare(
        "SELECT (SELECT COALESCE(SUM(counted), 0) FROM plays WHERE track_id = ?1), "
        "(SELECT MAX(started_at) FROM plays WHERE track_id = ?1), (SELECT rating FROM tracks WHERE id = ?1)");
    st.bind(1, track);
    st.step();
    s.playCount = st.integer(0);
    if (!st.isNull(1))
        s.lastPlayedAt = st.integer(1);
    if (!st.isNull(2))
        s.rating = static_cast<int>(st.integer(2));
    return s;
}

void setRating(LibraryDb& lib, std::int64_t track, std::optional<int> rating)
{
    if (rating && (*rating < 0 || *rating > 100))
        throw std::out_of_range("a rating is 0 to 100");
    auto st = lib.database().prepare("UPDATE tracks SET rating = ? WHERE id = ?");
    if (rating)
        st.bind(1, static_cast<std::int64_t>(*rating));
    else
        st.bindNull(1);
    st.bind(2, track).run();
}

} // namespace ap::library
