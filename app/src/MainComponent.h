#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ap::app {

// Placeholder content until the WebView UI arrives in step S3.
class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void paint(juce::Graphics& g) override;

private:
    juce::String statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace ap::app
