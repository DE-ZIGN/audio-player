#pragma once

#include <ap/signalgen/pcm.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Test-signal generators.
//
// Level convention: dBFS is peak-referenced. A sine whose peak equals full
// scale is 0 dBFS (for sines this agrees with AES17).
//
// Generators returning std::vector<double> produce interleaved, normalized
// samples (1.0 = full scale) with the same value in every channel; pass the
// result to quantize() to get integer PCM.

namespace ap::signalgen {

struct Layout
{
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::size_t frames = 0;
};

double dbToLinear(double db) noexcept;

// x[n] = A sin(2 pi f n / fs + phase), A = dbToLinear(levelDbfs).
// Requires 0 < frequencyHz < fs/2.
std::vector<double> sine(const Layout& layout, double frequencyHz, double levelDbfs,
                         double phaseRadians = 0.0);

// Inter-sample-peak test signal: a sine at fs/4 with 45 degree phase.
// Every sample is +-A (A = dbToLinear(samplePeakDbfs)) in the order +, +, -, -.
// The band-limited waveform between samples peaks at sqrt(2) * A, i.e.
// +3.01 dB above the sample peak (+3.01 dBTP for a 0 dBFS sample peak).
// The samples are written as exact values rather than via std::sin so that
// the sample peak is exactly A.
std::vector<double> ispSine(const Layout& layout, double samplePeakDbfs = 0.0);

// Exponential (logarithmic) sine sweep, Farina's form:
//   x(t) = A sin( 2 pi f1 T / ln(f2/f1) * (exp(t ln(f2/f1) / T) - 1) )
// with T = frames / fs. Requires 0 < startHz < endHz < fs/2 and frames >= 2.
std::vector<double> logSweep(const Layout& layout, double startHz, double endHz,
                             double levelDbfs);

// Instantaneous phase (radians) of logSweep() at time t seconds; exposed for tests.
double logSweepPhase(double t, double durationSeconds, double startHz, double endHz) noexcept;

// One period of the low-bit pattern for an integer format:
//   0; walking ones +2^k and -2^k (k = 0 .. N-2); maxCode; minCode;
//   ramp 0..255; ramp -1..-256; 0x55.. and 0x2A.. masks and their complements.
// Together these set and clear every bit of the N-bit word at least once.
std::vector<std::int32_t> bitPatternSequence(SampleFormat format);

// The pattern repeated to fill layout.frames. Even channels carry the pattern;
// odd channels carry its bitwise complement (~x), so swapped or mixed
// channels are detectable (left + right == -1 for every frame).
// Throws std::invalid_argument for float formats.
PcmData bitPattern(const Layout& layout, SampleFormat format);

} // namespace ap::signalgen
