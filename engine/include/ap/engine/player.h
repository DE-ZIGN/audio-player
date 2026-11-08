#pragma once

#include <ap/engine/output/audio_output.h>
#include <ap/engine/pcm_source.h>
#include <ap/engine/playback.h>
#include <ap/engine/signal_path.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace ap::engine {

// One entry of the play queue. `open` is called when the track is needed
// (the next track is opened in the background while the current one plays)
// and may throw; the error is reported as PlayerErrorKind::Load.
struct QueueItem
{
    std::string name; // display name (UTF-8)
    std::function<std::unique_ptr<PcmSource>()> open;
};

enum class TransitionKind
{
    Gapless,  // same format: the next track's first frame follows the last frame directly
    Reopened, // different format: the output was drained, closed and opened again
};

struct TrackTransition
{
    std::size_t fromIndex = 0;
    std::size_t toIndex = 0;
    TransitionKind kind = TransitionKind::Gapless;
    PcmFormat fromFormat; // source formats
    PcmFormat toFormat;
    // Reopened only: time from the moment the previous track's data ran out to
    // the first request for the next track's data. An upper bound of the
    // silence between the two tracks (it includes the end of the previous
    // track still in the device buffer).
    double switchMilliseconds = 0.0;
};

enum class PlayerState
{
    Empty,   // nothing loaded
    Stopped, // loaded, not playing
    Playing,
    Ended,   // reached the end of the source
    Error,   // the last attempt to play failed; see PlayerStatus::error
};

enum class PlayerErrorKind
{
    None,
    FormatNotSupported, // see formatCheck
    Output,             // see outputError
    Load,               // a track could not be opened; see detail
    DopNotAllowed,      // a DSD track would be sent as DoP, but DoP is not enabled for this output
    Other,
};

struct PlayerError
{
    PlayerErrorKind kind = PlayerErrorKind::None;
    FormatCheck formatCheck;
    OutputError outputError = OutputError::DeviceError;
    std::string detail; // English, for logs
};

// Dropouts since the queue was set.
struct DropoutCounts
{
    std::uint32_t lateBuffers = 0;     // device side: buffers not ready in time
    std::uint32_t deviceTimeouts = 0;  // device side: the device did not ask for data
    std::uint32_t readStarvations = 0; // read-ahead side: data not read from disk in time
    std::uint64_t starvedFrames = 0;   // frames replaced by silence because of that
    bool realtimePriority = false;     // the playback thread ran with MMCSS "Pro Audio"

    std::uint32_t total() const noexcept { return lateBuffers + deviceTimeouts + readStarvations; }
};

struct PlayerStatus
{
    PlayerState state = PlayerState::Empty;
    std::optional<PcmFormat> sourceFormat;
    std::optional<DopInfo> dop;       // set when the current track is DSD sent as DoP
    std::optional<PlaybackPlan> plan; // set once the output has been opened
    // The current track's path (input, processing, output), set once the
    // output has been opened and the output reported what it does.
    std::optional<SignalPath> path;
    std::string outputName;
    std::uint64_t positionFrames = 0; // within the current track
    std::uint64_t totalFrames = 0;    // of the current track
    std::size_t queueIndex = 0;
    std::size_t queueLength = 0;
    std::string trackName;
    std::vector<TrackTransition> transitions; // since the queue was set (most recent last)
    DropoutCounts dropouts;
    PlayerError error;
};

// Plays one source at a time on its own thread through the bit-perfect path.
// All public methods are thread-safe and intended to be called from a UI thread.
class Player
{
public:
    using OutputFactory = std::function<std::unique_ptr<AudioOutput>()>;

    explicit Player(OutputFactory outputFactory, PlaybackOptions options = {});
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Stops playback and plays `source` as a queue of one. Position goes to 0.
    void load(std::unique_ptr<PcmSource> source);

    // Stops playback and replaces the queue. Opens the start track now (a
    // failure is reported as a Load error) and the following one in the background.
    void setQueue(std::vector<QueueItem> items, std::size_t startIndex = 0);

    // Starts playback from the current position in the current track. After
    // the end of the queue, starts again from the first track. After an
    // error, retries the current track. Does nothing when empty or playing.
    void play();

    // Stops playback and keeps the track and position. Blocks until the output has stopped.
    void stop();

    // Moves the position within the current track. While playing, the jump
    // happens at the next read without reopening the output.
    void seek(std::uint64_t frame);

    PlayerStatus status() const;

    // Blocks until the playback thread has finished (end, stop or error).
    void waitUntilIdle();

private:
    class QueueReader;
    friend class QueueReader;

    void threadMain();
    void joinThread();
    bool openCurrentLocked();      // opens queue_[index_] into source_; on failure sets a Load error
    void startLoadingNextLocked(); // opens queue_[index_ + 1] in the background
    void publishTrackLocked();     // copies the current track's facts into status_
    void retireSourceLocked();     // adds the current source's dropout counters to the totals

    OutputFactory outputFactory_;
    PlaybackOptions options_;

    mutable std::mutex mutex_;
    std::condition_variable idle_;
    PlayerStatus status_;
    std::optional<OutputPathInfo> outputPath_; // reported by the output at the start of the session
    std::string container_;                    // of the current track
    AudioOutput* activeOutput_ = nullptr; // owned by the playback thread
    std::thread thread_;
    bool running_ = false;

    // Queue state. While the playback thread runs, only it touches these
    // (it takes mutex_ only to publish changes to status_).
    std::vector<QueueItem> queue_;
    std::size_t index_ = 0;
    std::unique_ptr<PcmSource> source_;
    std::future<std::unique_ptr<PcmSource>> nextLoad_;
    std::unique_ptr<PcmSource> pendingNext_; // next track, different format: needs a reopen
    std::string loadError_;                  // next track failed to open
    bool loadFailed_ = false;
    std::optional<TrackTransition> reopenPending_; // recorded at the first read after reopening
    std::chrono::steady_clock::time_point dataEndedAt_;

    // Dropout totals from sources and outputs that are gone (guarded by mutex_).
    SourceHealth retiredSourceHealth_;
    OutputHealth retiredOutputHealth_;

    std::atomic<std::uint64_t> position_{0};
    std::atomic<std::int64_t> pendingSeek_{-1};
    std::atomic<bool> stopRequested_{false};
};

} // namespace ap::engine
