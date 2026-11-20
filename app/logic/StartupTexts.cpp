#include "StartupTexts.h"

#include <array>

#include <windows.h>

namespace ap::app {

namespace {

constexpr std::array kAll{StartupText::WebViewMissing, StartupText::PageMissing, StartupText::ArticleWindowTitle};

// {path} is where the path goes. Say what is missing and what to do; nothing more.
std::string_view templateFor(StartupText text, StartupLanguage language) noexcept
{
    const bool ja = language == StartupLanguage::Japanese;
    switch (text)
    {
        case StartupText::WebViewMissing:
            return ja ? "画面の表示に必要な Microsoft Edge WebView2 ランタイムが見つかりません。"
                        "Microsoft のサイトから「WebView2 ランタイム」をインストールしてください。"
                      : "The Microsoft Edge WebView2 Runtime, which shows this app's screens, was not found. "
                        "Install the \"WebView2 Runtime\" from Microsoft's website.";
        case StartupText::PageMissing:
            return ja ? "画面のファイルが見つかりません: {path}" : "The screen's files were not found: {path}";
        case StartupText::ArticleWindowTitle:
            return ja ? "記事" : "Articles";
    }
    return {};
}

} // namespace

StartupLanguage startupLanguageFor(unsigned short windowsLanguageId) noexcept
{
    return PRIMARYLANGID(windowsLanguageId) == LANG_JAPANESE ? StartupLanguage::Japanese : StartupLanguage::English;
}

StartupLanguage windowsStartupLanguage() noexcept
{
    return startupLanguageFor(GetUserDefaultUILanguage());
}

std::span<const StartupText> allStartupTexts() noexcept
{
    return kAll;
}

std::string startupText(StartupText text, StartupLanguage language, std::string_view path)
{
    std::string s(templateFor(text, language));
    if (const auto at = s.find("{path}"); at != std::string::npos)
        s.replace(at, 6, path);
    return s;
}

} // namespace ap::app
