#include <ap/engine/mp4_reader.h>

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>

namespace ap::engine {

namespace {

[[noreturn]] void fail(const std::string& what)
{
    throw Mp4FormatError(what);
}

std::uint16_t be16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint32_t be32(const std::uint8_t* p)
{
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}

std::uint64_t be64(const std::uint8_t* p)
{
    return (std::uint64_t{be32(p)} << 32) | be32(p + 4);
}

// Limits that keep a damaged or hostile file from making us allocate without bound.
constexpr std::uint64_t kMaxTableBox = 256ull << 20;   // sample tables of a few hours fit easily
constexpr std::uint64_t kMaxSmallBox = 1u << 20;       // headers, sample descriptions, edit lists
constexpr std::uint32_t kMaxPackets = 1u << 26;

struct Box
{
    std::array<char, 4> type{};
    std::uint64_t start = 0;   // of the header
    std::uint64_t body = 0;    // first byte after the header
    std::uint64_t end = 0;     // one past the last byte

    bool is(const char* t) const { return std::memcmp(type.data(), t, 4) == 0; }
    std::uint64_t size() const { return end - body; }
};

class Reader
{
public:
    explicit Reader(const std::filesystem::path& path)
    {
        std::error_code ec;
        fileSize_ = std::filesystem::file_size(path, ec);
        if (ec)
            fail("cannot open file: " + path.string());
        in_.open(path, std::ios::binary);
        if (!in_)
            fail("cannot open file: " + path.string());
    }

    std::uint64_t fileSize() const { return fileSize_; }

    // Reads the box header at `pos`, which must lie within [pos, limit).
    std::optional<Box> boxAt(std::uint64_t pos, std::uint64_t limit)
    {
        if (pos + 8 > limit)
            return std::nullopt;
        std::array<std::uint8_t, 16> h{};
        readAt(pos, h.data(), 8);
        Box b;
        std::memcpy(b.type.data(), h.data() + 4, 4);
        b.start = pos;
        std::uint64_t size = be32(h.data());
        b.body = pos + 8;
        if (size == 1)
        {
            if (pos + 16 > limit)
                fail("truncated 64-bit box header");
            readAt(pos + 8, h.data() + 8, 8);
            size = be64(h.data() + 8);
            b.body = pos + 16;
        }
        else if (size == 0)
        {
            size = limit - pos; // to the end of the enclosing box / file
        }
        if (size < b.body - pos || size > limit - pos)
            fail(std::string("box '") + std::string(b.type.data(), 4) + "' has an invalid size");
        b.end = pos + size;
        return b;
    }

    // The first child of `type` inside [from, to).
    std::optional<Box> find(std::uint64_t from, std::uint64_t to, const char* type)
    {
        for (auto pos = from; auto b = boxAt(pos, to); pos = b->end)
        {
            if (b->is(type))
                return b;
            if (b->end <= pos)
                break;
        }
        return std::nullopt;
    }

    Box require(const Box& parent, const char* type)
    {
        auto b = find(parent.body, parent.end, type);
        if (!b)
            fail(std::string("missing '") + type + "' box");
        return *b;
    }

    std::vector<std::uint8_t> payload(const Box& b, std::uint64_t maxSize)
    {
        if (b.size() > maxSize)
            fail(std::string("box '") + std::string(b.type.data(), 4) + "' is too large");
        std::vector<std::uint8_t> v(static_cast<std::size_t>(b.size()));
        if (!v.empty())
            readAt(b.body, v.data(), v.size());
        return v;
    }

    void readAt(std::uint64_t pos, std::uint8_t* dst, std::size_t n)
    {
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(pos));
        if (!in_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n)))
            fail("unexpected end of file");
    }

private:
    std::ifstream in_;
    std::uint64_t fileSize_ = 0;
};

// Checked reads from a box payload.
class Cursor
{
public:
    explicit Cursor(const std::vector<std::uint8_t>& v) : v_(v) {}
    void need(std::uint64_t n) const
    {
        if (n > v_.size() - pos_)
            fail("box is shorter than its contents");
    }
    std::uint8_t u8() { need(1); return v_[pos_++]; }
    std::uint16_t u16() { need(2); const auto x = be16(&v_[pos_]); pos_ += 2; return x; }
    std::uint32_t u32() { need(4); const auto x = be32(&v_[pos_]); pos_ += 4; return x; }
    std::uint64_t u64() { need(8); const auto x = be64(&v_[pos_]); pos_ += 8; return x; }
    void skip(std::uint64_t n) { need(n); pos_ += static_cast<std::size_t>(n); }
    std::size_t pos() const { return pos_; }
    std::size_t remaining() const { return v_.size() - pos_; }

private:
    const std::vector<std::uint8_t>& v_;
    std::size_t pos_ = 0;
};

bool isSoundTrack(Reader& r, const Box& trak)
{
    const auto mdia = r.find(trak.body, trak.end, "mdia");
    if (!mdia)
        return false;
    const auto hdlr = r.find(mdia->body, mdia->end, "hdlr");
    if (!hdlr)
        return false;
    const auto p = r.payload(*hdlr, kMaxSmallBox);
    return p.size() >= 12 && std::memcmp(&p[8], "soun", 4) == 0;
}

} // namespace

bool looksLikeMp4(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    std::array<char, 8> h{};
    return f.read(h.data(), h.size()) && std::memcmp(h.data() + 4, "ftyp", 4) == 0;
}

Mp4AudioTrack readMp4AudioTrack(const std::filesystem::path& path)
{
    Reader r(path);
    const auto moov = r.find(0, r.fileSize(), "moov");
    if (!moov)
        fail("no 'moov' box (not an MPEG-4 file, or an incomplete one)");

    // Movie timescale (edit list durations are in it).
    std::uint32_t movieTimescale = 0;
    if (const auto mvhd = r.find(moov->body, moov->end, "mvhd"))
    {
        const auto p = r.payload(*mvhd, kMaxSmallBox);
        Cursor c(p);
        const auto version = c.u8();
        c.skip(3);
        c.skip(version == 1 ? 16 : 8);
        movieTimescale = c.u32();
    }

    std::optional<Box> trak;
    for (auto pos = moov->body; auto b = r.boxAt(pos, moov->end); pos = b->end)
    {
        if (b->is("trak") && isSoundTrack(r, *b))
        {
            trak = b;
            break;
        }
    }
    if (!trak)
        fail("no sound track");

    Mp4AudioTrack t;
    const auto mdia = r.require(*trak, "mdia");
    {
        const auto p = r.payload(r.require(mdia, "mdhd"), kMaxSmallBox);
        Cursor c(p);
        const auto version = c.u8();
        c.skip(3);
        c.skip(version == 1 ? 16 : 8);
        t.timescale = c.u32();
        if (t.timescale == 0)
            fail("media timescale is 0");
    }
    const auto stbl = r.require(r.require(mdia, "minf"), "stbl");

    // Sample description: the first entry.
    {
        const auto stsd = r.require(stbl, "stsd");
        const auto entry = r.boxAt(stsd.body + 8, stsd.end);
        if (!entry)
            fail("empty sample description");
        t.codec.assign(entry->type.data(), 4);
        const auto p = r.payload(*entry, kMaxSmallBox);
        Cursor c(p);
        c.skip(6 + 2);       // reserved, data reference index
        const auto version = c.u16();
        c.skip(6);           // revision, vendor
        t.channels = c.u16();
        t.sampleSize = c.u16();
        c.skip(4);           // compression id, packet size
        t.sampleRate = c.u32() >> 16;
        if (version == 1)
            c.skip(16);
        else if (version == 2)
            c.skip(36);
        // Child boxes of the sample entry: the codec configuration.
        const auto childStart = entry->body + c.pos();
        const char* configType = t.codec == "alac" ? "alac" : t.codec == "mp4a" ? "esds" : nullptr;
        if (configType)
        {
            if (const auto cfg = r.find(childStart, entry->end, configType))
                t.config = r.payload(*cfg, kMaxSmallBox);
        }
    }

    // Time-to-sample: frames per packet.
    std::vector<std::uint32_t> framesPerPacket;
    {
        const auto p = r.payload(r.require(stbl, "stts"), kMaxTableBox);
        Cursor c(p);
        c.skip(4);
        const auto entries = c.u32();
        c.need(std::uint64_t{entries} * 8);
        for (std::uint32_t i = 0; i < entries; ++i)
        {
            const auto count = c.u32();
            const auto delta = c.u32();
            if (framesPerPacket.size() + count > kMaxPackets)
                fail("too many packets");
            framesPerPacket.insert(framesPerPacket.end(), count, delta);
        }
    }

    // Sample sizes.
    std::vector<std::uint32_t> sizes;
    {
        const auto p = r.payload(r.require(stbl, "stsz"), kMaxTableBox);
        Cursor c(p);
        c.skip(4);
        const auto fixed = c.u32();
        const auto count = c.u32();
        if (count > kMaxPackets)
            fail("too many packets");
        if (fixed != 0)
        {
            sizes.assign(count, fixed);
        }
        else
        {
            c.need(std::uint64_t{count} * 4);
            sizes.resize(count);
            for (auto& s : sizes)
                s = c.u32();
        }
    }

    // Chunk offsets (32- or 64-bit).
    std::vector<std::uint64_t> chunkOffsets;
    if (const auto stco = r.find(stbl.body, stbl.end, "stco"))
    {
        const auto p = r.payload(*stco, kMaxTableBox);
        Cursor c(p);
        c.skip(4);
        const auto count = c.u32();
        c.need(std::uint64_t{count} * 4);
        chunkOffsets.resize(count);
        for (auto& o : chunkOffsets)
            o = c.u32();
    }
    else if (const auto co64 = r.find(stbl.body, stbl.end, "co64"))
    {
        const auto p = r.payload(*co64, kMaxTableBox);
        Cursor c(p);
        c.skip(4);
        const auto count = c.u32();
        c.need(std::uint64_t{count} * 8);
        chunkOffsets.resize(count);
        for (auto& o : chunkOffsets)
            o = c.u64();
    }
    else
    {
        fail("missing 'stco' / 'co64' box");
    }

    // Sample-to-chunk: lay the packets out in their chunks.
    {
        const auto p = r.payload(r.require(stbl, "stsc"), kMaxTableBox);
        Cursor c(p);
        c.skip(4);
        const auto entries = c.u32();
        c.need(std::uint64_t{entries} * 12);
        struct Run
        {
            std::uint32_t firstChunk, samplesPerChunk;
        };
        std::vector<Run> runs(entries);
        for (auto& run : runs)
        {
            run.firstChunk = c.u32();
            run.samplesPerChunk = c.u32();
            c.skip(4);
            if (run.firstChunk == 0)
                fail("invalid sample-to-chunk entry");
        }

        if (sizes.size() != framesPerPacket.size())
            fail("sample size and time tables disagree");
        t.packets.reserve(sizes.size());
        std::size_t packet = 0;
        for (std::size_t i = 0; i < runs.size() && packet < sizes.size(); ++i)
        {
            const std::uint64_t lastChunk = i + 1 < runs.size() ? runs[i + 1].firstChunk - 1 : chunkOffsets.size();
            for (std::uint64_t chunk = runs[i].firstChunk; chunk <= lastChunk && packet < sizes.size(); ++chunk)
            {
                if (chunk > chunkOffsets.size())
                    fail("sample-to-chunk refers to a missing chunk");
                std::uint64_t offset = chunkOffsets[static_cast<std::size_t>(chunk - 1)];
                for (std::uint32_t k = 0; k < runs[i].samplesPerChunk && packet < sizes.size(); ++k, ++packet)
                {
                    if (offset + sizes[packet] > r.fileSize())
                        fail("a packet lies outside the file");
                    t.packets.push_back({offset, sizes[packet], framesPerPacket[packet]});
                    offset += sizes[packet];
                }
            }
        }
        if (t.packets.size() != sizes.size())
            fail("the chunk tables do not hold every packet");
    }
    for (const auto& pk : t.packets)
        t.totalFrames += pk.frames;

    // Edit list: apply the first non-empty edit (start offset and length).
    t.editStart = 0;
    t.editFrames = t.totalFrames;
    if (const auto edts = r.find(trak->body, trak->end, "edts"))
    {
        if (const auto elst = r.find(edts->body, edts->end, "elst"))
        {
            const auto p = r.payload(*elst, kMaxSmallBox);
            Cursor c(p);
            const auto version = c.u8();
            c.skip(3);
            const auto entries = c.u32();
            for (std::uint32_t i = 0; i < entries; ++i)
            {
                const std::uint64_t duration = version == 1 ? c.u64() : c.u32();
                const std::int64_t mediaTime =
                    version == 1 ? static_cast<std::int64_t>(c.u64()) : static_cast<std::int32_t>(c.u32());
                c.skip(4); // rate
                if (mediaTime < 0)
                    continue; // an empty edit (a pause); not applied
                t.hasEditList = true;
                t.editStart = std::min<std::uint64_t>(static_cast<std::uint64_t>(mediaTime), t.totalFrames);
                std::uint64_t frames = t.totalFrames - t.editStart;
                if (movieTimescale != 0 && duration != 0)
                {
                    // segment duration is in the movie timescale; convert to media ticks
                    const long double ticks = static_cast<long double>(duration) * t.timescale / movieTimescale;
                    frames = std::min<std::uint64_t>(frames, static_cast<std::uint64_t>(ticks + 0.5L));
                }
                t.editFrames = frames;
                break;
            }
        }
    }
    return t;
}

} // namespace ap::engine
