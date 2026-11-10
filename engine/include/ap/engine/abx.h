#pragma once

// ABX listening test (task 4-1). Decided with the user: results stay hidden
// until the last trial (seeing them invites stopping while ahead, which breaks
// the meaning of p), and a result counts as "distinguished" at p < 0.05.
//
// Each trial, X is A (the original) or B (the processed sound), drawn with the
// operating system's cryptographic random generator so it cannot be guessed.
// The listener may switch among A, B and X as often as they like (the
// comparison's 5 ms crossfade), then answers whether X is A or B. p is the
// chance of at least that many correct answers by guessing (binomial, one
// sided). The wording claims only that: "unlikely to be chance" or "not shown
// to be distinguishable", never which one sounds better.
//
// The loudness of the two must agree within 0.1 dB (CLAUDE.md chapter 5); the
// largest difference seen during the test is kept with the result.

#include <ap/engine/dsp/comparison.h>

#include <functional>
#include <optional>
#include <vector>

namespace ap::engine {

enum class AbxChoice
{
    A,
    B,
    X,
};

struct AbxResult
{
    int trials = 0;
    int answered = 0;
    bool finished = false;
    bool aborted = false;
    std::optional<int> correct;   // only when finished
    std::optional<double> pValue; // only when finished
    bool distinguished = false;   // finished and p < 0.05
    double maxLevelDifferenceDb = 0.0;
    bool levelMatched = true;     // the largest difference was within 0.1 dB
};

// P(at least `correct` of `trials` by guessing with probability 1/2).
double abxPValue(int correct, int trials);

// One random bit from the operating system (BCryptGenRandom).
bool secureRandomBit();

class AbxTest
{
public:
    static constexpr int kDefaultTrials = 16;
    static constexpr double kSignificance = 0.05;
    static constexpr double kMaxLevelDifferenceDb = 0.1;

    // randomIsB: true means X is B for a trial (drawn when the trial starts).
    explicit AbxTest(int trials = kDefaultTrials, std::function<bool()> randomIsB = secureRandomBit);

    int trials() const noexcept { return trials_; }
    int trial() const noexcept { return static_cast<int>(answers_.size()) + 1; } // the current one, from 1
    bool finished() const noexcept { return static_cast<int>(answers_.size()) == trials_; }

    // The side to play for A, B or X in the current trial.
    ComparisonSide sideFor(AbxChoice choice) const noexcept;

    // The listener's answer for the current trial: X is A, or X is B.
    void answer(AbxChoice xIs);
    void abort() noexcept { aborted_ = !finished(); }
    void noteLevelDifference(double db) noexcept;

    AbxResult result() const;

private:
    void draw();

    int trials_;
    std::function<bool()> random_;
    std::vector<bool> xIsB_;    // per trial drawn so far
    std::vector<bool> answers_; // per answered trial: correct?
    bool aborted_ = false;
    double maxLevelDifference_ = 0.0;
};

} // namespace ap::engine
