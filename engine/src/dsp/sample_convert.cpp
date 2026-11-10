#include <ap/engine/dsp/sample_convert.h>

#include <cassert>
#include <cmath>
#include <cstring>

namespace ap::engine::dsp {

namespace {

std::int64_t readInt(const std::byte* p, unsigned bytes) noexcept
{
    std::uint64_t u = 0;
    for (unsigned k = 0; k < bytes; ++k)
        u |= static_cast<std::uint64_t>(p[k]) << (8 * k);
    const unsigned shift = 64u - bytes * 8u;
    return static_cast<std::int64_t>(u << shift) >> shift; // sign-extend
}

void writeInt(std::byte* p, unsigned bytes, std::int64_t v) noexcept
{
    const auto u = static_cast<std::uint64_t>(v);
    for (unsigned k = 0; k < bytes; ++k)
        p[k] = static_cast<std::byte>(u >> (8 * k));
}

} // namespace

void toDouble(std::span<const std::byte> src, const PcmFormat& format, std::span<double> dst)
{
    const unsigned bytes = format.bytesPerSample();
    assert(src.size() >= dst.size() * bytes);
    const std::byte* s = src.data();

    if (format.type == SampleType::Float)
    {
        if (format.containerBits == 32)
        {
            for (auto& d : dst)
            {
                float f;
                std::memcpy(&f, s, 4);
                d = f;
                s += 4;
            }
        }
        else
        {
            std::memcpy(dst.data(), s, dst.size() * 8);
        }
        return;
    }

    const double scale = std::ldexp(1.0, -(static_cast<int>(format.containerBits) - 1));
    for (auto& d : dst)
    {
        d = static_cast<double>(readInt(s, bytes)) * scale;
        s += bytes;
    }
}

std::uint64_t fromDouble(std::span<const double> src, const PcmFormat& format, std::span<std::byte> dst)
{
    const unsigned bytes = format.bytesPerSample();
    assert(dst.size() >= src.size() * bytes);
    std::byte* d = dst.data();
    std::uint64_t overs = 0;

    if (format.type == SampleType::Float)
    {
        for (double x : src)
        {
            if (std::isnan(x))
            {
                x = 0.0;
                ++overs;
            }
            else if (x > 1.0 || x < -1.0)
            {
                ++overs;
            }
            if (format.containerBits == 32)
            {
                const float f = static_cast<float>(x);
                std::memcpy(d, &f, 4);
                d += 4;
            }
            else
            {
                std::memcpy(d, &x, 8);
                d += 8;
            }
        }
        return overs;
    }

    const int valid = format.validBits;
    const double scale = std::ldexp(1.0, valid - 1);
    const double maxCode = scale - 1.0;
    const double minCode = -scale;
    const unsigned shift = format.containerBits - format.validBits;
    for (double x : src)
    {
        double q = x * scale;
        if (std::isnan(q))
        {
            q = 0.0;
            ++overs;
        }
        else if (q > maxCode)
        {
            // Values that round to the largest code anyway are not counted.
            if (q > maxCode + 0.5)
                ++overs;
            q = maxCode;
        }
        else if (q < minCode)
        {
            if (q < minCode - 0.5)
                ++overs;
            q = minCode;
        }
        // Default rounding mode: to nearest, ties to even.
        const auto code = static_cast<std::int64_t>(std::nearbyint(q));
        writeInt(d, bytes, static_cast<std::int64_t>(static_cast<std::uint64_t>(code) << shift));
        d += bytes;
    }
    return overs;
}

} // namespace ap::engine::dsp
