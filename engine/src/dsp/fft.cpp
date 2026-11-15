#include <ap/engine/dsp/fft.h>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace ap::engine {

RealFft::RealFft(std::size_t n) : n_(n), m_(n / 2)
{
    if (n < 4 || (n & (n - 1)) != 0)
        throw std::invalid_argument("RealFft: the size must be a power of two, at least 4");
    unsigned bits = 0;
    while ((std::size_t{1} << bits) < m_)
        ++bits;
    bitReverse_.resize(m_);
    for (std::size_t i = 0; i < m_; ++i)
    {
        std::size_t r = 0;
        for (unsigned b = 0; b < bits; ++b)
            if (i & (std::size_t{1} << b))
                r |= std::size_t{1} << (bits - 1 - b);
        bitReverse_[i] = r;
    }
    twRe_.resize(m_ / 2 + 1);
    twIm_.resize(m_ / 2 + 1);
    for (std::size_t k = 0; k < twRe_.size(); ++k)
    {
        const double a = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(m_);
        twRe_[k] = std::cos(a);
        twIm_[k] = std::sin(a);
    }
    postRe_.resize(m_ + 1);
    postIm_.resize(m_ + 1);
    for (std::size_t k = 0; k <= m_; ++k)
    {
        const double a = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n_);
        postRe_[k] = std::cos(a);
        postIm_[k] = std::sin(a);
    }
    zRe_.resize(m_);
    zIm_.resize(m_);
}

// Iterative radix-2 decimation in time; `inverse` uses the conjugate twiddles
// (no scaling).
void RealFft::complexFft(double* re, double* im, bool inverse) const noexcept
{
    for (std::size_t i = 0; i < m_; ++i)
    {
        const std::size_t j = bitReverse_[i];
        if (j > i)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    const double sign = inverse ? -1.0 : 1.0;
    for (std::size_t len = 2; len <= m_; len <<= 1)
    {
        const std::size_t half = len / 2;
        const std::size_t step = m_ / len;
        for (std::size_t start = 0; start < m_; start += len)
        {
            for (std::size_t k = 0; k < half; ++k)
            {
                const double wr = twRe_[k * step];
                const double wi = sign * twIm_[k * step];
                const std::size_t a = start + k, b = a + half;
                const double tr = re[b] * wr - im[b] * wi;
                const double ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

void RealFft::forward(const double* in, double* re, double* im) noexcept
{
    // z[j] = x[2j] + i x[2j+1]
    for (std::size_t j = 0; j < m_; ++j)
    {
        zRe_[j] = in[2 * j];
        zIm_[j] = in[2 * j + 1];
    }
    complexFft(zRe_.data(), zIm_.data(), false);
    // E[k] = (Z[k] + conj Z[m-k]) / 2, O[k] = (Z[k] - conj Z[m-k]) / 2i,
    // X[k] = E[k] + W^k O[k].
    for (std::size_t k = 0; k <= m_; ++k)
    {
        const std::size_t a = k % m_, b = (m_ - k) % m_;
        const double zr = zRe_[a], zi = zIm_[a];
        const double cr = zRe_[b], ci = -zIm_[b]; // conj Z[m-k]
        const double er = 0.5 * (zr + cr), ei = 0.5 * (zi + ci);
        // (Z - conj) / 2i = (dr + i di) / 2i = (di - i dr) / 2
        const double dr = zr - cr, di = zi - ci;
        const double orr = 0.5 * di, oi = -0.5 * dr;
        const double wr = postRe_[k], wi = postIm_[k];
        re[k] = er + (orr * wr - oi * wi);
        im[k] = ei + (orr * wi + oi * wr);
    }
    im[0] = 0.0;
    im[m_] = 0.0;
}

void RealFft::inverse(const double* re, const double* im, double* out) noexcept
{
    // E[k] = (X[k] + conj X[m-k]) / 2, O[k] = (X[k] - conj X[m-k]) W^-k / 2,
    // Z[k] = E[k] + i O[k]; z = IFFT(Z) gives x[2j] + i x[2j+1] (times m).
    for (std::size_t k = 0; k < m_; ++k)
    {
        const double xr = re[k], xi = k == 0 ? 0.0 : im[k];
        const double cr = re[m_ - k], ci = (m_ - k == m_) ? 0.0 : -im[m_ - k];
        const double er = 0.5 * (xr + cr), ei = 0.5 * (xi + ci);
        const double dr = 0.5 * (xr - cr), di = 0.5 * (xi - ci);
        const double wr = postRe_[k], wi = -postIm_[k]; // W^-k
        const double orr = dr * wr - di * wi, oi = dr * wi + di * wr;
        // Z = E + i O = (er - oi) + i (ei + orr)
        zRe_[k] = er - oi;
        zIm_[k] = ei + orr;
    }
    complexFft(zRe_.data(), zIm_.data(), true);
    // The packing halves: forward-then-inverse is m times for z, i.e. the
    // samples come out m times; scale by 2 so the total is n times.
    for (std::size_t j = 0; j < m_; ++j)
    {
        out[2 * j] = 2.0 * zRe_[j];
        out[2 * j + 1] = 2.0 * zIm_[j];
    }
}

} // namespace ap::engine
