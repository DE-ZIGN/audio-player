#include "ArticleWindow.h"

#include "UiResources.h"

#include <StartupTexts.h>

namespace ap::app {

namespace {

const juce::Colour kBackground{0xff14120f};

// A profile of its own: nothing stored by the main screen (its settings in
// localStorage) can be read or changed from an article.
juce::File readerDataFolder()
{
    return juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
        .getChildFile(JUCE_APPLICATION_NAME_STRING)
        .getChildFile("WebView2-Articles");
}

// Only the reader's page, what it loads and the articles: not the main
// screen's page, which expects the bridge.
std::optional<juce::WebBrowserComponent::Resource> provideReaderResource(const juce::File& root, const juce::String& url)
{
    const auto path = url.upToFirstOccurrenceOf("?", false, false)
                          .upToFirstOccurrenceOf("#", false, false)
                          .trimCharactersAtStart("/");
    if (path != "reader.html" && !path.startsWith("assets/") && !path.startsWith("articles/"))
        return std::nullopt;
    return provideUiResource(root, url);
}

// Commands and web links take effect at most this often, so a page cannot
// open a row of browser tabs or screens.
constexpr juce::uint32 kEffectIntervalMs = 500;

} // namespace

class ArticleWindow::Browser final : public juce::WebBrowserComponent
{
public:
    Browser(ArticleWindow& owner, const Options& options) : WebBrowserComponent(options), owner_(owner) {}

    bool pageAboutToLoad(const juce::String& newURL) override { return owner_.decide(newURL); }

    // No second window, whatever asks for one: a web link goes to the default
    // browser and a command to the main screen, as from a plain link.
    void newWindowAttemptingToLoad(const juce::String& newURL) override
    {
        if (owner_.decide(newURL))
            goToURL(newURL); // the reader's own page: in this window
    }

    void pageFinishedLoading(const juce::String& url) override
    {
        if (owner_.hooks_.onPageLoaded)
            owner_.hooks_.onPageLoaded(url);
    }

    // No error page of JUCE's (a data: address, which the reader refuses).
    bool pageLoadHadNetworkError(const juce::String&) override { return false; }

private:
    ArticleWindow& owner_;
};

ArticleWindow::ArticleWindow(const juce::File& uiRoot, Hooks hooks)
    : DocumentWindow(juce::String::fromUTF8(startupText(StartupText::ArticleWindowTitle, windowsStartupLanguage()).c_str()),
                     kBackground, DocumentWindow::allButtons),
      hooks_(std::move(hooks))
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(420, 320, 10000, 10000);

    // The reader's origin in ArticlePolicy.h is JUCE's resource provider root.
    jassert(juce::WebBrowserComponent::getResourceProviderRoot()
            == juce::String(std::string(kArticleReaderOrigin)) + "/");

    using Options = juce::WebBrowserComponent::Options;
    // No withNativeIntegrationEnabled, no native functions, no event listeners.
    const auto options = Options{}
                             .withBackend(Options::Backend::webview2)
                             .withWinWebView2Options(Options::WinWebView2{}
                                                         .withUserDataFolder(readerDataFolder())
                                                         .withBackgroundColour(kBackground))
                             .withResourceProvider(
                                 [uiRoot](const juce::String& url) { return provideReaderResource(uiRoot, url); });

    if (!juce::WebBrowserComponent::areOptionsSupported(options))
        problem_ = msg::articleWebViewMissing();
    else if (!uiRoot.getChildFile("reader.html").existsAsFile())
        problem_ = msg::articlePageMissing(uiRoot.getFullPathName().toStdString());
    if (!problem_.empty())
        return;

    auto browser = std::make_unique<Browser>(*this, options);
    browser->setSize(860, 900);
    browser_ = browser.get();
    setContentOwned(browser.release(), true);
    centreWithSize(getWidth(), getHeight());
}

ArticleWindow::~ArticleWindow() = default;

void ArticleWindow::showArticle(const juce::String& id, const juce::String& section, const juce::String& lang)
{
    if (browser_ == nullptr)
        return;
    const auto language = pageStartupLanguage(lang.toStdString(), windowsStartupLanguage());
    setName(juce::String::fromUTF8(startupText(StartupText::ArticleWindowTitle, language).c_str()));
    setVisible(true);
    toFront(true);
    // The reader's words follow the language too (it is left out when not "ja" or "en").
    browser_->goToURL(juce::String(articleReaderUrl(id.toStdString(), section.toStdString(), lang.toStdString())));
}

void ArticleWindow::closeButtonPressed()
{
    setVisible(false);
}

bool ArticleWindow::decide(const juce::String& url)
{
    // UTF-8: anything outside ASCII is refused by the policy.
    const auto decision = decideArticleNavigation(url.toStdString());
    if (hooks_.onDecision)
        hooks_.onDecision(url, decision);

    switch (decision.action)
    {
        case ArticleAction::show:
        case ArticleAction::blank:
            return true;
        case ArticleAction::command:
        case ArticleAction::external:
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (lastEffectMs_ != 0 && now - lastEffectMs_ < kEffectIntervalMs)
                return false;
            lastEffectMs_ = now == 0 ? 1 : now;
            // Not from inside WebView2's callback.
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ArticleWindow>(this), decision] {
                if (safe == nullptr)
                    return;
                if (decision.action == ArticleAction::command)
                {
                    if (safe->hooks_.onCommand)
                        safe->hooks_.onCommand(decision);
                }
                else if (safe->hooks_.openExternally)
                    safe->hooks_.openExternally(juce::String(decision.url));
                else
                    juce::URL(juce::String(decision.url)).launchInDefaultBrowser();
            });
            return false;
        }
        case ArticleAction::refuse:
            DBG("article reader: refused " << url << " (" << juce::String(decision.reason) << ")");
            return false;
    }
    return false;
}

} // namespace ap::app
