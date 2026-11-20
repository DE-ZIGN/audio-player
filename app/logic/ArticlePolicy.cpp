#include "ArticlePolicy.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace ap::app {

namespace {

constexpr std::size_t kMaxUrlLength = 2048;
constexpr std::size_t kMaxNameLength = 64;

// The settings screen's sections (ui/index.html, data-section).
constexpr std::array<std::string_view, 11> kSettingsSections{
    "output", "mode", "isp-safe", "replaygain", "eq", "tone", "tube", "space", "crossfeed", "output-rate", "look"};

bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return isLower(c) || (c >= 'A' && c <= 'Z'); }
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lowered(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), lower);
    return out;
}

bool all(std::string_view s, bool (*ok)(char))
{
    return std::all_of(s.begin(), s.end(), ok);
}

// A command's name or argument: lower-case letters, digits, hyphens.
bool isWordChar(char c) { return isLower(c) || isDigit(c) || c == '-'; }
bool isWord(std::string_view s) { return !s.empty() && s.size() <= kMaxNameLength && all(s, isWordChar); }

// Printable ASCII without the characters that end or escape an attribute, a
// command line or a path: an address from WebView2 arrives percent-encoded,
// so nothing else is needed.
bool isUrlChar(char c)
{
    const auto u = static_cast<unsigned char>(c);
    if (u <= 0x20 || u >= 0x7f)
        return false;
    return c != '"' && c != '\'' && c != '<' && c != '>' && c != '\\' && c != '`' && c != '^' && c != '{' && c != '}'
        && c != '|';
}

bool isHostChar(char c) { return isAlpha(c) || isDigit(c) || c == '.' || c == '-'; }
bool isQueryChar(char c) { return isAlpha(c) || isDigit(c) || c == '=' || c == '&' || c == '-' || c == '_' || c == '.'; }
bool isSectionChar(char c) { return isAlpha(c) || isDigit(c) || c == '-'; }

ArticleDecision refuse(std::string reason)
{
    ArticleDecision d;
    d.action = ArticleAction::refuse;
    d.reason = std::move(reason);
    return d;
}

// "app:<name>" or "app:<name>/<argument>", exactly.
ArticleDecision decideCommand(std::string_view rest)
{
    const auto slash = rest.find('/');
    const auto name = rest.substr(0, slash);
    const bool hasArgument = slash != std::string_view::npos;
    const auto argument = hasArgument ? rest.substr(slash + 1) : std::string_view{};
    if (!isWord(name) || (hasArgument && !isWord(argument)))
        return refuse("not a command: only app:<name> or app:<name>/<argument> in lower case");

    ArticleDecision d;
    d.action = ArticleAction::command;
    d.command = std::string(name);
    d.argument = std::string(argument);

    if (name == "abx" || name == "output-check")
        return hasArgument ? refuse("this command takes no argument") : d;
    if (name == "settings")
    {
        if (!hasArgument)
            return d;
        if (std::find(kSettingsSections.begin(), kSettingsSections.end(), argument) != kSettingsSections.end())
            return d;
        return refuse("no such settings section");
    }
    return refuse("command not in the list");
}

// On the reader's origin only the reader's page is a document.
ArticleDecision decideReaderOrigin(std::string_view afterOrigin)
{
    if (afterOrigin.substr(0, kArticleReaderPage.size()) != kArticleReaderPage)
        return refuse("only the reader's page may be shown");
    auto rest = afterOrigin.substr(kArticleReaderPage.size());

    std::string_view query, fragment;
    if (const auto hash = rest.find('#'); hash != std::string_view::npos)
    {
        fragment = rest.substr(hash + 1);
        rest = rest.substr(0, hash);
    }
    if (!rest.empty())
    {
        if (rest.front() != '?')
            return refuse("only the reader's page may be shown");
        query = rest.substr(1);
    }
    // The query ("lang=en&id=abx", task 11-5) is the page's to read, and the
    // page checks each value (reader/links.ts); here only its characters.
    if (query.size() > 200 || !all(query, isQueryChar) || fragment.size() > kMaxNameLength || !all(fragment, isSectionChar))
        return refuse("unexpected characters after the reader's page");

    ArticleDecision d;
    d.action = ArticleAction::show;
    return d;
}

ArticleDecision decideWeb(std::string_view url, std::string_view afterScheme)
{
    if (afterScheme.substr(0, 2) != "//")
        return refuse("web address without //host");
    const auto rest = afterScheme.substr(2);
    const auto authority = rest.substr(0, rest.find_first_of("/?#"));

    auto host = authority;
    if (const auto colon = authority.rfind(':'); colon != std::string_view::npos)
    {
        const auto port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !all(port, isDigit))
            return refuse("web address with an unexpected port");
        host = authority.substr(0, colon);
    }
    // Letters, digits, dots and hyphens only: no user info ("name@host"), no brackets.
    if (host.empty() || host.size() > 253 || !all(host, isHostChar) || host.front() == '.' || host.front() == '-')
        return refuse("web address with an unexpected host");
    if (lowered(host) == kArticleReaderOrigin.substr(kArticleReaderOrigin.find("//") + 2))
        return refuse("the reader's own host, but not the reader's page");

    ArticleDecision d;
    d.action = ArticleAction::external;
    d.url = std::string(url);
    return d;
}

} // namespace

std::span<const std::string_view> articleSettingsSections()
{
    return kSettingsSections;
}

bool isArticleId(std::string_view id)
{
    return isWord(id) && id.front() != '-';
}

bool isArticleSection(std::string_view section)
{
    return !section.empty() && section.size() <= kMaxNameLength && all(section, isSectionChar);
}

bool isArticleReaderLanguage(std::string_view lang)
{
    return lang == "ja" || lang == "en";
}

std::string articleReaderUrl(std::string_view articleId, std::string_view section, std::string_view lang)
{
    std::string url(kArticleReaderOrigin);
    url += kArticleReaderPage;
    const bool hasLang = isArticleReaderLanguage(lang);
    if (hasLang)
    {
        url += "?lang=";
        url += lang;
    }
    if (!isArticleId(articleId))
        return url;
    url += hasLang ? "&id=" : "?id=";
    url += articleId;
    if (isArticleSection(section))
    {
        url += '#';
        url += section;
    }
    return url;
}

ArticleDecision decideArticleNavigation(std::string_view url)
{
    if (url.empty())
        return refuse("empty address");
    if (url.size() > kMaxUrlLength)
        return refuse("address too long");
    if (!all(url, isUrlChar))
        return refuse("address with a space, a control character or a quote");

    if (url == "about:blank")
    {
        ArticleDecision d;
        d.action = ArticleAction::blank;
        return d;
    }

    const auto colon = url.find(':');
    if (colon == std::string_view::npos || colon == 0)
        return refuse("no scheme");
    const auto scheme = lowered(url.substr(0, colon));
    const auto afterScheme = url.substr(colon + 1);

    if (scheme == "app")
        return decideCommand(afterScheme);

    if (scheme == "https" || scheme == "http")
    {
        // The reader's origin, exactly: "https://juce.backend" and then a path.
        if (url.size() > kArticleReaderOrigin.size() && lowered(url.substr(0, kArticleReaderOrigin.size())) == kArticleReaderOrigin
            && url[kArticleReaderOrigin.size()] == '/')
            return decideReaderOrigin(url.substr(kArticleReaderOrigin.size()));
        return decideWeb(url, afterScheme);
    }

    return refuse("scheme not allowed: " + scheme.substr(0, 32));
}

} // namespace ap::app
