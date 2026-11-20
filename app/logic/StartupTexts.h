#pragma once

// Task 11-3 (M11; CLAUDE.md: 多言語化): the few texts the app draws itself,
// before the page exists (the page's own texts are in its tables, ui/src/i18n/):
// that WebView2 or the page's files are missing, and the article window's
// title. In Japanese or English, by the Windows display language: Japanese
// when its primary language is Japanese, else English (the page's rule for its
// default, ui/src/i18n.ts). The article window opens from the page, so its
// title follows the language the page passes (pageStartupLanguage, task 11-5).
//
// No JUCE here, so the tests link it without the application.

#include <span>
#include <string>
#include <string_view>

namespace ap::app {

enum class StartupLanguage
{
    Japanese,
    English,
};

// From a Windows language id (LANGID): its primary language decides.
StartupLanguage startupLanguageFor(unsigned short windowsLanguageId) noexcept;
// The Windows display language of this user (GetUserDefaultUILanguage).
StartupLanguage windowsStartupLanguage() noexcept;
// The language a page says it shows ("ja" or "en", as ui/src/i18n.ts names
// them; task 11-5); `otherwise` when it gives none or another.
StartupLanguage pageStartupLanguage(std::string_view pageLang, StartupLanguage otherwise) noexcept;

enum class StartupText
{
    WebViewMissing,     // the main window: no WebView2 runtime
    PageMissing,        // the main window: no index.html ({path}: the page's folder)
    ArticleWindowTitle, // the article reader's window
};
std::span<const StartupText> allStartupTexts() noexcept;

// The text in that language (UTF-8), `path` put in where it has one.
std::string startupText(StartupText text, StartupLanguage language, std::string_view path = {});

} // namespace ap::app
