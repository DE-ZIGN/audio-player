#include <ap/engine/dsp/resampler.h>

#include <CDSPResampler.h>

#include <algorithm>
#include <cmath>

namespace ap::engine::dsp {

struct Resampler::Impl
{
    std::vector<std::unique_ptr<r8b::CDSPResampler>> channels;
    std::vector<double> in;               // one channel's input block
    std::vector<std::vector<double>> got; // each channel's output of the current block
    std::vector<double> zeros;
};

Resampler::Resampler() : impl_(std::make_unique<Impl>()) {}

Resampler::~Resampler() = default;

void Resampler::prepare(std::uint32_t inRate, std::uint32_t outRate, std::uint16_t channels, std::size_t maxInFrames)
{
    inRate_ = inRate;
    outRate_ = outRate;
    channels_ = channels;
    maxIn_ = std::max<std::size_t>(1, maxInFrames);
    auto& m = *impl_;
    m.channels.clear();
    m.got.clear();
    if (active())
    {
        for (std::uint16_t c = 0; c < channels; ++c)
            m.channels.push_back(std::make_unique<r8b::CDSPResampler>(static_cast<double>(inRate),
                                                                      static_cast<double>(outRate),
                                                                      static_cast<int>(maxIn_), kTransitionBandPercent,
                                                                      kAttenuationDb, r8b::fprLinearPhase));
        m.in.assign(maxIn_, 0.0);
        m.zeros.assign(maxIn_, 0.0);
        m.got.assign(channels, std::vector<double>(maxOutputFrames(maxIn_) + 16, 0.0));
    }
    reset();
}

void Resampler::reset() noexcept
{
    for (auto& c : impl_->channels)
        c->clear();
    inFrames_ = 0;
    outFrames_ = 0;
    finished_ = false;
}

std::uint64_t Resampler::outputFrames(std::uint64_t inFrames, std::uint32_t inRate, std::uint32_t outRate) noexcept
{
    if (inRate == 0 || inRate == outRate)
        return inFrames;
    // ceil(inFrames * outRate / inRate), exactly.
    const std::uint64_t num = inFrames * outRate;
    return (num + inRate - 1) / inRate;
}

std::size_t Resampler::maxOutputFrames(std::size_t inFrames) const noexcept
{
    if (!active())
        return inFrames;
    return static_cast<std::size_t>(std::ceil(static_cast<double>(inFrames) * outRate_ / inRate_)) + 2;
}

void Resampler::process(std::span<const double> in, std::vector<double>& out) noexcept
{
    const std::size_t ch = channels_;
    if (ch == 0)
        return;
    if (!active())
    {
        out.insert(out.end(), in.begin(), in.end());
        inFrames_ += in.size() / ch;
        outFrames_ += in.size() / ch;
        return;
    }
    auto& m = *impl_;
    const std::size_t frames = in.size() / ch;
    for (std::size_t from = 0; from < frames; from += maxIn_)
    {
        const std::size_t n = std::min(maxIn_, frames - from);
        int produced = 0;
        for (std::size_t c = 0; c < ch; ++c)
        {
            for (std::size_t i = 0; i < n; ++i)
                m.in[i] = in[(from + i) * ch + c];
            double* op = nullptr;
            produced = m.channels[c]->process(m.in.data(), static_cast<int>(n), op);
            std::copy(op, op + produced, m.got[c].begin());
        }
        for (int i = 0; i < produced; ++i)
            for (std::size_t c = 0; c < ch; ++c)
                out.push_back(m.got[c][static_cast<std::size_t>(i)]);
        inFrames_ += n;
        outFrames_ += static_cast<std::uint64_t>(produced);
    }
}

void Resampler::finish(std::vector<double>& out) noexcept
{
    if (finished_)
        return;
    finished_ = true;
    const std::size_t ch = channels_;
    if (ch == 0 || !active())
        return;
    const std::uint64_t want = outputFrames(inFrames_, inRate_, outRate_);
    auto& m = *impl_;
    // Push silence through until the filter has given out the last input's frames.
    for (int guard = 0; outFrames_ < want && guard < 1000; ++guard)
    {
        int produced = 0;
        for (std::size_t c = 0; c < ch; ++c)
        {
            double* op = nullptr;
            produced = m.channels[c]->process(m.zeros.data(), static_cast<int>(maxIn_), op);
            std::copy(op, op + produced, m.got[c].begin());
        }
        const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(produced),
                                                                            want - outFrames_));
        for (std::size_t i = 0; i < take; ++i)
            for (std::size_t c = 0; c < ch; ++c)
                out.push_back(m.got[c][i]);
        outFrames_ += take;
    }
}

} // namespace ap::engine::dsp
