#pragma once

#include <ap/engine/dsp/processing.h>
#include <ap/engine/output/audio_output.h>
#include <ap/engine/pcm_source.h>
#include <ap/engine/repack.h>

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ap::engine {

// What actually goes to the device for a given source.
struct PlaybackPlan
{
    PcmFormat source;
    PcmFormat output;
    Repack repack = Repack::None; // direct path only
    // Processing path: the samples go through 64-bit processing and are
    // converted to `output`. False means the direct (bit-perfect) path.
    bool processing = false;
    std::vector<std::string> processingStages; // stages that were active at the start
    // The direct path cannot take the source at all: a float source (MP3,
    // AAC, float WAV) on an output without float. Such a track goes through
    // the processing path even with every stage at identity.
    bool processingRequired = false;
    bool dither = false; // TPDF dither when quantizing (processing path, integer output)
    bool comparison = false; // listening comparison (task 3-7): original or processed, crossfaded
};

struct NegotiationResult
{
    std::optional<PlaybackPlan> plan;
    FormatCheck exactCheck; // result for the unchanged source format
};

// Picks the output format: the source format unchanged if the output accepts
// it, otherwise (when allowRepack) the first accepted lossless repack. Never
// changes sample rate, channel count or sample values.
NegotiationResult negotiate(const PcmFormat& source, AudioOutput& output, bool allowRepack);

// Picks the output format for the processing path: the same sample rate and
// channels as the source (never changed), with the most resolution the output
// accepts: 32-bit integer, 24-bit in a 32-bit container, packed 24-bit,
// 16-bit, then 32-bit float (only for outputs that take no integer format).
// The plan has processing = true; exactCheck is the check of the source format.
NegotiationResult negotiateProcessing(const PcmFormat& source, AudioOutput& output);

struct PlaybackOptions
{
    bool allowRepack = true;
    // DoP is sent only when enabled for the output: a DAC without DoP turns
    // it into loud noise, and whether a DAC decodes DoP cannot be detected.
    bool allowDop = false;
    // Processing stages (task 3-1). Null or bypassed(): the direct path. DoP
    // is never processed. Whether the chain is bypassed is decided when the
    // output is opened; Player reopens the output when that changes.
    std::shared_ptr<ProcessingChain> processing;
    // Called on the playback thread after the output has been opened, before data flows.
    std::function<void(const PlaybackPlan&)> onStart;
};

// The plan play() would use now for `source` on `output`: the direct path
// (unchanged or losslessly repacked) when options.processing is null or
// bypassed and the output takes the format, otherwise the processing path.
// A float source the output cannot take goes to the processing path to be
// quantized; an integer source is refused (never converted). DoP is never
// processed. Does not open the output.
NegotiationResult planPlayback(const PcmSource& source, AudioOutput& output, const PlaybackOptions& options);

struct PlaybackReport
{
    PlaybackPlan plan;
    RunStats stats;
    std::uint64_t overs = 0; // processing path: samples the output format could not represent (clamped)
};

// Thrown when no acceptable output format exists. reason() explains why the
// unchanged source format was refused.
class FormatNotSupportedError : public std::runtime_error
{
public:
    FormatNotSupportedError(const PcmFormat& source, FormatCheck check)
        : std::runtime_error("output does not accept " + describe(source)), source_(source), check_(check)
    {
    }

    const PcmFormat& source() const noexcept { return source_; }
    const FormatCheck& check() const noexcept { return check_; }

private:
    PcmFormat source_;
    FormatCheck check_;
};

// Direct path (bit-perfect): source bytes -> (optional lossless repack) -> output.
// No volume, no conversion, no dither.
// Processing path (options.processing active, or a float source the output
// cannot take; never DoP): source -> 64-bit float -> stages -> output format
// (see negotiateProcessing), with TPDF dither when the output is integer.
// Blocks until playback ends.
// Throws FormatNotSupportedError or OutputException.
PlaybackReport play(PcmSource& source, AudioOutput& output, const PlaybackOptions& options = {});

} // namespace ap::engine
