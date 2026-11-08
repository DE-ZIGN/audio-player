#include <ap/engine/pcm_source.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ap::engine {

MemoryPcmSource::MemoryPcmSource(PcmFormat format, std::vector<std::byte> data)
    : format_(format), data_(std::move(data))
{
    if (!format_.isValid())
        throw std::invalid_argument("MemoryPcmSource: invalid format");
    data_.resize(data_.size() - data_.size() % format_.bytesPerFrame());
}

MemoryPcmSource MemoryPcmSource::loadAll(PcmSource& source)
{
    const auto bpf = source.format().bytesPerFrame();
    std::vector<std::byte> all;
    all.reserve(static_cast<std::size_t>(source.totalFrames() * bpf));

    std::vector<std::byte> chunk(65536 * static_cast<std::size_t>(bpf));
    while (const auto frames = source.read(chunk))
        all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(frames * bpf));
    return MemoryPcmSource(source.format(), std::move(all));
}

std::uint64_t MemoryPcmSource::totalFrames() const noexcept
{
    return data_.size() / format_.bytesPerFrame();
}

std::size_t MemoryPcmSource::read(std::span<std::byte> dst)
{
    const std::size_t bpf = format_.bytesPerFrame();
    const std::size_t frames = std::min(dst.size() / bpf, (data_.size() - position_) / bpf);
    const std::size_t bytes = frames * bpf;
    if (bytes > 0)
        std::memcpy(dst.data(), data_.data() + position_, bytes);
    position_ += bytes;
    return frames;
}

bool MemoryPcmSource::seek(std::uint64_t frame)
{
    const std::uint64_t clamped = std::min(frame, totalFrames());
    position_ = static_cast<std::size_t>(clamped * format_.bytesPerFrame());
    return true;
}

std::uint64_t MemoryPcmSource::position() const noexcept
{
    return position_ / format_.bytesPerFrame();
}

} // namespace ap::engine
