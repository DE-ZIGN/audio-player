#pragma once

#include <ap/engine/player.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

namespace ap::app {

// Connects the web UI to the engine's Player.
//
// Native functions (JavaScript -> C++), see ui/src/backend.ts:
//   openFile()   shows a file dialog and loads the chosen WAV
//   play(), stop(), seek(frame)
//   getStatus()  returns the current status object
//   uiReady()    the page has finished starting up
// Event (C++ -> JavaScript): "playbackStatus" with the status object,
// sent at most 10 times per second and only when it changed.
class PlayerBridge final : private juce::Timer
{
public:
    // Default: exclusive WASAPI on the default device.
    explicit PlayerBridge(ap::engine::Player::OutputFactory outputFactory = {});
    ~PlayerBridge() override;

    juce::WebBrowserComponent::Options registerFunctions(juce::WebBrowserComponent::Options options);
    void attach(juce::WebBrowserComponent* browser) { browser_ = browser; }

    std::function<void()> onUiReady;
    // selfTestResult({ ok, log }) from the page in self-test mode.
    std::function<void(const juce::var&)> onSelfTestResult;

    // Loads a WAV file; returns an empty string on success, else a Japanese message.
    juce::String load(const juce::File& file);

    juce::var statusAsVar() const;

private:
    void timerCallback() override;
    void openFileAsync(juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void pushStatus(bool force);

    ap::engine::Player player_;
    juce::String fileName_;
    juce::WebBrowserComponent* browser_ = nullptr;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String lastSent_;
};

} // namespace ap::app
