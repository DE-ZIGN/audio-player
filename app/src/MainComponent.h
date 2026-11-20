#pragma once

#include "ArticleSelfTest.h"
#include "ArticleWindow.h"
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

    // The article reader's self-test (task 7-1, ArticleSelfTest.h): run once
    // the main screen is ready; web links are recorded instead of opened.
    std::function<void(bool ok, const juce::StringArray& log)> onArticleSelfTestResult;
};

class MainComponent final : public juce::Component
{
public:
    explicit MainComponent(MainOptions options);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // The article reader (task 7-1): a window of its own, made when first asked for.
    // Returns what is wrong (for the page, task 11-3), or an empty message.
    NativeMessage openArticle(const juce::String& id, const juce::String& section);
    ArticleWindow& articleWindow();
    // A listed command from an article: the main screen opens that screen.
    void sendArticleCommand(const ArticleDecision& command);

    MainOptions options_;
    ap::engine::AppSettings startSettings_; // as saved, read before the player is made
    PlayerBridge bridge_;
    SettingsBridge settings_;
    LibraryBridge library_;
    std::unique_ptr<juce::WebBrowserComponent> browser_;
    juce::String problem_; // shown instead of the UI when it cannot be loaded (StartupTexts.h)
    std::unique_ptr<ArticleWindow> articles_;
    std::unique_ptr<ArticleSelfTest> articleTest_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace ap::app
