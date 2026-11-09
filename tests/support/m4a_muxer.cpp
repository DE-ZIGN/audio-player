#include "m4a_muxer.h"

#include <ap/engine/mp4_reader.h>

#include <cstdio>
#include <format>
#include <fstream>
#include <stdexcept>

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

std::string makeITunSmpb(std::uint64_t priming, std::uint64_t padding, std::uint64_t length)
{
    return std::format(" 00000000 {:08X} {:08X} {:016X} 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000",
                       priming, padding, length);
}

void writeM4a(const std::filesystem::path& path, const M4aTrack& t)
{
    Bytes ftypBody;
    tag(ftypBody, "M4A ");
    be32(ftypBody, 0);
    tag(ftypBody, "M4A ");
    tag(ftypBody, "mp42");
    tag(ftypBody, "isom");
    const Bytes ftyp = box("ftyp", ftypBody);

    Bytes mdatBody;
    for (const auto& p : t.packets)
        mdatBody.insert(mdatBody.end(), p.begin(), p.end());
    const Bytes mdat = box("mdat", mdatBody);
    const auto firstPacketOffset = static_cast<std::uint32_t>(ftyp.size() + 8);

    std::uint64_t mediaDuration = 0;
    for (const auto f : t.packetFrames)
        mediaDuration += f;
    const auto duration = static_cast<std::uint32_t>(t.editFrames ? t.editFrames : mediaDuration);

    Bytes mvhd;
    be32(mvhd, 0);
    be32(mvhd, 0);
    be32(mvhd, t.timescale);
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
    be32(tkhd, 0);
    be16(tkhd, 0x0100);
    be16(tkhd, 0);
    matrix(tkhd);
    be32(tkhd, 0);
    be32(tkhd, 0);

    Bytes mdhd;
    be32(mdhd, 0);
    be32(mdhd, 0);
    be32(mdhd, t.timescale);
    be32(mdhd, static_cast<std::uint32_t>(mediaDuration));
    be16(mdhd, 0x55C4);
    be16(mdhd, 0);

    Bytes hdlr;
    be32(hdlr, 0);
    tag(hdlr, "soun");
    hdlr.resize(hdlr.size() + 12, 0);
    hdlr.push_back(0);

    Bytes stsdBody;
    be32(stsdBody, 1);
    stsdBody.insert(stsdBody.end(), t.sampleEntry.begin(), t.sampleEntry.end());

    Bytes stts;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
    for (const auto f : t.packetFrames)
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
    be32(stsc, static_cast<std::uint32_t>(t.packets.size()));
    be32(stsc, 1);

    Bytes stsz;
    be32(stsz, 0);
    be32(stsz, static_cast<std::uint32_t>(t.packets.size()));
    for (const auto& p : t.packets)
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
    if (t.editFrames > 0)
    {
        Bytes elst;
        be32(elst, 1);
        be32(elst, static_cast<std::uint32_t>(t.editFrames));
        be32(elst, static_cast<std::uint32_t>(t.editStart));
        be32(elst, 0x00010000);
        const Bytes edts = box("edts", fullBox("elst", 0, elst));
        trakBody.insert(trakBody.end(), edts.begin(), edts.end());
    }
    trakBody.insert(trakBody.end(), mdia.begin(), mdia.end());

    Bytes moovBody = concat({fullBox("mvhd", 0, mvhd), box("trak", trakBody)});
    if (!t.itunSmpb.empty())
    {
        Bytes mean;
        be32(mean, 0);
        const std::string m = "com.apple.iTunes";
        mean.insert(mean.end(), m.begin(), m.end());
        Bytes name;
        be32(name, 0);
        const std::string n = "iTunSMPB";
        name.insert(name.end(), n.begin(), n.end());
        Bytes data;
        be32(data, 1); // type: UTF-8 text
        be32(data, 0); // locale
        data.insert(data.end(), t.itunSmpb.begin(), t.itunSmpb.end());
        const Bytes item = box("----", concat({box("mean", mean), box("name", name), box("data", data)}));
        Bytes metaHdlr;
        be32(metaHdlr, 0);
        tag(metaHdlr, "mdir");
        tag(metaHdlr, "appl");
        metaHdlr.resize(metaHdlr.size() + 9, 0);
        const Bytes meta = fullBox("meta", 0, concat({fullBox("hdlr", 0, metaHdlr), box("ilst", item)}));
        const Bytes udta = box("udta", meta);
        moovBody.insert(moovBody.end(), udta.begin(), udta.end());
    }
    const Bytes moov = box("moov", moovBody);

    std::ofstream out(path, std::ios::binary);
    for (const Bytes* part : {&ftyp, &mdat, &moov})
        out.write(reinterpret_cast<const char*>(part->data()), static_cast<std::streamsize>(part->size()));
    if (!out)
        throw std::runtime_error("cannot write " + path.string());
}

void rewriteAacM4a(const std::filesystem::path& from, const std::filesystem::path& to, std::uint64_t editStart,
                   std::uint64_t editFrames, const std::string& itunSmpb)
{
    const auto track = ap::engine::readMp4AudioTrack(from);
    if (track.codec != "mp4a")
        throw std::runtime_error("not an AAC track");
    std::ifstream in(from, std::ios::binary);

    M4aTrack t;
    t.timescale = track.timescale;
    for (const auto& p : track.packets)
    {
        std::vector<std::uint8_t> bytes(p.size);
        in.seekg(static_cast<std::streamoff>(p.offset));
        in.read(reinterpret_cast<char*>(bytes.data()), p.size);
        t.packets.push_back(std::move(bytes));
        t.packetFrames.push_back(p.frames);
    }
    Bytes entry(6, 0);
    be16(entry, 1);
    be16(entry, 0);
    be16(entry, 0);
    be32(entry, 0);
    be16(entry, track.channels);
    be16(entry, 16);
    be16(entry, 0);
    be16(entry, 0);
    be32(entry, (track.timescale < 65536 ? track.timescale : 0) << 16);
    const Bytes esds = box("esds", track.config); // config = the esds payload (version/flags + descriptors)
    entry.insert(entry.end(), esds.begin(), esds.end());
    t.sampleEntry = box("mp4a", entry);
    t.editStart = editStart;
    t.editFrames = editFrames;
    t.itunSmpb = itunSmpb;
    writeM4a(to, t);
}

} // namespace ap::test
