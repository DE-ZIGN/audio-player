// Task 7-1: where the article reader may go. The reader is a WebView of its
// own with no native functions; everything it tries to open passes through
// decideArticleNavigation, and only what is listed there is let through.

#include <ArticlePolicy.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using ap::app::ArticleAction;
using ap::app::decideArticleNavigation;

namespace {

ArticleAction actionOf(const std::string& url)
{
    return decideArticleNavigation(url).action;
}

} // namespace

TEST_CASE("The reader's own page is shown", "[article]")
{
    CHECK(actionOf("https://juce.backend/reader.html") == ArticleAction::show);
    CHECK(actionOf("https://juce.backend/reader.html?id=bit-perfect") == ArticleAction::show);
    CHECK(actionOf("https://juce.backend/reader.html?id=bit-perfect#checking") == ArticleAction::show);
    CHECK(actionOf("HTTPS://JUCE.BACKEND/reader.html?id=abx") == ArticleAction::show);
    CHECK(actionOf("about:blank") == ArticleAction::blank); // what a WebView starts on
}

TEST_CASE("Nothing else on the reader's origin is a page", "[article]")
{
    // The main screen's page (it expects the bridge), files, other paths.
    for (const char* url : {"https://juce.backend/", "https://juce.backend/index.html",
                            "https://juce.backend/articles/abx.md", "https://juce.backend/assets/reader.js",
                            "https://juce.backend/reader.html/../index.html", "https://juce.backend/reader.htmlx",
                            "https://juce.backend/reader.html?id=<script>", "https://juce.backend/reader.html?id=a b",
                            "https://juce.backend/reader.html#a\"b", "https://juce.backend:8443/reader.html",
                            "http://juce.backend/reader.html"})
    {
        INFO(url);
        CHECK(actionOf(url) == ArticleAction::refuse);
    }
}

TEST_CASE("The listed commands are accepted", "[article]")
{
    auto d = decideArticleNavigation("app:abx");
    CHECK(d.action == ArticleAction::command);
    CHECK(d.command == "abx");
    CHECK(d.argument.empty());

    d = decideArticleNavigation("app:output-check");
    CHECK(d.action == ArticleAction::command);
    CHECK(d.command == "output-check");

    d = decideArticleNavigation("app:settings");
    CHECK(d.action == ArticleAction::command);
    CHECK(d.command == "settings");
    CHECK(d.argument.empty());

    d = decideArticleNavigation("APP:settings/eq");
    CHECK(d.action == ArticleAction::command);
    CHECK(d.command == "settings");
    CHECK(d.argument == "eq");

    for (const auto& section : ap::app::articleSettingsSections())
    {
        INFO(section);
        d = decideArticleNavigation("app:settings/" + std::string(section));
        CHECK(d.action == ArticleAction::command);
        CHECK(d.argument == section);
    }
}

TEST_CASE("A command that is not listed is refused", "[article]")
{
    for (const char* url : {
             "app:",
             "app:play",                  // commands open screens; they never start playback
             "app:delete-library",
             "app:set-mode/tube",         // nor change a setting
             "app:settings/unknown",
             "app:settings/eq/extra",
             "app:settings/",
             "app:abx/now",               // abx takes no argument
             "app:output-check/1",
             "app:ABX",                   // names are lower case
             "app:abx?x=1",
             "app:abx#x",
             "app:abx%20",
             "app://abx",
             "app:/abx",
             "app:settings/../abx",
             "app:abx\n",
             "app: abx",
             "apps:abx",
             "xapp:abx",
         })
    {
        INFO(url);
        const auto d = decideArticleNavigation(url);
        CHECK(d.action == ArticleAction::refuse);
        CHECK_FALSE(d.reason.empty());
    }
}

TEST_CASE("Web links go to the default browser", "[article]")
{
    auto d = decideArticleNavigation("https://example.com/paper.pdf?x=1&y=2#p3");
    CHECK(d.action == ArticleAction::external);
    CHECK(d.url == "https://example.com/paper.pdf?x=1&y=2#p3");

    d = decideArticleNavigation("http://example.org");
    CHECK(d.action == ArticleAction::external);

    d = decideArticleNavigation("https://ja.wikipedia.org/wiki/%E7%9C%9F%E7%A9%BA%E7%AE%A1");
    CHECK(d.action == ArticleAction::external);

    d = decideArticleNavigation("https://example.com:8443/a");
    CHECK(d.action == ArticleAction::external);

    // A host that only looks like the reader's.
    d = decideArticleNavigation("https://juce.backend.example.com/reader.html");
    CHECK(d.action == ArticleAction::external);
}

TEST_CASE("Web links that hide where they go are refused", "[article]")
{
    for (const char* url : {
             "https://",
             "https:///path",
             "https://juce.backend@example.com/",  // user info in front of the real host
             "https://user:pw@example.com/",
             "https://example.com\\@juce.backend/",
             "https://exa mple.com/",
             "https://example.com/\"onmouseover=",
             "https://example.com/<x>",
             "https://[::1]/",
             "https://example.com:port/",
             "https:example.com",
             "https:/example.com",
         })
    {
        INFO(url);
        CHECK(actionOf(url) == ArticleAction::refuse);
    }
    CHECK(actionOf("https://example.com/" + std::string(3000, 'a')) == ArticleAction::refuse);
}

TEST_CASE("Every other scheme is refused", "[article]")
{
    for (const char* url : {
             "",
             "reader.html",
             "javascript:alert(1)",
             "JaVaScRiPt:alert(1)",
             "data:text/html,<script>alert(1)</script>",
             "file:///C:/Windows/win.ini",
             "file://server/share/x.exe",
             "blob:https://juce.backend/1234",
             "about:srcdoc",
             "about:blank#x",
             "ms-settings:display",
             "mailto:someone@example.com",
             "ftp://example.com/x",
             "ws://example.com/",
             "view-source:https://example.com/",
             "chrome://settings",
             "edge://settings",
             "vbscript:msgbox(1)",
             " https://example.com/",
             "\tapp:abx",
         })
    {
        INFO(url);
        const auto d = decideArticleNavigation(url);
        CHECK(d.action == ArticleAction::refuse);
        CHECK_FALSE(d.reason.empty());
    }
    CHECK(actionOf(std::string("app:abx\0x", 9)) == ArticleAction::refuse);
}

TEST_CASE("Article ids and the reader's address", "[article]")
{
    using ap::app::isArticleId;
    CHECK(isArticleId("bit-perfect"));
    CHECK(isArticleId("abx"));
    CHECK(isArticleId("a1"));
    CHECK_FALSE(isArticleId(""));
    CHECK_FALSE(isArticleId("-abx"));
    CHECK_FALSE(isArticleId("Abx"));
    CHECK_FALSE(isArticleId("a/b"));
    CHECK_FALSE(isArticleId("a.md"));
    CHECK_FALSE(isArticleId(".."));
    CHECK_FALSE(isArticleId(std::string(65, 'a')));

    using ap::app::articleReaderUrl;
    CHECK(articleReaderUrl("", "") == "https://juce.backend/reader.html");
    CHECK(articleReaderUrl("abx", "") == "https://juce.backend/reader.html?id=abx");
    CHECK(articleReaderUrl("abx", "level-matching") == "https://juce.backend/reader.html?id=abx#level-matching");
    // What cannot be an id or a section is left out rather than passed on.
    CHECK(articleReaderUrl("../index", "") == "https://juce.backend/reader.html");
    CHECK(articleReaderUrl("abx", "a b") == "https://juce.backend/reader.html?id=abx");
    // Whatever it returns, the reader is allowed to show.
    CHECK(actionOf(articleReaderUrl("abx", "level-matching")) == ArticleAction::show);
}
