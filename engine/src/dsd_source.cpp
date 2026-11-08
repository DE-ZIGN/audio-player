#include <ap/engine/dsd_source.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>

namespace ap::engine {

namespace {

[[noreturn]] void fail(const std::string& what)
{
    throw DsdFormatError(what);
}

std::uint64_t le(const std::uint8_t* p, int bytes)
{
    std::uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i)
        v = (v << 8) | p[i];
    return v;
}

std::uint64_t be(const std::uint8_t* p, int bytes)
{
    std::uint64_t v = 0;
    for (int i = 0; i < bytes; ++i)
        v = (v << 8) | p[i];
    return v;
}

std::uint8_t reverseBits(std::uint8_t b)
{
    b = static_cast<std::uint8_t>((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = static_cast<std::uint8_t>((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = static_cast<std::uint8_t>((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

void readExact(std::ifstream& f, std::uint64_t offset, void* dst, std::size_t n, const char* what)
{
    f.clear();
    f.seekg(static_cast<std::streamoff>(offset));
    if (!f.read(static_cast<char*>(dst), static_cast<std::streamsize>(n)))
        fail(std::string("truncated file (") + what + ")");
}

std::uint32_t defaultMask(std::uint16_t channels)
{
    return channels == 2 ? 0x3u : channels == 1 ? 0x4u : 0u;
}

} // namespace

std::string dsdRateName(std::uint32_t rate)
{
    if (rate % 44100 == 0 && (rate / 44100) % 64 == 0)
        return std::format("DSD{}", rate / 44100);
    if (rate % 48000 == 0 && (rate / 48000) % 64 == 0)
        return std::format("DSD{} (48 kHz family)", rate / 48000);
    return std::format("DSD {} Hz", rate);
}

// --- memory ------------------------------------------------------------------

MemoryDsdSource::MemoryDsdSource(DsdFormat format, std::vector<std::byte> interleaved)
    : format_(format), data_(std::move(interleaved))
{
    if (format_.channels == 0)
        throw std::invalid_argument("MemoryDsdSource: no channels");
    data_.resize(data_.size() - data_.size() % format_.channels);
}

std::size_t MemoryDsdSource::read(std::span<std::byte> dst)
{
    const std::size_t ch = format_.channels;
    const std::size_t frames = std::min<std::uint64_t>(dst.size() / ch, totalBytes() - position_);
    std::memcpy(dst.data(), data_.data() + position_ * ch, frames * ch);
    position_ += frames;
    return frames;
}

bool MemoryDsdSource::seek(std::uint64_t byteFrame)
{
    position_ = std::min(byteFrame, totalBytes());
    return true;
}

// --- DSF -------------------------------------------------------------------------

DsfFileSource::DsfFileSource(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec)
        fail("cannot open file: " + path.string());
    file_.open(path, std::ios::binary);
    if (!file_)
        fail("cannot open file: " + path.string());

    std::array<std::uint8_t, 28> dsd{};
    readExact(file_, 0, dsd.data(), dsd.size(), "DSD chunk");
    if (std::memcmp(dsd.data(), "DSD ", 4) != 0 || le(&dsd[4], 8) != 28)
        fail("not a DSF file");

    std::array<std::uint8_t, 52> fmt{};
    readExact(file_, 28, fmt.data(), fmt.size(), "fmt chunk");
    if (std::memcmp(fmt.data(), "fmt ", 4) != 0)
        fail("DSF fmt chunk not found");
    const std::uint64_t fmtSize = le(&fmt[4], 8);
    if (le(&fmt[16], 4) != 0)
        fail("unsupported DSF format id (only raw DSD)");
    const auto channels = static_cast<std::uint32_t>(le(&fmt[24], 4));
    const auto rate = static_cast<std::uint32_t>(le(&fmt[28], 4));
    const auto bits = static_cast<std::uint32_t>(le(&fmt[32], 4));
    const std::uint64_t sampleCount = le(&fmt[36], 8);
    blockSize_ = static_cast<std::uint32_t>(le(&fmt[44], 4));
    if (channels == 0 || channels > 8 || rate == 0 || (bits != 1 && bits != 8) || blockSize_ == 0)
        fail("unsupported DSF parameters");
    lsbFirst_ = bits == 1;

    const std::uint64_t dataChunk = 28 + fmtSize;
    std::array<std::uint8_t, 12> data{};
    readExact(file_, dataChunk, data.data(), data.size(), "data chunk");
    if (std::memcmp(data.data(), "data", 4) != 0)
        fail("DSF data chunk not found");
    dataOffset_ = dataChunk + 12;

    format_ = {rate, static_cast<std::uint16_t>(channels), defaultMask(static_cast<std::uint16_t>(channels)),
               DsdContainer::Dsf};
    totalBytes_ = (sampleCount + 7) / 8;
    // Never read beyond what the file holds (whole block groups).
    const std::uint64_t groupBytes = static_cast<std::uint64_t>(blockSize_) * channels;
    const std::uint64_t groupsInFile = (fileSize > dataOffset_ ? fileSize - dataOffset_ : 0) / groupBytes;
    totalBytes_ = std::min(totalBytes_, groupsInFile * blockSize_);
    group_.resize(groupBytes);
}

void DsfFileSource::loadGroup(std::uint64_t g)
{
    if (g == loadedGroup_)
        return;
    readExact(file_, dataOffset_ + g * group_.size(), group_.data(), group_.size(), "DSD data");
    if (lsbFirst_)
        for (auto& b : group_)
            b = static_cast<std::byte>(reverseBits(std::to_integer<std::uint8_t>(b)));
    loadedGroup_ = g;
}

std::size_t DsfFileSource::read(std::span<std::byte> dst)
{
    const std::size_t ch = format_.channels;
    const std::size_t frames = std::min<std::uint64_t>(dst.size() / ch, totalBytes_ - position_);
    for (std::size_t i = 0; i < frames;)
    {
        const std::uint64_t p = position_ + i;
        const std::uint64_t g = p / blockSize_;
        const std::size_t off = static_cast<std::size_t>(p % blockSize_);
        loadGroup(g);
        const std::size_t run = std::min<std::size_t>(frames - i, blockSize_ - off);
        for (std::size_t k = 0; k < run; ++k)
            for (std::size_t c = 0; c < ch; ++c)
                dst[(i + k) * ch + c] = group_[c * blockSize_ + off + k];
        i += run;
    }
    position_ += frames;
    return frames;
}

bool DsfFileSource::seek(std::uint64_t byteFrame)
{
    position_ = std::min(byteFrame, totalBytes_);
    return true;
}

// --- DFF ---------------------------------------------------------------------------

DffFileSource::DffFileSource(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec)
        fail("cannot open file: " + path.string());
    file_.open(path, std::ios::binary);
    if (!file_)
        fail("cannot open file: " + path.string());

    std::array<std::uint8_t, 16> head{};
    readExact(file_, 0, head.data(), head.size(), "FRM8");
    if (std::memcmp(head.data(), "FRM8", 4) != 0 || std::memcmp(&head[12], "DSD ", 4) != 0)
        fail("not a DSDIFF file");
    const std::uint64_t formEnd = std::min<std::uint64_t>(12 + be(&head[4], 8), fileSize);

    std::optional<std::uint32_t> rate;
    std::optional<std::uint16_t> channels;
    bool compressed = false;
    std::optional<std::uint64_t> dataSize;

    std::uint64_t pos = 16;
    while (pos + 12 <= formEnd)
    {
        std::array<std::uint8_t, 12> ck{};
        readExact(file_, pos, ck.data(), ck.size(), "chunk header");
        const std::uint64_t size = be(&ck[4], 8);
        const std::uint64_t body = pos + 12;

        if (std::memcmp(ck.data(), "PROP", 4) == 0)
        {
            std::array<std::uint8_t, 4> type{};
            readExact(file_, body, type.data(), 4, "PROP");
            if (std::memcmp(type.data(), "SND ", 4) == 0)
            {
                std::uint64_t sp = body + 4;
                const std::uint64_t propEnd = body + size;
                while (sp + 12 <= propEnd)
                {
                    std::array<std::uint8_t, 12> sk{};
                    readExact(file_, sp, sk.data(), sk.size(), "PROP sub-chunk");
                    const std::uint64_t ssize = be(&sk[4], 8);
                    std::array<std::uint8_t, 4> v{};
                    if (std::memcmp(sk.data(), "FS  ", 4) == 0)
                    {
                        readExact(file_, sp + 12, v.data(), 4, "FS");
                        rate = static_cast<std::uint32_t>(be(v.data(), 4));
                    }
                    else if (std::memcmp(sk.data(), "CHNL", 4) == 0)
                    {
                        readExact(file_, sp + 12, v.data(), 2, "CHNL");
                        channels = static_cast<std::uint16_t>(be(v.data(), 2));
                    }
                    else if (std::memcmp(sk.data(), "CMPR", 4) == 0)
                    {
                        readExact(file_, sp + 12, v.data(), 4, "CMPR");
                        compressed = std::memcmp(v.data(), "DSD ", 4) != 0;
                    }
                    sp += 12 + ssize + (ssize & 1u);
                }
            }
        }
        else if (std::memcmp(ck.data(), "DSD ", 4) == 0)
        {
            dataOffset_ = body;
            dataSize = std::min(size, fileSize - body);
        }
        else if (std::memcmp(ck.data(), "DST ", 4) == 0)
        {
            compressed = true;
        }
        pos = body + size + (size & 1u);
    }

    if (compressed)
        fail("DST-compressed DSDIFF is not supported");
    if (!rate || !channels || *channels == 0 || !dataSize)
        fail("incomplete DSDIFF file (FS, CHNL or DSD chunk missing)");

    format_ = {*rate, *channels, defaultMask(*channels), DsdContainer::Dff};
    totalBytes_ = *dataSize / *channels;
}

std::size_t DffFileSource::read(std::span<std::byte> dst)
{
    const std::size_t ch = format_.channels;
    const std::size_t frames = std::min<std::uint64_t>(dst.size() / ch, totalBytes_ - position_);
    if (frames == 0)
        return 0;
    readExact(file_, dataOffset_ + position_ * ch, dst.data(), frames * ch, "DSD data");
    position_ += frames;
    return frames;
}

bool DffFileSource::seek(std::uint64_t byteFrame)
{
    position_ = std::min(byteFrame, totalBytes_);
    return true;
}

// --- helpers ---------------------------------------------------------------------

bool looksLikeDsdFile(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    std::array<char, 16> head{};
    if (!f.read(head.data(), head.size()))
        return false;
    return std::memcmp(head.data(), "DSD ", 4) == 0
        || (std::memcmp(head.data(), "FRM8", 4) == 0 && std::memcmp(head.data() + 12, "DSD ", 4) == 0);
}

std::unique_ptr<DsdSource> openDsdFile(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    std::array<char, 4> magic{};
    if (!f.read(magic.data(), 4))
        fail("not a DSD file: " + path.string());
    if (std::memcmp(magic.data(), "DSD ", 4) == 0)
        return std::make_unique<DsfFileSource>(path);
    if (std::memcmp(magic.data(), "FRM8", 4) == 0)
        return std::make_unique<DffFileSource>(path);
    fail("not a DSD file: " + path.string());
}

} // namespace ap::engine
