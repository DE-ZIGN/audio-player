#include <ap/engine/wav_file_source.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <vector>

namespace ap::engine {

namespace {

constexpr std::uint16_t kTagPcm = 0x0001;
constexpr std::uint16_t kTagFloat = 0x0003;
constexpr std::uint16_t kTagExtensible = 0xFFFE;

// Bytes 2..15 of KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT.
constexpr std::array<std::uint8_t, 14> kSubtypeTail{
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

std::uint16_t le16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t le32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

[[noreturn]] void fail(const std::string& what)
{
    throw WavFormatError(what);
}

PcmFormat parseFmt(const std::vector<std::uint8_t>& b)
{
    if (b.size() < 16)
        fail("fmt chunk is too small");

    PcmFormat f;
    std::uint16_t tag = le16(&b[0]);
    f.channels = le16(&b[2]);
    f.sampleRate = le32(&b[4]);
    const std::uint32_t byteRate = le32(&b[8]);
    const std::uint16_t blockAlign = le16(&b[12]);
    f.containerBits = le16(&b[14]);
    f.validBits = f.containerBits;

    if (tag == kTagExtensible)
    {
        if (b.size() < 40 || le16(&b[16]) < 22)
            fail("WAVE_FORMAT_EXTENSIBLE fmt chunk is too small");
        f.validBits = le16(&b[18]);
        f.channelMask = le32(&b[20]);
        tag = le16(&b[24]);
        if (std::memcmp(&b[26], kSubtypeTail.data(), kSubtypeTail.size()) != 0)
            fail("unsupported WAVE_FORMAT_EXTENSIBLE subformat");
        if (f.validBits == 0)
            f.validBits = f.containerBits;
    }

    if (tag == kTagPcm)
        f.type = SampleType::Int;
    else if (tag == kTagFloat)
        f.type = SampleType::Float;
    else
        fail("unsupported format tag " + std::to_string(tag) + " (compressed WAV is not supported)");

    if (f.type == SampleType::Int && f.containerBits == 8)
        fail("8-bit WAV is not supported");
    if (!f.isValid())
        fail("unsupported sample layout: " + std::to_string(f.containerBits) + "-bit container, "
             + std::to_string(f.validBits) + " valid bits, " + std::to_string(f.channels) + " channels");
    if (blockAlign != f.bytesPerFrame() || byteRate != f.sampleRate * f.bytesPerFrame())
        fail("inconsistent fmt chunk (block align / byte rate)");
    return f;
}

} // namespace

WavFileSource::WavFileSource(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec)
        fail("cannot open file: " + path.string());

    file_.open(path, std::ios::binary);
    if (!file_)
        fail("cannot open file: " + path.string());

    std::array<std::uint8_t, 12> header{};
    if (!file_.read(reinterpret_cast<char*>(header.data()), header.size()))
        fail("file is too small to be WAV");
    // RF64 (EBU Tech 3306) and BW64 (ITU-R BS.2088): like RIFF, but 32-bit
    // sizes set to 0xFFFFFFFF are given as 64-bit values in a ds64 chunk.
    const bool rf64 = std::memcmp(header.data(), "RF64", 4) == 0 || std::memcmp(header.data(), "BW64", 4) == 0;
    if ((!rf64 && std::memcmp(header.data(), "RIFF", 4) != 0) || std::memcmp(header.data() + 8, "WAVE", 4) != 0)
        fail("not a RIFF/WAVE file");

    std::optional<PcmFormat> fmt;
    std::optional<std::uint64_t> dataSize;
    std::optional<std::uint64_t> ds64DataSize;
    std::uint64_t pos = 12;

    while (pos + 8 <= fileSize && !(fmt && dataSize))
    {
        file_.seekg(static_cast<std::streamoff>(pos));
        std::array<std::uint8_t, 8> ch{};
        if (!file_.read(reinterpret_cast<char*>(ch.data()), ch.size()))
            break;
        std::uint64_t size = le32(ch.data() + 4);
        const std::uint64_t body = pos + 8;

        if (rf64 && std::memcmp(ch.data(), "ds64", 4) == 0)
        {
            std::array<std::uint8_t, 16> b{};
            if (size < 16 || !file_.read(reinterpret_cast<char*>(b.data()), b.size()))
                fail("ds64 chunk is too small");
            ds64DataSize = static_cast<std::uint64_t>(le32(&b[8])) | (static_cast<std::uint64_t>(le32(&b[12])) << 32);
        }
        else if (rf64 && std::memcmp(ch.data(), "data", 4) == 0 && size == 0xFFFFFFFFu)
        {
            if (!ds64DataSize)
                fail("RF64 data size is not given (no ds64 chunk before the data)");
            size = *ds64DataSize;
        }

        if (std::memcmp(ch.data(), "fmt ", 4) == 0)
        {
            if (size > 1024 || body + size > fileSize)
                fail("fmt chunk has an invalid size");
            std::vector<std::uint8_t> b(static_cast<std::size_t>(size));
            if (!file_.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size())))
                fail("cannot read fmt chunk");
            fmt = parseFmt(b);
        }
        else if (std::memcmp(ch.data(), "data", 4) == 0)
        {
            dataOffset_ = body;
            // Some writers leave the size unset or too large; use what the file holds.
            dataSize = std::min(size, fileSize - body);
            if (size > fileSize - body)
                break;
        }

        pos = body + size + (size & 1u);
    }

    if (!fmt)
        fail("fmt chunk not found");
    if (!dataSize)
        fail("data chunk not found");

    format_ = *fmt;
    totalFrames_ = *dataSize / format_.bytesPerFrame();
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(dataOffset_));
}

std::size_t WavFileSource::read(std::span<std::byte> dst)
{
    const std::size_t bpf = format_.bytesPerFrame();
    const auto remaining = totalFrames_ - framesRead_;
    const std::size_t frames = static_cast<std::size_t>(std::min<std::uint64_t>(dst.size() / bpf, remaining));
    if (frames == 0)
        return 0;

    file_.read(reinterpret_cast<char*>(dst.data()), static_cast<std::streamsize>(frames * bpf));
    const std::size_t got = static_cast<std::size_t>(file_.gcount()) / bpf;
    framesRead_ += got;
    if (got < frames)
        totalFrames_ = framesRead_; // truncated underneath us
    return got;
}

bool WavFileSource::seek(std::uint64_t frame)
{
    const std::uint64_t clamped = std::min(frame, totalFrames_);
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(dataOffset_ + clamped * format_.bytesPerFrame()));
    if (!file_)
        return false;
    framesRead_ = clamped;
    return true;
}

} // namespace ap::engine
