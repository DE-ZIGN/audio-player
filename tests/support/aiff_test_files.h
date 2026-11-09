#pragma once

// Builds AIFF and AIFC files byte by byte, for testing the reader
// independently of it.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ap::test {

inline void putBe32(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    for (int i = 3; i >= 0; --i)
        v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}

inline void putBe16(std::vector<std::uint8_t>& v, std::uint16_t x)
{
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

// IEEE 754 80-bit extended, big-endian, for positive integral rates.
inline void putExtended(std::vector<std::uint8_t>& v, std::uint32_t rate)
{
    int exponent = 31;
    std::uint64_t mantissa = rate;
    while (!(mantissa & 0x80000000ull))
    {
        mantissa <<= 1;
        --exponent;
    }
    mantissa <<= 32;
    putBe16(v, static_cast<std::uint16_t>(16383 + exponent));
    for (int i = 7; i >= 0; --i)
        v.push_back(static_cast<std::uint8_t>(mantissa >> (8 * i)));
}

struct AiffSpec
{
    bool aifc = false;
    std::string compression = "NONE"; // AIFC only
    std::uint16_t channels = 2;
    std::uint16_t sampleSize = 24;    // bits
    std::uint32_t sampleRate = 48000;
    std::uint32_t ssndOffset = 0;     // extra bytes between the SSND header and the samples
    bool extraChunks = true;          // an odd-sized unknown chunk before COMM, and one after SSND
};

// `samples` are the sample bytes exactly as stored in the file (already in
// the file's byte order). numSampleFrames = samples / (channels * bytes per sample).
inline std::vector<std::uint8_t> makeAiff(const AiffSpec& s, const std::vector<std::uint8_t>& samples)
{
    const std::uint32_t bytesPerSample = (s.sampleSize + 7u) / 8u;
    const std::uint32_t frames = static_cast<std::uint32_t>(samples.size() / (s.channels * bytesPerSample));

    std::vector<std::uint8_t> body;
    const char* formType = s.aifc ? "AIFC" : "AIFF";
    body.insert(body.end(), formType, formType + 4);

    auto chunk = [&](const char* id, const std::vector<std::uint8_t>& data) {
        body.insert(body.end(), id, id + 4);
        putBe32(body, static_cast<std::uint32_t>(data.size()));
        body.insert(body.end(), data.begin(), data.end());
        if (data.size() & 1u)
            body.push_back(0);
    };

    if (s.extraChunks)
        chunk("NAME", {'t', 'e', 's', 't', 'x'}); // 5 bytes: padded
    if (s.aifc)
        chunk("FVER", {0xA2, 0x80, 0x51, 0x40});

    std::vector<std::uint8_t> comm;
    putBe16(comm, s.channels);
    putBe32(comm, frames);
    putBe16(comm, s.sampleSize);
    putExtended(comm, s.sampleRate);
    if (s.aifc)
    {
        comm.insert(comm.end(), s.compression.begin(), s.compression.begin() + 4);
        comm.push_back(0); // empty Pascal string, padded to even length
        comm.push_back(0);
    }
    chunk("COMM", comm);

    std::vector<std::uint8_t> ssnd;
    putBe32(ssnd, s.ssndOffset);
    putBe32(ssnd, 0); // block size
    ssnd.resize(ssnd.size() + s.ssndOffset, 0xEE);
    ssnd.insert(ssnd.end(), samples.begin(), samples.end());
    chunk("SSND", ssnd);

    if (s.extraChunks)
        chunk("ANNO", {'x'});

    std::vector<std::uint8_t> file{'F', 'O', 'R', 'M'};
    putBe32(file, static_cast<std::uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

} // namespace ap::test
