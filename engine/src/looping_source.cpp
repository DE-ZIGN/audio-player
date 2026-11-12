#include <ap/engine/looping_source.h>

#include <ap/engine/dsp/sample_convert.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace ap::engine {

LoopingSource::LoopingSource(std::unique_ptr<PcmSource> source, std::uint64_t start, std::uint64_t length,
                             std::uint32_t fade)
    : source_(std::move(source))
{
    if (!source_)
        throw std::invalid_argument("no source");
    if (source_->dop())
        throw std::invalid_argument("DoP is never looped or mixed");
    const auto total = source_->totalFrames();
    start_ = std::min(start, total);
    length_ = std::min(length, total - start_);
    if (length_ == 0)
        throw std::invalid_argument("empty region");
    fade_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(fade, length_ / 4));
    position_ = start_;
    source_->seek(start_);
}

bool LoopingSource::seek(std::uint64_t frame)
{
    const auto last = start_ + length_ - fade_; // the join starts here
    position_ = frame < start_ || frame >= last ? start_ : frame;
    inJoin_ = false;
    joinPos_ = 0;
    return source_->seek(position_);
}

void LoopingSource::makeJoin()
{
    const auto& f = format();
    const auto bpf = f.bytesPerFrame();
    const std::size_t n = fade_;
    std::vector<std::byte> tail(n * bpf), head(n * bpf);
    auto readAll = [this, bpf](std::span<std::byte> dst) {
        std::size_t done = 0;
        while (done < dst.size())
        {
            const auto got = source_->read(dst.subspan(done));
            if (got == 0)
                break;
            done += got * bpf;
        }
        std::fill(dst.begin() + static_cast<std::ptrdiff_t>(done), dst.end(), std::byte{0});
    };
    source_->seek(start_ + length_ - fade_);
    readAll(tail);
    source_->seek(start_);
    readAll(head);
    // The source is now at start + fade: where the next pass goes on.

    const std::size_t count = n * f.channels;
    std::vector<double> a(count), b(count);
    dsp::toDouble(tail, f, a);
    dsp::toDouble(head, f, b);
    for (std::size_t j = 0; j < n; ++j)
    {
        // Raised cosine from the tail to the head; the weights add up to 1.
        const double w = 0.5 - 0.5 * std::cos(std::numbers::pi * (static_cast<double>(j) + 0.5) / static_cast<double>(n));
        for (std::uint16_t c = 0; c < f.channels; ++c)
        {
            const auto k = j * f.channels + c;
            a[k] = a[k] * (1.0 - w) + b[k] * w;
        }
    }
    join_.resize(n * bpf);
    dsp::fromDouble(a, f, join_);
    joinPos_ = 0;
    inJoin_ = true;
}

std::size_t LoopingSource::read(std::span<std::byte> dst)
{
    const auto bpf = format().bytesPerFrame();
    const std::size_t want = dst.size() / bpf;
    std::size_t done = 0;
    const auto joinAt = start_ + length_ - fade_;
    while (done < want)
    {
        if (inJoin_)
        {
            const std::size_t k = std::min(want - done, join_.size() / bpf - joinPos_);
            std::copy_n(join_.begin() + static_cast<std::ptrdiff_t>(joinPos_ * bpf), k * bpf,
                        dst.begin() + static_cast<std::ptrdiff_t>(done * bpf));
            joinPos_ += k;
            done += k;
            position_ = start_ + length_ - fade_ + joinPos_;
            if (joinPos_ * bpf == join_.size())
            {
                inJoin_ = false;
                position_ = start_ + fade_;
            }
            continue;
        }
        if (position_ >= joinAt)
        {
            if (fade_ == 0)
            {
                source_->seek(start_);
                position_ = start_;
                if (length_ == 0)
                    break;
            }
            else
                makeJoin();
            continue;
        }
        const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(want - done, joinAt - position_));
        const auto got = source_->read(dst.subspan(done * bpf, k * bpf));
        if (got == 0)
        {
            // The file ended early (shorter than it said): treat it as the join.
            position_ = joinAt;
            continue;
        }
        done += got;
        position_ += got;
    }
    return done;
}

} // namespace ap::engine
