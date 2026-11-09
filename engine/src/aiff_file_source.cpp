#include <ap/engine/aiff_file_source.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

namespace ap::engine {

namespace {

[[noreturn]] void fail(const std::string& what)
{
    throw AiffFormatError(what);
}

std::uint16_t be16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint32_t be32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

struct Encoding
{
    SampleType type;
    bool bigEndian;
};

// AIFC compression types that are plain PCM. Anything else is compressed.
std::optional<Encoding> encodingFor(const char* id, std::uint16_t sampleSize)
{
    auto is = [id](const char* t) { return std::memcmp(id, t, 4) == 0; };
    if (is("NONE") || is("twos") || is("in24") || is("in32"))
        return Encoding{SampleType::Int, true};
    if (is("sowt") || is("23ni") || is("42ni"))
        return Encoding{SampleType::Int, false};
    if (is("fl32") || is("FL32"))
        return sampleSize == 32 ? std::optional<Encoding>(Encoding{SampleType::Float, true}) : std::nullopt;
    if (is("fl64") || is("FL64"))
        return sampleSize == 64 ? std::optional<Encoding>(Encoding{SampleType::Float, true}) : std::nullopt;
    return std::nullopt;
}

} // namespace

double readExtended80(const std::uint8_t* b) noexcept
{
    const int exponent = ((b[0] & 0x7F) << 8) | b[1];
    std::uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i)
        mantissa = (mantissa << 8) | b[2 + i];
    if (exponent == 0 && mantissa == 0)
        return 0.0;
    const double value = std::ldexp(static_cast<double>(mantissa), exponent - 16383 - 63);
    return (b[0] & 0x80) ? -value : value;
}

AiffFileSource::AiffFileSource(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec)
        fail("cannot open file: " + path.string());
    file_.open(path, std::ios::binary);
    if (!file_)
        fail("cannot open file: " + path.string());

    std::array<std::uint8_t, 12> header{};
    if (!file_.read(reinterpret_cast<char*>(header.data()), header.size()) || std::memcmp(header.data(), "FORM", 4) != 0)
        fail("not an AIFF file");
    if (std::memcmp(header.data() + 8, "AIFC", 4) == 0)
        aifc_ = true;
    else if (std::memcmp(header.data() + 8, "AIFF", 4) != 0)
        fail("not an AIFF file (FORM type is not AIFF or AIFC)");

    std::optional<PcmFormat> fmt;
    std::uint32_t commFrames = 0;
    std::optional<std::uint64_t> dataSize;
    std::uint64_t pos = 12;

    while (pos + 8 <= fileSize && !(fmt && dataSize))
    {
        file_.seekg(static_cast<std::streamoff>(pos));
        std::array<std::uint8_t, 8> ch{};
        if (!file_.read(reinterpret_cast<char*>(ch.data()), ch.size()))
            break;
        const std::uint64_t size = be32(ch.data() + 4);
        const std::uint64_t body = pos + 8;

        if (std::memcmp(ch.data(), "COMM", 4) == 0)
        {
            if (size < 18 || size > 1024 || body + size > fileSize)
                fail("COMM chunk has an invalid size");
            std::vector<std::uint8_t> b(static_cast<std::size_t>(size));
            if (!file_.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size())))
                fail("cannot read COMM chunk");

            PcmFormat f;
            f.channels = be16(&b[0]);
            commFrames = be32(&b[2]);
            const std::uint16_t sampleSize = be16(&b[6]);
            const double rate = readExtended80(&b[8]);
            if (!(rate >= 1.0 && rate <= 10'000'000.0) || rate != std::floor(rate))
                fail("unsupported sample rate in COMM");
            f.sampleRate = static_cast<std::uint32_t>(rate);

            Encoding enc{SampleType::Int, true};
            if (aifc_)
            {
                if (size < 22)
                    fail("AIFC COMM chunk is too small");
                const char* id = reinterpret_cast<const char*>(&b[18]);
                const auto e = encodingFor(id, sampleSize);
                if (!e)
                    fail("unsupported AIFC compression type '" + std::string(id, 4)
                         + "' (compressed AIFC is not supported)");
                enc = *e;
            }
            if (sampleSize <= 8)
                fail("8-bit AIFF is not supported");
            bigEndian_ = enc.bigEndian;
            f.type = enc.type;
            f.containerBits = static_cast<std::uint16_t>((sampleSize + 7) / 8 * 8);
            f.validBits = sampleSize;
            f.channelMask = f.channels == 1 ? 0x4u : f.channels == 2 ? 0x3u : 0u; // AIFF's order for more channels differs from WAV's
            if (!f.isValid())
                fail("unsupported sample layout: " + std::to_string(sampleSize) + " bits, "
                     + std::to_string(f.channels) + " channels");
            fmt = f;
        }
        else if (std::memcmp(ch.data(), "SSND", 4) == 0)
        {
            std::array<std::uint8_t, 8> s{};
            if (size < 8 || !file_.read(reinterpret_cast<char*>(s.data()), s.size()))
                fail("SSND chunk is too small");
            const std::uint64_t offset = be32(s.data());
            dataOffset_ = body + 8 + offset;
            // The file may hold less than the chunk claims; use what is there.
            const std::uint64_t claimed = size >= 8 + offset ? size - 8 - offset : 0;
            dataSize = dataOffset_ <= fileSize ? std::min(claimed, fileSize - dataOffset_) : 0;
        }

        pos = body + size + (size & 1u);
    }

    if (!fmt)
        fail("COMM chunk not found");
    if (!dataSize)
        fail("SSND chunk not found");

    format_ = *fmt;
    totalFrames_ = std::min<std::uint64_t>(commFrames, *dataSize / format_.bytesPerFrame());
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(dataOffset_));
}

std::size_t AiffFileSource::read(std::span<std::byte> dst)
{
    const std::size_t bpf = format_.bytesPerFrame();
    const auto remaining = totalFrames_ - framesRead_;
    const std::size_t frames = static_cast<std::size_t>(std::min<std::uint64_t>(dst.size() / bpf, remaining));
    if (frames == 0)
        return 0;

    file_.read(reinterpret_cast<char*>(dst.data()), static_cast<std::streamsize>(frames * bpf));
    const std::size_t got = static_cast<std::size_t>(file_.gcount()) / bpf;
    if (bigEndian_)
    {
        // Reverse the bytes of each sample; the value does not change.
        const std::size_t bps = format_.bytesPerSample();
        for (std::size_t i = 0; i < got * bpf; i += bps)
            std::reverse(dst.data() + i, dst.data() + i + bps);
    }
    framesRead_ += got;
    if (got < frames)
        totalFrames_ = framesRead_; // truncated underneath us
    return got;
}

bool AiffFileSource::seek(std::uint64_t frame)
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
