#include <ap/engine/dsp/convolver.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace ap::engine {

ConvolverLayout ConvolverLayout::forRate(double sampleRate) noexcept
{
    std::size_t m = 1;
    while (48000.0 * static_cast<double>(m) < sampleRate - 1.0 && m < 64)
        m *= 2;
    return {128 * m, 15};
}

std::shared_ptr<const ConvolutionKernel> ConvolutionKernel::make(const ConvolverLayout& layout,
                                                                 const std::vector<std::vector<double>>& taps)
{
    if (taps.empty())
        throw std::invalid_argument("ConvolutionKernel: no channels");
    auto k = std::make_shared<ConvolutionKernel>();
    k->layout_ = layout;
    k->channels_ = taps.size();
    for (const auto& t : taps)
        k->frames_ = std::max(k->frames_, t.size());
    const std::size_t B = layout.head, T = layout.tail(), P = layout.headParts;
    const std::size_t headFrames = B * P;
    k->tailParts_ = k->frames_ > headFrames ? (k->frames_ - headFrames + T - 1) / T : 0;

    RealFft headFft(2 * B), tailFft(2 * T);
    std::vector<double> block(2 * T);
    const std::size_t hb = headFft.bins(), tb = tailFft.bins();
    auto spectra = [&](RealFft& fft, const std::vector<double>& t, std::size_t from, std::size_t len,
                       double* re, double* im) {
        std::fill(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(2 * len), 0.0);
        for (std::size_t i = 0; i < len && from + i < t.size(); ++i)
            block[i] = t[from + i];
        fft.forward(block.data(), re, im);
        const double scale = 1.0 / static_cast<double>(2 * len); // for the unscaled inverse
        for (std::size_t i = 0; i < fft.bins(); ++i)
        {
            re[i] *= scale;
            im[i] *= scale;
        }
    };
    for (const auto& t : taps)
    {
        auto& hr = k->headRe_.emplace_back(P * hb);
        auto& hi = k->headIm_.emplace_back(P * hb);
        for (std::size_t p = 0; p < P; ++p)
            spectra(headFft, t, p * B, B, hr.data() + p * hb, hi.data() + p * hb);
        auto& tr = k->tailRe_.emplace_back(k->tailParts_ * tb);
        auto& ti = k->tailIm_.emplace_back(k->tailParts_ * tb);
        for (std::size_t p = 0; p < k->tailParts_; ++p)
            spectra(tailFft, t, headFrames + p * T, T, tr.data() + p * tb, ti.data() + p * tb);
    }
    return k;
}

void PartitionedConvolver::prepare(const ConvolverLayout& layout, std::size_t channels, std::size_t maxKernelFrames)
{
    layout_ = layout;
    channels_ = channels;
    const std::size_t B = layout.head, T = layout.tail(), P = layout.headParts;
    headFft_ = std::make_unique<RealFft>(2 * B);
    tailFft_ = std::make_unique<RealFft>(2 * T);
    headBins_ = headFft_->bins();
    tailBins_ = tailFft_->bins();
    const std::size_t headFrames = B * P;
    tailCapacity_ = maxKernelFrames > headFrames ? (maxKernelFrames - headFrames + T - 1) / T : 0;
    headIn_.assign(channels * 2 * B, 0.0);
    tailIn_.assign(channels * 2 * T, 0.0);
    headFdlRe_.assign(channels * P * headBins_, 0.0);
    headFdlIm_.assign(channels * P * headBins_, 0.0);
    tailFdlRe_.assign(channels * tailCapacity_ * tailBins_, 0.0);
    tailFdlIm_.assign(channels * tailCapacity_ * tailBins_, 0.0);
    accRe_.assign(tailBins_, 0.0);
    accIm_.assign(tailBins_, 0.0);
    time_.assign(2 * T, 0.0);
    for (auto& s : slots_)
    {
        s.headOut.assign(channels * B, 0.0);
        s.tailOut.assign(channels * T, 0.0);
    }
    headPos_ = tailPos_ = fill_ = 0;
    current_ = next_ = nullptr;
    nextStarted_ = false;
    fadePos_ = 0;
}

void PartitionedConvolver::reset() noexcept
{
    std::fill(headIn_.begin(), headIn_.end(), 0.0);
    std::fill(tailIn_.begin(), tailIn_.end(), 0.0);
    std::fill(headFdlRe_.begin(), headFdlRe_.end(), 0.0);
    std::fill(headFdlIm_.begin(), headFdlIm_.end(), 0.0);
    std::fill(tailFdlRe_.begin(), tailFdlRe_.end(), 0.0);
    std::fill(tailFdlIm_.begin(), tailFdlIm_.end(), 0.0);
    for (auto& s : slots_)
    {
        std::fill(s.headOut.begin(), s.headOut.end(), 0.0);
        std::fill(s.tailOut.begin(), s.tailOut.end(), 0.0);
    }
}

void PartitionedConvolver::setKernel(const ConvolutionKernel* kernel) noexcept
{
    if (current_ == nullptr && kernel != nullptr)
        reset();
    current_ = kernel;
    next_ = nullptr;
    nextStarted_ = false;
    fadePos_ = 0;
    if (kernel == nullptr)
        for (auto& s : slots_)
        {
            std::fill(s.headOut.begin(), s.headOut.end(), 0.0);
            std::fill(s.tailOut.begin(), s.tailOut.end(), 0.0);
        }
}

bool PartitionedConvolver::crossfadeTo(const ConvolutionKernel* next) noexcept
{
    if (next_ != nullptr)
        return false;
    if (current_ == nullptr)
    {
        setKernel(next);
        return true;
    }
    next_ = next;
    nextStarted_ = false;
    fadePos_ = 0;
    return true;
}

namespace {

// acc += x * k over `bins` complex values (split form).
inline void multiplyAdd(double* __restrict accRe, double* __restrict accIm, const double* __restrict xr,
                        const double* __restrict xi, const double* __restrict kr, const double* __restrict ki,
                        std::size_t bins) noexcept
{
    for (std::size_t i = 0; i < bins; ++i)
    {
        accRe[i] += xr[i] * kr[i] - xi[i] * ki[i];
        accIm[i] += xr[i] * ki[i] + xi[i] * kr[i];
    }
}

} // namespace

void PartitionedConvolver::computeHead(const ConvolutionKernel* k, Slot& slot) noexcept
{
    const std::size_t B = layout_.head, P = layout_.headParts, nb = headBins_;
    for (std::size_t c = 0; c < channels_; ++c)
    {
        double* out = slot.headOut.data() + c * B;
        if (k == nullptr)
        {
            std::fill(out, out + B, 0.0);
            continue;
        }
        const std::size_t kc = c % k->channels_;
        std::fill(accRe_.begin(), accRe_.begin() + static_cast<std::ptrdiff_t>(nb), 0.0);
        std::fill(accIm_.begin(), accIm_.begin() + static_cast<std::ptrdiff_t>(nb), 0.0);
        const double* fr = headFdlRe_.data() + c * P * nb;
        const double* fi = headFdlIm_.data() + c * P * nb;
        for (std::size_t p = 0; p < P; ++p)
        {
            const std::size_t slotIndex = (headPos_ + P - p) % P;
            multiplyAdd(accRe_.data(), accIm_.data(), fr + slotIndex * nb, fi + slotIndex * nb,
                        k->headRe_[kc].data() + p * nb, k->headIm_[kc].data() + p * nb, nb);
        }
        headFft_->inverse(accRe_.data(), accIm_.data(), time_.data());
        std::copy(time_.begin() + static_cast<std::ptrdiff_t>(B), time_.begin() + static_cast<std::ptrdiff_t>(2 * B), out);
    }
}

void PartitionedConvolver::computeTail(const ConvolutionKernel* k, Slot& slot) noexcept
{
    const std::size_t T = layout_.tail(), C = tailCapacity_, nb = tailBins_;
    for (std::size_t c = 0; c < channels_; ++c)
    {
        double* out = slot.tailOut.data() + c * T;
        const std::size_t parts = k ? std::min(k->tailParts_, C) : 0;
        if (parts == 0)
        {
            std::fill(out, out + T, 0.0);
            continue;
        }
        const std::size_t kc = c % k->channels_;
        std::fill(accRe_.begin(), accRe_.end(), 0.0);
        std::fill(accIm_.begin(), accIm_.end(), 0.0);
        const double* fr = tailFdlRe_.data() + c * C * nb;
        const double* fi = tailFdlIm_.data() + c * C * nb;
        for (std::size_t p = 0; p < parts; ++p)
        {
            const std::size_t slotIndex = (tailPos_ + C - p) % C;
            multiplyAdd(accRe_.data(), accIm_.data(), fr + slotIndex * nb, fi + slotIndex * nb,
                        k->tailRe_[kc].data() + p * nb, k->tailIm_[kc].data() + p * nb, nb);
        }
        tailFft_->inverse(accRe_.data(), accIm_.data(), time_.data());
        std::copy(time_.begin() + static_cast<std::ptrdiff_t>(T), time_.begin() + static_cast<std::ptrdiff_t>(2 * T), out);
    }
}

// A head block is complete: its spectrum goes into the delay line, and the
// next head block's output is computed.
void PartitionedConvolver::runHead() noexcept
{
    const std::size_t B = layout_.head, P = layout_.headParts, nb = headBins_;
    headPos_ = (headPos_ + 1) % P;
    for (std::size_t c = 0; c < channels_; ++c)
    {
        double* in = headIn_.data() + c * 2 * B;
        headFft_->forward(in, headFdlRe_.data() + (c * P + headPos_) * nb, headFdlIm_.data() + (c * P + headPos_) * nb);
        std::copy(in + B, in + 2 * B, in); // this block becomes the previous one
    }
    computeHead(current_, slots_[0]);
    if (nextStarted_)
        computeHead(next_, slots_[1]);
}

void PartitionedConvolver::runTail() noexcept
{
    const std::size_t T = layout_.tail(), C = tailCapacity_, nb = tailBins_;
    if (C > 0)
    {
        tailPos_ = (tailPos_ + 1) % C;
        for (std::size_t c = 0; c < channels_; ++c)
        {
            double* in = tailIn_.data() + c * 2 * T;
            tailFft_->forward(in, tailFdlRe_.data() + (c * C + tailPos_) * nb,
                              tailFdlIm_.data() + (c * C + tailPos_) * nb);
            std::copy(in + T, in + 2 * T, in);
        }
    }
    computeTail(current_, slots_[0]);
    if (nextStarted_)
        computeTail(next_, slots_[1]);
}

void PartitionedConvolver::process(const double* in, double* out, std::size_t frames) noexcept
{
    if (channels_ == 0)
        return;
    const std::size_t B = layout_.head, T = layout_.tail(), ch = channels_;
    std::size_t done = 0;
    while (done < frames)
    {
        const std::size_t hpos = fill_ % B;
        const std::size_t n = std::min(frames - done, B - hpos);
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t f = done + i;
            const std::size_t h = hpos + i, t = fill_ + i;
            double w = 0.0;
            if (nextStarted_)
                w = 0.5 - 0.5 * std::cos(std::numbers::pi * (static_cast<double>(fadePos_ + i) + 0.5) / static_cast<double>(T));
            for (std::size_t c = 0; c < ch; ++c)
            {
                const double x = in[f * ch + c];
                headIn_[c * 2 * B + B + h] = x;
                tailIn_[c * 2 * T + T + t] = x;
                double y = slots_[0].headOut[c * B + h] + slots_[0].tailOut[c * T + t];
                if (nextStarted_)
                {
                    const double y1 = slots_[1].headOut[c * B + h] + slots_[1].tailOut[c * T + t];
                    y += w * (y1 - y);
                }
                out[f * ch + c] = y;
            }
        }
        if (nextStarted_)
            fadePos_ += n;
        fill_ += n;
        done += n;
        if (fill_ % B == 0)
        {
            const bool tailBoundary = fill_ == T;
            if (tailBoundary)
            {
                fill_ = 0;
                if (nextStarted_ && fadePos_ >= T)
                {
                    // The crossfade is over: the next kernel is the current one.
                    current_ = next_;
                    next_ = nullptr;
                    nextStarted_ = false;
                    fadePos_ = 0;
                    std::swap(slots_[0], slots_[1]);
                }
                if (next_ != nullptr && !nextStarted_)
                {
                    nextStarted_ = true;
                    fadePos_ = 0;
                }
            }
            runHead();
            if (tailBoundary)
                runTail();
        }
    }
}

} // namespace ap::engine
