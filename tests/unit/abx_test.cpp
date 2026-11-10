// Task 4-1: ABX — the judging logic (decided with the user: results hidden
// until the end; "distinguished" at p < 0.05).

#include <ap/engine/abx.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ap::engine;

namespace {

// A fixed sequence for X: B, A, B, B, A, ...
std::function<bool()> sequence(std::vector<bool> bits)
{
    auto i = std::make_shared<std::size_t>(0);
    return [bits, i] { return bits[(*i)++ % bits.size()]; };
}

} // namespace

TEST_CASE("ABX p-value: the chance of at least this many correct by guessing", "[abx]")
{
    CHECK(std::abs(abxPValue(12, 16) - 0.0384063720703125) < 1e-15); // 2517 / 65536
    CHECK(std::abs(abxPValue(11, 16) - 0.1050567626953125) < 1e-15);
    CHECK(std::abs(abxPValue(16, 16) - 1.0 / 65536) < 1e-18);
    CHECK(abxPValue(0, 16) == 1.0);
    CHECK(std::abs(abxPValue(8, 16) - 39203.0 / 65536) < 1e-15); // (2^16 + C(16, 8)) / 2 / 2^16
    CHECK(std::abs(abxPValue(7, 10) - 0.171875) < 1e-15);
}

TEST_CASE("ABX: 16 trials by default; A, B and X map to the sides", "[abx]")
{
    AbxTest t(AbxTest::kDefaultTrials, sequence({true, false}));
    CHECK(t.trials() == 16);
    CHECK(t.trial() == 1);
    CHECK(t.sideFor(AbxChoice::A) == ComparisonSide::Original);
    CHECK(t.sideFor(AbxChoice::B) == ComparisonSide::Processed);
    CHECK(t.sideFor(AbxChoice::X) == ComparisonSide::Processed); // X is B in trial 1
    t.answer(AbxChoice::B);
    CHECK(t.trial() == 2);
    CHECK(t.sideFor(AbxChoice::X) == ComparisonSide::Original); // X is A in trial 2
}

TEST_CASE("ABX: results stay hidden until the last trial", "[abx]")
{
    AbxTest t(4, sequence({true, true, false, false}));
    t.answer(AbxChoice::B); // correct
    t.answer(AbxChoice::A); // wrong
    auto r = t.result();
    CHECK(r.answered == 2);
    CHECK_FALSE(r.finished);
    CHECK_FALSE(r.correct.has_value());
    CHECK_FALSE(r.pValue.has_value());
    t.answer(AbxChoice::A); // correct
    t.answer(AbxChoice::A); // correct
    r = t.result();
    CHECK(r.finished);
    CHECK(r.correct == 3);
    REQUIRE(r.pValue.has_value());
    CHECK(std::abs(*r.pValue - 5.0 / 16) < 1e-15);
    CHECK_FALSE(r.distinguished);
    t.answer(AbxChoice::B); // ignored after the end
    CHECK(t.result().answered == 4);
}

TEST_CASE("ABX: 12 of 16 is p < 0.05 (distinguished), 11 of 16 is not", "[abx]")
{
    for (int correct : {11, 12})
    {
        AbxTest t(16, sequence({false})); // X is always A
        for (int i = 0; i < 16; ++i)
            t.answer(i < correct ? AbxChoice::A : AbxChoice::B);
        const auto r = t.result();
        CHECK(r.correct == correct);
        CHECK(r.distinguished == (correct == 12));
    }
}

TEST_CASE("ABX: stopping early gives no result", "[abx]")
{
    AbxTest t(16, sequence({true}));
    for (int i = 0; i < 10; ++i)
        t.answer(AbxChoice::B);
    t.abort();
    const auto r = t.result();
    CHECK(r.aborted);
    CHECK_FALSE(r.finished);
    CHECK_FALSE(r.correct.has_value());
    CHECK_FALSE(r.pValue.has_value());
    t.answer(AbxChoice::B);
    CHECK(t.result().answered == 10);
}

TEST_CASE("ABX: a level difference over 0.1 dB marks the result", "[abx]")
{
    AbxTest t(2, sequence({true}));
    t.noteLevelDifference(0.04);
    t.noteLevelDifference(-0.08);
    t.answer(AbxChoice::B);
    t.answer(AbxChoice::B);
    auto r = t.result();
    CHECK(r.levelMatched);
    CHECK(std::abs(r.maxLevelDifferenceDb - 0.08) < 1e-12);

    AbxTest u(1, sequence({true}));
    u.noteLevelDifference(-0.3);
    u.answer(AbxChoice::B);
    CHECK_FALSE(u.result().levelMatched);
}

TEST_CASE("ABX: the OS random source gives both sides about equally", "[abx]")
{
    int b = 0;
    for (int i = 0; i < 20000; ++i)
        b += secureRandomBit() ? 1 : 0;
    CHECK(std::abs(b - 10000) < 400); // about 5.6 standard deviations
}
