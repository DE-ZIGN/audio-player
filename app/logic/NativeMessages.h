#pragma once

// Task 11-3 (M11; CLAUDE.md: 多言語化): what the engine and the app tell the
// page, as a code and its values, never as a sentence. The page writes the
// sentence in the language in use from its tables: the key "native.<code>"
// (ui/src/i18n/parts/native.ts), each value put in as {name}
// (ui/src/nativeText.ts). A technical text from an exception (e.what(),
// English) is a value ("detail"), shown as it is.
//
// The list below is read as text by a test of the page (ui/src/nativeText.test.ts):
// every code must have its key, with exactly the value names given here. So
// each code stays on one line of its own, written as
//     inline constexpr NativeCode name{"group.code", "valueName", ...};
// Codes are lower camelCase words joined by dots, grouped by subject; value
// names are lower camelCase. The constructor checks both when compiling.
//
// No JUCE here, so the tests link it without the application.

#include <array>
#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ap::app {

// What the page is told. Empty code: nothing to say.
struct NativeMessage
{
    std::string code;
    std::vector<std::pair<std::string, std::string>> values; // name and value (UTF-8), in the declared order

    bool empty() const noexcept { return code.empty(); }
    bool operator==(const NativeMessage&) const = default;
};

// A value name: a lower-case letter, then letters and digits.
constexpr bool isNativeValueName(std::string_view s) noexcept
{
    if (s.empty() || s[0] < 'a' || s[0] > 'z')
        return false;
    for (const char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return false;
    return true;
}

// A code: two or more value-name-like words joined by dots ("abx.alreadyRunning").
constexpr bool isNativeCodeText(std::string_view s) noexcept
{
    std::size_t words = 0;
    while (true)
    {
        const auto dot = s.find('.');
        if (!isNativeValueName(s.substr(0, dot)))
            return false;
        ++words;
        if (dot == std::string_view::npos)
            return words >= 2;
        s.remove_prefix(dot + 1);
    }
}

// A code and the names of its values. A message is made only from one of
// these, with exactly as many values (strings) as it names, in that order:
// no name can be wrong.
template <std::size_t N>
struct NativeCode
{
    std::string_view code;
    std::array<std::string_view, N> names{};

    template <class... Names>
        requires(sizeof...(Names) == N)
    consteval NativeCode(const char* c, Names... n) : code(c), names{std::string_view(n)...}
    {
        if (!isNativeCodeText(code))
            throw "a code is lower camelCase words joined by dots";
        for (std::size_t i = 0; i < N; ++i)
        {
            if (!isNativeValueName(names[i]))
                throw "a value name is lower camelCase";
            for (std::size_t j = 0; j < i; ++j)
                if (names[j] == names[i])
                    throw "a value is named once";
        }
    }

    template <class... Values>
        requires(sizeof...(Values) == N && (std::convertible_to<const Values&, std::string_view> && ...))
    NativeMessage operator()(const Values&... values) const
    {
        NativeMessage m{std::string(code), {}};
        m.values.reserve(N);
        std::size_t i = 0;
        (m.values.emplace_back(std::string(names[i++]), std::string(std::string_view(values))), ...);
        return m;
    }

    // True when `m` was made from this code: the same code, the declared names in order.
    bool made(const NativeMessage& m) const
    {
        if (m.code != code || m.values.size() != N)
            return false;
        for (std::size_t i = 0; i < N; ++i)
            if (m.values[i].first != names[i])
                return false;
        return true;
    }
};
template <class... Names>
NativeCode(const char*, Names...) -> NativeCode<sizeof...(Names)>;

// For logs and the self test's own failures (not for the user): "code (name=value, ...)".
inline std::string technicalText(const NativeMessage& m)
{
    std::string s = m.code;
    for (std::size_t i = 0; i < m.values.size(); ++i)
        s += (i == 0 ? " (" : ", ") + m.values[i].first + "=" + m.values[i].second;
    return m.values.empty() ? s : s + ")";
}

// --- the codes (one per line, see above) ---------------------------------------------
namespace msg {

// ABX (PlayerBridge::abxStartAsync).
inline constexpr NativeCode abxAlreadyRunning{"abx.alreadyRunning"};
inline constexpr NativeCode abxNotPlaying{"abx.notPlaying"};
inline constexpr NativeCode abxDop{"abx.dop"};
inline constexpr NativeCode abxChoosePair{"abx.choosePair"};
inline constexpr NativeCode abxAnalysisFailed{"abx.analysisFailed", "detail"};

// The article reader (MainComponent::openArticle, from ArticleWindow).
inline constexpr NativeCode articleWebViewMissing{"article.webViewMissing"};
inline constexpr NativeCode articlePageMissing{"article.pageMissing", "path"};

// ASIO drivers (AsioCheck, the settings, the output notice).
inline constexpr NativeCode asioLoadFailed{"asio.loadFailed", "detail"};
inline constexpr NativeCode asioCrashed{"asio.crashed"};
inline constexpr NativeCode asioTimedOut{"asio.timedOut"};
inline constexpr NativeCode asioUnusable{"asio.unusable"};
inline constexpr NativeCode asioNotRegistered{"asio.notRegistered"};
inline constexpr NativeCode asioDriverMissing{"asio.driverMissing", "name"};

// Reading an EQ file (the engine's EqWarning by kind; the file itself).
inline constexpr NativeCode eqPreampUnreadable{"eq.preampUnreadable", "line"};
inline constexpr NativeCode eqLineSkipped{"eq.lineSkipped", "line"};
inline constexpr NativeCode eqFilterUnreadable{"eq.filterUnreadable", "line"};
inline constexpr NativeCode eqTypeUnsupported{"eq.typeUnsupported", "line", "type"};
inline constexpr NativeCode eqValuesUnreadable{"eq.valuesUnreadable", "line"};
inline constexpr NativeCode eqFileUnreadable{"eq.fileUnreadable", "file", "detail"};

// The library (LibraryBridge).
inline constexpr NativeCode libraryNewerDatabase{"library.newerDatabase", "path"};
inline constexpr NativeCode libraryOpenFailed{"library.openFailed", "path", "detail"};
inline constexpr NativeCode libraryNoPlayableTracks{"library.noPlayableTracks"};
inline constexpr NativeCode libraryAddFolderFailed{"library.addFolderFailed", "detail"};

// The output (PlayerBridge's notice, the settings' device list).
inline constexpr NativeCode outputFallbackDefault{"output.fallbackDefault", "name"};
inline constexpr NativeCode outputListFailed{"output.listFailed"};

// Opening files to play (PlayerBridge).
inline constexpr NativeCode playerUnreadable{"player.unreadable", "file", "detail"};
inline constexpr NativeCode playerOpenFailed{"player.openFailed", "detail"};
inline constexpr NativeCode playerNothingToPlay{"player.nothingToPlay"};

// A user IR file for the space (the engine's IrFileProblem by kind).
inline constexpr NativeCode spaceIrMissing{"space.irMissing"};
inline constexpr NativeCode spaceIrNotAudio{"space.irNotAudio"};
inline constexpr NativeCode spaceIrDsd{"space.irDsd"};
inline constexpr NativeCode spaceIrUnreadable{"space.irUnreadable"};
inline constexpr NativeCode spaceIrTooLong{"space.irTooLong"};
inline constexpr NativeCode spaceIrSilent{"space.irSilent"};

} // namespace msg

} // namespace ap::app
