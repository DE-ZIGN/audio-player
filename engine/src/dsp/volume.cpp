#include <ap/engine/dsp/volume.h>

#include <algorithm>
#include <cmath>

namespace ap::engine {

void VolumeStage::setGainDb(double db) noexcept
{
    if (std::isnan(db))
        return;
    db = std::min(db, 0.0);
    targetDb_ = db;
    // Exactly 1.0 at 0 dB, so the stage is recognised as identity.
    ramp_.setTarget(db == 0.0 ? 1.0 : std::pow(10.0, db / 20.0));
}

} // namespace ap::engine
