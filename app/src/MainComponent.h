#pragma once

#include "LibraryBridge.h"
#include "PlayerBridge.h"
#include "SettingsBridge.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

namespace ap::app {

struct MainOptions
{
    // When set, load the UI from the Vite dev server instead of <exe dir>/ui.
    juce::String devServerUrl;
    // Folder with the built UI.
    juce::File uiRoot;
    // Send DSD files as DoP on the saved output for this run, whatever the
    // settings say (only for DACs that decode DoP; normally set in the settings screen).
    bool allowDop = false;
    // Holds the library database, the cover cache (task 6-1) and settings.txt (task 6-3).
    juce::File libraryFolder;
    // Called once the page has started and talked to the bridge.
    std::function<void()> onUiReady;

    // Self-test mode: load this WAV, play through a dummy output written to
    // selfTestOutput, and open the page with ?selftest=1 so it drives the
    // bridge. onSelfTestResult receives { ok, log } from the page.
    juce::File selfTestWav;
    juce::File selfTestOutput;
    std::function<void(const juce::var&)> onSelfTestResult;
};

class MainComponent final : public juce::Component
{
public:
    explicit MainComponent(MainOptions options);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    MainOptions options_;
    ap::engine::AppSettings startSettings_; // as saved, read before the player is made
    PlayerBridge bridge_;
    SettingsBridge settings_;
    LibraryBridge library_;
    std::unique_ptr<juce::WebBrowserComponent> browser_;
    juce::String problem_; // shown instead of the UI when it cannot be loaded

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace ap::app
