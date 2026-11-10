#include <ap/engine/output/delivery_check.h>

#include <ap/engine/output/asio_sample_types.h>

#include <algorithm>
#include <climits>
#include <cstring>

namespace ap::engine {

namespace {

std::int64_t signExtend(std::uint64_t u, unsigned bits) noexcept
{
    const unsigned shift = 64 - bits;
    return static_cast<std::int64_t>(u << shift) >> shift;
}

std::uint64_t readLe(const std::byte* p, unsigned bytes) noexcept
{
    std::uint64_t u = 0;
    for (unsigned k = 0; k < bytes; ++k)
        u |= static_cast<std::uint64_t>(p[k]) << (8 * k);
    return u;
}

// An integer of `bits` bits, left-justified in 32 bits.
std::int64_t leftJustified(std::uint64_t u, unsigned bits) noexcept
{
    return signExtend(u, bits) * (std::int64_t{1} << (32 - bits));
}

// One sample of interleaved PCM in canonical form. An integer container keeps
// its value left-justified (24-bit in a 32-bit slot is already so).
std::int64_t canonical(const std::byte* p, const PcmFormat& f) noexcept
{
    const unsigned bytes = f.bytesPerSample();
    if (f.type == SampleType::Float)
        return static_cast<std::int64_t>(readLe(p, bytes));
    return leftJustified(readLe(p, bytes), bytes * 8);
}

std::int64_t canonicalAsio(const std::byte* p, long t) noexcept
{
    switch (t)
    {
        case asio::Int16LSB: return leftJustified(readLe(p, 2), 16);
        case asio::Int24LSB: return leftJustified(readLe(p, 3), 24);
        case asio::Int32LSB: return leftJustified(readLe(p, 4), 32);
        case asio::Int32LSB16: return leftJustified(readLe(p, 4) & 0xFFFF, 16);
        case asio::Int32LSB18: return leftJustified(readLe(p, 4) & 0x3FFFF, 18);
        case asio::Int32LSB20: return leftJustified(readLe(p, 4) & 0xFFFFF, 20);
        case asio::Int32LSB24: return leftJustified(readLe(p, 4) & 0xFFFFFF, 24);
        case asio::Float32LSB: return static_cast<std::int64_t>(readLe(p, 4));
        case asio::Float64LSB: return static_cast<std::int64_t>(readLe(p, 8));
        default: return INT64_MIN; // a type this check does not know: never equal
    }
}

} // namespace

void DeliveryCheck::read(std::span<const std::byte> bytes, const PcmFormat& format, std::size_t frames)
{
    channels_ = format.channels;
    const std::size_t n = frames * channels_;
    if (expected_.size() < n)
    {
        expected_.resize(n);
        delivered_.resize(n);
    }
    const std::size_t bps = format.bytesPerSample();
    for (std::size_t i = 0; i < n; ++i)
        expected_[i] = canonical(bytes.data() + i * bps, format);
    expectedFrames_ = frames;
}

void DeliveryCheck::deliveredInterleaved(std::span<const std::byte> bytes, const PcmFormat& format,
                                         std::size_t frames) noexcept
{
    const std::size_t n = std::min(frames, expectedFrames_);
    if (format.channels != channels_ || bytes.size() < n * format.bytesPerFrame())
    {
        mismatched_ += frames; // another layout than what was read: not the same data
        expectedFrames_ = 0;
        return;
    }
    const std::size_t bps = format.bytesPerSample();
    for (std::size_t i = 0; i < n * channels_; ++i)
        delivered_[i] = canonical(bytes.data() + i * bps, format);
    compare(n, frames);
}

void DeliveryCheck::deliveredAsio(long sampleType, std::span<void* const> channelBuffers, std::size_t frames) noexcept
{
    const std::size_t n = std::min(frames, expectedFrames_);
    const std::size_t ch = channelBuffers.size();
    const std::size_t bps = asio::bytesPerSample(sampleType);
    if (ch != channels_ || bps == 0)
    {
        mismatched_ += frames;
        expectedFrames_ = 0;
        return;
    }
    for (std::size_t c = 0; c < ch; ++c)
    {
        const auto* p = static_cast<const std::byte*>(channelBuffers[c]);
        for (std::size_t i = 0; i < n; ++i)
            delivered_[i * ch + c] = canonicalAsio(p + i * bps, sampleType);
    }
    compare(n, frames);
}

void DeliveryCheck::compare(std::size_t frames, std::size_t deliveredFrames) noexcept
{
    std::uint64_t good = 0;
    const std::size_t rowBytes = channels_ * sizeof(std::int64_t);
    for (std::size_t f = 0; f < frames; ++f)
        if (std::memcmp(delivered_.data() + f * channels_, expected_.data() + f * channels_, rowBytes) == 0)
            ++good;
    // Frames delivered beyond what was read, or read but not delivered, are not the same data either.
    const std::size_t total = std::max(deliveredFrames, expectedFrames_);
    matched_ += good;
    mismatched_ += total - good;
    expectedFrames_ = 0; // each read is compared once
}

void DeliveryCheck::reset() noexcept
{
    matched_ = 0;
    mismatched_ = 0;
    expectedFrames_ = 0;
}

DeliveryResult DeliveryCheck::result() const noexcept
{
    return {matched_.load(std::memory_order_relaxed), mismatched_.load(std::memory_order_relaxed)};
}

} // namespace ap::engine
