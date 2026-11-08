#pragma once

// Builds DSF and DFF files byte by byte, for testing the readers. Input is
// per-channel DSD bytes in canonical order (earliest bit in the MSB).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ap::test {

inline std::uint8_t reverseBits(std::uint8_t b)
{
    b = static_cast<std::uint8_t>((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = static_cast<std::uint8_t>((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = static_cast<std::uint8_t>((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

inline void putLe(std::vector<std::uint8_t>& v, std::uint64_t x, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}

inline void putBe(std::vector<std::uint8_t>& v, std::uint64_t x, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}

inline void putTag(std::vector<std::uint8_t>& v, const char* tag)
{
    v.insert(v.end(), tag, tag + 4);
}

// DSF: "DSD " / "fmt " / "data" chunks, little-endian, blocks of `blockSize`
// bytes per channel, last block zero-padded. bitsPerSample 1 = LSB first.
inline std::vector<std::uint8_t> makeDsf(const std::vector<std::vector<std::uint8_t>>& channels, std::uint32_t dsdRate,
                                         std::uint32_t bitsPerSample = 1, std::uint32_t blockSize = 4096)
{
    const std::size_t n = channels.at(0).size();
    const std::size_t groups = (n + blockSize - 1) / blockSize;
    std::vector<std::uint8_t> data;
    for (std::size_t g = 0; g < groups; ++g)
        for (const auto& ch : channels)
            for (std::size_t i = 0; i < blockSize; ++i)
            {
                const std::size_t k = g * blockSize + i;
                std::uint8_t b = k < n ? ch[k] : 0;
                data.push_back(bitsPerSample == 1 ? reverseBits(b) : b);
            }

    std::vector<std::uint8_t> f;
    putTag(f, "DSD ");
    putLe(f, 28, 8);
    putLe(f, 28 + 52 + 12 + data.size(), 8); // total file size
    putLe(f, 0, 8);                          // no metadata
    putTag(f, "fmt ");
    putLe(f, 52, 8);
    putLe(f, 1, 4);                          // format version
    putLe(f, 0, 4);                          // format id: DSD raw
    putLe(f, channels.size() == 2 ? 2 : 1, 4); // channel type (2 = stereo)
    putLe(f, channels.size(), 4);
    putLe(f, dsdRate, 4);
    putLe(f, bitsPerSample, 4);
    putLe(f, static_cast<std::uint64_t>(n) * 8, 8); // sample count per channel (bits)
    putLe(f, blockSize, 4);
    putLe(f, 0, 4);
    putTag(f, "data");
    putLe(f, 12 + data.size(), 8);
    f.insert(f.end(), data.begin(), data.end());
    return f;
}

// DFF: FRM8/DSD with FVER, PROP(SND: FS, CHNL, CMPR) and a DSD chunk, big-endian.
inline std::vector<std::uint8_t> makeDff(const std::vector<std::vector<std::uint8_t>>& channels, std::uint32_t dsdRate,
                                         const char* compression = "DSD ")
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
    std::size_t cmprSize = 4 + 1 + name.size();
    putBe(prop, cmprSize, 8);
    putTag(prop, compression);
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

inline std::vector<std::uint8_t> interleave(const std::vector<std::vector<std::uint8_t>>& channels)
{
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < channels.at(0).size(); ++i)
        for (const auto& ch : channels)
            out.push_back(ch[i]);
    return out;
}

} // namespace ap::test
