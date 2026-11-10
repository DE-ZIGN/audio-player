#pragma once

// ReplayGain 2.0 (task 3-4): the gain that brings a track (or an album) to
// -18 LUFS, from the integrated loudness measured by libebur128.
//
// Gain limit: a gain never lifts the true peak above 0 dBTP (the gain is at
// most -truePeak in dB). The further 0.5 dB of headroom is the ISP-safe
// gain's job (task 3-5). There is no fixed maximum boost: lifting quiet
// tracks is what ReplayGain is for.

#include <ap/engine/dsp/gain_ramp.h>
#include <ap/engine/dsp/loudness.h>
#include <ap/engine/dsp/processing.h>
#include <ap/engine/track_info.h>

#include <atomic>
#include <cstdint>

namespace ap::engine {

constexpr double kReplayGainReferenceLufs = -18.0;

// -18 LUFS minus the loudness; 0 dB for silence (nothing to measure).
double replayGainDb(double integratedLufs);

// The gain, lowered if needed so that truePeak (linear) times the gain stays
// at or below 0 dBTP.
double peakLimitedGainDb(double gainDb, double truePeak);

// Track values, and album values when `album` is given.
ReplayGainInfo makeReplayGain(const TrackAnalysis& track, const AlbumAnalysis* album = nullptr);

enum class ReplayGainMode
{
    Off,
    Track,
    Album, // falls back to the track values when a track has no album values
};

// Applies the current track's ReplayGain on the processing path (first stage).
// A new track takes its gain from its first sample, without a ramp (tracks
// joined gaplessly switch exactly at the boundary); a mode change while
// playing ramps over 10 ms like the volume. A track without values, mode Off,
// or a gain of exactly 0 dB is identity (no processing).
class ReplayGainStage final : public ProcessingStage
{
public:
    static constexpr double kRampSeconds = dsp::GainRamp::kRampSeconds;

    void setMode(ReplayGainMode mode) noexcept; // thread-safe
    ReplayGainMode mode() const noexcept { return mode_; }

    // The linear gain the current track would get now (after the limit).
    double targetGain() const noexcept { return ramp_.target(); }

    std::string name() const override { return "replaygain"; }
    void prepare(std::uint32_t sampleRate, std::uint16_t) override { ramp_.prepare(sampleRate); }
    bool isIdentity() const noexcept override { return ramp_.isUnity(); }
    double peakGain() const noexcept override { return ramp_.target(); }
    std::optional<double> displayGainDb() const noexcept override;
    void trackStarted(const TrackInfo& track) noexcept override;
    void process(std::span<double> samples, std::uint16_t channels) noexcept override
    {
        ramp_.apply(samples, channels);
    }

private:
    void updateTarget() noexcept;

    std::atomic<ReplayGainMode> mode_{ReplayGainMode::Off};
    // The current track's values (written when a track starts).
    std::atomic<bool> hasValues_{false};
    std::atomic<double> trackGainDb_{0.0};
    std::atomic<double> trackPeak_{0.0};
    std::atomic<bool> hasAlbum_{false};
    std::atomic<double> albumGainDb_{0.0};
    std::atomic<double> albumPeak_{0.0};

    dsp::GainRamp ramp_;
};

} // namespace ap::engine
