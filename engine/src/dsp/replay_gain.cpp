#include <ap/engine/dsp/replay_gain.h>

#include <algorithm>
#include <cmath>

namespace ap::engine {

double replayGainDb(double integratedLufs)
{
    return std::isfinite(integratedLufs) ? kReplayGainReferenceLufs - integratedLufs : 0.0;
}

double peakLimitedGainDb(double gainDb, double truePeak)
{
    if (!(truePeak > 0.0))
        return gainDb;
    return std::min(gainDb, -20.0 * std::log10(truePeak));
}

ReplayGainInfo makeReplayGain(const TrackAnalysis& track, const AlbumAnalysis* album)
{
    ReplayGainInfo info;
    info.trackGainDb = replayGainDb(track.integratedLufs);
    info.trackPeak = track.peaks.truePeakMax;
    if (album)
    {
        info.albumGainDb = replayGainDb(album->integratedLufs);
        info.albumPeak = album->truePeak;
    }
    return info;
}

std::optional<double> ReplayGainStage::displayGainDb() const noexcept
{
    const double g = ramp_.target();
    return g == 1.0 ? 0.0 : 20.0 * std::log10(g);
}

void ReplayGainStage::setMode(ReplayGainMode mode) noexcept
{
    mode_ = mode;
    updateTarget();
}

void ReplayGainStage::trackStarted(const TrackInfo& track) noexcept
{
    const auto& rg = track.replayGain;
    hasValues_ = rg.has_value();
    trackGainDb_ = rg ? rg->trackGainDb : 0.0;
    trackPeak_ = rg ? rg->trackPeak : 0.0;
    hasAlbum_ = rg && rg->albumGainDb.has_value();
    albumGainDb_ = rg ? rg->albumGainDb.value_or(0.0) : 0.0;
    albumPeak_ = rg ? rg->albumPeak.value_or(0.0) : 0.0;
    updateTarget();
    ramp_.jump();
}

void ReplayGainStage::updateTarget() noexcept
{
    double db = 0.0;
    const auto mode = mode_.load();
    if (mode != ReplayGainMode::Off && hasValues_)
    {
        if (mode == ReplayGainMode::Album && hasAlbum_)
            db = peakLimitedGainDb(albumGainDb_, albumPeak_);
        else
            db = peakLimitedGainDb(trackGainDb_, trackPeak_);
    }
    ramp_.setTarget(db == 0.0 ? 1.0 : std::pow(10.0, db / 20.0));
}

} // namespace ap::engine
