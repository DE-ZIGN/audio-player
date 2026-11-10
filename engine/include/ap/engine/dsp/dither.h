#pragma once

// TPDF (triangular probability density) dither of +-1 LSB, added before
// rounding when the processing path quantizes to an integer format (task 3-2).
// It makes the mean and the variance of the quantization error independent of
// the signal. Only plain TPDF: no noise shaping (CLAUDE.md chapter 6 does not
// claim any difference between dither types).

#include <cstdint>

namespace ap::engine::dsp {

class TpdfDither
{
public:
    // A fixed seed: the same input gives the same output (reproducible tests).
    explicit TpdfDither(std::uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept;

    // A value in (-1, 1), in units of the output LSB: the difference of two
    // independent uniform values on [0, 1).
    double next() noexcept
    {
        return uniform() - uniform();
    }

private:
    double uniform() noexcept
    {
        // xoshiro256** (public domain, Blackman and Vigna): 53 random bits -> [0, 1).
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return static_cast<double>(result >> 11) * 0x1.0p-53;
    }

    static std::uint64_t rotl(std::uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }

    std::uint64_t s_[4];
};

} // namespace ap::engine::dsp
