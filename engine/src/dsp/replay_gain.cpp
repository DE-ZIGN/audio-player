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
    jump_ = true;
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
    target_ = db == 0.0 ? 1.0 : std::pow(10.0, db / 20.0);
}

void ReplayGainStage::prepare(std::uint32_t sampleRate, std::uint16_t)
{
    rampFrames_ = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(sampleRate * kRampSeconds)));
    rampTarget_ = target_;
    current_ = rampTarget_;
    rampLeft_ = 0;
    jump_ = false;
}

bool ReplayGainStage::isIdentity() const noexcept
{
    return target_.load(std::memory_order_relaxed) == 1.0 && current_.load(std::memory_order_relaxed) == 1.0;
}

void ReplayGainStage::process(std::span<double> samples, std::uint16_t channels) noexcept
{
    const double target = target_.load(std::memory_order_relaxed);
    double g = current_.load(std::memory_order_relaxed);
    if (jump_.exchange(false))
    {
        g = target;
        rampTarget_ = target;
        rampLeft_ = 0;
    }
    else if (target != rampTarget_)
    {
        rampTarget_ = target;
        rampLeft_ = rampFrames_;
        step_ = (target - g) / rampFrames_;
    }

    const std::size_t frames = samples.size() / channels;
    double* s = samples.data();
    for (std::size_t f = 0; f < frames; ++f)
    {
        if (rampLeft_ > 0)
            g = --rampLeft_ == 0 ? rampTarget_ : g + step_;
        for (std::uint16_t c = 0; c < channels; ++c)
            *s++ *= g;
    }
    current_.store(g, std::memory_order_relaxed);
}

} // namespace ap::engine
