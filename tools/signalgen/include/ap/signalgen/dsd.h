#pragma once

// DSD (1-bit) test signals and file writers.
//
// Level convention: 0 dB is 50 % modulation (the SACD reference level), so
// a -20 dB sine has an amplitude of 0.05 in the +-1 bit stream.
// The modulator is a plain second-order sigma-delta: enough to make a DAC
// show "DSD" and play a clean tone, not a reference-quality encoder.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ap::signalgen {

// Per-channel DSD bytes, earliest bit in the MSB. All channels carry the same signal.
std::vector<std::vector<std::uint8_t>> dsdSine(std::uint32_t dsdRate, std::uint16_t channels, std::size_t bytesPerChannel,
                                               double frequencyHz, double levelDb);

// Sony DSF: 1 bit per sample (LSB first), 4096-byte blocks per channel.
std::vector<std::uint8_t> encodeDsf(const std::vector<std::vector<std::uint8_t>>& channels, std::uint32_t dsdRate);

// Philips DSDIFF (.dff), uncompressed.
std::vector<std::uint8_t> encodeDff(const std::vector<std::vector<std::uint8_t>>& channels, std::uint32_t dsdRate);

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);

} // namespace ap::signalgen
