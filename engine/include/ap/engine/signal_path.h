#pragma once

// The playback path of the current track, recorded from facts: what each
// stage did, as reported by the source, the format negotiation and the
// output. The path display (M4) shows it and judges "bit-perfect" from it;
// this record itself makes no judgement.

#include <ap/engine/output/audio_output.h>
#include <ap/engine/pcm_source.h>
#include <ap/engine/repack.h>

#include <optional>
#include <string>

namespace ap::engine {

struct SignalPath
{
    // Input: the current track as read from its file.
    std::string container;      // "WAV", "DSF", "DFF"; empty when not read from a file
    PcmFormat source;           // DoP: the 24-bit PCM that carries the DSD
    std::optional<DopInfo> dop; // set when DSD is sent as DoP

    // Processing. Phase 1 has only the direct path: no volume, conversion,
    // resampling or dither. The processing stages are added with M3.
    bool processing = false;

    // Delivery: a lossless container repack (values unchanged), then the output.
    Repack repack = Repack::None;
    OutputPathInfo output; // as reported by the output after open()
};

} // namespace ap::engine
