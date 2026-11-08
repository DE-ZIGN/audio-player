#include <ap/engine/repack.h>

#include <cassert>
#include <cstring>

namespace ap::engine {

std::vector<PcmFormat> repackCandidates(const PcmFormat& src)
{
    std::vector<PcmFormat> out;
    if (src.type != SampleType::Int)
        return out;

    auto with = [&](std::uint16_t container, std::uint16_t valid) {
        PcmFormat f = src;
        f.containerBits = container;
        f.validBits = valid;
        return f;
    };

    // 1. Same valid bits in a larger container.
    for (std::uint16_t container : {std::uint16_t{24}, std::uint16_t{32}})
        if (container > src.containerBits)
            out.push_back(with(container, src.validBits));

    // 2. More valid bits declared; the added low bits are zero.
    for (std::uint16_t container : {std::uint16_t{24}, std::uint16_t{32}})
        for (std::uint16_t valid : {std::uint16_t{24}, std::uint16_t{32}})
            if (container > src.containerBits && valid <= container && valid > src.validBits)
                out.push_back(with(container, valid));

    return out;
}

Repack classifyRepack(const PcmFormat& src, const PcmFormat& dst)
{
    if (src == dst)
        return Repack::None;
    return dst.validBits == src.validBits ? Repack::WiderContainer : Repack::ZeroPadded;
}

void repackSamples(std::span<const std::byte> src, std::uint32_t srcBytesPerSample,
                   std::span<std::byte> dst, std::uint32_t dstBytesPerSample, std::size_t samples)
{
    assert(dstBytesPerSample >= srcBytesPerSample);
    assert(src.size() >= samples * srcBytesPerSample);
    assert(dst.size() >= samples * dstBytesPerSample);

    const std::uint32_t pad = dstBytesPerSample - srcBytesPerSample;
    const std::byte* s = src.data();
    std::byte* d = dst.data();
    for (std::size_t i = 0; i < samples; ++i)
    {
        std::memset(d, 0, pad);
        std::memcpy(d + pad, s, srcBytesPerSample);
        s += srcBytesPerSample;
        d += dstBytesPerSample;
    }
}

} // namespace ap::engine
