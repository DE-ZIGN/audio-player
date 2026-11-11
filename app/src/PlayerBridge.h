#pragma once

#include <ap/engine/dsp/volume.h>
#include <ap/engine/player.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <memory>

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
// Event (C++ -> JavaScript): "playbackStatus" with the status object,
// sent at most 10 times per second and only when it changed.
class PlayerBridge final : private juce::Timer
{
public:
    // Default: exclusive WASAPI on the default device.
    explicit PlayerBridge(ap::engine::Player::OutputFactory outputFactory = {}, bool allowDop = false);
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
    };
    juce::String playQueue(std::vector<QueueFile> files, std::size_t start);
    // Replaces the tracks after the current one; the current track plays on (task 6-2).
    void replaceUpcoming(std::vector<QueueFile> files);

    // Looked at 10 times a second with the library id of the current track
    // (0: none or not from the library), for counting plays (task 6-2).
    std::function<void(const ap::engine::PlayerStatus&, std::int64_t libraryId)> onPoll;

    juce::var statusAsVar() const;

private:
    void timerCallback() override;
    void openFileAsync(juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void pushStatus(bool force);
    static ap::engine::QueueItem itemFor(const QueueFile& f);

    struct QueueMeta
    {
        std::int64_t id = 0;
        std::string name, artist;
        std::optional<double> seconds;
    };
    std::shared_ptr<ap::engine::VolumeStage> volume_ = std::make_shared<ap::engine::VolumeStage>();
    std::vector<QueueMeta> queueMeta_; // parallel to the player's queue
    int queueVersion_ = 0;

    ap::engine::Player player_;
    juce::WebBrowserComponent* browser_ = nullptr;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String lastSent_;
};

} // namespace ap::app
