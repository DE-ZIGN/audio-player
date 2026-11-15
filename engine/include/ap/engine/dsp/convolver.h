#pragma once

// Partitioned FFT convolution (task 9-6) in 64-bit floating point: the space
// stage's IR, and the part the linear-phase EQ will use later (eq.h).
//
// Uniformly partitioned overlap-save in two levels, so a long kernel costs
// little and the latency stays short:
//   - the head: partitions of `head` frames (the latency), for the kernel's
//     first `head * headParts` frames;
//   - the tail: partitions of `tail` = head * (headParts + 1) frames for the
//     rest. Its own latency (one tail block) is exactly the head's latency
//     plus the frames the head covers, so it needs no extra delay.
// The output is the input convolved with the kernel, delayed by `head` frames:
//   out[t] = sum_j kernel[j] * in[t - head - j]
// (a user that wants no delay puts `head` zeros in front of its kernel and
// gives the convolver the rest; see SpaceStage).
//
// The input's spectra (the frequency-domain delay line) are kept apart from
// the kernel, so the kernel can be replaced while it runs: the new kernel
// starts at a tail-block boundary with the same input history, and the output
// crossfades from the old kernel to the new over one tail block. Nothing is
// cut off and nothing has to build up again.
//
// The kernels are made ahead (ConvolutionKernel::make allocates and runs
// FFTs; not for the playback thread). process() does not allocate.

#include <ap/engine/dsp/fft.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace ap::engine {

struct ConvolverLayout
{
    std::size_t head = 128;    // frames: the latency and the head's partition size (a power of two)
    std::size_t headParts = 15; // the tail's partition size is head * (headParts + 1), a power of two

    std::size_t tail() const noexcept { return head * (headParts + 1); }
    bool operator==(const ConvolverLayout&) const = default;

    // 128 frames and 2048-frame tail partitions at 44.1 and 48 kHz, scaled with
    // the rate (twice at 88.2/96 kHz, ...), so the latency stays about 2.7-2.9 ms.
    static ConvolverLayout forRate(double sampleRate) noexcept;
};

// A kernel's spectra, ready for the convolver (immutable once made).
class ConvolutionKernel
{
public:
    // taps: one vector per kernel channel (input channel c uses kernel channel
    // c % channels). Throws std::invalid_argument if there are none.
    static std::shared_ptr<const ConvolutionKernel> make(const ConvolverLayout& layout,
                                                         const std::vector<std::vector<double>>& taps);

    const ConvolverLayout& layout() const noexcept { return layout_; }
    std::size_t channels() const noexcept { return channels_; }
    std::size_t frames() const noexcept { return frames_; }   // the longest channel's taps
    std::size_t tailParts() const noexcept { return tailParts_; }

private:
    friend class PartitionedConvolver;
    ConvolverLayout layout_;
    std::size_t channels_ = 0, frames_ = 0, tailParts_ = 0;
    // Per channel, partition after partition, split spectra (scaled for the
    // unscaled inverse FFT).
    std::vector<std::vector<double>> headRe_, headIm_, tailRe_, tailIm_;
};

class PartitionedConvolver
{
public:
    // Allocates for `channels` and kernels up to `maxKernelFrames` (longer
    // kernels use only their first maxKernelFrames frames). Clears the history.
    void prepare(const ConvolverLayout& layout, std::size_t channels, std::size_t maxKernelFrames);
    const ConvolverLayout& layout() const noexcept { return layout_; }
    std::size_t latencyFrames() const noexcept { return layout_.head; }

    // Forgets the input (as if it had been silence). No allocation.
    void reset() noexcept;

    // Uses `kernel` from now on, at once (no crossfade); nullptr gives silence.
    // Clears the history when there was no kernel. The kernel must have the
    // prepared layout and stay alive while it is in use; it is in use until
    // kernel() no longer returns it.
    void setKernel(const ConvolutionKernel* kernel) noexcept;
    // Replaces the kernel with a crossfade: `next` takes over at the next tail-
    // block boundary and the output crossfades to it over one tail block.
    // False (nothing done) while a crossfade is pending or running.
    bool crossfadeTo(const ConvolutionKernel* next) noexcept;
    bool crossfading() const noexcept { return next_ != nullptr; }
    const ConvolutionKernel* kernel() const noexcept { return current_; }

    // in, out: `frames` interleaved frames of the prepared channel count. out
    // receives only the convolved signal. in and out may be the same buffer.
    void process(const double* in, double* out, std::size_t frames) noexcept;

private:
    struct Slot // one kernel's output, per channel
    {
        std::vector<double> headOut, tailOut; // head and tail frames per channel
    };
    void runHead() noexcept;
    void runTail() noexcept;
    void computeHead(const ConvolutionKernel* k, Slot& slot) noexcept;
    void computeTail(const ConvolutionKernel* k, Slot& slot) noexcept;

    ConvolverLayout layout_;
    std::size_t channels_ = 0, headBins_ = 0, tailBins_ = 0, tailCapacity_ = 0;
    std::unique_ptr<RealFft> headFft_, tailFft_;
    // Per channel: the last two blocks of input (overlap-save) and the delay
    // lines of spectra (ring buffers, newest at *Pos_).
    std::vector<double> headIn_, tailIn_;
    std::vector<double> headFdlRe_, headFdlIm_, tailFdlRe_, tailFdlIm_;
    std::size_t headPos_ = 0, tailPos_ = 0;
    std::size_t fill_ = 0; // frames of the current tail block received so far
    std::vector<double> accRe_, accIm_, time_; // scratch
    const ConvolutionKernel* current_ = nullptr;
    const ConvolutionKernel* next_ = nullptr;
    bool nextStarted_ = false;
    std::size_t fadePos_ = 0;
    Slot slots_[2]; // [0] current, [1] next
};

} // namespace ap::engine
