#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace ap::app {

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, bool visible)
        : DocumentWindow(name,
                         juce::Desktop::getInstance().getDefaultLookAndFeel()
                             .findColour(juce::ResizableWindow::backgroundColourId),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
        setResizable(true, true);
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

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        // --smoke-test: build the window hidden, then quit once the message
        // loop is running. Used by CTest to check that the app starts.
        const bool smokeTest = commandLine.contains("--smoke-test");

        mainWindow = std::make_unique<MainWindow>(getApplicationName(), !smokeTest);

        if (smokeTest)
            juce::MessageManager::callAsync([] { juce::JUCEApplication::quit(); });
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace ap::app

START_JUCE_APPLICATION(ap::app::Application)
