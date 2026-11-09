#include <ap/engine/flac_file_source.h>

#include <FLAC/stream_decoder.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace ap::engine {

namespace {

[[noreturn]] void fail(const std::string& what)
{
    throw FlacFormatError(what);
}

// FLAC's default channel assignments, as WAVE_FORMAT_EXTENSIBLE speaker masks.
std::uint32_t defaultChannelMask(unsigned channels)
{
    switch (channels)
    {
        case 1:  return 0x4;   // FC
        case 2:  return 0x3;   // FL FR
        case 3:  return 0x7;   // FL FR FC
        case 4:  return 0x33;  // FL FR BL BR
        case 5:  return 0x37;  // FL FR FC BL BR
        case 6:  return 0x3F;  // FL FR FC LFE BL BR
        case 7:  return 0x70F; // FL FR FC LFE BC SL SR
        case 8:  return 0x63F; // FL FR FC LFE BL BR SL SR
        default: return 0;
    }
}

bool isAllZero(const Md5Digest& d)
{
    return std::all_of(d.begin(), d.end(), [](std::uint8_t b) { return b == 0; });
}

} // namespace

struct FlacFileSource::Decoder
{
    FLAC__StreamDecoder* decoder = nullptr;
    FlacFileSource* owner = nullptr;
    bool haveStreamInfo = false;
    FLAC__StreamMetadata_StreamInfo info{};
    bool seeking = false; // a seek is in progress: the frame written is not part of a decode from the start

    ~Decoder()
    {
        if (decoder)
            FLAC__stream_decoder_delete(decoder); // also closes the FILE
    }

    static FLAC__StreamDecoderWriteStatus onWrite(const FLAC__StreamDecoder*, const FLAC__Frame* frame,
                                                  const FLAC__int32* const buffer[], void* client)
    {
        return static_cast<Decoder*>(client)->write(*frame, buffer);
    }

    static void onMetadata(const FLAC__StreamDecoder*, const FLAC__StreamMetadata* m, void* client)
    {
        auto& self = *static_cast<Decoder*>(client);
        if (m->type == FLAC__METADATA_TYPE_STREAMINFO)
        {
            self.info = m->data.stream_info;
            self.haveStreamInfo = true;
        }
    }

    static void onError(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* client)
    {
        auto& self = *static_cast<Decoder*>(client);
        ++self.owner->decodeErrors_;
    }

    FLAC__StreamDecoderWriteStatus write(const FLAC__Frame& frame, const FLAC__int32* const buffer[])
    {
        auto& o = *owner;
        const unsigned channels = frame.header.channels;
        const unsigned blocksize = frame.header.blocksize;
        if (channels != o.format_.channels || frame.header.bits_per_sample != o.bitsPerSample_)
        {
            ++o.decodeErrors_; // a frame that does not match STREAMINFO: not decodable as this stream
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }

        // Output: each sample left-justified in its container, little-endian.
        const unsigned containerBytes = o.format_.containerBits / 8u;
        const unsigned shift = o.format_.containerBits - o.bitsPerSample_;
        const std::size_t start = o.pending_.size();
        o.pending_.resize(start + std::size_t{blocksize} * channels * containerBytes);
        std::byte* out = o.pending_.data() + start;
        for (unsigned i = 0; i < blocksize; ++i)
            for (unsigned c = 0; c < channels; ++c)
            {
                const auto u = static_cast<std::uint32_t>(buffer[c][i]) << shift;
                for (unsigned b = 0; b < containerBytes; ++b)
                    *out++ = static_cast<std::byte>(u >> (8 * b));
            }

        // MD5 as FLAC defines it: each sample unshifted, little-endian, in
        // (bits + 7) / 8 bytes, interleaved.
        if (o.md5_ && !seeking)
        {
            const unsigned md5Bytes = (o.bitsPerSample_ + 7) / 8;
            if (shift == 0 && md5Bytes == containerBytes)
            {
                o.md5_->update(std::span(o.pending_).subspan(start));
            }
            else
            {
                o.md5Scratch_.resize(std::size_t{blocksize} * channels * md5Bytes);
                std::byte* m = o.md5Scratch_.data();
                for (unsigned i = 0; i < blocksize; ++i)
                    for (unsigned c = 0; c < channels; ++c)
                    {
                        const auto u = static_cast<std::uint32_t>(buffer[c][i]);
                        for (unsigned b = 0; b < md5Bytes; ++b)
                            *m++ = static_cast<std::byte>(u >> (8 * b));
                    }
                o.md5_->update(o.md5Scratch_);
            }
            o.md5Frames_ += blocksize;
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }
};

FlacFileSource::FlacFileSource(const std::filesystem::path& path) : d_(std::make_unique<Decoder>())
{
    d_->owner = this;
    d_->decoder = FLAC__stream_decoder_new();
    if (!d_->decoder)
        fail("cannot create the FLAC decoder");
    // MD5 is checked here (libFLAC stops checking after a seek and reports only at finish()).
    FLAC__stream_decoder_set_md5_checking(d_->decoder, false);

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
        fail("cannot open file: " + path.string());
    // The decoder owns the FILE from here on.
    if (FLAC__stream_decoder_init_FILE(d_->decoder, file, &Decoder::onWrite, &Decoder::onMetadata, &Decoder::onError,
                                       d_.get())
        != FLAC__STREAM_DECODER_INIT_STATUS_OK)
        fail("cannot start the FLAC decoder");

    // A non-FLAC file makes the decoder search for a frame sync until the end
    // of the file; STREAMINFO is what tells a FLAC stream apart.
    if (!FLAC__stream_decoder_process_until_end_of_metadata(d_->decoder) || !d_->haveStreamInfo)
        fail("not a FLAC file (no STREAMINFO)");

    const auto& si = d_->info;
    bitsPerSample_ = si.bits_per_sample;
    if (bitsPerSample_ < 8 || bitsPerSample_ > 32)
        fail("unsupported FLAC bit depth: " + std::to_string(bitsPerSample_));
    if (si.total_samples == 0)
        fail("FLAC streams without a total sample count are not supported");

    format_.sampleRate = si.sample_rate;
    format_.channels = static_cast<std::uint16_t>(si.channels);
    format_.containerBits = static_cast<std::uint16_t>(bitsPerSample_ <= 16 ? 16 : bitsPerSample_ <= 24 ? 24 : 32);
    format_.validBits = static_cast<std::uint16_t>(bitsPerSample_);
    format_.type = SampleType::Int;
    format_.channelMask = defaultChannelMask(si.channels);
    if (!format_.isValid())
        fail("unsupported FLAC layout");
    totalFrames_ = si.total_samples;

    std::memcpy(storedMd5_.data(), si.md5sum, storedMd5_.size());
    if (!isAllZero(storedMd5_))
    {
        md5_ = std::make_unique<Md5>();
        state_ = IntegrityState::Pending;
    }
}

FlacFileSource::~FlacFileSource()
{
    d_.reset();
}

IntegrityCheck FlacFileSource::integrity() const
{
    IntegrityCheck c;
    c.decodeErrors = decodeErrors_.load();
    c.state = state_.load();
    // A decode error means samples were lost or replaced, whatever the MD5 says.
    if (c.decodeErrors > 0 && c.state != IntegrityState::Skipped)
        c.state = IntegrityState::Failed;
    return c;
}

void FlacFileSource::finishVerification()
{
    if (!md5_)
        return;
    const bool complete = md5Frames_ == totalFrames_;
    const auto digest = md5_->finish();
    md5_.reset();
    state_ = complete && digest == storedMd5_ ? IntegrityState::Passed : IntegrityState::Failed;
}

std::size_t FlacFileSource::read(std::span<std::byte> dst)
{
    const std::size_t bpf = format_.bytesPerFrame();
    const std::size_t want = dst.size() / bpf;
    std::size_t got = 0;

    while (got < want)
    {
        const std::size_t available = (pending_.size() - pendingOffset_) / bpf;
        if (available > 0)
        {
            std::uint64_t n = std::min<std::uint64_t>(want - got, available);
            n = std::min<std::uint64_t>(n, totalFrames_ - std::min(position_, totalFrames_)); // never past STREAMINFO's length
            if (n == 0)
            {
                endOfStream_ = true;
                break;
            }
            std::memcpy(dst.data() + got * bpf, pending_.data() + pendingOffset_, static_cast<std::size_t>(n) * bpf);
            pendingOffset_ += static_cast<std::size_t>(n) * bpf;
            got += static_cast<std::size_t>(n);
            position_ += n;
            continue;
        }

        pending_.clear();
        pendingOffset_ = 0;
        if (endOfStream_)
            break;
        const bool ok = FLAC__stream_decoder_process_single(d_->decoder);
        const auto state = FLAC__stream_decoder_get_state(d_->decoder);
        if (!ok || state == FLAC__STREAM_DECODER_END_OF_STREAM || state == FLAC__STREAM_DECODER_ABORTED)
        {
            if (!ok || state == FLAC__STREAM_DECODER_ABORTED)
                ++decodeErrors_;
            if (pending_.empty())
                endOfStream_ = true;
        }
    }

    if (endOfStream_ && pending_.size() == pendingOffset_)
    {
        if (position_ < totalFrames_)
            totalFrames_ = position_; // the file ended early
        finishVerification();
    }
    return got;
}

bool FlacFileSource::seek(std::uint64_t frame)
{
    const std::uint64_t target = std::min(frame, totalFrames_);
    pending_.clear();
    pendingOffset_ = 0;

    // A check in progress can only continue from the start.
    if (state_ == IntegrityState::Pending || state_ == IntegrityState::Skipped)
    {
        md5_.reset();
        if (target == 0 && !isAllZero(storedMd5_))
        {
            md5_ = std::make_unique<Md5>();
            md5Frames_ = 0;
            state_ = IntegrityState::Pending;
        }
        else
        {
            state_ = IntegrityState::Skipped;
        }
    }

    if (target == totalFrames_)
    {
        position_ = target;
        endOfStream_ = true;
        return true;
    }

    auto* dec = d_->decoder;
    if (FLAC__stream_decoder_get_state(dec) == FLAC__STREAM_DECODER_END_OF_STREAM
        || FLAC__stream_decoder_get_state(dec) == FLAC__STREAM_DECODER_SEEK_ERROR)
        FLAC__stream_decoder_flush(dec);

    // libFLAC writes the frame holding `target`, starting at `target`, during the seek.
    d_->seeking = target != 0;
    const bool ok = FLAC__stream_decoder_seek_absolute(dec, target);
    d_->seeking = false;
    if (!ok)
    {
        FLAC__stream_decoder_flush(dec);
        pending_.clear();
        return false;
    }
    // Seeking to 0 writes the first frame too; it belongs to the new check.
    position_ = target;
    endOfStream_ = false;
    return true;
}

} // namespace ap::engine
