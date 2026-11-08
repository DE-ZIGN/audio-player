#include <ap/engine/dop_source.h>

#include <algorithm>
#include <stdexcept>

namespace ap::engine {

DopSource::DopSource(std::unique_ptr<DsdSource> dsd) : dsd_(std::move(dsd))
{
    if (!dsd_)
        throw std::invalid_argument("DopSource: no DSD source");
    const auto& f = dsd_->format();
    if (f.dsdRate % 16 != 0 || f.channels == 0)
        throw std::invalid_argument("DopSource: unsupported DSD format");

    format_ = PcmFormat{f.dsdRate / 16, f.channels, 24, 24, SampleType::Int, f.channelMask};
    info_.dsdRate = f.dsdRate;
    info_.container = f.container == DsdContainer::Dsf ? "DSF" : f.container == DsdContainer::Dff ? "DFF" : "memory";

    const std::uint64_t frames = (dsd_->totalBytes() + 1) / 2; // 2 DSD bytes per channel per frame
    totalFrames_ = frames + (frames % 2);                       // even, so markers continue across tracks
    position_ = dsd_->position() / 2;
}

std::size_t DopSource::read(std::span<std::byte> dst)
{
    const std::size_t ch = format_.channels;
    const std::size_t bpf = format_.bytesPerFrame();
    const std::size_t frames = static_cast<std::size_t>(std::min<std::uint64_t>(dst.size() / bpf, totalFrames_ - position_));
    if (frames == 0)
        return 0;

    scratch_.resize(frames * 2 * ch);
    const std::size_t got = dsd_->read(scratch_); // byte frames (one byte per channel each)

    auto dsdByte = [&](std::size_t k, std::size_t c) -> std::byte {
        return k < got ? scratch_[k * ch + c] : std::byte{kDsdSilence};
    };

    for (std::size_t i = 0; i < frames; ++i)
    {
        const std::byte marker{(position_ + i) % 2 == 0 ? kDopMarkerEven : kDopMarkerOdd};
        for (std::size_t c = 0; c < ch; ++c)
        {
            std::byte* s = dst.data() + (i * ch + c) * 3; // 24-bit little-endian
            s[0] = dsdByte(2 * i + 1, c);                 // later 8 DSD bits
            s[1] = dsdByte(2 * i, c);                     // earlier 8 DSD bits
            s[2] = marker;
        }
    }
    position_ += frames;
    return frames;
}

bool DopSource::seek(std::uint64_t frame)
{
    frame = std::min(frame, totalFrames_);
    if (!dsd_->seek(frame * 2))
        return false;
    position_ = frame;
    return true;
}

} // namespace ap::engine
