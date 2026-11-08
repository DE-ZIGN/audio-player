#include <ap/signalgen/wav_writer.h>

#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace ap::signalgen {

namespace {

constexpr std::uint16_t kFormatPcm = 0x0001;
constexpr std::uint16_t kFormatIeeeFloat = 0x0003;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

void put8(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    b.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void put16(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    put8(b, v);
    put8(b, v >> 8);
}

void put32(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    put16(b, v);
    put16(b, v >> 16);
}

void putTag(std::vector<std::uint8_t>& b, const char (&tag)[5])
{
    b.insert(b.end(), tag, tag + 4);
}

// KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT = {0000xxxx-0000-0010-8000-00AA00389B71}
void putSubformatGuid(std::vector<std::uint8_t>& b, std::uint16_t formatCode)
{
    put16(b, formatCode);
    put16(b, 0x0000);
    put16(b, 0x0000);
    put16(b, 0x0010);
    constexpr std::uint8_t tail[] = {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    b.insert(b.end(), std::begin(tail), std::end(tail));
}

std::uint32_t channelMaskFor(std::uint16_t channels)
{
    switch (channels)
    {
        case 1:  return 0x4; // SPEAKER_FRONT_CENTER
        case 2:  return 0x3; // SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT
        default: return 0x0; // unspecified
    }
}

} // namespace

std::vector<std::uint8_t> encodeWav(const PcmData& pcm)
{
    if (pcm.channels == 0 || pcm.sampleRate == 0)
        throw std::invalid_argument("encodeWav: sample rate and channel count must be non-zero");

    const bool integer = isInteger(pcm.format);
    const std::size_t sampleCount = integer ? pcm.intSamples.size() : pcm.floatSamples.size();
    if (sampleCount % pcm.channels != 0)
        throw std::invalid_argument("encodeWav: sample count is not a multiple of the channel count");

    const std::uint32_t bits = static_cast<std::uint32_t>(bitsPerSample(pcm.format));
    const std::uint32_t bytesPerSample = bits / 8;
    const std::uint32_t blockAlign = pcm.channels * bytesPerSample;
    const std::uint64_t frames = sampleCount / pcm.channels;
    const std::uint64_t dataSize = frames * blockAlign;

    const bool extensible = !(pcm.format == SampleFormat::Int16 && pcm.channels <= 2);
    const std::uint32_t fmtSize = extensible ? 40 : 16;
    const bool withFact = !integer;

    const std::uint64_t fileSize = 12 + (8 + fmtSize) + (withFact ? 12 : 0) + 8 + dataSize + (dataSize & 1u);
    if (fileSize > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("encodeWav: file would exceed the 4 GiB RIFF limit");

    std::vector<std::uint8_t> b;
    b.reserve(static_cast<std::size_t>(fileSize));

    putTag(b, "RIFF");
    put32(b, static_cast<std::uint32_t>(fileSize - 8));
    putTag(b, "WAVE");

    putTag(b, "fmt ");
    put32(b, fmtSize);
    put16(b, extensible ? kFormatExtensible : kFormatPcm);
    put16(b, pcm.channels);
    put32(b, pcm.sampleRate);
    put32(b, pcm.sampleRate * blockAlign);
    put16(b, blockAlign);
    put16(b, bits);
    if (extensible)
    {
        put16(b, 22);                    // cbSize
        put16(b, bits);                  // wValidBitsPerSample
        put32(b, channelMaskFor(pcm.channels));
        putSubformatGuid(b, integer ? kFormatPcm : kFormatIeeeFloat);
    }

    if (withFact)
    {
        putTag(b, "fact");
        put32(b, 4);
        put32(b, static_cast<std::uint32_t>(frames));
    }

    putTag(b, "data");
    put32(b, static_cast<std::uint32_t>(dataSize));

    switch (pcm.format)
    {
        case SampleFormat::Int16:
            for (auto v : pcm.intSamples)
                put16(b, static_cast<std::uint32_t>(v));
            break;
        case SampleFormat::Int24:
            for (auto v : pcm.intSamples)
            {
                const auto u = static_cast<std::uint32_t>(v);
                put16(b, u);
                put8(b, u >> 16);
            }
            break;
        case SampleFormat::Int32:
            for (auto v : pcm.intSamples)
                put32(b, static_cast<std::uint32_t>(v));
            break;
        case SampleFormat::Float32:
            for (double v : pcm.floatSamples)
                put32(b, std::bit_cast<std::uint32_t>(static_cast<float>(v)));
            break;
        case SampleFormat::Float64:
            for (double v : pcm.floatSamples)
            {
                const auto u = std::bit_cast<std::uint64_t>(v);
                put32(b, static_cast<std::uint32_t>(u));
                put32(b, static_cast<std::uint32_t>(u >> 32));
            }
            break;
    }

    if (dataSize & 1u)
        put8(b, 0);

    return b;
}

void writeWav(const std::filesystem::path& path, const PcmData& pcm)
{
    const auto bytes = encodeWav(pcm);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot open for writing: " + path.string());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out)
        throw std::runtime_error("write failed: " + path.string());
}

} // namespace ap::signalgen
