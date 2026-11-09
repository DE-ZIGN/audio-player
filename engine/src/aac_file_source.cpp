#include <ap/engine/aac_file_source.h>

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <string>

namespace ap::engine {

using Microsoft::WRL::ComPtr;

namespace {

[[noreturn]] void fail(const std::string& what)
{
    throw AacFormatError(what);
}

void check(HRESULT hr, const char* what)
{
    if (FAILED(hr))
        fail(std::format("{} failed (0x{:08X})", what, static_cast<unsigned long>(hr)));
}

// Media Foundation is used from whichever thread reads (the read-ahead runs
// its own); each such thread joins the multithreaded apartment once.
void ensureMta()
{
    thread_local const bool joined = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    (void)joined;
}

// MPEG-4 descriptor length: up to 4 bytes, 7 bits each.
bool readDescriptor(const std::vector<std::uint8_t>& b, std::size_t& pos, std::uint8_t& tag, std::size_t& length)
{
    if (pos >= b.size())
        return false;
    tag = b[pos++];
    length = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (pos >= b.size())
            return false;
        const std::uint8_t c = b[pos++];
        length = (length << 7) | (c & 0x7F);
        if (!(c & 0x80))
            break;
    }
    return length <= b.size() - pos;
}

struct AudioSpecificConfig
{
    std::vector<std::uint8_t> bytes;
    unsigned objectType = 0;
    std::uint32_t sampleRate = 0;
    unsigned channels = 0;
};

// esds payload (version/flags + ES_Descriptor) -> AudioSpecificConfig.
AudioSpecificConfig parseEsds(const std::vector<std::uint8_t>& esds)
{
    std::size_t pos = 4; // version/flags
    std::uint8_t tag = 0;
    std::size_t len = 0;
    if (!readDescriptor(esds, pos, tag, len) || tag != 0x03 || len < 3)
        fail("AAC: no ES descriptor");
    pos += 2; // ES_ID
    const std::uint8_t flags = esds[pos++];
    if (flags & 0x80)
        pos += 2; // dependsOn_ES_ID
    if (flags & 0x40)
    {
        if (pos >= esds.size())
            fail("AAC: bad ES descriptor");
        pos += 1 + esds[pos]; // URL
    }
    if (flags & 0x20)
        pos += 2; // OCR_ES_Id
    if (!readDescriptor(esds, pos, tag, len) || tag != 0x04 || len < 13)
        fail("AAC: no decoder configuration");
    const std::uint8_t objectTypeIndication = esds[pos];
    if (objectTypeIndication != 0x40 && !(objectTypeIndication >= 0x66 && objectTypeIndication <= 0x68))
        fail(std::format("AAC: unsupported object type indication 0x{:02X}", objectTypeIndication));
    pos += 13;
    if (!readDescriptor(esds, pos, tag, len) || tag != 0x05 || len < 2 || len > 64)
        fail("AAC: no AudioSpecificConfig");

    AudioSpecificConfig a;
    a.bytes.assign(esds.begin() + static_cast<std::ptrdiff_t>(pos), esds.begin() + static_cast<std::ptrdiff_t>(pos + len));
    // Bit fields: objectType (5, or 31 + 6), frequency index (4, or 15 + 24), channels (4).
    std::size_t bit = 0;
    auto bits = [&](unsigned n) {
        std::uint32_t v = 0;
        for (unsigned i = 0; i < n; ++i, ++bit)
        {
            if (bit / 8 >= a.bytes.size())
                fail("AAC: AudioSpecificConfig is too short");
            v = (v << 1) | ((a.bytes[bit / 8] >> (7 - bit % 8)) & 1u);
        }
        return v;
    };
    a.objectType = bits(5);
    if (a.objectType == 31)
        a.objectType = 32 + bits(6);
    static constexpr std::uint32_t kRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                               22050, 16000, 12000, 11025, 8000,  7350};
    const auto index = bits(4);
    if (index == 15)
        a.sampleRate = bits(24);
    else if (index < std::size(kRates))
        a.sampleRate = kRates[index];
    else
        fail("AAC: invalid sampling frequency index");
    a.channels = bits(4);
    return a;
}

} // namespace

struct AacFileSource::Decoder
{
    bool mfStarted = false;
    ComPtr<IMFTransform> mft;
    std::uint32_t outRate = 0;
    unsigned outChannels = 0;
    bool providesSamples = false;
    DWORD outBufferSize = 0;

    ~Decoder()
    {
        mft.Reset();
        if (mfStarted)
            MFShutdown();
    }

    void start(const AudioSpecificConfig& asc, unsigned channels)
    {
        check(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET), "MFStartup");
        mfStarted = true;

        MFT_REGISTER_TYPE_INFO in{MFMediaType_Audio, MFAudioFormat_AAC};
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        check(MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                        &in, nullptr, &activates, &count),
              "MFTEnumEx (AAC decoder)");
        HRESULT hr = count > 0 ? activates[0]->ActivateObject(IID_PPV_ARGS(&mft)) : E_FAIL;
        for (UINT32 i = 0; i < count; ++i)
            activates[i]->Release();
        CoTaskMemFree(activates);
        if (FAILED(hr) || !mft)
            fail("no AAC decoder in this Windows (Media Foundation)");

        // Input: raw AAC frames; user data = HEAACWAVEINFO's tail (12 bytes) + AudioSpecificConfig.
        ComPtr<IMFMediaType> it;
        check(MFCreateMediaType(&it), "MFCreateMediaType");
        check(it->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "SetGUID");
        check(it->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC), "SetGUID(AAC)");
        check(it->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, asc.sampleRate), "rate");
        check(it->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels), "channels");
        check(it->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0), "payload type");
        std::vector<std::uint8_t> userData(12, 0);
        userData[2] = 0xFE; // audio profile/level: unspecified
        userData.insert(userData.end(), asc.bytes.begin(), asc.bytes.end());
        check(it->SetBlob(MF_MT_USER_DATA, userData.data(), static_cast<UINT32>(userData.size())), "user data");
        check(mft->SetInputType(0, it.Get(), 0), "SetInputType (AAC)");

        // Output: 32-bit float.
        for (DWORD i = 0;; ++i)
        {
            ComPtr<IMFMediaType> ot;
            if (FAILED(mft->GetOutputAvailableType(0, i, &ot)))
                fail("the AAC decoder offers no float output");
            GUID sub{};
            if (SUCCEEDED(ot->GetGUID(MF_MT_SUBTYPE, &sub)) && sub == MFAudioFormat_Float)
            {
                check(mft->SetOutputType(0, ot.Get(), 0), "SetOutputType (float)");
                UINT32 rate = 0, ch = 0, bits = 0;
                check(ot->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate), "output rate");
                check(ot->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch), "output channels");
                check(ot->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits), "output bits");
                if (bits != 32)
                    fail("unexpected float output width");
                outRate = rate;
                outChannels = ch;
                break;
            }
        }
        MFT_OUTPUT_STREAM_INFO info{};
        check(mft->GetOutputStreamInfo(0, &info), "GetOutputStreamInfo");
        providesSamples = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
        outBufferSize = std::max<DWORD>(info.cbSize, 2048u * 8u * sizeof(float));
        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    }

    void flush() { mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0); }

    // Appends all output the decoder has ready. Returns false on a decoder error.
    bool drainOutput(std::vector<float>& out)
    {
        for (;;)
        {
            MFT_OUTPUT_DATA_BUFFER ob{};
            ComPtr<IMFSample> sample;
            if (!providesSamples)
            {
                ComPtr<IMFMediaBuffer> buf;
                if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(outBufferSize, &buf))
                    || FAILED(sample->AddBuffer(buf.Get())))
                    return false;
                ob.pSample = sample.Get();
            }
            DWORD status = 0;
            const HRESULT hr = mft->ProcessOutput(0, 1, &ob, &status);
            ComPtr<IMFSample> produced;
            produced.Attach(providesSamples ? ob.pSample : nullptr);
            if (ob.pEvents)
                ob.pEvents->Release();
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
                return true;
            if (FAILED(hr))
                return false;
            IMFSample* s = providesSamples ? produced.Get() : sample.Get();
            if (!s)
                continue;
            ComPtr<IMFMediaBuffer> buf;
            if (FAILED(s->ConvertToContiguousBuffer(&buf)))
                return false;
            BYTE* p = nullptr;
            DWORD length = 0;
            if (FAILED(buf->Lock(&p, nullptr, &length)))
                return false;
            const auto* f = reinterpret_cast<const float*>(p);
            out.insert(out.end(), f, f + length / sizeof(float));
            buf->Unlock();
        }
    }

    bool feed(const std::vector<std::uint8_t>& packet)
    {
        ComPtr<IMFMediaBuffer> buf;
        ComPtr<IMFSample> sample;
        if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(packet.size()), &buf)) || FAILED(MFCreateSample(&sample)))
            return false;
        BYTE* p = nullptr;
        if (FAILED(buf->Lock(&p, nullptr, nullptr)))
            return false;
        std::memcpy(p, packet.data(), packet.size());
        buf->Unlock();
        buf->SetCurrentLength(static_cast<DWORD>(packet.size()));
        sample->AddBuffer(buf.Get());
        return SUCCEEDED(mft->ProcessInput(0, sample.Get(), 0));
    }
};

AacFileSource::AacFileSource(const std::filesystem::path& path) : track_(readMp4AudioTrack(path)), d_(std::make_unique<Decoder>())
{
    ensureMta();
    if (track_.codec != "mp4a")
        fail("not an AAC track (codec '" + track_.codec + "')");
    if (track_.packets.empty())
        fail("the AAC track has no packets");
    const auto asc = parseEsds(track_.config);
    const unsigned channels = asc.channels ? asc.channels : track_.channels;
    if (channels < 1 || channels > 8)
        fail("unsupported AAC channel configuration");
    d_->start(asc, channels);

    // Packet durations are in the media timescale; the decoder's output rate
    // may be a multiple of it (HE-AAC doubles the rate).
    const std::uint32_t outRate = d_->outRate;
    if (outRate == 0 || track_.timescale == 0 || outRate % track_.timescale != 0)
        fail(std::format("AAC output rate {} is not a multiple of the timescale {}", outRate, track_.timescale));
    const std::uint64_t ratio = outRate / track_.timescale;

    packetStart_.resize(track_.packets.size());
    std::uint64_t start = 0;
    for (std::size_t i = 0; i < track_.packets.size(); ++i)
    {
        packetStart_[i] = start;
        start += std::uint64_t{track_.packets[i].frames} * ratio;
    }
    const std::uint64_t streamFrames = start;

    // Trimming: the edit list, else iTunSMPB, else none.
    if (track_.hasEditList)
    {
        streamStart_ = track_.editStart * ratio;
        totalFrames_ = track_.editFrames * ratio;
        gaplessSource_ = "elst";
    }
    else if (track_.itunSmpb)
    {
        streamStart_ = track_.itunSmpb->priming;
        totalFrames_ = track_.itunSmpb->originalFrames;
        gaplessSource_ = "iTunSMPB";
    }
    else
    {
        streamStart_ = 0;
        totalFrames_ = streamFrames;
    }
    streamStart_ = std::min(streamStart_, streamFrames);
    totalFrames_ = std::min(totalFrames_, streamFrames - streamStart_);

    format_.sampleRate = outRate;
    format_.channels = static_cast<std::uint16_t>(d_->outChannels);
    format_.containerBits = 32;
    format_.validBits = 32;
    format_.type = SampleType::Float;
    format_.channelMask = d_->outChannels == 1 ? 0x4u : d_->outChannels == 2 ? 0x3u : 0u;

    file_.open(path, std::ios::binary);
    if (!file_)
        fail("cannot open file: " + path.string());
    seek(0);
}

AacFileSource::~AacFileSource()
{
    ensureMta();
    d_.reset();
}

IntegrityCheck AacFileSource::integrity() const
{
    IntegrityCheck c;
    c.decodeErrors = decodeErrors_.load();
    c.state = c.decodeErrors > 0 ? IntegrityState::Failed : IntegrityState::NotAvailable;
    return c;
}

void AacFileSource::startDecodingAt(std::size_t packet)
{
    d_->flush();
    nextPacket_ = packet;
    decodedFrame_ = packet < packetStart_.size() ? packetStart_[packet] : 0;
    inputEnded_ = false;
    pending_.clear();
    pendingOffset_ = 0;
}

bool AacFileSource::fillPending()
{
    const std::size_t ch = format_.channels;
    // Drop what was taken; keep the rest.
    decodedFrame_ += pendingOffset_ / ch;
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(pendingOffset_));
    pendingOffset_ = 0;

    while (pending_.empty())
    {
        if (nextPacket_ >= track_.packets.size())
        {
            if (inputEnded_)
                return false;
            inputEnded_ = true;
            d_->mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
            if (!d_->drainOutput(pending_))
                ++decodeErrors_;
            continue;
        }
        const auto& p = track_.packets[nextPacket_++];
        std::vector<std::uint8_t> bytes(p.size);
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(p.offset));
        bool ok = static_cast<bool>(file_.read(reinterpret_cast<char*>(bytes.data()), p.size));
        ok = ok && d_->feed(bytes);
        ok = ok && d_->drainOutput(pending_);
        if (!ok)
        {
            ++decodeErrors_;
            d_->flush();
        }
    }
    return true;
}

std::size_t AacFileSource::read(std::span<std::byte> dst)
{
    ensureMta();
    const std::size_t ch = format_.channels;
    const std::size_t want = dst.size() / format_.bytesPerFrame();
    std::size_t got = 0;
    auto* out = reinterpret_cast<float*>(dst.data());
    while (got < want && position_ < totalFrames_)
    {
        if (pendingOffset_ >= pending_.size() && !fillPending())
            break;
        // Skip decoded frames before the current stream position (priming, seek preroll).
        const std::uint64_t wantStream = streamStart_ + position_;
        const std::uint64_t at = decodedFrame_ + pendingOffset_ / ch;
        if (at < wantStream)
        {
            const auto skip = std::min<std::uint64_t>(wantStream - at, (pending_.size() - pendingOffset_) / ch);
            pendingOffset_ += static_cast<std::size_t>(skip) * ch;
            continue;
        }
        const auto n = static_cast<std::size_t>(
            std::min<std::uint64_t>({want - got, (pending_.size() - pendingOffset_) / ch, totalFrames_ - position_}));
        std::memcpy(out + got * ch, pending_.data() + pendingOffset_, n * ch * sizeof(float));
        pendingOffset_ += n * ch;
        got += n;
        position_ += n;
    }
    if (got < want && position_ < totalFrames_)
        totalFrames_ = position_; // the stream ended before the length the file gives
    return got;
}

bool AacFileSource::seek(std::uint64_t frame)
{
    ensureMta();
    position_ = std::min(frame, totalFrames_);
    const std::uint64_t target = streamStart_ + position_;
    const auto it = std::upper_bound(packetStart_.begin(), packetStart_.end(), target);
    const auto packet = static_cast<std::size_t>(std::max<std::ptrdiff_t>(std::distance(packetStart_.begin(), it) - 1, 0));
    // AAC frames overlap: start two packets earlier so the target's output is complete.
    startDecodingAt(packet >= 2 ? packet - 2 : 0);
    return true;
}

} // namespace ap::engine
