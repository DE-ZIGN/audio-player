#include "ArticleSelfTest.h"

namespace ap::app {

namespace {

constexpr juce::uint32 kStepTimeoutMs = 15000; // the first start of a WebView2 profile is slow

juce::String actionName(ArticleAction a)
{
    switch (a)
    {
        case ArticleAction::show:     return "show";
        case ArticleAction::blank:    return "blank";
        case ArticleAction::command:  return "command";
        case ArticleAction::external: return "external";
        case ArticleAction::refuse:   return "refuse";
    }
    return "?";
}

juce::String describe(const ArticleDecision& d)
{
    auto s = actionName(d.action);
    if (d.action == ArticleAction::command)
        s += " " + juce::String(d.command) + (d.argument.empty() ? "" : "/" + juce::String(d.argument));
    if (d.action == ArticleAction::refuse)
        s += " (" + juce::String(d.reason) + ")";
    return s;
}

juce::String click(const juce::String& href)
{
    return "document.querySelector('#article a[href=\"" + href + "\"]').click(); ''";
}

juce::String go(const juce::String& url)
{
    return "location.assign('" + url + "'); ''";
}

} // namespace

ArticleSelfTest::ArticleSelfTest(ArticleWindow& window, juce::WebBrowserComponent& mainBrowser, Done done)
    : window_(window), mainBrowser_(mainBrowser), done_(std::move(done))
{
    const auto value = [this](juce::String name, Kind kind, juce::String script, juce::String expected) {
        Step s;
        s.name = std::move(name);
        s.kind = kind;
        s.script = std::move(script);
        s.value = std::move(expected);
        steps_.push_back(std::move(s));
    };
    const auto decision = [this](juce::String name, juce::String script, juce::String url, ArticleAction action,
                                 juce::String command = {}, juce::String argument = {}) {
        Step s;
        s.name = std::move(name);
        s.script = std::move(script);
        s.url = std::move(url);
        s.action = action;
        s.command = std::move(command);
        s.argument = std::move(argument);
        steps_.push_back(std::move(s));
    };

    value("the bundled article is shown, with its command links", Kind::readerValue,
          "String(document.querySelectorAll('#article a[href^=\"app:\"]').length >= 3)", "true");
    value("the reader has no native functions", Kind::readerValue,
          "String((((window.__JUCE__ || {}).initialisationData || {}).__juce__functions || []).length)", "0");

    // The article's own links.
    decision("link: open the output check", click("app:output-check"), "app:output-check", ArticleAction::command,
             "output-check");
    value("the command reached the main screen (the output check is open)", Kind::mainValue,
          "String(document.getElementById('output-dialog').open)", "true");
    decision("link: open the ABX", click("app:abx"), "app:abx", ArticleAction::command, "abx");
    decision("link: open the settings", click("app:settings"), "app:settings", ArticleAction::command, "settings");
    decision("a settings section", go("app:settings/eq"), "app:settings/eq", ArticleAction::command, "settings", "eq");

    // Web links: the default browser (recorded here, not opened), never this window.
    decision("a web link", go("https://example.com/paper?x=1"), "https://example.com/paper?x=1", ArticleAction::external);
    decision("a web link asking for a new window", "window.open('https://example.org/new'); ''",
             "https://example.org/new", ArticleAction::external);

    // What is not listed.
    decision("a command that is not listed", go("app:delete-library"), "app:delete-library", ArticleAction::refuse);
    decision("a command that would change a setting", go("app:set-mode/tube"), "app:set-mode/tube", ArticleAction::refuse);
    decision("a settings section that does not exist", go("app:settings/unknown"), "app:settings/unknown",
             ArticleAction::refuse);
    decision("the main screen's page", go("https://juce.backend/index.html"), "https://juce.backend/index.html",
             ArticleAction::refuse);
    value("the main screen's page is not sent to the reader either", Kind::readerValue,
          "window.__r = ''; fetch('index.html').then(r => { window.__r = String(r.status); }, () => { window.__r = "
          "'failed'; }); ''",
          "");
    value("(result)", Kind::readerValue, "window.__r === '200' ? '200' : (window.__r ? 'not sent' : '')", "not sent");
    value("after all that, the reader still shows its own page", Kind::readerValue, "location.pathname", "/reader.html");

    // Nothing is loaded from outside (no ads, no tracking): the page's CSP stops it.
    value("asking for an image and data from another site", Kind::readerValue,
          "window.__v = 0; document.addEventListener('securitypolicyviolation', () => { window.__v++; });"
          "new Image().src = 'https://example.invalid/x.png'; fetch('https://example.invalid/x').catch(() => {}); ''",
          "");
    value("both were stopped by the page's policy", Kind::readerValue, "String(window.__v >= 2)", "true");

    // Another article: the reader's own page.
    decision("another article", go("reader.html?id=no-such-article"), "/reader.html?id=no-such-article",
             ArticleAction::show);
    value("an article that does not exist says so", Kind::readerValue,
          "String(document.querySelector('#article .missing') !== null)", "true");
}

ArticleSelfTest::~ArticleSelfTest()
{
    stopTimer();
}

void ArticleSelfTest::start()
{
    if (window_.problem().isNotEmpty())
    {
        finish(false, "the reader cannot be shown: " + window_.problem());
        return;
    }
    window_.setTopLeftPosition(-20000, -20000); // out of the way, like the main window in a test
    window_.showArticle("about-articles", {});
    stepStartMs_ = juce::Time::getMillisecondCounter();
    startTimer(50);
}

void ArticleSelfTest::record(const juce::String& url, const ArticleDecision& decision)
{
    log_.add("  asked: " + url + " -> " + describe(decision));
    seen_.push_back({url, decision});
}

void ArticleSelfTest::evaluate(juce::WebBrowserComponent& where, const juce::String& script, bool wantValue)
{
    if (wantValue)
        evaluating_ = true;
    where.evaluateJavascript(
        script, [weak = juce::WeakReference<ArticleSelfTest>(this), wantValue](juce::WebBrowserComponent::EvaluationResult r) {
            if (weak == nullptr || !wantValue)
                return;
            weak->evaluating_ = false;
            weak->hasValue_ = true;
            weak->lastValue_ = r.getResult() != nullptr ? r.getResult()->toString() : juce::String("(error)");
        });
}

void ArticleSelfTest::begin(const Step& step)
{
    begun_ = true;
    hasValue_ = false;
    stepStartMs_ = juce::Time::getMillisecondCounter();
    seen_.clear();
    if (step.kind == Kind::decision)
        evaluate(*window_.browser(), step.script, false);
}

void ArticleSelfTest::pass(const Step& step, const juce::String& detail)
{
    log_.add("ok: " + step.name + (detail.isEmpty() ? "" : " [" + detail + "]"));
    ++index_;
    begun_ = false;
}

void ArticleSelfTest::finish(bool ok, const juce::String& lastLine)
{
    if (finished_)
        return;
    finished_ = true;
    stopTimer();
    if (lastLine.isNotEmpty())
        log_.add(lastLine);
    log_.add(ok ? "ARTICLE SELF TEST PASSED" : "ARTICLE SELF TEST FAILED");
    if (done_)
        done_(ok, log_);
}

void ArticleSelfTest::timerCallback()
{
    if (index_ >= steps_.size())
    {
        finish(true, {});
        return;
    }
    const auto& step = steps_[index_];
    if (!begun_)
        begin(step);
    const bool timedOut = juce::Time::getMillisecondCounter() - stepStartMs_ > kStepTimeoutMs;

    if (step.kind == Kind::decision)
    {
        for (const auto& s : seen_)
        {
            if (!s.url.endsWith(step.url))
                continue;
            const auto& d = s.decision;
            const bool same = d.action == step.action
                           && (step.action != ArticleAction::command
                               || (juce::String(d.command) == step.command && juce::String(d.argument) == step.argument))
                           && (step.action != ArticleAction::external || juce::String(d.url) == step.url);
            if (same)
                pass(step, describe(d));
            else
                finish(false, "FAILED: " + step.name + ": expected " + actionName(step.action) + ", got " + describe(d));
            return;
        }
        if (timedOut)
            finish(false, "FAILED: " + step.name + ": the reader never asked for " + step.url);
        return;
    }

    // A value: asked again until it is the expected one (pages load and dialogs open in their own time).
    if (hasValue_ && lastValue_ == step.value)
    {
        pass(step, {});
        return;
    }
    if (timedOut)
    {
        finish(false, "FAILED: " + step.name + ": expected \"" + step.value + "\", got \""
                          + (hasValue_ ? lastValue_ : juce::String("(nothing)")) + "\"");
        return;
    }
    if (!evaluating_)
        evaluate(step.kind == Kind::mainValue ? mainBrowser_ : *window_.browser(), step.script, true);
}

} // namespace ap::app
