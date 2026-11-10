#pragma once

// The BIT-PERFECT judgement (task 4-2, CLAUDE.md chapter 8: 実際の状態から判定
// する). Made from the recorded facts of the path, never from settings alone.
//
// Bit-perfect here means: from the application to the driver, every sample
// value reaches the output unchanged. That holds on the direct path, also
// when the container is widened (24-bit in a 32-bit slot: the values are the
// same; decided with the user) and for DoP (the DSD bits are carried as they
// are). It does not hold on the processing path; the reasons are listed with
// the current values (e.g. the digital volume at -6.0 dB).
// What happens after the driver is shown as notes, not as reasons: a volume
// the device applies itself (the endpoint volume) changes the signal inside
// the DAC.

#include <ap/engine/dsp/processing.h>
#include <ap/engine/signal_path.h>

#include <optional>
#include <span>
#include <vector>

namespace ap::engine {

struct BitPerfectVerdict
{
    bool bitPerfect = false;

    // Why not (the processing path), in order.
    std::vector<StageState> stages; // the stages that ran, with their current values
    bool quantized = false;         // a float source made integer for the output
    bool dither = false;
    bool comparison = false;        // the listening comparison (its original side keeps values only)

    // Notes that do not change the verdict.
    bool repacked = false;        // lossless container change (values unchanged)
    bool dop = false;             // DSD carried as DoP
    bool floatSource = false;     // the decoded values of a lossy (float) source, passed as they are
    bool dummyOutput = false;     // written to a file, not a device
    std::optional<double> deviceVolume; // the device's own volume (0..1) when it is not 100% or muted
    bool deviceMuted = false;
};

BitPerfectVerdict judgeBitPerfect(const SignalPath& path, std::span<const StageState> stages);

} // namespace ap::engine
