#include <ap/library/analysis.h>

#include <ap/engine/audio_file.h>
#include <ap/engine/dsp/loudness.h>
#include <ap/engine/dsp/replay_gain.h>
#include <ap/engine/dsp/sample_convert.h>
#include <ap/engine/dsp/true_peak.h>

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <map>
#include <memory>

namespace ap::library {

namespace fs = std::filesystem;
namespace eng = ap::engine;

namespace {

fs::path fromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

struct Member
{
    std::int64_t track = 0;
    std::string path;
    std::optional<std::string> fingerprint;
    std::optional<std::string> albumKey; // none: not in an album
    bool needs = false;                  // this track's own values are missing or stale
};

struct Library
{
    std::vector<Member> tracks; // present ones
    std::map<std::string, std::vector<std::size_t>> albums; // key -> indices into tracks
};

// The album a track belongs to: the shown album artist (or artist) and album.
std::optional<std::string> albumKeyOf(const std::string& artist, const std::string& album)
{
    if (album.empty())
        return std::nullopt;
    return artist + '\x1F' + album;
}

Library collect(LibraryDb& lib)
{
    Library L;
    auto st = lib.database().prepare(
        "SELECT t.id, f.path, t.fingerprint, "
        "COALESCE((SELECT value FROM track_values v WHERE v.track_id = t.id AND v.field = 'albumartist' AND v.idx = 0), "
        "         (SELECT value FROM track_values v WHERE v.track_id = t.id AND v.field = 'artist' AND v.idx = 0), ''), "
        "COALESCE((SELECT value FROM track_values v WHERE v.track_id = t.id AND v.field = 'album' AND v.idx = 0), ''), "
        "a.track_id IS NOT NULL, a.analyzer_version, a.fingerprint IS NOT t.fingerprint, a.album_key "
        "FROM tracks AS t JOIN files AS f ON f.id = t.file_id AND f.present = 1 "
        "LEFT JOIN track_analysis AS a ON a.track_id = t.id ORDER BY t.id");
    while (st.step())
    {
        Member m;
        m.track = st.integer(0);
        m.path = st.text(1);
        if (!st.isNull(2))
            m.fingerprint = st.text(2);
        m.albumKey = albumKeyOf(st.text(3), st.text(4));
        const bool measured = st.integer(5) != 0;
        const auto storedKey = st.isNull(8) ? std::optional<std::string>() : std::optional<std::string>(st.text(8));
        m.needs = !measured || st.integer(6) < kAnalyzerVersion || st.integer(7) != 0 || storedKey != m.albumKey;
        if (m.albumKey)
            L.albums[*m.albumKey].push_back(L.tracks.size());
        L.tracks.push_back(std::move(m));
    }
    return L;
}

// Which tracks were measured for an album, and from which audio.
std::string membersHash(const Library& L, const std::vector<std::size_t>& members)
{
    std::uint64_t h = 1469598103934665603ull; // FNV-1a
    auto mix = [&](const std::string& s) {
        for (const unsigned char c : s)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
    };
    for (const auto i : members)
        mix(std::to_string(L.tracks[i].track) + ':' + L.tracks[i].fingerprint.value_or("") + '|');
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool albumIsCurrent(LibraryDb& lib, const std::string& key, const std::string& hash)
{
    auto st = lib.database().prepare("SELECT members_hash, analyzer_version FROM album_analysis WHERE album_key = ?");
    st.bind(1, key);
    return st.step() && st.text(0) == hash && st.integer(1) >= kAnalyzerVersion;
}

struct Measured
{
    enum class Status
    {
        Ok,
        NotPcm,
        Failed,
        Stopped
    } status = Status::Failed;
    std::string error;
    std::string container;
    std::optional<eng::PcmFormat> format;
    std::uint64_t frames = 0;
    std::optional<std::uint32_t> dsdRate;
    std::uint16_t dsdChannels = 0;
    double samplePeak = 0.0, truePeak = 0.0, truePeakFrame = 0.0;
    double lufs = 0.0;
    std::shared_ptr<eng::LoudnessMeter> meter; // for the album
};

// Decodes the whole file with the engine (like engine::analyzeTrack, with a stop check).
Measured measure(const std::string& path, const std::atomic<bool>* stop)
{
    Measured m;
    std::unique_ptr<eng::PcmSource> src;
    try
    {
        src = eng::openAudioFile(fromUtf8(path));
    }
    catch (const std::exception& e)
    {
        m.error = e.what();
        return m;
    }
    m.container = src->containerName();
    if (const auto dop = src->dop())
    {
        m.status = Measured::Status::NotPcm;
        m.dsdRate = dop->dsdRate;
        m.dsdChannels = src->format().channels;
        m.frames = src->totalFrames() * 16; // DoP carries 16 DSD samples per channel and frame
        return m;
    }
    try
    {
        const auto f = src->format();
        m.format = f;
        m.frames = src->totalFrames();
        m.meter = std::make_shared<eng::LoudnessMeter>(f.sampleRate, f.channels);
        eng::TruePeakMeter peaks(f.channels);
        constexpr std::size_t kChunk = 16384;
        std::vector<std::byte> bytes(kChunk * f.bytesPerFrame());
        std::vector<double> samples(kChunk * f.channels);
        while (const std::size_t n = src->read(bytes))
        {
            if (stop && stop->load())
            {
                m.status = Measured::Status::Stopped;
                return m;
            }
            const auto block = std::span(samples).first(n * f.channels);
            eng::dsp::toDouble(std::span(bytes).first(n * f.bytesPerFrame()), f, block);
            m.meter->process(block);
            peaks.process(block);
        }
        peaks.finish();
        m.samplePeak = peaks.samplePeak();
        m.truePeak = peaks.truePeak();
        m.truePeakFrame = peaks.truePeakPosition();
        m.lufs = m.meter->integratedLufs();
        m.status = Measured::Status::Ok;
    }
    catch (const std::exception& e)
    {
        m.status = Measured::Status::Failed;
        m.error = e.what();
    }
    return m;
}

void bindOptional(Statement& st, int i, const std::optional<std::string>& v)
{
    if (v)
        st.bind(i, std::string_view(*v));
    else
        st.bindNull(i);
}

// Stores one unit's results (tracks, and the album when there is one).
void store(LibraryDb& lib, const Library& L, const std::vector<std::size_t>& members,
           const std::vector<Measured>& results, const std::optional<std::string>& albumKey, AnalysisReport& report)
{
    auto& db = lib.database();
    const auto now = nowUnixMs();
    Database::Transaction tx(db);
    auto put = db.prepare(
        "INSERT OR REPLACE INTO track_analysis (track_id, analyzer_version, fingerprint, status, error, sample_peak, "
        "true_peak, true_peak_frame, loudness_lufs, album_key, analyzed_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    auto facts = db.prepare("UPDATE tracks SET container = ?, sample_rate = ?, bits = ?, channels = ?, frames = ?, "
                            "dsd_rate = ? WHERE id = ?");
    std::vector<const eng::LoudnessMeter*> meters;
    double albumPeak = 0.0;
    for (std::size_t k = 0; k < members.size(); ++k)
    {
        const auto& t = L.tracks[members[k]];
        const auto& m = results[k];
        put.reset();
        put.bind(1, t.track).bind(2, static_cast<std::int64_t>(kAnalyzerVersion));
        bindOptional(put, 3, t.fingerprint);
        const char* status = m.status == Measured::Status::Ok       ? "ok"
                           : m.status == Measured::Status::NotPcm ? "not_pcm"
                                                                   : "failed";
        put.bind(4, std::string_view(status));
        if (m.error.empty())
            put.bindNull(5);
        else
            put.bind(5, std::string_view(m.error));
        if (m.status == Measured::Status::Ok)
        {
            put.bind(6, m.samplePeak).bind(7, m.truePeak).bind(8, m.truePeakFrame);
            if (std::isfinite(m.lufs))
                put.bind(9, m.lufs);
            else
                put.bindNull(9); // nothing above the gate (silence)
            meters.push_back(m.meter.get());
            albumPeak = std::max(albumPeak, m.truePeak);
            ++report.tracks;
        }
        else
        {
            put.bindNull(6).bindNull(7).bindNull(8).bindNull(9);
            if (m.status == Measured::Status::NotPcm)
                ++report.notPcm;
            else
            {
                ++report.failed;
                report.errors.push_back(t.path + ": " + m.error);
            }
        }
        bindOptional(put, 10, t.albumKey);
        put.bind(11, now).run();

        if (m.status != Measured::Status::Failed)
        {
            // The decoder's exact facts replace what the headers said.
            facts.reset();
            facts.bind(1, std::string_view(m.container));
            if (m.format)
            {
                facts.bind(2, static_cast<std::int64_t>(m.format->sampleRate));
                if (m.format->type == eng::SampleType::Float)
                    facts.bindNull(3); // lossy (decoded to float): no bit depth
                else
                    facts.bind(3, static_cast<std::int64_t>(m.format->validBits));
                facts.bind(4, static_cast<std::int64_t>(m.format->channels));
            }
            else
            {
                facts.bindNull(2).bind(3, std::int64_t{1}).bind(4, static_cast<std::int64_t>(m.dsdChannels)); // DSD
            }
            facts.bind(5, static_cast<std::int64_t>(m.frames));
            if (m.dsdRate)
                facts.bind(6, static_cast<std::int64_t>(*m.dsdRate));
            else
                facts.bindNull(6);
            facts.bind(7, t.track).run();
        }
    }
    if (albumKey)
    {
        if (meters.empty())
            db.prepare("DELETE FROM album_analysis WHERE album_key = ?").bind(1, std::string_view(*albumKey)).run();
        else
        {
            const double lufs = eng::LoudnessMeter::combinedLufs(meters); // gated together
            auto a = db.prepare("INSERT OR REPLACE INTO album_analysis (album_key, members_hash, analyzer_version, "
                                "true_peak, loudness_lufs, analyzed_at) VALUES (?, ?, ?, ?, ?, ?)");
            a.bind(1, std::string_view(*albumKey))
                .bind(2, membersHash(L, members))
                .bind(3, static_cast<std::int64_t>(kAnalyzerVersion))
                .bind(4, albumPeak);
            if (std::isfinite(lufs))
                a.bind(5, lufs);
            else
                a.bindNull(5);
            a.bind(6, now).run();
            ++report.albums;
        }
    }
    tx.commit();
}

} // namespace

std::size_t pendingAnalysis(LibraryDb& lib)
{
    const auto L = collect(lib);
    return static_cast<std::size_t>(std::count_if(L.tracks.begin(), L.tracks.end(), [](const Member& m) { return m.needs; }));
}

AnalysisReport analyzePending(LibraryDb& lib, const std::atomic<bool>* stop, std::size_t maxUnits)
{
    AnalysisReport report;
    const auto L = collect(lib);

    // The units: albums with anything stale (all their members), and lone tracks.
    std::vector<std::pair<std::optional<std::string>, std::vector<std::size_t>>> units;
    for (const auto& [key, members] : L.albums)
    {
        const bool any = std::any_of(members.begin(), members.end(), [&](std::size_t i) { return L.tracks[i].needs; });
        if (any || !albumIsCurrent(lib, key, membersHash(L, members)))
            units.emplace_back(key, members);
    }
    for (std::size_t i = 0; i < L.tracks.size(); ++i)
        if (!L.tracks[i].albumKey && L.tracks[i].needs)
            units.emplace_back(std::nullopt, std::vector<std::size_t>{i});

    std::size_t done = 0;
    for (const auto& [key, members] : units)
    {
        if (done++ >= maxUnits)
            break;
        std::vector<Measured> results;
        for (const auto i : members)
        {
            results.push_back(measure(L.tracks[i].path, stop));
            if (results.back().status == Measured::Status::Stopped)
            {
                report.stopped = true;
                return report; // nothing of this unit is stored
            }
        }
        store(lib, L, members, results, key, report);
    }
    // Albums no track belongs to any more.
    lib.database().exec("DELETE FROM album_analysis WHERE album_key NOT IN "
                        "(SELECT album_key FROM track_analysis WHERE album_key IS NOT NULL)");
    return report;
}

std::optional<eng::ReplayGainInfo> replayGainOf(LibraryDb& lib, std::int64_t track)
{
    auto st = lib.database().prepare(
        "SELECT a.loudness_lufs, a.true_peak, al.loudness_lufs, al.true_peak, al.album_key IS NOT NULL "
        "FROM track_analysis AS a LEFT JOIN album_analysis AS al ON al.album_key = a.album_key "
        "WHERE a.track_id = ? AND a.status = 'ok'");
    st.bind(1, track);
    if (!st.step())
        return std::nullopt;
    auto lufs = [&](int c) { return st.isNull(c) ? -HUGE_VAL : st.real(c); };
    eng::ReplayGainInfo rg;
    rg.trackPeak = st.real(1);
    rg.trackGainDb = eng::peakLimitedGainDb(eng::replayGainDb(lufs(0)), rg.trackPeak);
    if (st.integer(4) != 0)
    {
        rg.albumPeak = st.real(3);
        rg.albumGainDb = eng::peakLimitedGainDb(eng::replayGainDb(lufs(2)), *rg.albumPeak);
    }
    return rg;
}

// --- AnalysisWorker ------------------------------------------------------------

AnalysisWorker::AnalysisWorker(fs::path databaseFile, std::chrono::milliseconds idlePoll)
    : databaseFile_(std::move(databaseFile)), idlePoll_(idlePoll)
{
    thread_ = std::thread([this] { run(); });
}

AnalysisWorker::~AnalysisWorker()
{
    stop_ = true;
    {
        std::lock_guard lock(mutex_);
    }
    wake_.notify_all();
    thread_.join();
}

void AnalysisWorker::wake()
{
    {
        std::lock_guard lock(mutex_);
        woken_ = true;
    }
    wake_.notify_all();
}

bool AnalysisWorker::waitUntilIdle(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(mutex_);
    return idle_.wait_for(lock, timeout, [this] { return !busy_ && !woken_; });
}

AnalysisReport AnalysisWorker::total() const
{
    std::lock_guard lock(mutex_);
    return total_;
}

void AnalysisWorker::run()
{
    // Lower CPU and disk priority for this thread: playback and the screen go first.
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    try
    {
        auto lib = LibraryDb::open(databaseFile_);
        for (;;)
        {
            {
                std::unique_lock lock(mutex_);
                if (!woken_)
                {
                    busy_ = false;
                    idle_.notify_all();
                    wake_.wait_for(lock, idlePoll_, [this] { return stop_.load() || woken_; });
                }
                if (stop_)
                    break;
                woken_ = false;
                busy_ = true;
            }
            const auto r = analyzePending(lib, &stop_);
            std::lock_guard lock(mutex_);
            total_.tracks += r.tracks;
            total_.albums += r.albums;
            total_.notPcm += r.notPcm;
            total_.failed += r.failed;
            total_.errors.insert(total_.errors.end(), r.errors.begin(), r.errors.end());
            total_.stopped = total_.stopped || r.stopped;
        }
    }
    catch (const std::exception& e)
    {
        std::lock_guard lock(mutex_);
        total_.errors.push_back(e.what());
    }
    {
        std::lock_guard lock(mutex_);
        busy_ = false;
        woken_ = false;
    }
    idle_.notify_all();
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
}

} // namespace ap::library
