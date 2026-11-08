#pragma once

#include <ap/engine/output/audio_output.h>
#include <ap/engine/pcm_source.h>
#include <ap/engine/playback.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace ap::engine {

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
    Other,
};

struct PlayerError
{
    PlayerErrorKind kind = PlayerErrorKind::None;
    FormatCheck formatCheck;
    OutputError outputError = OutputError::DeviceError;
    std::string detail; // English, for logs
};

struct PlayerStatus
{
    PlayerState state = PlayerState::Empty;
    std::optional<PcmFormat> sourceFormat;
    std::optional<PlaybackPlan> plan; // set once the output has been opened
    std::string outputName;
    std::uint64_t positionFrames = 0;
    std::uint64_t totalFrames = 0;
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

    // Stops playback and replaces the source. Position goes to 0.
    void load(std::unique_ptr<PcmSource> source);

    // Starts playback from the current position (from 0 after the end).
    // Does nothing when empty or already playing.
    void play();

    // Stops playback and keeps the position. Blocks until the output has stopped.
    void stop();

    // Moves the position. While playing, the jump happens at the next read
    // without reopening the output.
    void seek(std::uint64_t frame);

    PlayerStatus status() const;

    // Blocks until the playback thread has finished (end, stop or error).
    void waitUntilIdle();

private:
    void threadMain();
    void joinThread();

    OutputFactory outputFactory_;
    PlaybackOptions options_;

    mutable std::mutex mutex_;
    std::condition_variable idle_;
    std::unique_ptr<PcmSource> source_;
    PlayerStatus status_;
    AudioOutput* activeOutput_ = nullptr; // owned by the playback thread
    std::thread thread_;
    bool running_ = false;

    std::atomic<std::uint64_t> position_{0};
    std::atomic<std::int64_t> pendingSeek_{-1};
    std::atomic<bool> stopRequested_{false};
};

} // namespace ap::engine
