// Task 11-3 (M11): the engine and the app tell the page a code and its values;
// the page writes the sentence (ui/src/nativeText.ts, whose test checks the
// codes against the page's tables).

#include <NativeMessages.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

using namespace ap::app;

TEST_CASE("A message is made from its code, with the declared value names in order", "[native]")
{
    const auto m = msg::playerUnreadable(std::string("a.flac"), "not a FLAC file");
    CHECK(m.code == "player.unreadable");
    CHECK(m.values == std::vector<std::pair<std::string, std::string>>{{"file", "a.flac"}, {"detail", "not a FLAC file"}});
    CHECK(msg::playerUnreadable.made(m));
    CHECK_FALSE(msg::playerOpenFailed.made(m));

    const auto none = msg::abxAlreadyRunning();
    CHECK(none.code == "abx.alreadyRunning");
    CHECK(none.values.empty());
    CHECK_FALSE(none.empty());
    CHECK(NativeMessage{}.empty());

    // A message that claims a code but carries other names is not one of its own.
    CHECK_FALSE(msg::asioDriverMissing.made(NativeMessage{"asio.driverMissing", {{"driver", "x"}}}));
    CHECK_FALSE(msg::asioDriverMissing.made(NativeMessage{"asio.driverMissing", {}}));
}

TEST_CASE("A message takes exactly as many values as its code names, and only text", "[native]")
{
    using Unreadable = decltype(msg::playerUnreadable);
    using AlreadyRunning = decltype(msg::abxAlreadyRunning);
    STATIC_CHECK(std::is_invocable_v<Unreadable, std::string, std::string>);
    STATIC_CHECK(std::is_invocable_v<Unreadable, const char*, std::string_view>);
    STATIC_CHECK_FALSE(std::is_invocable_v<Unreadable, std::string>);
    STATIC_CHECK_FALSE(std::is_invocable_v<Unreadable, std::string, std::string, std::string>);
    STATIC_CHECK_FALSE(std::is_invocable_v<Unreadable, int, std::string>);
    STATIC_CHECK(std::is_invocable_v<AlreadyRunning>);
    STATIC_CHECK_FALSE(std::is_invocable_v<AlreadyRunning, std::string>);
}

TEST_CASE("Codes and value names follow the rules the compiler checks", "[native]")
{
    STATIC_CHECK(isNativeCodeText("abx.alreadyRunning"));
    STATIC_CHECK(isNativeCodeText("a.b.c2"));
    STATIC_CHECK_FALSE(isNativeCodeText("abx"));
    STATIC_CHECK_FALSE(isNativeCodeText("Abx.running"));
    STATIC_CHECK_FALSE(isNativeCodeText("abx..running"));
    STATIC_CHECK_FALSE(isNativeCodeText("abx.running."));
    STATIC_CHECK_FALSE(isNativeCodeText("abx.already-running"));
    STATIC_CHECK(isNativeValueName("detail"));
    STATIC_CHECK(isNativeValueName("fileName2"));
    STATIC_CHECK_FALSE(isNativeValueName(""));
    STATIC_CHECK_FALSE(isNativeValueName("File"));
    STATIC_CHECK_FALSE(isNativeValueName("file_name"));
}

TEST_CASE("The technical form of a message, for logs", "[native]")
{
    CHECK(technicalText(msg::abxDop()) == "abx.dop");
    CHECK(technicalText(msg::eqTypeUnsupported("12", "BWLP")) == "eq.typeUnsupported (line=12, type=BWLP)");
}
