#include <ap/engine/abx.h>

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ap::engine {

double abxPValue(int correct, int trials)
{
    if (trials <= 0 || correct <= 0)
        return 1.0;
    if (correct > trials)
        return 0.0;
    // Sum of C(n, k) / 2^n for k = correct .. n, with C(n, k) built up exactly in double.
    double sum = 0.0;
    double c = 1.0; // C(n, 0)
    for (int k = 0; k <= trials; ++k)
    {
        if (k >= correct)
            sum += c;
        c = c * (trials - k) / (k + 1);
    }
    return sum / std::ldexp(1.0, trials);
}

bool secureRandomBit()
{
    unsigned char b = 0;
    if (BCryptGenRandom(nullptr, &b, 1, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("BCryptGenRandom failed");
    return (b & 1) != 0;
}

AbxTest::AbxTest(int trials, std::function<bool()> randomIsB) : trials_(std::max(1, trials)), random_(std::move(randomIsB))
{
    draw();
}

void AbxTest::draw()
{
    if (static_cast<int>(xIsB_.size()) < trials_)
        xIsB_.push_back(random_());
}

ComparisonSide AbxTest::sideFor(AbxChoice choice) const noexcept
{
    switch (choice)
    {
        case AbxChoice::A: return ComparisonSide::Original;
        case AbxChoice::B: return ComparisonSide::Processed;
        case AbxChoice::X:
        default:
        {
            const std::size_t i = std::min(answers_.size(), xIsB_.size() - 1);
            return xIsB_[i] ? ComparisonSide::Processed : ComparisonSide::Original;
        }
    }
}

void AbxTest::answer(AbxChoice xIs)
{
    if (finished() || aborted_ || xIs == AbxChoice::X)
        return;
    const bool saysB = xIs == AbxChoice::B;
    answers_.push_back(saysB == xIsB_[answers_.size()]);
    draw();
}

void AbxTest::noteLevelDifference(double db) noexcept
{
    maxLevelDifference_ = std::max(maxLevelDifference_, std::abs(db));
}

AbxResult AbxTest::result() const
{
    AbxResult r;
    r.trials = trials_;
    r.answered = static_cast<int>(answers_.size());
    r.aborted = aborted_;
    r.finished = finished();
    r.maxLevelDifferenceDb = maxLevelDifference_;
    r.levelMatched = maxLevelDifference_ <= kMaxLevelDifferenceDb;
    if (r.finished)
    {
        r.correct = static_cast<int>(std::count(answers_.begin(), answers_.end(), true));
        r.pValue = abxPValue(*r.correct, trials_);
        r.distinguished = *r.pValue < kSignificance;
    }
    return r;
}

} // namespace ap::engine
