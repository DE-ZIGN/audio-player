// Task 11-3 (M11): the few texts the app draws before the page exists, in
// Japanese and English, chosen by the Windows display language (the page's own
// rule for its default, ui/src/i18n.ts).

#include <StartupTexts.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>

using namespace ap::app;

TEST_CASE("Japanese for a Japanese Windows display language, English for any other", "[startup]")
{
    CHECK(startupLanguageFor(0x0411) == StartupLanguage::Japanese); // ja-JP
    CHECK(startupLanguageFor(0x0011) == StartupLanguage::Japanese); // ja (neutral)
    CHECK(startupLanguageFor(0x0409) == StartupLanguage::English);  // en-US
    CHECK(startupLanguageFor(0x0809) == StartupLanguage::English);  // en-GB
    CHECK(startupLanguageFor(0x0407) == StartupLanguage::English);  // de-DE
    CHECK(startupLanguageFor(0x0804) == StartupLanguage::English);  // zh-CN
    CHECK(startupLanguageFor(0x0412) == StartupLanguage::English);  // ko-KR
    CHECK(startupLanguageFor(0) == StartupLanguage::English);
}

TEST_CASE("Both languages have every text, and they differ", "[startup]")
{
    std::set<std::string> seen;
    for (const auto text : allStartupTexts())
    {
        const auto ja = startupText(text, StartupLanguage::Japanese, "C:\\ui");
        const auto en = startupText(text, StartupLanguage::English, "C:\\ui");
        CHECK_FALSE(ja.empty());
        CHECK_FALSE(en.empty());
        CHECK(ja != en);
        CHECK(std::all_of(en.begin(), en.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; })); // plain ASCII
        CHECK(seen.insert(ja).second);
        CHECK(ja.find('{') == std::string::npos); // nothing left unfilled
        CHECK(en.find('{') == std::string::npos);
    }
    CHECK(seen.size() == 3);
}

TEST_CASE("The Japanese is the app's text as before", "[startup]")
{
    CHECK(startupText(StartupText::WebViewMissing, StartupLanguage::Japanese)
          == "画面の表示に必要な Microsoft Edge WebView2 ランタイムが見つかりません。"
             "Microsoft のサイトから「WebView2 ランタイム」をインストールしてください。");
    CHECK(startupText(StartupText::PageMissing, StartupLanguage::Japanese, "C:\\app\\ui")
          == "画面のファイルが見つかりません: C:\\app\\ui");
    CHECK(startupText(StartupText::ArticleWindowTitle, StartupLanguage::Japanese) == "記事");
    CHECK(startupText(StartupText::PageMissing, StartupLanguage::English, "C:\\app\\ui").ends_with("C:\\app\\ui"));
}
