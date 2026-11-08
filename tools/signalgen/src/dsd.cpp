#include <ap/signalgen/dsd.h>

#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace ap::signalgen {

namespace {

std::uint8_t reverseBits(std::uint8_t b)
{
    b = static_cast<std::uint8_t>((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = static_cast<std::uint8_t>((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = static_cast<std::uint8_t>((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

void putLe(std::vector<std::uint8_t>& v, std::uint64_t x, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}

void putBe(std::vector<std::uint8_t>& v, std::uint64_t x, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}

void putTag(std::vector<std::uint8_t>& v, const char* tag)
{
    v.insert(v.end(), tag, tag + 4);
}

} // namespace

std::vector<std::vector<std::uint8_t>> dsdSine(std::uint32_t dsdRate, std::uint16_t channels, std::size_t bytesPerChannel,
                                               double frequencyHz, double levelDb)
{
    if (dsdRate == 0 || channels == 0)
        throw std::invalid_argument("dsdSine: rate and channels must be non-zero");
    const double amplitude = 0.5 * std::pow(10.0, levelDb / 20.0);
    if (amplitude > 0.5)
        throw std::invalid_argument("dsdSine: level above 0 dB (50 % modulation) is not allowed");

    std::vector<std::uint8_t> bytes(bytesPerChannel);
    double v1 = 0.0, v2 = 0.0, y = 1.0;
    const double step = 2.0 * std::numbers::pi * frequencyHz / dsdRate;
    double phase = 0.0;
    for (std::size_t k = 0; k < bytesPerChannel; ++k)
    {
        std::uint8_t b = 0;
        for (int bit = 0; bit < 8; ++bit)
        {
            const double x = amplitude * std::sin(phase);
            phase += step;
            if (phase >= 2.0 * std::numbers::pi)
                phase -= 2.0 * std::numbers::pi;
            v1 += x - y;
            v2 += v1 - y;
            y = v2 >= 0.0 ? 1.0 : -1.0;
            b = static_cast<std::uint8_t>((b << 1) | (y > 0 ? 1 : 0)); // earliest bit ends up in the MSB
        }
        bytes[k] = b;
    }
    return std::vector<std::vector<std::uint8_t>>(channels, bytes);
}

std::vector<std::uint8_t> encodeDsf(const std::vector<std::vector<std::uint8_t>>& channels, std::uint32_t dsdRate)
{
    constexpr std::uint32_t blockSize = 4096;
    const std::size_t n = channels.at(0).size();
    const std::size_t groups = (n + blockSize - 1) / blockSize;
    std::vector<std::uint8_t> data;
    data.reserve(groups * blockSize * channels.size());
    for (std::size_t g = 0; g < groups; ++g)
        for (const auto& ch : channels)
            for (std::size_t i = 0; i < blockSize; ++i)
            {
                const std::size_t k = g * blockSize + i;
                data.push_back(k < n ? reverseBits(ch[k]) : 0); // LSB first; padding is zero per the format
            }

    std::vector<std::uint8_t> f;
    putTag(f, "DSD ");
    putLe(f, 28, 8);
    putLe(f, 28 + 52 + 12 + data.size(), 8);
    putLe(f, 0, 8);
    putTag(f, "fmt ");
    putLe(f, 52, 8);
    putLe(f, 1, 4);
    putLe(f, 0, 4);
    putLe(f, channels.size() == 2 ? 2 : 1, 4);
    putLe(f, channels.size(), 4);
    putLe(f, dsdRate, 4);
    putLe(f, 1, 4);
    putLe(f, static_cast<std::uint64_t>(n) * 8, 8);
    putLe(f, blockSize, 4);
    putLe(f, 0, 4);
    putTag(f, "data");
    putLe(f, 12 + data.size(), 8);
    f.insert(f.end(), data.begin(), data.end());
    return f;
}

std::vector<std::uint8_t> encodeDff(const std::vector<std::vector<std::uint8_t>>& channels, std::uint32_t dsdRate)
{
    const std::size_t n = channels.at(0).size();
    std::vector<std::uint8_t> prop;
    putTag(prop, "SND ");
    putTag(prop, "FS  ");
    putBe(prop, 4, 8);
    putBe(prop, dsdRate, 4);
    putTag(prop, "CHNL");
    putBe(prop, 2 + 4 * channels.size(), 8);
    putBe(prop, channels.size(), 2);
    const char* ids[] = {"SLFT", "SRGT", "C   ", "LFE ", "LS  ", "RS  "};
    for (std::size_t c = 0; c < channels.size(); ++c)
        putTag(prop, ids[c % 6]);
    putTag(prop, "CMPR");
    const std::string name = "not compressed";
    const std::size_t cmprSize = 4 + 1 + name.size();
    putBe(prop, cmprSize, 8);
    putTag(prop, "DSD ");
    prop.push_back(static_cast<std::uint8_t>(name.size()));
    prop.insert(prop.end(), name.begin(), name.end());
    if (cmprSize % 2)
        prop.push_back(0);

    std::vector<std::uint8_t> body;
    putTag(body, "DSD ");
    putTag(body, "FVER");
    putBe(body, 4, 8);
    putBe(body, 0x01050000, 4);
    putTag(body, "PROP");
    putBe(body, prop.size(), 8);
    body.insert(body.end(), prop.begin(), prop.end());
    putTag(body, "DSD ");
    putBe(body, n * channels.size(), 8);
    for (std::size_t i = 0; i < n; ++i)
        for (const auto& ch : channels)
            body.push_back(ch[i]);
    if ((n * channels.size()) % 2)
        body.push_back(0);

    std::vector<std::uint8_t> f;
    putTag(f, "FRM8");
    putBe(f, body.size(), 8);
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot open for writing: " + path.string());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out)
        throw std::runtime_error("write failed: " + path.string());
}

} // namespace ap::signalgen
