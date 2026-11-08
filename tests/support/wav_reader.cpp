#include "wav_reader.h"

#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace ap::test {

namespace {

[[noreturn]] void fail(const std::string& what)
{
    throw std::runtime_error("WAV parse error: " + what);
}

std::uint16_t le16(std::span<const std::uint8_t> b, std::size_t at)
{
    if (at + 2 > b.size())
        fail("truncated");
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

std::uint32_t le32(std::span<const std::uint8_t> b, std::size_t at)
{
    if (at + 4 > b.size())
        fail("truncated");
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8)
         | (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

bool tagIs(std::span<const std::uint8_t> b, std::size_t at, const char* tag)
{
    return at + 4 <= b.size() && std::memcmp(b.data() + at, tag, 4) == 0;
}

// KSDATAFORMAT_SUBTYPE_xxx = {0000xxxx-0000-0010-8000-00AA00389B71}
constexpr std::array<std::uint8_t, 14> kSubtypeTail{
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

} // namespace

std::size_t WavFile::frameCount() const
{
    return blockAlign == 0 ? 0 : data.size() / blockAlign;
}

std::vector<std::int32_t> WavFile::intSamples() const
{
    if (sampleFormat != 1)
        fail("not integer PCM");
    const std::size_t bytesPer = bitsPerSample / 8u;
    std::vector<std::int32_t> out;
    out.reserve(data.size() / bytesPer);
    for (std::size_t i = 0; i + bytesPer <= data.size(); i += bytesPer)
    {
        std::uint32_t u = 0;
        for (std::size_t k = 0; k < bytesPer; ++k)
            u |= static_cast<std::uint32_t>(data[i + k]) << (8 * k);
        const unsigned shift = 32u - static_cast<unsigned>(bitsPerSample);
        // Sign-extend from bitsPerSample to 32 bits.
        out.push_back(static_cast<std::int32_t>(u << shift) >> shift);
    }
    return out;
}

std::vector<double> WavFile::floatSamples() const
{
    if (sampleFormat != 3)
        fail("not IEEE float");
    std::vector<double> out;
    if (bitsPerSample == 32)
    {
        for (std::size_t i = 0; i + 4 <= data.size(); i += 4)
        {
            std::uint32_t u = le32(data, i);
            out.push_back(static_cast<double>(std::bit_cast<float>(u)));
        }
    }
    else if (bitsPerSample == 64)
    {
        for (std::size_t i = 0; i + 8 <= data.size(); i += 8)
        {
            const std::uint64_t u = static_cast<std::uint64_t>(le32(data, i))
                                  | (static_cast<std::uint64_t>(le32(data, i + 4)) << 32);
            out.push_back(std::bit_cast<double>(u));
        }
    }
    else
    {
        fail("unsupported float size");
    }
    return out;
}

WavFile parseWav(std::span<const std::uint8_t> b)
{
    if (!tagIs(b, 0, "RIFF") || !tagIs(b, 8, "WAVE"))
        fail("not RIFF/WAVE");
    if (static_cast<std::size_t>(le32(b, 4)) + 8 != b.size())
        fail("RIFF size does not match file size");

    WavFile w;
    bool haveFmt = false;
    bool haveData = false;

    std::size_t pos = 12;
    while (pos + 8 <= b.size())
    {
        const std::size_t size = le32(b, pos + 4);
        const std::size_t body = pos + 8;
        if (body + size > b.size())
            fail("chunk overruns file");

        if (tagIs(b, pos, "fmt "))
        {
            if (size < 16)
                fail("fmt chunk too small");
            w.formatTag = le16(b, body);
            w.channels = le16(b, body + 2);
            w.sampleRate = le32(b, body + 4);
            w.byteRate = le32(b, body + 8);
            w.blockAlign = le16(b, body + 12);
            w.bitsPerSample = le16(b, body + 14);
            w.validBitsPerSample = w.bitsPerSample;
            w.sampleFormat = w.formatTag;

            if (w.formatTag == 0xFFFE)
            {
                if (size < 40 || le16(b, body + 16) != 22)
                    fail("bad WAVE_FORMAT_EXTENSIBLE");
                w.validBitsPerSample = le16(b, body + 18);
                w.channelMask = le32(b, body + 20);
                w.sampleFormat = le16(b, body + 24);
                if (std::memcmp(b.data() + body + 26, kSubtypeTail.data(), kSubtypeTail.size()) != 0)
                    fail("unknown subformat GUID");
            }
            if (w.sampleFormat != 1 && w.sampleFormat != 3)
                fail("unsupported sample format");
            if (w.channels == 0 || w.bitsPerSample % 8 != 0
                || w.blockAlign != w.channels * (w.bitsPerSample / 8)
                || w.byteRate != w.sampleRate * w.blockAlign)
                fail("inconsistent fmt fields");
            haveFmt = true;
        }
        else if (tagIs(b, pos, "fact"))
        {
            w.hasFact = true;
            w.factSampleLength = le32(b, body);
        }
        else if (tagIs(b, pos, "data"))
        {
            w.data.assign(b.begin() + static_cast<std::ptrdiff_t>(body),
                          b.begin() + static_cast<std::ptrdiff_t>(body + size));
            haveData = true;
        }

        pos = body + size + (size & 1u); // chunks are word-aligned
    }

    if (!haveFmt || !haveData)
        fail("missing fmt or data chunk");
    if (w.data.size() % w.blockAlign != 0)
        fail("data size is not a whole number of frames");
    return w;
}

WavFile readWavFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.string());
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return parseWav(bytes);
}

} // namespace ap::test
