#pragma once

// Device-independent format checking. An output supplies a predicate that
// answers "does the device accept exactly this format?"; the functions here
// turn that into a FormatCheck with a reason and the rates the device
// currently accepts. Nothing here converts or alters audio.

#include <ap/engine/output/audio_output.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace ap::engine {

using AcceptsFormat = std::function<bool(const PcmFormat&)>;

// 44.1 to 384 kHz in both families.
const std::vector<std::uint32_t>& standardSampleRates();

// Integer layouts probed for each rate: 16, 24, 24-in-32, 32 bit.
std::vector<PcmFormat> standardIntLayouts(std::uint32_t sampleRate, std::uint16_t channels);

// Rates (from standardSampleRates) at which at least one integer layout is
// accepted with this channel count.
std::vector<std::uint32_t> acceptedSampleRates(const AcceptsFormat& accepts, std::uint16_t channels);

// Why `requested` is refused, judged by probing nearby formats:
//   another integer layout works at the same rate/channels -> BitDepth
//     (SampleType when the request was float)
//   the rate works with 2 channels but not with the requested count -> Channels
//   otherwise -> SampleRate
FormatRejection classifyRejection(const PcmFormat& requested, const AcceptsFormat& accepts);

// Full check: ok() if accepted; otherwise the reason and acceptedRates for
// the requested channel count.
FormatCheck checkFormatWith(const PcmFormat& requested, const AcceptsFormat& accepts);

} // namespace ap::engine
