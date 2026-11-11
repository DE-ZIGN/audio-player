#pragma once

// Measuring the tracks when they come into the library (task 5-6): True Peak
// by 8x oversampling and BS.1770 integrated loudness (tasks 3-3, 3-4), with the
// engine's decoders. Tag peak values are never used (CLAUDE.md chapter 4).
//
// Albums are measured together: an album's loudness gates all its tracks'
// blocks at once (not an average of the tracks), so all members are decoded in
// one go. An album is the tracks with the same shown album artist (or artist
// when there is none) and album; a track without an album is measured alone.
//
// What is measured again: a track whose file changed (another fingerprint), or
// whose values come from an older analyser (kAnalyzerVersion), and an album
// whose set of tracks changed. A DSD file (DoP, not PCM) and a file that does
// not decode are recorded as such and not tried again until the file changes.
//
// The decoder's exact format and length replace what the headers said (tag
// reading). Measuring only reads the files.

#include <ap/library/library_db.h>

#include <ap/engine/track_info.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ap::library {

inline constexpr int kAnalyzerVersion = 1;

struct AnalysisReport
{
    std::size_t tracks = 0;  // measured
    std::size_t albums = 0;  // album values computed
    std::size_t notPcm = 0;  // DSD: nothing to measure as PCM
    std::size_t failed = 0;  // could not be decoded
    bool stopped = false;    // asked to stop before everything was measured
    std::vector<std::string> errors; // "path: reason"
};

// How many present tracks wait for measuring (tracks, not albums).
std::size_t pendingAnalysis(LibraryDb& db);

// Measures what waits, one album (or lone track) at a time, up to `maxUnits`
// of them. `stop` is checked between pieces of audio (every 16384 frames).
AnalysisReport analyzePending(LibraryDb& db, const std::atomic<bool>* stop = nullptr,
                              std::size_t maxUnits = std::numeric_limits<std::size_t>::max());

// The measured ReplayGain 2.0 values of a track (and its album), for the play
// queue (tasks 3-4, 3-5). Empty when the track has not been measured.
std::optional<engine::ReplayGainInfo> replayGainOf(LibraryDb& db, std::int64_t track);

// Measures in the background, on a thread of its own with its own database
// connection, at low priority (Windows background mode: lower CPU and disk
// priority), so playback and the screen go first. wake() after tracks were
// added or changed; it also looks again every `idlePoll`.
class AnalysisWorker
{
public:
    explicit AnalysisWorker(std::filesystem::path databaseFile,
                            std::chrono::milliseconds idlePoll = std::chrono::minutes(1));
    ~AnalysisWorker(); // stops at the next piece of audio
    AnalysisWorker(const AnalysisWorker&) = delete;
    AnalysisWorker& operator=(const AnalysisWorker&) = delete;

    void wake();
    // Until nothing waits (or the timeout). false: timed out.
    bool waitUntilIdle(std::chrono::milliseconds timeout);
    AnalysisReport total() const; // since the start

private:
    void run();

    std::filesystem::path databaseFile_;
    std::chrono::milliseconds idlePoll_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    bool woken_ = true;
    bool busy_ = true;
    AnalysisReport total_;
    std::thread thread_;
};

} // namespace ap::library
