#include <ap/engine/alac_file_source.h>

#include <ALACBitUtilities.h>
#include <ALACDecoder.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace ap::engine {

namespace {

[[noreturn]] void fail(const std::string& what)
{
    throw AlacFormatError(what);
}

std::uint32_t be32(const std::uint8_t* p)
{
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}

constexpr std::uint32_t kMaxFrameLength = 16384; // same limit as the hardened decoder (VENDORED.md)

} // namespace

AlacFileSource::AlacFileSource(const std::filesystem::path& path) : track_(readMp4AudioTrack(path))
{
    if (track_.codec != "alac")
        fail("not an ALAC track (codec '" + track_.codec + "')");
    if (track_.packets.empty())
        fail("the ALAC track has no packets");

    // 'alac' box payload: version/flags (4 bytes), then the 24-byte ALACSpecificConfig
    // (big-endian), optionally followed by a channel layout.
    const auto& c = track_.config;
    if (c.size() < 4 + 24)
        fail("ALAC configuration is missing or too short");
    const std::uint8_t* s = c.data() + 4;
    frameLength_ = be32(s);
    const unsigned compatibleVersion = s[4];
    const unsigned bitDepth = s[5];
    const unsigned kb = s[8];
    const unsigned channels = s[9];
    const std::uint32_t sampleRate = be32(s + 20);

    // Checked here so the error says what is wrong; the decoder checks the same (VENDORED.md).
    if (compatibleVersion != 0)
        fail("unsupported ALAC version " + std::to_string(compatibleVersion));
    if (frameLength_ < 1 || frameLength_ > kMaxFrameLength)
        fail("unsupported ALAC frame length " + std::to_string(frameLength_));
    if (bitDepth != 16 && bitDepth != 20 && bitDepth != 24 && bitDepth != 32)
        fail("unsupported ALAC bit depth " + std::to_string(bitDepth));
    if (channels < 1 || channels > 8)
        fail("unsupported ALAC channel count " + std::to_string(channels));
    if (kb > 31)
        fail("invalid ALAC parameter kb " + std::to_string(kb));
    if (sampleRate == 0)
        fail("ALAC sample rate is 0");
    // Packet durations are in the media timescale; this reader counts sample frames.
    if (track_.timescale != sampleRate)
        fail("media timescale (" + std::to_string(track_.timescale) + ") differs from the sample rate ("
             + std::to_string(sampleRate) + ")");

    decoder_ = std::make_unique<ALACDecoder>();
    std::vector<std::uint8_t> cookie(c.begin() + 4, c.end());
    if (decoder_->Init(cookie.data(), static_cast<uint32_t>(cookie.size())) != ALAC_noErr)
        fail("the ALAC decoder refused the configuration");

    format_.sampleRate = sampleRate;
    format_.channels = static_cast<std::uint16_t>(channels);
    format_.containerBits = static_cast<std::uint16_t>(bitDepth == 20 ? 24 : bitDepth);
    format_.validBits = static_cast<std::uint16_t>(bitDepth);
    format_.type = SampleType::Int;
    format_.channelMask = channels == 1 ? 0x4u : channels == 2 ? 0x3u : 0u; // ALAC's order for more channels differs from WAV's
    decodedBytesPerSample_ = format_.containerBits / 8u;

    // A packet can hold at most every sample uncompressed, plus headers.
    const std::size_t maxPacket = std::size_t{frameLength_} * channels * (bitDepth / 8u + 1) + 4096;
    for (const auto& p : track_.packets)
        if (p.size > maxPacket)
            fail("an ALAC packet is larger than its configuration allows");
    input_.resize(maxPacket + kInputPadding);
    decoded_.resize(std::size_t{frameLength_} * channels * decodedBytesPerSample_);

    packetStart_.resize(track_.packets.size());
    std::uint64_t start = 0;
    for (std::size_t i = 0; i < track_.packets.size(); ++i)
    {
        packetStart_[i] = start;
        start += track_.packets[i].frames;
    }

    file_.open(path, std::ios::binary);
    if (!file_)
        fail("cannot open file: " + path.string());

    // The last packet may hold fewer frames than the table says (some encoders
    // record every packet as full length); its own header has the real count.
    const std::size_t last = track_.packets.size() - 1;
    decodePacket(last);
    const std::uint64_t lastFrames = (pending_.size() / format_.bytesPerFrame());
    const std::uint64_t streamFrames = packetStart_[last] + lastFrames;
    totalFrames_ = streamFrames > track_.editStart ? std::min(track_.editFrames, streamFrames - track_.editStart) : 0;
    track_.packets[last].frames = static_cast<std::uint32_t>(lastFrames);

    seek(0);
}

AlacFileSource::~AlacFileSource() = default;

IntegrityCheck AlacFileSource::integrity() const
{
    IntegrityCheck c;
    c.decodeErrors = decodeErrors_.load();
    c.state = c.decodeErrors > 0 ? IntegrityState::Failed : IntegrityState::NotAvailable;
    return c;
}

bool AlacFileSource::decodePacket(std::size_t index)
{
    pending_.clear();
    pendingOffset_ = 0;
    const auto& p = track_.packets[index];
    const std::size_t bpf = format_.bytesPerFrame();
    const std::uint32_t expected = std::min(p.frames, frameLength_);

    std::fill(input_.begin() + p.size, input_.begin() + p.size + kInputPadding, std::uint8_t{0});
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(p.offset));
    const bool readOk = static_cast<bool>(file_.read(reinterpret_cast<char*>(input_.data()), p.size));

    uint32_t produced = 0;
    bool ok = false;
    if (readOk)
    {
        BitBuffer bits;
        BitBufferInit(&bits, input_.data(), p.size);
        ok = decoder_->Decode(&bits, reinterpret_cast<uint8_t*>(decoded_.data()), frameLength_, format_.channels, &produced)
            == ALAC_noErr;
    }

    std::uint32_t frames = ok ? std::min(produced, expected) : expected;
    const bool last = index + 1 == track_.packets.size();
    if (!ok || (frames < expected && !last))
    {
        // A packet the decoder rejected, or a short packet in the middle: keep the
        // timing with silence of the length the table gives, and count it.
        ++decodeErrors_;
        if (!ok)
            frames = expected;
        pending_.assign(std::size_t{expected} * bpf, std::byte{0});
        if (ok)
            std::memcpy(pending_.data(), decoded_.data(), std::size_t{frames} * bpf);
        return false;
    }
    pending_.assign(decoded_.begin(), decoded_.begin() + static_cast<std::ptrdiff_t>(std::size_t{frames} * bpf));
    return true;
}

std::size_t AlacFileSource::read(std::span<std::byte> dst)
{
    const std::size_t bpf = format_.bytesPerFrame();
    const std::size_t want = dst.size() / bpf;
    std::size_t got = 0;
    while (got < want && position_ < totalFrames_)
    {
        if (pendingOffset_ >= pending_.size())
        {
            if (nextPacket_ >= track_.packets.size())
                break;
            decodePacket(nextPacket_++);
            continue;
        }
        const std::uint64_t available = (pending_.size() - pendingOffset_) / bpf;
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>({want - got, available, totalFrames_ - position_}));
        std::memcpy(dst.data() + got * bpf, pending_.data() + pendingOffset_, n * bpf);
        pendingOffset_ += n * bpf;
        got += n;
        position_ += n;
    }
    return got;
}

bool AlacFileSource::seek(std::uint64_t frame)
{
    position_ = std::min(frame, totalFrames_);
    pending_.clear();
    pendingOffset_ = 0;
    if (position_ == totalFrames_)
    {
        nextPacket_ = track_.packets.size();
        return true;
    }
    const std::uint64_t streamFrame = track_.editStart + position_;
    const auto it = std::upper_bound(packetStart_.begin(), packetStart_.end(), streamFrame);
    const auto index = static_cast<std::size_t>(std::distance(packetStart_.begin(), it)) - 1;
    decodePacket(index);
    nextPacket_ = index + 1;
    const std::uint64_t skip = streamFrame - packetStart_[index];
    pendingOffset_ = static_cast<std::size_t>(std::min<std::uint64_t>(skip * format_.bytesPerFrame(), pending_.size()));
    return true;
}

} // namespace ap::engine
