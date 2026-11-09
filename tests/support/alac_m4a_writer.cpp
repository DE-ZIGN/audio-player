#include "alac_m4a_writer.h"

#include <ALACAudioTypes.h>
#include <ALACEncoder.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace ap::test {

namespace {

using Bytes = std::vector<std::uint8_t>;

void be16(Bytes& b, std::uint32_t v)
{
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}

void be32(Bytes& b, std::uint32_t v)
{
    be16(b, v >> 16);
    be16(b, v & 0xFFFF);
}

void tag(Bytes& b, const char* t)
{
    b.insert(b.end(), t, t + 4);
}

Bytes box(const char* type, const Bytes& body)
{
    Bytes b;
    be32(b, static_cast<std::uint32_t>(body.size() + 8));
    tag(b, type);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

Bytes fullBox(const char* type, std::uint32_t versionFlags, const Bytes& body)
{
    Bytes v;
    be32(v, versionFlags);
    v.insert(v.end(), body.begin(), body.end());
    return box(type, v);
}

Bytes concat(std::initializer_list<Bytes> parts)
{
    Bytes out;
    for (const auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

void matrix(Bytes& b)
{
    for (std::uint32_t v : {0x00010000u, 0u, 0u, 0u, 0x00010000u, 0u, 0u, 0u, 0x40000000u})
        be32(b, v);
}

} // namespace

void writeAppleAlacM4a(const std::filesystem::path& path, std::uint32_t sampleRate, std::uint16_t channels,
                       std::uint16_t bitDepth, const std::vector<std::byte>& pcm, const AppleAlacOptions& options)
{
    const std::uint32_t bytesPerSample = bitDepth == 16 ? 2 : bitDepth == 32 ? 4 : 3;
    const std::uint32_t bytesPerFrame = bytesPerSample * channels;
    if (pcm.size() % bytesPerFrame != 0)
        throw std::invalid_argument("writeAppleAlacM4a: PCM size is not a whole number of frames");
    const std::uint64_t frames = pcm.size() / bytesPerFrame;

    AudioFormatDescription input{};
    input.mSampleRate = sampleRate;
    input.mFormatID = kALACFormatLinearPCM;
    input.mFormatFlags = kALACFormatFlagIsSignedInteger | kALACFormatFlagIsPacked;
    input.mBytesPerPacket = bytesPerFrame;
    input.mFramesPerPacket = 1;
    input.mBytesPerFrame = bytesPerFrame;
    input.mChannelsPerFrame = channels;
    input.mBitsPerChannel = bitDepth;

    AudioFormatDescription output{};
    output.mSampleRate = sampleRate;
    output.mFormatID = kALACFormatAppleLossless;
    output.mFormatFlags = bitDepth == 16 ? 1 : bitDepth == 20 ? 2 : bitDepth == 24 ? 3 : 4;
    output.mFramesPerPacket = options.frameSize;
    output.mChannelsPerFrame = channels;

    ALACEncoder encoder;
    encoder.SetFrameSize(options.frameSize);
    if (encoder.InitializeEncoder(output) != 0)
        throw std::runtime_error("ALAC encoder: InitializeEncoder failed");
    std::vector<std::uint8_t> cookie(encoder.GetMagicCookieSize(channels));
    std::uint32_t cookieSize = static_cast<std::uint32_t>(cookie.size());
    encoder.GetMagicCookie(cookie.data(), &cookieSize);
    cookie.resize(cookieSize);

    // Encode packet by packet; the last one holds only the remaining frames.
    std::vector<Bytes> packets;
    std::vector<std::uint32_t> packetFrames;
    std::vector<std::uint8_t> in(std::size_t{options.frameSize} * bytesPerFrame);
    std::vector<std::uint8_t> outBuf(std::size_t{options.frameSize} * channels * 6 + 1024);
    for (std::uint64_t done = 0; done < frames;)
    {
        const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(options.frameSize, frames - done));
        std::memcpy(in.data(), pcm.data() + done * bytesPerFrame, std::size_t{n} * bytesPerFrame);
        std::int32_t ioBytes = static_cast<std::int32_t>(n * bytesPerFrame);
        if (encoder.Encode(input, output, in.data(), outBuf.data(), &ioBytes) != 0)
            throw std::runtime_error("ALAC encoder: Encode failed");
        packets.emplace_back(outBuf.begin(), outBuf.begin() + ioBytes);
        packetFrames.push_back(n);
        done += n;
    }
    encoder.Finish();

    // MPEG-4: ftyp, mdat (all packets in one chunk), moov.
    Bytes ftypBody;
    tag(ftypBody, "M4A ");
    be32(ftypBody, 0);
    tag(ftypBody, "M4A ");
    tag(ftypBody, "mp42");
    tag(ftypBody, "isom");
    const Bytes ftyp = box("ftyp", ftypBody);

    Bytes mdatBody;
    for (const auto& p : packets)
        mdatBody.insert(mdatBody.end(), p.begin(), p.end());
    const Bytes mdat = box("mdat", mdatBody);
    const std::uint32_t firstPacketOffset = static_cast<std::uint32_t>(ftyp.size() + 8);

    const auto duration = static_cast<std::uint32_t>(frames);
    Bytes mvhd;
    be32(mvhd, 0);
    be32(mvhd, 0);
    be32(mvhd, sampleRate); // movie timescale
    be32(mvhd, duration);
    be32(mvhd, 0x00010000);
    be16(mvhd, 0x0100);
    mvhd.resize(mvhd.size() + 10, 0);
    matrix(mvhd);
    mvhd.resize(mvhd.size() + 24, 0);
    be32(mvhd, 2);

    Bytes tkhd;
    be32(tkhd, 0);
    be32(tkhd, 0);
    be32(tkhd, 1);
    be32(tkhd, 0);
    be32(tkhd, duration);
    tkhd.resize(tkhd.size() + 8, 0);
    be16(tkhd, 0);
    be16(tkhd, 0);
    be16(tkhd, 0x0100);
    be16(tkhd, 0);
    matrix(tkhd);
    be32(tkhd, 0);
    be32(tkhd, 0);

    Bytes mdhd;
    be32(mdhd, 0);
    be32(mdhd, 0);
    be32(mdhd, sampleRate);
    be32(mdhd, duration);
    be16(mdhd, 0x55C4);
    be16(mdhd, 0);

    Bytes hdlr;
    be32(hdlr, 0);
    tag(hdlr, "soun");
    hdlr.resize(hdlr.size() + 12, 0);
    hdlr.push_back(0);

    Bytes alacConfig;
    alacConfig.insert(alacConfig.end(), cookie.begin(), cookie.end());
    Bytes entry;
    entry.resize(6, 0);
    be16(entry, 1);           // data reference index
    be16(entry, 0);           // version
    be16(entry, 0);           // revision
    be32(entry, 0);           // vendor
    be16(entry, channels);
    be16(entry, bitDepth);
    be16(entry, 0);
    be16(entry, 0);
    be32(entry, sampleRate < 65536 ? sampleRate << 16 : 0);
    const Bytes alacBox = fullBox("alac", 0, alacConfig);
    entry.insert(entry.end(), alacBox.begin(), alacBox.end());
    Bytes stsdBody;
    be32(stsdBody, 1);
    const Bytes entryBox = box("alac", entry);
    stsdBody.insert(stsdBody.end(), entryBox.begin(), entryBox.end());

    // Time-to-sample: runs of equal packet lengths (the last one is shorter).
    Bytes stts;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
    for (const auto f : packetFrames)
    {
        if (!runs.empty() && runs.back().second == f)
            ++runs.back().first;
        else
            runs.emplace_back(1, f);
    }
    be32(stts, static_cast<std::uint32_t>(runs.size()));
    for (const auto& [count, delta] : runs)
    {
        be32(stts, count);
        be32(stts, delta);
    }

    Bytes stsc;
    be32(stsc, 1);
    be32(stsc, 1);
    be32(stsc, static_cast<std::uint32_t>(packets.size()));
    be32(stsc, 1);

    Bytes stsz;
    be32(stsz, 0);
    be32(stsz, static_cast<std::uint32_t>(packets.size()));
    for (const auto& p : packets)
        be32(stsz, static_cast<std::uint32_t>(p.size()));

    Bytes stco;
    be32(stco, 1);
    be32(stco, firstPacketOffset);

    Bytes dref;
    be32(dref, 1);
    const Bytes url = fullBox("url ", 1, {});
    dref.insert(dref.end(), url.begin(), url.end());

    const Bytes stbl = box("stbl", concat({fullBox("stsd", 0, stsdBody), fullBox("stts", 0, stts), fullBox("stsc", 0, stsc),
                                           fullBox("stsz", 0, stsz), fullBox("stco", 0, stco)}));
    const Bytes minf = box("minf", concat({fullBox("smhd", 0, Bytes(4, 0)), box("dinf", fullBox("dref", 0, dref)), stbl}));
    const Bytes mdia = box("mdia", concat({fullBox("mdhd", 0, mdhd), fullBox("hdlr", 0, hdlr), minf}));

    Bytes trakBody = fullBox("tkhd", 7, tkhd);
    if (options.editFrames > 0)
    {
        Bytes elst;
        be32(elst, 1);
        be32(elst, static_cast<std::uint32_t>(options.editFrames)); // movie timescale = sample rate
        be32(elst, static_cast<std::uint32_t>(options.editStart));
        be32(elst, 0x00010000);
        const Bytes edts = box("edts", fullBox("elst", 0, elst));
        trakBody.insert(trakBody.end(), edts.begin(), edts.end());
    }
    trakBody.insert(trakBody.end(), mdia.begin(), mdia.end());
    const Bytes moov = box("moov", concat({fullBox("mvhd", 0, mvhd), box("trak", trakBody)}));

    std::ofstream out(path, std::ios::binary);
    for (const Bytes* part : {&ftyp, &mdat, &moov})
        out.write(reinterpret_cast<const char*>(part->data()), static_cast<std::streamsize>(part->size()));
    if (!out)
        throw std::runtime_error("cannot write " + path.string());
}

} // namespace ap::test
