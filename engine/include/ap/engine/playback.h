#pragma once

#include <ap/engine/output/audio_output.h>
#include <ap/engine/pcm_source.h>
#include <ap/engine/repack.h>

#include <functional>
#include <optional>
#include <stdexcept>

namespace ap::engine {

// What actually goes to the device for a given source.
struct PlaybackPlan
{
    PcmFormat source;
    PcmFormat output;
    Repack repack = Repack::None;
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

struct PlaybackOptions
{
    bool allowRepack = true;
    // DoP is sent only when enabled for the output: a DAC without DoP turns
    // it into loud noise, and whether a DAC decodes DoP cannot be detected.
    bool allowDop = false;
    // Called on the playback thread after the output has been opened, before data flows.
    std::function<void(const PlaybackPlan&)> onStart;
};

struct PlaybackReport
{
    PlaybackPlan plan;
    RunStats stats;
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

// Bit-perfect path: source bytes -> (optional lossless repack) -> output.
// No volume, no conversion, no dither. Blocks until playback ends.
// Throws FormatNotSupportedError or OutputException.
PlaybackReport play(PcmSource& source, AudioOutput& output, const PlaybackOptions& options = {});

} // namespace ap::engine
