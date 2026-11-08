#include <ap/engine/output/asio_sample_types.h>

#include <cstring>
#include <stdexcept>

namespace ap::engine::asio {

std::size_t bytesPerSample(long t) noexcept
{
    switch (t)
    {
        case Int16LSB:   return 2;
        case Int24LSB:   return 3;
        case Int32LSB:
        case Int32LSB16:
        case Int32LSB24:
        case Float32LSB: return 4;
        case Float64LSB: return 8;
        default:         return 0; // MSB types and the 18/20-bit right-aligned types are not supported
    }
}

bool accepts(long t, const PcmFormat& f) noexcept
{
    if (!f.isValid())
        return false;
    const bool isInt = f.type == SampleType::Int;
    switch (t)
    {
        case Int16LSB:   return isInt && f.containerBits == 16 && f.validBits == 16;
        case Int24LSB:   return isInt && f.containerBits == 24;
        case Int32LSB:   return isInt && f.containerBits == 32;
        case Int32LSB16: return isInt && f.containerBits == 16 && f.validBits == 16;
        case Int32LSB24: return isInt && f.containerBits == 24 && f.validBits == 24;
        case Float32LSB: return !isInt && f.containerBits == 32;
        case Float64LSB: return !isInt && f.containerBits == 64;
        default:         return false;
    }
}

void deinterleave(long t, const PcmFormat& f, std::span<const std::byte> src, std::size_t frames,
                  std::span<void* const> channels)
{
    if (!accepts(t, f) || channels.size() < f.channels)
        throw std::invalid_argument("asio::deinterleave: format not accepted for this sample type");

    const std::size_t inBps = f.bytesPerSample();
    const std::size_t outBps = bytesPerSample(t);
    const std::size_t bpf = f.bytesPerFrame();
    if (src.size() < frames * bpf)
        throw std::invalid_argument("asio::deinterleave: source too short");

    const bool signExtend = rightAligned(t);
    for (std::size_t c = 0; c < f.channels; ++c)
    {
        auto* out = static_cast<std::byte*>(channels[c]);
        const std::byte* in = src.data() + c * inBps;
        if (!signExtend)
        {
            // Same width: plain copy of each sample.
            for (std::size_t i = 0; i < frames; ++i)
                std::memcpy(out + i * outBps, in + i * bpf, outBps);
        }
        else
        {
            // Right-aligned: copy the sample into the low bytes, fill the rest with the sign.
            for (std::size_t i = 0; i < frames; ++i)
            {
                std::byte* o = out + i * outBps;
                std::memcpy(o, in + i * bpf, inBps);
                const bool negative = (std::to_integer<unsigned>(in[i * bpf + inBps - 1]) & 0x80u) != 0;
                std::memset(o + inBps, negative ? 0xFF : 0x00, outBps - inBps);
            }
        }
    }
}

bool rightAligned(long t) noexcept
{
    return t == Int32LSB16 || t == Int32LSB24;
}

std::string sampleTypeName(long t)
{
    switch (t)
    {
        case Int16LSB:   return "Int16LSB";
        case Int24LSB:   return "Int24LSB";
        case Int32LSB:   return "Int32LSB";
        case Float32LSB: return "Float32LSB";
        case Float64LSB: return "Float64LSB";
        case Int32LSB16: return "Int32LSB16";
        case Int32LSB18: return "Int32LSB18";
        case Int32LSB20: return "Int32LSB20";
        case Int32LSB24: return "Int32LSB24";
        default:         return "type " + std::to_string(t);
    }
}

} // namespace ap::engine::asio
