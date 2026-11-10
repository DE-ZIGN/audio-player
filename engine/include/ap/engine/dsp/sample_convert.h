#pragma once

// Conversion between the PCM formats a source delivers or an output takes and
// the processing format: interleaved 64-bit floating point, full scale = +-1.0.
//
// Integer samples are read at their container width and divided by
// 2^(containerBits - 1); every integer and float format is exact in double,
// so PCM -> double -> the same PCM format gives back the same bytes.

#include <ap/engine/pcm_format.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace ap::engine::dsp {

class TpdfDither;

// Converts dst.size() samples of `format` from src (little-endian, interleaved).
void toDouble(std::span<const std::byte> src, const PcmFormat& format, std::span<double> dst);

// Writes src.size() samples in `format` to dst and returns how many samples
// could not be represented: integer values beyond the code range after
// rounding (+1.0 is one step above the largest code), float values beyond
// +-1.0, and NaN.
//  - Integer: adds `dither` (TPDF, +-1 LSB of validBits) when given, rounds to
//    the nearest code of validBits (ties to even), clamps to the code range,
//    leaves the low (containerBits - validBits) bits zero. Overs are counted
//    from the value before dither.
//  - Float: converts (32-bit: to the nearest float) without clamping; a DAC
//    that takes float decides how to handle values beyond full scale. Never
//    dithered.
//  - NaN becomes 0.
std::uint64_t fromDouble(std::span<const double> src, const PcmFormat& format, std::span<std::byte> dst,
                         TpdfDither* dither = nullptr);

} // namespace ap::engine::dsp
