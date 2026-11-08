#include "MainComponent.h"

#include <ap/engine/version.h>

namespace ap::app {

MainComponent::MainComponent()
    : statusText(juce::String::fromUTF8("準備中（画面はステップS3で作ります）")
                 + "\nengine " + ap::engine::versionString())
{
    setSize(640, 400);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(16.0f));
    g.drawFittedText(statusText, getLocalBounds().reduced(20), juce::Justification::centred, 4);
}

} // namespace ap::app
