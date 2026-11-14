#include <ap/engine/dsp/tone.h>

#include <algorithm>
#include <cmath>

namespace ap::engine {

namespace {
double clampDb(double db)
{
    return std::isfinite(db) ? std::clamp(db, -ToneStage::kMaxDb, ToneStage::kMaxDb) : 0.0;
}
} // namespace

std::vector<EqBand> ToneStage::bands() const
{
    return {EqBand{EqFilterType::LowShelf, kBassHz, bass_, kShelfQ, true},
            EqBand{EqFilterType::HighShelf, kTrebleHz, treble_, kShelfQ, true}};
}

void ToneStage::setBassDb(double db)
{
    std::lock_guard lock(mutex_);
    bass_ = clampDb(db);
    update();
}

void ToneStage::setTrebleDb(double db)
{
    std::lock_guard lock(mutex_);
    treble_ = clampDb(db);
    update();
}

void ToneStage::update()
{
    if (eq_.bands() != bands()) // the same bands again would start a needless crossfade
        eq_.setBands(bands());
}

} // namespace ap::engine
