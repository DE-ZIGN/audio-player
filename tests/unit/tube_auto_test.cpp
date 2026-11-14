// Task 9-7: choosing the tube type by genre - the table, the album choices,
// the order between them, the settings file, and the stage switching type at
// a track's start (smoothly, at the same level).

#include <ap/engine/dsp/tube.h>
#include <ap/engine/dsp/tube_auto.h>
#include <ap/engine/listening_settings.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using namespace ap::engine;

namespace {

TubeAutoSettings on(std::vector<TubeGenreRule> rules = {})
{
    TubeAutoSettings s;
    s.enabled = true;
    s.rules = std::move(rules);
    return s;
}

std::optional<TubeType> typeFor(const TubeAutoSettings& s, std::vector<std::string> genres, std::string albumKey = {})
{
    const auto c = chooseTubeType(s, genres, albumKey);
    return c ? std::optional(c->type) : std::nullopt;
}

} // namespace

TEST_CASE("Tube auto: off chooses nothing (the manual type plays)", "[tube][auto]")
{
    TubeAutoSettings s; // the default table, but off
    CHECK_FALSE(s.enabled);
    CHECK_FALSE(chooseTubeType(s, std::vector<std::string>{"Jazz"}, "").has_value());
}

TEST_CASE("Tube auto: a genre in the table chooses its type, ignoring case, width and spaces", "[tube][auto]")
{
    const auto s = on({{"Jazz", TubeType::Se300B}, {"Rock", TubeType::PpEl34}, {"クラシック", TubeType::PpClassA}});
    CHECK(typeFor(s, {"jazz"}) == TubeType::Se300B);
    CHECK(typeFor(s, {"  ROCK "}) == TubeType::PpEl34);
    CHECK(typeFor(s, {"Ｊａｚｚ"}) == TubeType::Se300B); // full-width letters
    CHECK(typeFor(s, {"クラシック"}) == TubeType::PpClassA);
    CHECK_FALSE(typeFor(s, {"Reggae"}).has_value()); // not in the table: the manual type
    CHECK_FALSE(typeFor(s, {}).has_value());         // no genre tag
    CHECK_FALSE(typeFor(s, {""}).has_value());

    const auto c = chooseTubeType(s, std::vector<std::string>{"Jazz"}, "");
    REQUIRE(c);
    CHECK(c->reason == TubeChoiceReason::Genre);
    CHECK(c->matched == "Jazz"); // the genre as the tag has it, for the page
}

TEST_CASE("Tube auto: exact matches first, then a table genre inside the tag's", "[tube][auto]")
{
    const auto s = on({{"Rock", TubeType::PpEl34}, {"Jazz", TubeType::Se300B}, {"Jazz Rock", TubeType::PpClassA}});
    // Exact beats a part, even when the part's rule comes first in the table.
    CHECK(typeFor(s, {"Jazz Rock"}) == TubeType::PpClassA);
    // A part: "Vocal Jazz" has "jazz" in it.
    CHECK(typeFor(s, {"Vocal Jazz"}) == TubeType::Se300B);
    // Two parts: the table's order decides.
    CHECK(typeFor(s, {"Rock and Jazz"}) == TubeType::PpEl34);
    // Several values in one tag ("Jazz; Rock"), or several tag values: the first that matches.
    CHECK(typeFor(s, {"Jazz; Rock"}) == TubeType::Se300B);
    CHECK(typeFor(s, {"Rock/Jazz"}) == TubeType::PpEl34);
    CHECK(typeFor(s, {"Reggae", "Jazz"}) == TubeType::Se300B);
    // An exact match in a later value beats a part in an earlier one.
    CHECK(typeFor(s, {"Hard Rock Jazz", "Jazz Rock"}) == TubeType::PpClassA);
}

TEST_CASE("Tube auto: an album's choice comes before the genre", "[tube][auto]")
{
    auto s = on({{"Jazz", TubeType::Se300B}});
    s.albums.push_back({"Bill Evans\x1FWaltz for Debby", "Waltz for Debby", TubeType::PpClassA});
    const auto c = chooseTubeType(s, std::vector<std::string>{"Jazz"}, "Bill Evans\x1FWaltz for Debby");
    REQUIRE(c);
    CHECK(c->type == TubeType::PpClassA);
    CHECK(c->reason == TubeChoiceReason::Album);
    CHECK(c->matched == "Waltz for Debby");
    // Another album: the genre.
    CHECK(typeFor(s, {"Jazz"}, "Bill Evans\x1FSunday at the Village Vanguard") == TubeType::Se300B);
    // No album (a file opened by itself): the genre.
    CHECK(typeFor(s, {"Jazz"}, "") == TubeType::Se300B);
}

TEST_CASE("Tube auto: the default table covers the common genres", "[tube][auto]")
{
    const auto s = on(defaultTubeGenreRules());
    CHECK(typeFor(s, {"Jazz"}) == TubeType::Se300B);
    CHECK(typeFor(s, {"Vocal"}) == TubeType::Se300B);
    CHECK(typeFor(s, {"ジャズ"}) == TubeType::Se300B);
    CHECK(typeFor(s, {"Rock"}) == TubeType::PpEl34);
    CHECK(typeFor(s, {"J-Pop"}) == TubeType::PpEl34);
    CHECK(typeFor(s, {"ロック"}) == TubeType::PpEl34);
    CHECK(typeFor(s, {"Classical"}) == TubeType::PpClassA);
    CHECK(typeFor(s, {"クラシック"}) == TubeType::PpClassA);
    CHECK(typeFor(s, {"Progressive Rock"}) == TubeType::PpEl34);
    // Every rule has a genre, and no genre is there twice.
    std::vector<std::string> seen;
    for (const auto& r : s.rules)
    {
        CHECK_FALSE(r.genre.empty());
        const auto k = normalizedGenre(r.genre);
        CHECK(std::find(seen.begin(), seen.end(), k) == seen.end());
        seen.push_back(k);
    }
}

TEST_CASE("Tube auto: saved and read back with the settings", "[tube][auto][settings]")
{
    AppSettings s;
    s.tubeAuto.enabled = true;
    s.tubeAuto.rules = {{"Jazz", TubeType::Se300B}, {"Rock=Roll", TubeType::PpEl34}};
    s.tubeAuto.albums = {{"Bill Evans\x1FWaltz for Debby", "Waltz for Debby", TubeType::PpClassA}};
    const auto back = settingsFromText(settingsToText(s));
    CHECK(back.tubeAuto == s.tubeAuto);

    // An emptied table stays empty (it does not fall back to the default).
    s.tubeAuto.rules.clear();
    CHECK(settingsFromText(settingsToText(s)).tubeAuto.rules.empty());

    // An old settings file (before 9-7): off, with the default table.
    const auto old = settingsFromText("mode=processed\ntube.enabled=1\n");
    CHECK_FALSE(old.tubeAuto.enabled);
    CHECK(old.tubeAuto.rules == defaultTubeGenreRules());
    CHECK(old.tubeAuto.albums.empty());

    // Broken lines are skipped.
    const auto broken = settingsFromText("tubeauto.rules=1\ntubeauto.genre=nosuchtype\tJazz\ntubeauto.genre=se300b\t\n"
                                         "tubeauto.genre=ppel34\tRock\ntubeauto.album=se300b\n");
    REQUIRE(broken.tubeAuto.rules.size() == 1);
    CHECK(broken.tubeAuto.rules[0] == TubeGenreRule{"Rock", TubeType::PpEl34});
    CHECK(broken.tubeAuto.albums.empty());
}

// --- The stage ---------------------------------------------------------------------------

namespace {

constexpr double kRate = 48000;

std::vector<double> music(std::size_t frames)
{
    // A few partials at about -18 dBFS RMS: the level a stage meets most of the time.
    std::vector<double> x(frames);
    const double f[] = {110, 220, 330, 523, 1250, 3100};
    for (std::size_t i = 0; i < frames; ++i)
    {
        double v = 0;
        for (double fk : f)
            v += std::sin(2 * std::numbers::pi * fk * static_cast<double>(i) / kRate + fk);
        x[i] = 0.0517 * v; // RMS 0.0517 * sqrt(6/2) = 0.0896 (-21 dBFS), peaks near -6 dBFS
    }
    return x;
}

double rms(const std::vector<double>& x, std::size_t from)
{
    double s = 0;
    for (std::size_t i = from; i < x.size(); ++i)
        s += x[i] * x[i];
    return std::sqrt(s / static_cast<double>(x.size() - from));
}

} // namespace

TEST_CASE("Tube auto: the types play at the same level (DRIVE at its default)", "[tube][auto][dsp]")
{
    const auto x = music(96000);
    const double in = rms(x, 48000);
    for (TubeType t : {TubeType::Se300B, TubeType::PpEl34, TubeType::PpClassA})
    {
        TubeStage s;
        s.setEnabled(true);
        s.setType(t);
        s.prepare(static_cast<std::uint32_t>(kRate), 1);
        auto y = x;
        for (std::size_t i = 0; i < y.size(); i += 480)
            s.process(std::span(y).subspan(i, 480), 1);
        const double diffDb = 20 * std::log10(rms(y, 48000) / in);
        CAPTURE(tubeTypeKey(t), diffDb);
        CHECK(std::abs(diffDb) < 0.1);
    }
}

TEST_CASE("Tube auto: the track's type plays from its start; the manual type otherwise", "[tube][auto][dsp]")
{
    TubeStage s;
    s.setEnabled(true);
    s.setType(TubeType::Se300B);
    s.prepare(static_cast<std::uint32_t>(kRate), 1);
    CHECK(s.displayDetail() == "se300b");

    TrackInfo t;
    t.tubeType = TubeType::PpEl34;
    s.trackStarted(t);
    CHECK(s.playingType() == TubeType::PpEl34);
    CHECK(s.displayDetail() == "ppel34");

    // A track with no choice: back to the manual type.
    s.trackStarted(TrackInfo{});
    CHECK(s.playingType() == TubeType::Se300B);

    // The app can change the current track's choice (the table was edited).
    s.setTrackType(TubeType::PpClassA);
    CHECK(s.playingType() == TubeType::PpClassA);
    s.setTrackType(std::nullopt);
    CHECK(s.playingType() == TubeType::Se300B);
}

TEST_CASE("Tube auto: the type switches smoothly at a track boundary", "[tube][auto][dsp]")
{
    TubeStage s;
    s.setEnabled(true);
    s.setType(TubeType::Se300B);
    s.setDriveDb(0.0); // the types differ the most
    s.prepare(static_cast<std::uint32_t>(kRate), 1);
    std::vector<double> y(48000);
    for (std::size_t i = 0; i < y.size(); ++i)
        y[i] = 0.5 * std::sin(2 * std::numbers::pi * 100 * static_cast<double>(i) / kRate);
    for (std::size_t i = 0; i < y.size(); i += 480)
    {
        if (i == 24000)
        {
            TrackInfo t;
            t.tubeType = TubeType::PpEl34;
            s.trackStarted(t);
        }
        s.process(std::span(y).subspan(i, 480), 1);
    }
    double worst = 0;
    for (std::size_t i = 1; i < y.size(); ++i)
        worst = std::max(worst, std::abs(y[i] - y[i - 1]));
    CAPTURE(worst);
    CHECK(worst < 0.012); // as for a type change by hand (tube_test)
}

TEST_CASE("Tube auto: a session that starts on a track with a choice starts in that type (no fade from the manual one)",
          "[tube][auto][dsp]")
{
    // The same input through a stage that was set to PP EL34 by hand, and one
    // that gets it from the track at the session's start: the same output.
    std::vector<double> x(4800);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = 0.8 * std::sin(2 * std::numbers::pi * 200 * static_cast<double>(i) / kRate);

    TubeStage manual;
    manual.setEnabled(true);
    manual.setType(TubeType::PpEl34);
    manual.setDriveDb(0.0);
    manual.prepare(static_cast<std::uint32_t>(kRate), 1);
    auto a = x;
    manual.process(a, 1);

    TubeStage automatic;
    automatic.setEnabled(true);
    automatic.setType(TubeType::Se300B);
    automatic.setDriveDb(0.0);
    automatic.prepare(static_cast<std::uint32_t>(kRate), 1);
    TrackInfo t;
    t.tubeType = TubeType::PpEl34;
    automatic.trackStarted(t);
    auto b = x;
    automatic.process(b, 1);

    double worst = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, std::abs(a[i] - b[i]));
    CHECK(worst < 1e-12);
}
