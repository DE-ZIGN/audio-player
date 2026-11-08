#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>

namespace ap::app {

// Serves the built UI (ui/dist copied to <exe dir>/ui) to WebBrowserComponent.
// Only files inside `root` are served; "/" maps to index.html.
std::optional<juce::WebBrowserComponent::Resource> provideUiResource(const juce::File& root, const juce::String& url);

// MIME type for a file extension such as ".js".
juce::String mimeTypeFor(const juce::String& extension);

} // namespace ap::app
