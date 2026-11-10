#pragma once

// Facts about a track that processing stages may need when it starts playing
// (task 3-4: its ReplayGain values). Separate from the samples: a source that
// joins tracks (the player's queue) reports where each one starts.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace ap::engine {

// ReplayGain 2.0 values (reference -18 LUFS). Peaks are true peaks (linear),
// measured by the engine; tag peak values are not trusted.
struct ReplayGainInfo
{
    double trackGainDb = 0.0;
    double trackPeak = 0.0;
    std::optional<double> albumGainDb;
    std::optional<double> albumPeak;
};

struct TrackInfo
{
    std::size_t queueIndex = 0;
    std::optional<ReplayGainInfo> replayGain; // not analysed: no gain is applied
};

// A track that starts `frameOffset` frames into the data of the last read.
struct TrackSegment
{
    std::size_t frameOffset = 0;
    TrackInfo track;
};

} // namespace ap::engine
