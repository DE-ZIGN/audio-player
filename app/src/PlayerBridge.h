#pragma once

#include <ap/engine/abx.h>
#include <ap/engine/listening_settings.h>
#include <ap/engine/player.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace ap::app {

// Connects the web UI to the engine's Player.
//
// Native functions (JavaScript -> C++), see ui/src/backend.ts:
//   openFile()   shows a file dialog and loads the chosen WAV
//   play(), stop(), seek(frame)
//   getStatus()  returns the current status object
//   uiReady()    the page has finished starting up
//   playAt(i)    goes to queue track i (task 6-2)
//   getQueue()   { version, items: [{ id, name, artist, seconds }] }; id 0: not from the library
//   setVolume(dB) the digital volume (0 = 100%: no processing, bit-perfect; "-inf" mutes)
//   setRepeat("off" | "all" | "one") what follows the last track, or each track (task 9-9).
//                The page keeps the mode and sends it at start; shuffle is the
//                page's own (it reorders what comes next with replaceUpcoming).
// The output and the processing settings come from the settings screen
// (SettingsBridge, task 6-3) through setOutput() and setListening().
//
// ABX (task 6-6), on the current track:
//   abxStart({ startSeconds, seconds, trials, kind, a?, b? })  kind "processed"
//       (原音 と 音作り), "16bit" (原音 と 16bit・TPDF) or "presets" (設定 A と
//       設定 B, task 6-9: a and b are EQ presets { name, bands, autoPreamp,
//       manualPreampDb }, each put on the current settings in 音作りモード;
//       task 9-4: each may also set toneBassDb, toneTrebleDb, tubeEnabled,
//       tubeType, tubeDriveDb, otherwise those stay as they are now).
//       Works out the loudness match over the region (on a background
//       thread), then plays the region over and over through the comparison.
//       { ok, message?, matchDb }
//   abxChoose("A" | "B" | "X"), abxAnswer("A" | "B")  (X is A / X is B)
//   abxAbort(), abxClose() (forgets the finished result)
// When the test ends (answered or aborted) the queue, the track and the
// position before it are put back. The status carries "abx" (see ui/src/abx.ts).
// Event (C++ -> JavaScript): "playbackStatus" with the status object,
// sent at most 10 times per second and only when it changed.
class PlayerBridge final : private juce::Timer
{
public:
    // Without an output factory, plays on `output` (exclusive WASAPI or ASIO);
    // with one (the self-test's dummy output), on that, and setOutput() only
    // changes whether DoP is allowed.
    PlayerBridge(ap::engine::Player::OutputFactory outputFactory, const ap::engine::AppSettings& settings);
    ~PlayerBridge() override;

    juce::WebBrowserComponent::Options registerFunctions(juce::WebBrowserComponent::Options options);
    void attach(juce::WebBrowserComponent* browser) { browser_ = browser; }

    std::function<void()> onUiReady;
    // selfTestResult({ ok, log }) from the page in self-test mode.
    std::function<void(const juce::var&)> onSelfTestResult;

    // Loads WAV files as the play queue (in file-name order); returns an empty
    // string on success, else a Japanese message.
    juce::String loadFiles(juce::Array<juce::File> files);
    juce::String load(const juce::File& file);

    // Plays tracks from the library as the queue, from `start` (task 6-1).
    // Each carries its measured ReplayGain values (task 5-6). Returns an empty
    // string on success, else a Japanese message.
    struct QueueFile
    {
        std::string name;
        std::filesystem::path path;
        std::optional<ap::engine::ReplayGainInfo> replayGain;
        std::int64_t libraryId = 0; // 0: not from the library
        std::string artist;
        std::optional<double> seconds;
        // For choosing the tube type (task 9-7).
        std::vector<std::string> genres;
        std::string albumKey; // empty: not in an album
        std::string album;
    };
    juce::String playQueue(std::vector<QueueFile> files, std::size_t start);
    // Replaces the tracks after the current one; the current track plays on (task 6-2).
    void replaceUpcoming(std::vector<QueueFile> files);

    // Looked at 10 times a second with the library id of the current track
    // (0: none or not from the library), for counting plays (task 6-2).
    std::function<void(const ap::engine::PlayerStatus&, std::int64_t libraryId)> onPoll;

    juce::var statusAsVar() const;

    // Plays on another output from now on. While playing, playback stops and
    // goes on at the same position on the new output.
    void setOutput(const ap::engine::OutputChoice& output);
    void setListening(const ap::engine::ListeningSettings& settings);
    // What became of the space's user IR files (task 9-6), as last applied.
    std::array<ap::engine::SpaceIrStatus, 3> spaceIrStatus() const { return chain_.spaceIrStatus(); }
    // The tube type by genre (task 9-7): applied to the queue at once (the
    // current track switches with the usual crossfade).
    void setTubeAuto(const ap::engine::TubeAutoSettings& settings);
    // Solo one EQ band (nullopt: off); not saved.
    void setEqSolo(std::optional<std::size_t> band);
    // The rate of what is playing now (0 when nothing has been opened), for the EQ curve.
    std::uint32_t currentSampleRate() const;

private:
    void timerCallback() override;
    void openFileAsync(juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void pushStatus(bool force);
    ap::engine::QueueItem itemFor(const QueueFile& f) const;
    std::optional<ap::engine::TubeChoice> tubeChoiceFor(const QueueFile& f) const;
    void refreshTubeTypes(); // the queue's chosen types again, after the settings changed
    // Runs on the playback thread. Throws OutputException.
    std::unique_ptr<ap::engine::AudioOutput> openChosenOutput();

    // ABX (task 6-6).
    void abxStartAsync(const juce::var& request, juce::WebBrowserComponent::NativeFunctionCompletion done);
    void abxFinish(); // stop, put back the queue, the settings and the position
    juce::var abxVar() const;
    struct AbxSession
    {
        ap::engine::AbxTest test;
        std::string kind;
        std::uint64_t start = 0, length = 0;
        std::uint32_t rate = 0;
        double matchDb = 0.0;
        std::string nameA, nameB; // 設定 A と設定 B: the presets' names
        ap::engine::AbxChoice listening = ap::engine::AbxChoice::A;
        bool running = true;
        // What was playing before, to put back.
        std::vector<QueueFile> queue;
        std::size_t index = 0;
        std::uint64_t position = 0;
    };
    std::optional<AbxSession> abx_;
    // 設定 A と設定 B (task 6-9): side A's chain while the ABX runs.
    std::shared_ptr<ap::engine::ListeningChain> abxReference_;
    bool abxPreparing_ = false;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    // The tube's warm-up (task 9-8) counts from the app's start (the amp's switch-on).
    const std::chrono::steady_clock::time_point switchedOn_ = std::chrono::steady_clock::now();
    double warmUpSeconds() const
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - switchedOn_).count();
    }
    void updateWarmUp(); // hands the time to the tube stages (not to a side held cold)
    // During an ABX: side A's and side B's warm-up held at switch-on (0 s).
    std::pair<bool, bool> abxHold_{false, false};

    using QueueMeta = QueueFile; // what each queue entry was made from
    ap::engine::ListeningChain chain_; // ReplayGain, EQ, volume, ISP-safe, limiter
    ap::engine::ListeningSettings listening_; // as last applied (the mode and ISP-safe go in the status)
    ap::engine::TubeAutoSettings tubeAuto_;
    mutable std::mutex outputMutex_;   // guards output_ and outputNotice_
    ap::engine::OutputChoice output_;
    std::string outputNotice_; // why playback is not on the chosen output (Japanese), if it is not
    std::vector<QueueMeta> queueMeta_; // parallel to the player's queue
    int queueVersion_ = 0;

    ap::engine::Player player_;
    juce::WebBrowserComponent* browser_ = nullptr;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String lastSent_;
};

} // namespace ap::app
