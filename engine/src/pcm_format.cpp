#include <ap/engine/pcm_format.h>

#include <format>

namespace ap::engine {

bool PcmFormat::isValid() const noexcept
{
    if (sampleRate == 0 || channels == 0 || channels > 32)
        return false;
    if (type == SampleType::Int)
    {
        const bool container = containerBits == 16 || containerBits == 24 || containerBits == 32;
        return container && validBits >= 8 && validBits <= containerBits;
    }
    return (containerBits == 32 || containerBits == 64) && validBits == containerBits;
}

std::string describe(const PcmFormat& f)
{
    const std::string rate = std::format("{:g} kHz", f.sampleRate / 1000.0);
    std::string bits;
    if (f.type == SampleType::Float)
        bits = std::format("{}bit 浮動小数点", f.containerBits);
    else if (f.validBits == f.containerBits)
        bits = std::format("{}bit", f.validBits);
    else
        bits = std::format("{}bit（{}bit コンテナ）", f.validBits, f.containerBits);
    return std::format("{} / {} / {}ch", rate, bits, f.channels);
}

} // namespace ap::engine
