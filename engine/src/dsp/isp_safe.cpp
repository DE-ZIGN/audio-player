#include <ap/engine/dsp/isp_safe.h>

#include <algorithm>
#include <cmath>

namespace ap::engine {

void IspSafeStage::setMode(IspSafeMode mode) noexcept
{
    mode_ = mode;
    updateTarget();
}

void IspSafeStage::setUseAlbumPeak(bool album) noexcept
{
    useAlbum_ = album;
    updateTarget();
}

void IspSafeStage::setUpstreamGain(double gain) noexcept
{
    if (upstream_.exchange(gain) != gain)
        updateTarget();
}

void IspSafeStage::trackStarted(const TrackInfo& track) noexcept
{
    const auto& rg = track.replayGain;
    hasPeak_ = rg.has_value() && rg->trackPeak > 0.0;
    trackPeak_ = rg ? rg->trackPeak : 0.0;
    hasAlbumPeak_ = rg && rg->albumPeak.has_value();
    albumPeak_ = rg ? rg->albumPeak.value_or(0.0) : 0.0;
    updateTarget();
    ramp_.jump();
}

double IspSafeStage::gainDb() const noexcept
{
    const double g = ramp_.target();
    return g == 1.0 ? 0.0 : 20.0 * std::log10(g);
}

void IspSafeStage::updateTarget() noexcept
{
    double db = 0.0;
    switch (mode_.load())
    {
        case IspSafeMode::Off:
            break;
        case IspSafeMode::Fixed:
            db = kFixedDb;
            break;
        case IspSafeMode::Measured:
            if (!hasPeak_)
            {
                db = kFixedDb; // not measured: the fixed reduction
                break;
            }
            {
                const double peak = useAlbum_ && hasAlbumPeak_ ? albumPeak_.load() : trackPeak_.load();
                const double upstream = upstream_.load();
                const double peakDb = 20.0 * std::log10(std::max(peak * upstream, 1e-12));
                db = -std::max(0.0, peakDb - kTargetDbtp);
            }
            break;
    }
    ramp_.setTarget(db == 0.0 ? 1.0 : std::pow(10.0, db / 20.0));
}

} // namespace ap::engine
