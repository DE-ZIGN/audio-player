#pragma once

// Integrated loudness by ITU-R BS.1770 / EBU R128 (task 3-4), measured with
// libebur128 (gated: absolute -70 LUFS, relative -10 LU). Mono is measured as
// dual mono (as ReplayGain 2.0 does: a mono file plays on both speakers).
// Peaks come from the engine's own True Peak meter, in the same pass.

#include <ap/engine/dsp/true_peak.h>
#include <ap/engine/pcm_source.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ap::engine {

class LoudnessMeter
{
public:
    LoudnessMeter(std::uint32_t sampleRate, std::uint16_t channels);
    ~LoudnessMeter();
    LoudnessMeter(const LoudnessMeter&) = delete;
    LoudnessMeter& operator=(const LoudnessMeter&) = delete;

    // Interleaved samples, full scale = +-1.0.
    void process(std::span<const double> samples);

    // LUFS; -infinity when nothing is above the absolute gate (silence).
    double integratedLufs() const;

    // The integrated loudness of several meters' signals taken together (an
    // album): the gating runs over all their blocks, not an average of tracks.
    static double combinedLufs(std::span<const LoudnessMeter* const> meters);

private:
    void* state_ = nullptr; // ebur128_state (an unnamed struct in its header, so not declared here)
    std::uint16_t channels_ = 0;
};

struct TrackAnalysis
{
    double integratedLufs = 0.0; // -infinity for silence
    PeakAnalysis peaks;
    std::shared_ptr<LoudnessMeter> meter; // kept for the album loudness
};

// Reads the source from its current position to the end. Throws
// std::invalid_argument for DoP (not PCM audio).
TrackAnalysis analyzeTrack(PcmSource& source);

struct AlbumAnalysis
{
    double integratedLufs = 0.0;
    double truePeak = 0.0; // the largest track true peak, linear
};

AlbumAnalysis analyzeAlbum(std::span<const TrackAnalysis> tracks);

} // namespace ap::engine
