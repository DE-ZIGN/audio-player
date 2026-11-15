#pragma once

// A real FFT in 64-bit floating point (task 9-6), for FFT convolution: the
// space stage's IR, and later the linear-phase EQ. Power-of-two sizes only.
// Written here (radix-2, a complex FFT of half the size with the usual
// even/odd packing) so the engine needs no FFT library.
//
// The spectrum is kept split (real and imaginary parts in separate arrays),
// n/2 + 1 bins, which suits the multiply-accumulate loops of the convolver.

#include <cstddef>
#include <vector>

namespace ap::engine {

class RealFft
{
public:
    // n: a power of two, at least 4.
    explicit RealFft(std::size_t n);

    std::size_t size() const noexcept { return n_; }
    std::size_t bins() const noexcept { return n_ / 2 + 1; }

    // in: n samples. re, im: bins() values each. No allocation.
    void forward(const double* in, double* re, double* im) noexcept;
    // re, im: bins() values (im[0] and im[n/2] are ignored). out: n samples,
    // NOT scaled: forward then inverse gives n times the input.
    void inverse(const double* re, const double* im, double* out) noexcept;

private:
    void complexFft(double* re, double* im, bool inverse) const noexcept;

    std::size_t n_, m_; // m = n / 2: the size of the complex FFT
    std::vector<std::size_t> bitReverse_;
    std::vector<double> twRe_, twIm_;     // e^{-2 pi i k / m}, k < m / 2
    std::vector<double> postRe_, postIm_; // e^{-2 pi i k / n}, k <= m
    std::vector<double> zRe_, zIm_;       // scratch, m values
};

} // namespace ap::engine
