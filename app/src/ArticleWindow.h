#pragma once

// Task 7-1: the article reader, a window and a WebView of its own.
//
// It is kept apart from the main screen: no native functions are registered
// (a page in it cannot call the engine, the library or the settings), it has
// its own WebView2 profile (no shared storage), and its resource provider
// sends only the reader's page, its scripts and the articles. Every address
// it tries to open goes through decideArticleNavigation (ArticlePolicy.h).

#include <ArticlePolicy.h>
#include <NativeMessages.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace ap::app {

class ArticleWindow final : public juce::DocumentWindow
{
public:
    struct Hooks
    {
        // A listed command from an article, for the main screen.
        std::function<void(const ArticleDecision&)> onCommand;
        // A web link. Unset: the default browser (the self-test sets it, to open nothing).
        std::function<void(const juce::String& url)> openExternally;
        // Every decision, with the address asked for (the self-test).
        std::function<void(const juce::String& url, const ArticleDecision&)> onDecision;
        std::function<void(const juce::String& url)> onPageLoaded;
    };

    ArticleWindow(const juce::File& uiRoot, Hooks hooks);
    ~ArticleWindow() override;

    // Empty when the reader can be shown; else what is missing (for the main page, task 11-3).
    const NativeMessage& problem() const { return problem_; }

    // Shows the window with that article (empty id: the list of articles).
    void showArticle(const juce::String& id, const juce::String& section);

    juce::WebBrowserComponent* browser() { return browser_; }

    void closeButtonPressed() override;

private:
    class Browser;

    // What the reader may do with this address; true to let the WebView load it.
    bool decide(const juce::String& url);

    Hooks hooks_;
    NativeMessage problem_;
    juce::WebBrowserComponent* browser_ = nullptr; // owned by the window (its content)
    juce::uint32 lastEffectMs_ = 0;                // commands and web links: one in a while

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArticleWindow)
};

} // namespace ap::app
