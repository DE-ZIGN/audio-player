#include "UiResources.h"

namespace ap::app {

juce::String mimeTypeFor(const juce::String& extension)
{
    const auto ext = extension.toLowerCase();
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".js" || ext == ".mjs")   return "text/javascript";
    if (ext == ".css")                   return "text/css";
    if (ext == ".json")                  return "application/json";
    if (ext == ".svg")                   return "image/svg+xml";
    if (ext == ".png")                   return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".webp")                  return "image/webp";
    if (ext == ".ico")                   return "image/x-icon";
    if (ext == ".woff2")                 return "font/woff2";
    if (ext == ".woff")                  return "font/woff";
    return "application/octet-stream";
}

std::optional<juce::WebBrowserComponent::Resource> provideUiResource(const juce::File& root, const juce::String& url)
{
    // url is the path part, e.g. "/" or "/assets/index-abc.js?v=1".
    auto path = url.upToFirstOccurrenceOf("?", false, false).upToFirstOccurrenceOf("#", false, false);
    path = juce::URL::removeEscapeChars(path).trimCharactersAtStart("/");
    if (path.isEmpty())
        path = "index.html";

    // Refuse anything that could leave the UI folder.
    if (path.contains("..") || path.contains(":") || path.startsWith("\\"))
        return std::nullopt;

    const auto file = root.getChildFile(path.replaceCharacter('/', juce::File::getSeparatorChar()));
    if (!file.existsAsFile() || !file.isAChildOf(root))
        return std::nullopt;

    juce::MemoryBlock block;
    if (!file.loadFileAsData(block))
        return std::nullopt;

    juce::WebBrowserComponent::Resource r;
    const auto* bytes = static_cast<const std::byte*>(block.getData());
    r.data.assign(bytes, bytes + block.getSize());
    r.mimeType = mimeTypeFor(file.getFileExtension());
    return r;
}

} // namespace ap::app
