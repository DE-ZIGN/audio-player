#include "MainComponent.h"

#include "UiResources.h"

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

} // namespace

MainComponent::MainComponent(MainOptions options)
    : options_(std::move(options)), bridge_(outputFactoryFor(options_), options_.allowDop),
      library_(options_.libraryFolder, [this](std::vector<LibraryBridge::QueueEntry> entries, std::size_t start) {
          std::vector<PlayerBridge::QueueFile> files;
          for (auto& e : entries)
              files.push_back({std::move(e.name), std::move(e.path), e.replayGain});
          return bridge_.playQueue(std::move(files), start);
      })
{
    setSize(1280, 800); // the library view (task 6-1): a list, the side bar and the queue side by side
    bridge_.onUiReady = options_.onUiReady;
    bridge_.onSelfTestResult = options_.onSelfTestResult;
    const bool selfTest = options_.selfTestWav != juce::File{};
    if (selfTest)
    {
        const auto message = bridge_.load(options_.selfTestWav);
        if (message.isNotEmpty())
        {
            problem_ = message;
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

    if (!juce::WebBrowserComponent::areOptionsSupported(browserOptions))
    {
        problem_ = juce::String::fromUTF8(
            "画面の表示に必要な Microsoft Edge WebView2 ランタイムが見つかりません。"
            "Microsoft のサイトから「WebView2 ランタイム」をインストールしてください。");
        return;
    }

    const bool useDevServer = options_.devServerUrl.isNotEmpty();
    if (!useDevServer && !options_.uiRoot.getChildFile("index.html").existsAsFile())
    {
        problem_ = juce::String::fromUTF8("画面のファイルが見つかりません: ") + options_.uiRoot.getFullPathName();
        return;
    }

    browser_ = std::make_unique<juce::WebBrowserComponent>(browserOptions);
    bridge_.attach(browser_.get());
    library_.attach(browser_.get());
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
