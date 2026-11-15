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
    return {128 * m, 16};
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
    const std::size_t B = layout.head, T = layout.tail(), P = layout.headParts();
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
    const std::size_t B = layout.head, T = layout.tail(), P = layout.headParts();
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
    accRe_.assign(headBins_, 0.0);
    accIm_.assign(headBins_, 0.0);
    time_.assign(2 * B, 0.0);
    tailWindow_.assign(channels * 2 * T, 0.0);
    tailAccRe_.assign(tailBins_, 0.0);
    tailAccIm_.assign(tailBins_, 0.0);
    tailTime_.assign(2 * T, 0.0);
    for (auto& s : slots_)
    {
        s.headOut.assign(channels * B, 0.0);
        s.tailOut[0].assign(channels * T, 0.0);
        s.tailOut[1].assign(channels * T, 0.0);
    }
    headPos_ = tailPos_ = fill_ = 0;
    validHead_ = validTail_ = 0;
    workKernel_[0] = workKernel_[1] = nullptr;
    workTotal_ = workDone_ = workBlocksLeft_ = 0;
    cursorChannel_ = cursorStep_ = 0;
    ready_ = 0;
    lastWork_ = 0;
    current_ = next_ = nullptr;
    phase_ = Phase::None;
    fadePos_ = 0;
}

void PartitionedConvolver::reset() noexcept
{
    // The delay lines are not cleared (megabytes at high rates): only the
    // blocks written since are used.
    for (auto* v : {&headIn_, &tailIn_, &tailWindow_})
        std::fill(v->begin(), v->end(), 0.0);
    validHead_ = validTail_ = 0;
    for (auto& s : slots_)
        for (auto* v : {&s.headOut, &s.tailOut[0], &s.tailOut[1]})
            std::fill(v->begin(), v->end(), 0.0);
    // The block's work, if any, starts again (from a silent window).
    workDone_ = 0;
    cursorChannel_ = cursorStep_ = 0;
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

void PartitionedConvolver::setKernel(const ConvolutionKernel* kernel) noexcept
{
    if (current_ == nullptr && kernel != nullptr)
        reset();
    current_ = kernel;
    next_ = nullptr;
    phase_ = Phase::None;
    fadePos_ = 0;
    if (kernel == nullptr)
        for (auto& s : slots_)
            for (auto* v : {&s.headOut, &s.tailOut[0], &s.tailOut[1]})
                std::fill(v->begin(), v->end(), 0.0);
    // The block being worked on is redone with this kernel (the old one may be gone).
    if (workBlocksLeft_ > 0)
    {
        workKernel_[0] = kernel;
        workKernel_[1] = nullptr;
        const std::size_t parts = kernel ? std::min(kernel->tailParts_, tailCapacity_) : 0;
        workTotal_ = channels_ * ((tailCapacity_ > 0 ? kFftCost : 0) + (kernel ? parts * kPartCost + kFftCost : 0));
        workDone_ = 0;
        cursorChannel_ = cursorStep_ = 0;
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
    phase_ = Phase::Pending;
    fadePos_ = 0;
    return true;
}

void PartitionedConvolver::computeHead(const ConvolutionKernel* k, Slot& slot) noexcept
{
    const std::size_t B = layout_.head, P = layout_.headParts(), nb = headBins_;
    for (std::size_t c = 0; c < channels_; ++c)
    {
        double* out = slot.headOut.data() + c * B;
        if (k == nullptr)
        {
            std::fill(out, out + B, 0.0);
            continue;
        }
        const std::size_t kc = c % k->channels_;
        std::fill(accRe_.begin(), accRe_.end(), 0.0);
        std::fill(accIm_.begin(), accIm_.end(), 0.0);
        const double* fr = headFdlRe_.data() + c * P * nb;
        const double* fi = headFdlIm_.data() + c * P * nb;
        for (std::size_t p = 0; p < std::min(P, validHead_); ++p)
        {
            const std::size_t slotIndex = (headPos_ + P - p) % P;
            multiplyAdd(accRe_.data(), accIm_.data(), fr + slotIndex * nb, fi + slotIndex * nb,
                        k->headRe_[kc].data() + p * nb, k->headIm_[kc].data() + p * nb, nb);
        }
        headFft_->inverse(accRe_.data(), accIm_.data(), time_.data());
        std::copy(time_.begin() + static_cast<std::ptrdiff_t>(B), time_.begin() + static_cast<std::ptrdiff_t>(2 * B), out);
    }
}

// A head block is complete: its spectrum goes into the delay line, and the
// next head block's output is computed.
void PartitionedConvolver::runHead() noexcept
{
    const std::size_t B = layout_.head, P = layout_.headParts(), nb = headBins_;
    headPos_ = (headPos_ + 1) % P;
    validHead_ = std::min(P, validHead_ + 1);
    for (std::size_t c = 0; c < channels_; ++c)
    {
        double* in = headIn_.data() + c * 2 * B;
        headFft_->forward(in, headFdlRe_.data() + (c * P + headPos_) * nb, headFdlIm_.data() + (c * P + headPos_) * nb);
        std::copy(in + B, in + 2 * B, in); // this block becomes the previous one
    }
    computeHead(current_, slots_[0]);
    if (phase_ == Phase::Fade)
        computeHead(next_, slots_[1]);
}

// A tail block is complete. The work for the previous one is done (it was
// finished at the last head block): it becomes the output played from now
// on, and the work for this one starts.
void PartitionedConvolver::tailBoundary() noexcept
{
    const std::size_t T = layout_.tail();
    tailWork(true); // nothing left normally

    if (phase_ == Phase::Fade && fadePos_ >= T)
    {
        // The crossfade is over: the next kernel is the current one.
        current_ = next_;
        next_ = nullptr;
        phase_ = Phase::None;
        fadePos_ = 0;
        std::swap(slots_[0], slots_[1]);
    }
    else if (phase_ == Phase::Warm)
    {
        phase_ = Phase::Fade; // next_'s tail is ready: its head starts now
        fadePos_ = 0;
    }
    else if (phase_ == Phase::Pending)
        phase_ = Phase::Warm;
    ready_ = 1 - ready_;

    const std::size_t C = tailCapacity_;
    if (C > 0)
        tailPos_ = (tailPos_ + 1) % C;
    validTail_ = std::min(C, validTail_ + 1); // this block's slot is written (per channel) before it is used
    for (std::size_t c = 0; c < channels_; ++c)
    {
        double* in = tailIn_.data() + c * 2 * T;
        std::copy(in, in + 2 * T, tailWindow_.data() + c * 2 * T);
        std::copy(in + T, in + 2 * T, in);
    }
    // What this block's tail is computed for: played after the next boundary.
    // During the fade only the next kernel is needed then (the current one is
    // dropped at that boundary).
    workKernel_[0] = phase_ == Phase::Fade ? nullptr : current_;
    workKernel_[1] = (phase_ == Phase::Warm || phase_ == Phase::Fade) ? next_ : nullptr;
    std::size_t perChannel = C > 0 ? kFftCost : 0;
    for (const auto* k : workKernel_)
        if (k != nullptr)
            perChannel += std::min(k->tailParts_, C) * kPartCost + kFftCost;
    workTotal_ = channels_ * perChannel;
    workDone_ = 0;
    workBlocksLeft_ = layout_.tailBlocks;
    cursorChannel_ = cursorStep_ = 0;
}

// Does a share of the tail's work: what is left, divided by the head blocks
// left in this tail block (all of it when `finish`).
void PartitionedConvolver::tailWork(bool finish) noexcept
{
    if (workBlocksLeft_ == 0)
        return;
    const std::size_t T = layout_.tail(), C = tailCapacity_, nb = tailBins_;
    const std::size_t left = workTotal_ > workDone_ ? workTotal_ - workDone_ : 0;
    const std::size_t budget = finish ? left : (left + workBlocksLeft_ - 1) / workBlocksLeft_;
    const std::size_t work = 1 - ready_;
    std::size_t spent = 0;
    while (cursorChannel_ < channels_ && (spent < budget || finish))
    {
        const std::size_t c = cursorChannel_;
        // The steps of one channel: [FFT], then for each slot in use: parts..., inverse FFT.
        std::size_t step = cursorStep_;
        if (step == 0)
        {
            if (C > 0)
            {
                tailFft_->forward(tailWindow_.data() + c * 2 * T, tailFdlRe_.data() + (c * C + tailPos_) * nb,
                                  tailFdlIm_.data() + (c * C + tailPos_) * nb);
                spent += kFftCost;
            }
            ++cursorStep_;
            continue;
        }
        step -= 1;
        bool advanced = false;
        for (std::size_t s = 0; s < 2 && !advanced; ++s)
        {
            const ConvolutionKernel* k = workKernel_[s];
            if (k == nullptr)
                continue; // no kernel: its output stays as it is (silence, or not played)
            // Only blocks written since the last reset (older ones count as silence).
            const std::size_t parts = std::min({k->tailParts_, C, validTail_});
            if (step < parts)
            {
                if (step == 0)
                {
                    std::fill(tailAccRe_.begin(), tailAccRe_.end(), 0.0);
                    std::fill(tailAccIm_.begin(), tailAccIm_.end(), 0.0);
                }
                const std::size_t kc = c % k->channels_;
                const std::size_t slotIndex = (tailPos_ + C - step) % C;
                multiplyAdd(tailAccRe_.data(), tailAccIm_.data(), tailFdlRe_.data() + (c * C + slotIndex) * nb,
                            tailFdlIm_.data() + (c * C + slotIndex) * nb, k->tailRe_[kc].data() + step * nb,
                            k->tailIm_[kc].data() + step * nb, nb);
                spent += kPartCost;
                advanced = true;
            }
            else if (step == parts)
            {
                double* out = slots_[s].tailOut[work].data() + c * T;
                if (parts == 0)
                    std::fill(out, out + T, 0.0);
                else
                {
                    tailFft_->inverse(tailAccRe_.data(), tailAccIm_.data(), tailTime_.data());
                    std::copy(tailTime_.begin() + static_cast<std::ptrdiff_t>(T), tailTime_.end(), out);
                }
                spent += kFftCost;
                advanced = true;
            }
            else
                step -= parts + 1;
        }
        if (advanced)
            ++cursorStep_;
        else
        {
            ++cursorChannel_; // this channel is done
            cursorStep_ = 0;
        }
    }
    workDone_ += spent;
    lastWork_ += spent;
    --workBlocksLeft_;
    if (finish)
        workBlocksLeft_ = 0;
}

void PartitionedConvolver::process(const double* in, double* out, std::size_t frames) noexcept
{
    lastWork_ = 0;
    if (channels_ == 0)
        return;
    const std::size_t B = layout_.head, T = layout_.tail(), ch = channels_;
    std::size_t done = 0;
    while (done < frames)
    {
        const std::size_t hpos = fill_ % B;
        const std::size_t n = std::min(frames - done, B - hpos);
        const bool fading = phase_ == Phase::Fade;
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t f = done + i;
            const std::size_t h = hpos + i, t = fill_ + i;
            double w = 0.0;
            if (fading)
                w = 0.5 - 0.5 * std::cos(std::numbers::pi * (static_cast<double>(fadePos_ + i) + 0.5) / static_cast<double>(T));
            for (std::size_t c = 0; c < ch; ++c)
            {
                const double x = in[f * ch + c];
                headIn_[c * 2 * B + B + h] = x;
                tailIn_[c * 2 * T + T + t] = x;
                double y = slots_[0].headOut[c * B + h] + slots_[0].tailOut[ready_][c * T + t];
                if (fading)
                {
                    const double y1 = slots_[1].headOut[c * B + h] + slots_[1].tailOut[ready_][c * T + t];
                    y += w * (y1 - y);
                }
                out[f * ch + c] = y;
            }
        }
        if (fading)
            fadePos_ += n;
        fill_ += n;
        done += n;
        if (fill_ % B == 0)
        {
            if (fill_ == T)
            {
                fill_ = 0;
                tailBoundary();
            }
            runHead();
            tailWork(workBlocksLeft_ == 1);
        }
    }
}

} // namespace ap::engine
