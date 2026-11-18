#pragma once

// Task 7-1: where the article reader may go.
//
// The reader (ArticleWindow) is a WebView of its own, apart from the main
// screen: it has no native functions, so a page in it cannot reach the
// engine, the library or the settings. Whatever it tries to open, by a link
// or by script, is given to decideArticleNavigation first:
//
//   - the reader's own page                    -> shown
//   - a listed command, "app:<name>[/<arg>]"   -> handed to the main screen
//   - an http/https address elsewhere          -> the default browser
//   - anything else                            -> refused
//
// Commands only open a screen of the main window (ABX, the output check, the
// settings). None changes a setting, starts playback or touches a file, and
// what is not in the list is refused.
//
// No JUCE here, so the tests link it without the application.

#include <span>
#include <string>
#include <string_view>

namespace ap::app {

enum class ArticleAction
{
    show,     // the reader's own page
    blank,    // about:blank (what a WebView starts on)
    command,  // a listed command for the main screen
    external, // open in the default browser
    refuse,
};

struct ArticleDecision
{
    ArticleAction action = ArticleAction::refuse;
    std::string command;  // command: "abx", "output-check" or "settings"
    std::string argument; // command: the settings section, or empty
    std::string url;      // external: the address for the default browser
    std::string reason;   // refuse: why (for the log; English)
};

// The reader's origin: JUCE's resource provider root on Windows.
inline constexpr std::string_view kArticleReaderOrigin = "https://juce.backend";
inline constexpr std::string_view kArticleReaderPage = "/reader.html";

ArticleDecision decideArticleNavigation(std::string_view url);

// The sections "app:settings/<section>" may name.
std::span<const std::string_view> articleSettingsSections();

// An article's fixed id: lower-case letters, digits and hyphens, 1 to 64, starting with a letter or digit.
bool isArticleId(std::string_view id);
// A section inside an article (a heading's id): letters, digits and hyphens, 1 to 64.
bool isArticleSection(std::string_view section);

// The reader's address for an article (empty id: the list of articles).
// An id or a section that is not valid is left out.
std::string articleReaderUrl(std::string_view articleId, std::string_view section);

} // namespace ap::app
