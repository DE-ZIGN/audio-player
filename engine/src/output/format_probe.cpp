#include <ap/engine/output/format_probe.h>

#include <algorithm>

namespace ap::engine {

namespace {

PcmFormat withLayout(const PcmFormat& base, std::uint16_t container, std::uint16_t valid, SampleType type)
{
    PcmFormat f = base;
    f.containerBits = container;
    f.validBits = valid;
    f.type = type;
    f.channelMask = 0; // let the output pick its default mask for the count
    return f;
}

bool anyIntLayout(const AcceptsFormat& accepts, std::uint32_t rate, std::uint16_t channels)
{
    const auto layouts = standardIntLayouts(rate, channels);
    return std::any_of(layouts.begin(), layouts.end(), accepts);
}

bool float32(const AcceptsFormat& accepts, std::uint32_t rate, std::uint16_t channels)
{
    return accepts(PcmFormat{rate, channels, 32, 32, SampleType::Float, 0});
}

} // namespace

const std::vector<std::uint32_t>& standardSampleRates()
{
    static const std::vector<std::uint32_t> rates{44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};
    return rates;
}

std::vector<PcmFormat> standardIntLayouts(std::uint32_t sampleRate, std::uint16_t channels)
{
    const PcmFormat base{sampleRate, channels, 16, 16, SampleType::Int, 0};
    return {withLayout(base, 16, 16, SampleType::Int), withLayout(base, 24, 24, SampleType::Int),
            withLayout(base, 32, 24, SampleType::Int), withLayout(base, 32, 32, SampleType::Int)};
}

std::vector<std::uint32_t> acceptedSampleRates(const AcceptsFormat& accepts, std::uint16_t channels)
{
    std::vector<std::uint32_t> out;
    for (const auto rate : standardSampleRates())
        if (anyIntLayout(accepts, rate, channels) || float32(accepts, rate, channels))
            out.push_back(rate);
    return out;
}

FormatRejection classifyRejection(const PcmFormat& requested, const AcceptsFormat& accepts)
{
    const auto rate = requested.sampleRate;
    const auto channels = requested.channels;

    const bool intWorks = anyIntLayout(accepts, rate, channels);
    const bool floatWorks = float32(accepts, rate, channels);

    if (requested.type == SampleType::Float)
    {
        if (intWorks)
            return FormatRejection::SampleType;
        if (floatWorks)
            return FormatRejection::BitDepth; // e.g. 64-bit float where 32-bit works
    }
    else
    {
        if (intWorks)
            return FormatRejection::BitDepth;
        if (floatWorks)
            return FormatRejection::SampleType;
    }

    if (channels != 2 && (anyIntLayout(accepts, rate, 2) || float32(accepts, rate, 2)))
        return FormatRejection::Channels;
    return FormatRejection::SampleRate;
}

FormatCheck checkFormatWith(const PcmFormat& requested, const AcceptsFormat& accepts)
{
    if (!requested.isValid())
        return FormatCheck::rejected(FormatRejection::Other);
    if (accepts(requested))
        return FormatCheck::ok();

    FormatCheck c = FormatCheck::rejected(classifyRejection(requested, accepts));
    c.acceptedRates = acceptedSampleRates(accepts, requested.channels);
    return c;
}

} // namespace ap::engine
