#include "MainComponent.h"

#include "NativeVar.h"
#include "UiResources.h"

#include <StartupTexts.h>

#include <ap/engine/output/dummy_output.h>

namespace ap::app {

namespace {

const juce::Colour kBackground{0xff14120f};

juce::File webViewDataFolder()
{
    return juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
        .getChildFile(JUCE_APPLICATION_NAME_STRING)
        .getChildFile("WebView2");
}

ap::engine::Player::OutputFactory outputFactoryFor(const MainOptions& o)
{
    if (o.selfTestWav == juce::File{})
        return {}; // exclusive WASAPI
    const std::filesystem::path out(o.selfTestOutput.getFullPathName().toWideCharPointer());
    return [out] {
        return std::make_unique<ap::engine::DummyOutput>(ap::engine::DummyOutput::Options{
            .path = out, .periodFrames = {480}, .check = {}, .paceInRealTime = true});
    };
}

std::vector<PlayerBridge::QueueFile> toFiles(std::vector<LibraryBridge::QueueEntry> entries)
{
    std::vector<PlayerBridge::QueueFile> files;
    for (auto& e : entries)
        files.push_back({std::move(e.name), std::move(e.path), e.replayGain, e.id, std::move(e.artist), e.seconds,
                         std::move(e.genres), std::move(e.albumKey), std::move(e.album)});
    return files;
}

juce::File settingsFile(const MainOptions& o)
{
    return o.libraryFolder.getChildFile("settings.txt");
}

ap::engine::AppSettings startSettingsFor(const MainOptions& o)
{
    auto s = SettingsBridge::load(settingsFile(o));
    if (o.allowDop)
        s.output.allowDop = true;
    return s;
}

} // namespace

MainComponent::MainComponent(MainOptions options)
    : options_(std::move(options)), startSettings_(startSettingsFor(options_)),
      bridge_(outputFactoryFor(options_), startSettings_), settings_(settingsFile(options_), startSettings_, bridge_),
      library_(
          options_.libraryFolder,
          [this](std::vector<LibraryBridge::QueueEntry> entries, std::size_t start) {
              return bridge_.playQueue(toFiles(std::move(entries)), start);
          },
          [this](std::vector<LibraryBridge::QueueEntry> entries) { bridge_.replaceUpcoming(toFiles(std::move(entries))); })
{
    bridge_.onPoll = [this](const ap::engine::PlayerStatus& s, std::int64_t id) { library_.observe(s, id); };
    setSize(1280, 800); // the library view (task 6-1): a list, the side bar and the queue side by side
    bridge_.onUiReady = options_.onUiReady;
    if (options_.onArticleSelfTestResult)
    {
        bridge_.onUiReady = [this] {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
                if (safe == nullptr || safe->articleTest_ != nullptr || safe->browser_ == nullptr)
                    return;
                safe->articleTest_ = std::make_unique<ArticleSelfTest>(safe->articleWindow(), *safe->browser_,
                                                                       safe->options_.onArticleSelfTestResult);
                safe->articleTest_->start();
            });
        };
    }
    bridge_.onSelfTestResult = options_.onSelfTestResult;
    const bool selfTest = options_.selfTestWav != juce::File{};
    if (selfTest)
    {
        const auto message = bridge_.load(options_.selfTestWav);
        if (!message.empty())
        {
            problem_ = juce::String(technicalText(message)); // the self test's file: not for users
            return;
        }
    }

    using Options = juce::WebBrowserComponent::Options;
    auto browserOptions =
        Options{}
            .withBackend(Options::Backend::webview2)
            .withWinWebView2Options(Options::WinWebView2{}
                                        .withUserDataFolder(webViewDataFolder())
                                        .withBackgroundColour(kBackground)
                                        .withStatusBarDisabled())
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withResourceProvider([this, root = options_.uiRoot](const juce::String& url) {
                if (auto art = library_.provideResource(url))
                    return art; // album covers
                return provideUiResource(root, url);
            });
    browserOptions = bridge_.registerFunctions(browserOptions);
    browserOptions = library_.registerFunctions(browserOptions);
    browserOptions = settings_.registerFunctions(browserOptions);
    // openArticle(id, section): the article reader (task 7-1). Empty id: the list of articles.
    browserOptions = browserOptions.withNativeFunction(
        "openArticle", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            const auto message = openArticle(args.size() > 0 ? args[0].toString() : juce::String(),
                                             args.size() > 1 ? args[1].toString() : juce::String());
            auto* result = new juce::DynamicObject();
            result->setProperty("ok", message.empty());
            if (!message.empty())
                result->setProperty("message", nativeVar(message));
            done(juce::var(result));
        });

    // Before the page exists: the app's own texts, in the Windows display language (task 11-3).
    const auto language = windowsStartupLanguage();
    if (!juce::WebBrowserComponent::areOptionsSupported(browserOptions))
    {
        problem_ = juce::String::fromUTF8(startupText(StartupText::WebViewMissing, language).c_str());
        return;
    }

    const bool useDevServer = options_.devServerUrl.isNotEmpty();
    if (!useDevServer && !options_.uiRoot.getChildFile("index.html").existsAsFile())
    {
        problem_ = juce::String::fromUTF8(
            startupText(StartupText::PageMissing, language, options_.uiRoot.getFullPathName().toStdString()).c_str());
        return;
    }

    browser_ = std::make_unique<juce::WebBrowserComponent>(browserOptions);
    bridge_.attach(browser_.get());
    library_.attach(browser_.get());
    settings_.attach(browser_.get());
    addAndMakeVisible(*browser_);
    resized(); // setSize() above ran before the browser existed
    juce::String url = useDevServer ? options_.devServerUrl : juce::WebBrowserComponent::getResourceProviderRoot();
    if (selfTest)
    {
        // The page checks that its viewport matches the component it lives in.
        url += (useDevServer ? "" : "index.html") + juce::String("?selftest=1")
             + "&w=" + juce::String(browser_->getWidth()) + "&h=" + juce::String(browser_->getHeight());
    }
    browser_->goToURL(url);
}

ArticleWindow& MainComponent::articleWindow()
{
    if (articles_ == nullptr)
    {
        ArticleWindow::Hooks hooks;
        hooks.onCommand = [this](const ArticleDecision& command) { sendArticleCommand(command); };
        if (options_.onArticleSelfTestResult)
        {
            hooks.openExternally = [](const juce::String&) {}; // the test opens no browser
            hooks.onDecision = [this](const juce::String& url, const ArticleDecision& d) {
                if (articleTest_ != nullptr)
                    articleTest_->record(url, d);
            };
        }
        articles_ = std::make_unique<ArticleWindow>(options_.uiRoot, std::move(hooks));
    }
    return *articles_;
}

NativeMessage MainComponent::openArticle(const juce::String& id, const juce::String& section)
{
    // The page's own values are checked like any other: what is not an id
    // or a section is left out (articleReaderUrl).
    auto& window = articleWindow();
    if (!window.problem().empty())
        return window.problem();
    window.showArticle(id, section);
    return {};
}

void MainComponent::sendArticleCommand(const ArticleDecision& command)
{
    if (browser_ == nullptr)
        return;
    auto* payload = new juce::DynamicObject();
    payload->setProperty("command", juce::String(command.command));
    payload->setProperty("argument", juce::String(command.argument));
    browser_->emitEventIfBrowserIsVisible("articleCommand", juce::var(payload));
    if (auto* top = getTopLevelComponent(); top != nullptr && !options_.onArticleSelfTestResult)
        top->toFront(true); // the screen that was asked for, in front of the article
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
    if (problem_.isNotEmpty())
    {
        g.setColour(juce::Colour(0xffece4d4));
        g.setFont(juce::FontOptions(15.0f));
        g.drawFittedText(problem_, getLocalBounds().reduced(32), juce::Justification::centred, 6);
    }
}

void MainComponent::resized()
{
    if (browser_)
        browser_->setBounds(getLocalBounds());
}

} // namespace ap::app
