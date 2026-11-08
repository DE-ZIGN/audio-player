#include <ap/signalgen/pcm.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ap::signalgen {

bool isInteger(SampleFormat format) noexcept
{
    return format == SampleFormat::Int16 || format == SampleFormat::Int24 || format == SampleFormat::Int32;
}

int bitsPerSample(SampleFormat format) noexcept
{
    switch (format)
    {
        case SampleFormat::Int16:   return 16;
        case SampleFormat::Int24:   return 24;
        case SampleFormat::Int32:   return 32;
        case SampleFormat::Float32: return 32;
        case SampleFormat::Float64: return 64;
    }
    return 0;
}

std::string_view formatName(SampleFormat format) noexcept
{
    switch (format)
    {
        case SampleFormat::Int16:   return "16bit";
        case SampleFormat::Int24:   return "24bit";
        case SampleFormat::Int32:   return "32bit";
        case SampleFormat::Float32: return "f32";
        case SampleFormat::Float64: return "f64";
    }
    return "?";
}

std::optional<SampleFormat> parseFormat(std::string_view text) noexcept
{
    if (text == "16" || text == "16bit") return SampleFormat::Int16;
    if (text == "24" || text == "24bit") return SampleFormat::Int24;
    if (text == "32" || text == "32bit") return SampleFormat::Int32;
    if (text == "f32" || text == "float32") return SampleFormat::Float32;
    if (text == "f64" || text == "float64") return SampleFormat::Float64;
    return std::nullopt;
}

std::int32_t maxCode(SampleFormat format) noexcept
{
    switch (format)
    {
        case SampleFormat::Int16: return 32767;
        case SampleFormat::Int24: return 8388607;
        case SampleFormat::Int32: return std::numeric_limits<std::int32_t>::max();
        default:                  return 0;
    }
}

std::int32_t minCode(SampleFormat format) noexcept
{
    switch (format)
    {
        case SampleFormat::Int16: return -32768;
        case SampleFormat::Int24: return -8388608;
        case SampleFormat::Int32: return std::numeric_limits<std::int32_t>::min();
        default:                  return 0;
    }
}

std::size_t PcmData::frameCount() const noexcept
{
    if (channels == 0)
        return 0;
    return (isInteger(format) ? intSamples.size() : floatSamples.size()) / channels;
}

PcmData quantize(const std::vector<double>& normalized,
                 std::uint32_t sampleRate,
                 std::uint16_t channels,
                 SampleFormat format)
{
    if (channels == 0 || normalized.size() % channels != 0)
        throw std::invalid_argument("quantize: sample count is not a multiple of the channel count");
    if (!std::ranges::all_of(normalized, [](double v) { return std::isfinite(v); }))
        throw std::invalid_argument("quantize: non-finite sample");

    PcmData out;
    out.sampleRate = sampleRate;
    out.channels = channels;
    out.format = format;

    if (isInteger(format))
    {
        const double scale = static_cast<double>(maxCode(format));
        const long long lo = minCode(format);
        const long long hi = maxCode(format);
        out.intSamples.reserve(normalized.size());
        for (double x : normalized)
        {
            const long long code = std::clamp(std::llround(x * scale), lo, hi);
            out.intSamples.push_back(static_cast<std::int32_t>(code));
        }
    }
    else
    {
        out.floatSamples = normalized;
    }
    return out;
}

} // namespace ap::signalgen
