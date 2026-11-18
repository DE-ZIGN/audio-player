#pragma once

// Task 7-1: the article reader's self-test (--article-self-test).
//
// In the real WebView2: opens the bundled article, follows its links and
// tries addresses the reader must not open, and checks what the policy
// decided for each. Web links are recorded, not opened. Also checks that the
// reader has no native functions, that its page loads nothing from outside,
// and that a command reaches the main screen.

#include "ArticleWindow.h"

#include <functional>
#include <vector>

namespace ap::app {

class ArticleSelfTest final : private juce::Timer
{
public:
    using Done = std::function<void(bool ok, const juce::StringArray& log)>;

    // mainBrowser: the main screen's WebView, to see that a command arrived.
    ArticleSelfTest(ArticleWindow& window, juce::WebBrowserComponent& mainBrowser, Done done);
    ~ArticleSelfTest() override;

    void start();

    // From the window's hooks.
    void record(const juce::String& url, const ArticleDecision& decision);

private:
    enum class Kind
    {
        decision,    // the script makes the reader ask for `url`; the policy must answer `action`
        readerValue, // the script's value in the reader must become `value`
        mainValue,   // the script's value in the main screen must become `value`
    };

    struct Step
    {
        juce::String name;
        Kind kind = Kind::decision;
        juce::String script;
        juce::String url; // decision: the address asked for (its end is enough)
        ArticleAction action = ArticleAction::refuse;
        juce::String command, argument;
        juce::String value;
    };

    struct Seen
    {
        juce::String url;
        ArticleDecision decision;
    };

    void timerCallback() override;
    void begin(const Step& step);
    void evaluate(juce::WebBrowserComponent& where, const juce::String& script, bool wantValue);
    void pass(const Step& step, const juce::String& detail);
    void finish(bool ok, const juce::String& lastLine);

    ArticleWindow& window_;
    juce::WebBrowserComponent& mainBrowser_;
    Done done_;
    std::vector<Step> steps_;
    std::size_t index_ = 0;
    bool begun_ = false;        // the current step's script has been sent
    bool evaluating_ = false;   // a value has been asked for and not come back yet
    juce::String lastValue_;
    bool hasValue_ = false;
    juce::uint32 stepStartMs_ = 0;
    std::vector<Seen> seen_;    // since the current step began
    juce::StringArray log_;
    bool finished_ = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE(ArticleSelfTest)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArticleSelfTest)
};

} // namespace ap::app
