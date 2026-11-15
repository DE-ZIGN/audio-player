#pragma once

// Partitioned FFT convolution (task 9-6) in 64-bit floating point: the space
// stage's IR, and the part the linear-phase EQ will use later (eq.h).
//
// Uniformly partitioned overlap-save in two levels, so a long kernel costs
// little and the latency stays short:
//   - the head: partitions of `head` frames (the latency), for the kernel's
//     first `head * headParts()` frames, headParts() = 2 * tailBlocks - 1;
//   - the tail: partitions of `tail()` = head * tailBlocks frames for the rest.
//     The tail's part of the output is needed two tail blocks after its input
//     block began (the head's latency plus the frames the head covers), so
//     the tail's work for one block - the FFT, the multiply-accumulate over
//     its partitions, the inverse FFT - is spread over the tailBlocks head
//     blocks of the following tail block, a share at each head block. No
//     single call does a whole tail block's work: at 192 kHz with a 3 s IR a
//     call costs at most about a tenth of what the whole block would (the
//     device's buffer can be 1.33 ms; task 9-6, crackles at 192 kHz).
// The output is the input convolved with the kernel, delayed by `head` frames:
//   out[t] = sum_j kernel[j] * in[t - head - j]
// (a user that wants no delay puts `head` zeros in front of its kernel and
// gives the convolver the rest; see SpaceStage).
//
// The input's spectra (the frequency-domain delay line) are kept apart from
// the kernel, so the kernel can be replaced while it runs: the new kernel's
// tail is computed alongside for one tail block, then the output crossfades
// from the old kernel to the new over one tail block, with the same input
// history. Nothing is cut off and nothing has to build up again.
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
    std::size_t head = 128;      // frames: the latency and the head's partition size (a power of two)
    std::size_t tailBlocks = 16; // head blocks per tail partition (a power of two, at least 2)

    std::size_t tail() const noexcept { return head * tailBlocks; }
    std::size_t headParts() const noexcept { return 2 * tailBlocks - 1; }
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
    // The cost units of the tail's work (for tests): an FFT or an inverse
    // FFT, and one partition's multiply-accumulate.
    static constexpr std::size_t kFftCost = 4;
    static constexpr std::size_t kPartCost = 1;

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
    // Replaces the kernel with a crossfade: from the next tail-block boundary
    // the new kernel's tail is computed alongside for one tail block, then the
    // output crossfades to it over one tail block. False (nothing done) while
    // a crossfade is pending or running.
    bool crossfadeTo(const ConvolutionKernel* next) noexcept;
    bool crossfading() const noexcept { return next_ != nullptr; }
    const ConvolutionKernel* kernel() const noexcept { return current_; }

    // in, out: `frames` interleaved frames of the prepared channel count. out
    // receives only the convolved signal. in and out may be the same buffer.
    void process(const double* in, double* out, std::size_t frames) noexcept;

    // The tail's work done in the last process() call, in cost units (tests).
    std::size_t lastTailWork() const noexcept { return lastWork_; }

private:
    enum class Phase
    {
        None,    // one kernel
        Pending, // next_ waits for a tail-block boundary
        Warm,    // next_'s tail is being computed alongside
        Fade,    // the output crossfades to next_
    };
    struct Slot // one kernel's output, per channel
    {
        std::vector<double> headOut;    // head frames per channel
        std::vector<double> tailOut[2]; // tail frames per channel: [ready_] is played, the other is being computed
    };
    void runHead() noexcept;
    void tailBoundary() noexcept;
    void tailWork(bool finish) noexcept;
    void computeHead(const ConvolutionKernel* k, Slot& slot) noexcept;

    ConvolverLayout layout_;
    std::size_t channels_ = 0, headBins_ = 0, tailBins_ = 0, tailCapacity_ = 0;
    std::unique_ptr<RealFft> headFft_, tailFft_;
    // Per channel: the last two blocks of input (overlap-save) and the delay
    // lines of spectra (ring buffers, newest at *Pos_).
    std::vector<double> headIn_, tailIn_;
    std::vector<double> headFdlRe_, headFdlIm_, tailFdlRe_, tailFdlIm_;
    std::size_t headPos_ = 0, tailPos_ = 0;
    std::size_t validHead_ = 0, validTail_ = 0; // delay-line slots written since the last reset
    std::size_t fill_ = 0; // frames of the current tail block received so far
    std::vector<double> accRe_, accIm_, time_; // the head's scratch

    // The tail's work for the block that last completed, done a share at a time.
    std::vector<double> tailWindow_;                   // per channel: the 2 * tail input frames to transform
    std::vector<double> tailAccRe_, tailAccIm_, tailTime_; // the tail's scratch
    const ConvolutionKernel* workKernel_[2] = {nullptr, nullptr}; // per slot, for this block's work
    std::size_t workTotal_ = 0, workDone_ = 0;  // cost units
    std::size_t workBlocksLeft_ = 0;            // head blocks left in this tail block
    std::size_t cursorChannel_ = 0, cursorStep_ = 0; // step: 0 FFT, then per slot: parts..., inverse FFT
    std::size_t ready_ = 0;
    std::size_t lastWork_ = 0;

    const ConvolutionKernel* current_ = nullptr;
    const ConvolutionKernel* next_ = nullptr;
    Phase phase_ = Phase::None;
    std::size_t fadePos_ = 0;
    Slot slots_[2]; // [0] current, [1] next
};

} // namespace ap::engine
