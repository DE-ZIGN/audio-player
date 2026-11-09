#include <ap/engine/mp3_file_source.h>

#include <mpg123.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace ap::engine {

namespace {

[[noreturn]] void fail(const std::string& what)
{
    throw Mp3FormatError(what);
}

std::string utf8(const std::filesystem::path& p)
{
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

} // namespace

Mp3FileSource::Mp3FileSource(const std::filesystem::path& path)
{
    int err = MPG123_OK;
    h_ = mpg123_new(nullptr, &err);
    if (!h_)
        fail(std::string("cannot create the MP3 decoder: ") + mpg123_plain_strerror(err));

    // Float output only (no integer rounding in the decoder), gapless (drop the
    // encoder delay and padding from the LAME / Xing tag), no console messages.
    mpg123_param2(h_, MPG123_FLAGS, MPG123_GAPLESS | MPG123_FORCE_FLOAT | MPG123_QUIET, 0.0);
    mpg123_format_none(h_);
    const long* rates = nullptr;
    std::size_t rateCount = 0;
    mpg123_rates(&rates, &rateCount);
    for (std::size_t i = 0; i < rateCount; ++i)
        mpg123_format(h_, rates[i], MPG123_MONO | MPG123_STEREO, MPG123_ENC_FLOAT_32);

    // On Windows, libmpg123 takes UTF-8 paths and opens them as wide paths.
    if (mpg123_open64(h_, utf8(path).c_str()) != MPG123_OK)
        fail(std::string("cannot open MP3: ") + mpg123_strerror(h_));

    long rate = 0;
    int channels = 0;
    int encoding = 0;
    if (mpg123_getformat2(h_, &rate, &channels, &encoding, 0) != MPG123_OK)
        fail(std::string("not an MPEG audio file: ") + mpg123_strerror(h_));
    if (encoding != MPG123_ENC_FLOAT_32 || (channels != 1 && channels != 2) || rate <= 0)
        fail("unexpected MP3 output format");
    // Keep this one format for the whole file (a change mid-stream ends it).
    mpg123_format_none(h_);
    mpg123_format(h_, rate, channels, MPG123_ENC_FLOAT_32);

    // The exact length: read through the file once (frame count, and the
    // LAME / Xing tag's delay and padding, which gapless decoding removes).
    if (mpg123_scan(h_) != MPG123_OK)
        fail(std::string("cannot scan MP3: ") + mpg123_strerror(h_));
    const int64_t length = mpg123_length64(h_);
    if (length < 0)
        fail("MP3 length is unknown");

    long delay = -1;
    double unused = 0.0;
    if (mpg123_getstate(h_, MPG123_ENC_DELAY, &delay, &unused) == MPG123_OK)
        gaplessInfo_ = delay >= 0;

    format_.sampleRate = static_cast<std::uint32_t>(rate);
    format_.channels = static_cast<std::uint16_t>(channels);
    format_.containerBits = 32;
    format_.validBits = 32;
    format_.type = SampleType::Float;
    format_.channelMask = channels == 1 ? 0x4u : 0x3u;
    totalFrames_ = static_cast<std::uint64_t>(length);

    if (mpg123_seek64(h_, 0, SEEK_SET) < 0)
        fail(std::string("cannot rewind MP3: ") + mpg123_strerror(h_));
}

Mp3FileSource::~Mp3FileSource()
{
    if (h_)
    {
        mpg123_close(h_);
        mpg123_delete(h_);
    }
}

std::size_t Mp3FileSource::read(std::span<std::byte> dst)
{
    const std::size_t bpf = format_.bytesPerFrame();
    const std::size_t want = static_cast<std::size_t>(
        std::min<std::uint64_t>(dst.size() / bpf, totalFrames_ - std::min(position_, totalFrames_)));
    std::size_t got = 0;
    while (got < want)
    {
        std::size_t done = 0;
        const int r = mpg123_read(h_, reinterpret_cast<unsigned char*>(dst.data()) + got * bpf, (want - got) * bpf, &done);
        got += done / bpf;
        if (r == MPG123_OK || r == MPG123_NEW_FORMAT)
        {
            if (done == 0 && r == MPG123_OK)
                break;
            continue;
        }
        break; // MPG123_DONE, or an error: the data ends here
    }
    position_ += got;
    if (got < want)
        totalFrames_ = position_; // the stream ended before the length the scan gave
    return got;
}

bool Mp3FileSource::seek(std::uint64_t frame)
{
    const std::uint64_t target = std::min(frame, totalFrames_);
    const int64_t r = mpg123_seek64(h_, static_cast<int64_t>(target), SEEK_SET);
    if (r < 0)
        return false;
    position_ = static_cast<std::uint64_t>(r);
    return true;
}

} // namespace ap::engine
