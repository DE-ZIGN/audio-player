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
#include <vector>

namespace ap::engine {

struct SignalPath
{
    // Input: the current track as read from its file.
    std::string container;      // "WAV", "DSF", "DFF"; empty when not read from a file
    PcmFormat source;           // DoP: the 24-bit PCM that carries the DSD
    std::optional<DopInfo> dop; // set when DSD is sent as DoP

    // Processing. False: the direct path (no volume, conversion, resampling
    // or dither). True: the samples went through 64-bit processing and were
    // converted to output.format (task 3-1); processingStages lists the
    // stages that were active when the output was opened.
    bool processing = false;
    std::vector<std::string> processingStages;
    // The source could not be sent as it is (a float source on an output
    // without float), so it went through the processing path to be quantized.
    bool processingRequired = false;
    bool dither = false; // TPDF dither when quantizing to the output format
    // Listening comparison: the output is the original or the processed sound
    // (the original side keeps its values but is not bit-perfect here).
    bool comparison = false;

    // Delivery: a lossless container repack (values unchanged), then the output.
    Repack repack = Repack::None;
    OutputPathInfo output; // as reported by the output after open()
};

} // namespace ap::engine
