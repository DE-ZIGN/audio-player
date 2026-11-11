#include "MainComponent.h"

#include <ap/engine/pcm_source.h>
#include <ap/engine/wav_file_source.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace ap::app {

namespace {

// After the page's self test: the last run was "play from the start, then
// stop", so the dummy output must be a non-empty prefix of the source PCM.
juce::String checkSelfTestOutput(const juce::File& wav, const juce::File& output)
{
    try
    {
        ap::engine::WavFileSource source(std::filesystem::path(wav.getFullPathName().toWideCharPointer()));
        const auto memory = ap::engine::MemoryPcmSource::loadAll(source);
        juce::MemoryBlock out;
        if (!output.loadFileAsData(out) || out.getSize() == 0)
            return "dummy output is empty or missing";
        const auto& data = memory.data();
        if (out.getSize() > data.size() || out.getSize() % source.format().bytesPerFrame() != 0)
            return "dummy output has an unexpected size";
        if (std::memcmp(out.getData(), data.data(), out.getSize()) != 0)
            return "dummy output differs from the source PCM";
        return {};
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
}

} // namespace

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, MainOptions options, bool visible)
        : DocumentWindow(name, juce::Colour(0xff14120f), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(std::move(options)), true);
        setResizable(true, true);
        setResizeLimits(480, 360, 10000, 10000);
        centreWithSize(getWidth(), getHeight());
        setVisible(visible);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

// Command line:
//   --dev-server[=URL]  load the UI from the Vite dev server (default http://localhost:5173/)
//   --ui-dir=PATH       load the built UI from PATH instead of <exe dir>/ui
//   --allow-dop         send DSD files as DoP (only for DACs that decode DoP)
//   --library-dir=PATH  keep the library database and the cover cache in PATH
//                       (default: %LOCALAPPDATA%\<app name>; a fresh temporary
//                       one in self-test mode)
//   --smoke-test        start, wait until the UI reports ready, then quit
//                       (exit code 0; 1 if the UI did not become ready in time)
//   --self-test=WAV     the page plays/seeks/stops WAV through the bridge into a
//                       dummy output (--self-test-output=RAW, default in TEMP);
//                       exit code 0 if the page and the output check pass.
//                       A log is written next to the output (RAW.log).
class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        smokeTest_ = args.contains("--smoke-test");

        MainOptions options;
        options.uiRoot = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getParentDirectory()
                             .getChildFile("ui");
        options.libraryFolder = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                                    .getChildFile(JUCE_APPLICATION_NAME_STRING);
        bool libraryGiven = false;
        for (auto arg : args)
        {
            arg = arg.unquoted();
            // Values may be quoted: --name="C:\path with spaces"
            const auto value = arg.fromFirstOccurrenceOf("=", false, false).unquoted();
            if (arg == "--dev-server")
                options.devServerUrl = "http://localhost:5173/";
            else if (arg.startsWith("--dev-server="))
                options.devServerUrl = value;
            else if (arg == "--allow-dop")
                options.allowDop = true;
            else if (arg.startsWith("--ui-dir="))
                options.uiRoot = juce::File(value);
            else if (arg.startsWith("--library-dir="))
            {
                options.libraryFolder = juce::File(value);
                libraryGiven = true;
            }
            else if (arg.startsWith("--self-test="))
                options.selfTestWav = juce::File(value);
            else if (arg.startsWith("--self-test-output="))
                options.selfTestOutput = juce::File(value);
        }

        if ((options.selfTestWav != juce::File{} || smokeTest_) && !libraryGiven)
        {
            // The self-test and the smoke test never touch the user's library.
            options.libraryFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                        .getChildFile("ap_app_self_test_library");
            options.libraryFolder.deleteRecursively();
        }

        if (options.selfTestWav != juce::File{})
        {
            smokeTest_ = true; // same window handling and timeout
            if (options.selfTestOutput == juce::File{})
                options.selfTestOutput = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                             .getChildFile("ap_app_self_test.raw");
            options.selfTestOutput.deleteFile();
            options.onSelfTestResult = [wav = options.selfTestWav, out = options.selfTestOutput](const juce::var& r) {
                juce::StringArray log;
                if (auto* lines = r.getProperty("log", {}).getArray())
                    for (const auto& line : *lines)
                        log.add(line.toString());
                bool ok = static_cast<bool>(r.getProperty("ok", false));
                if (ok)
                {
                    const auto problem = checkSelfTestOutput(wav, out);
                    log.add(problem.isEmpty() ? "dummy output is an exact prefix of the source PCM" : problem);
                    ok = problem.isEmpty();
                }
                log.add(ok ? "SELF TEST PASSED" : "SELF TEST FAILED");
                out.getSiblingFile(out.getFileName() + ".log").replaceWithText(log.joinIntoString("\n"));

                juce::MessageManager::callAsync([ok] {
                    juce::JUCEApplication::getInstance()->setApplicationReturnValue(ok ? 0 : 1);
                    juce::JUCEApplication::quit();
                });
            };
        }

        if (smokeTest_ && !options.onSelfTestResult)
        {
            options.onUiReady = [] {
                juce::MessageManager::callAsync([] {
                    juce::JUCEApplication::getInstance()->setApplicationReturnValue(0);
                    juce::JUCEApplication::quit();
                });
            };
        }
        if (smokeTest_)
        {
            // Fail if the page has not reported back in time.
            setApplicationReturnValue(1);
            startTimer(30000);
        }

        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), std::move(options), true);
        if (smokeTest_)
        {
            // Keep the window out of the way; WebView2 needs it to exist and be shown.
            mainWindow_->setTopLeftPosition(-20000, -20000);
        }
    }

    void shutdown() override { mainWindow_.reset(); }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    void timerCallback() override
    {
        stopTimer();
        quit(); // return value stays 1
    }

    std::unique_ptr<MainWindow> mainWindow_;
    bool smokeTest_ = false;
};

} // namespace ap::app

START_JUCE_APPLICATION(ap::app::Application)
