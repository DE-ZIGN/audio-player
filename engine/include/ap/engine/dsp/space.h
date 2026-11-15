#pragma once

// The space stage (task 9-6; docs/dsp-modeling-research.md, docs/ir-candidates.md):
// the input convolved with a room's impulse response (the "wet" signal), mixed
// with the input itself (the "dry" signal):
//   out = (1 - MIX) * dry + MIX * wet
// Nothing here is claimed to sound better; what it does is what is said.
//
//  - Rooms: OFF, CLUB, HALL, ARENA. Each is a built-in IR (room_ir.h), or an
//    IR file the user gave that button.
//  - The wet IR is normalised to unit energy (per channel, on average), so a
//    broadband signal comes out of the convolution at about its own level.
//  - Pre-delay: the wet signal starts this long after the dry one. Low cut:
//    a second-order Butterworth high-pass on the wet signal only (0: none).
//    Both are built into the kernel (the IR is filtered and shifted).
//  - Latency: none. The dry signal is not delayed; the convolution's own
//    latency (one head block, about 2.7-2.9 ms, ConvolverLayout) is taken out
//    of the pre-delay, so the wet signal starts exactly at the pre-delay. That
//    is why the pre-delay is at least the head block (effectivePreDelayMs).
//    The output is never reopened for the space: switching rooms or MIX does
//    not interrupt playback.
//  - Changes: a new room, IR, pre-delay or low cut is built on the calling
//    thread (a few ms; the IR is resampled to the output rate when it differs)
//    and the convolver crossfades to it over one tail block (about 43 ms),
//    with the same input history. MIX moves over 20 ms.
//  - Full bypass: OFF, or MIX 0, is identity once MIX has faded out, so the
//    output is the input bit for bit (the chain takes the direct path when
//    nothing else is on).

#include <ap/engine/dsp/convolver.h>
#include <ap/engine/dsp/processing.h>
#include <ap/engine/dsp/room_ir.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>

namespace ap::engine {

class SpaceStage final : public ProcessingStage
{
public:
    static constexpr double kDefaultMixPercent = 20.0;
    static constexpr double kMaxPreDelayMs = 100.0;
    static constexpr double kDefaultPreDelayMs = 10.0;
    static constexpr double kMinLowCutHz = 20.0; // 0: no low cut
    static constexpr double kMaxLowCutHz = 500.0;
    static constexpr double kDefaultLowCutHz = 120.0;
    static constexpr double kMixSeconds = 0.020;

    struct Params
    {
        SpaceRoom room = SpaceRoom::Off;
        double mixPercent = kDefaultMixPercent; // 0 .. 100
        double preDelayMs = kDefaultPreDelayMs; // 0 .. kMaxPreDelayMs (at least the latency is used)
        double lowCutHz = kDefaultLowCutHz;     // 0 (none) or kMinLowCutHz .. kMaxLowCutHz
        // The user's IR for CLUB, HALL, ARENA; nullptr: the built-in room.
        std::array<std::shared_ptr<const ImpulseResponse>, 3> user;

        bool operator==(const Params&) const = default;
    };

    SpaceStage();
    ~SpaceStage() override;

    // Thread-safe (not the playback thread): sets everything, building the
    // new kernel now when the room, an IR, the pre-delay or the low cut
    // changed and the stage has been prepared.
    void set(const Params& p);
    Params params() const;
    SpaceRoom room() const noexcept { return room_; }
    double mixPercent() const noexcept { return 100.0 * mixTarget_.load(); }

    // The pre-delay that applies at the prepared rate (at least the head block).
    double effectivePreDelayMs() const noexcept;

    // The wet signal's impulse response at `sampleRate` as the stage builds it
    // (normalised, low cut, from time 0: the pre-delay's zeros included), per
    // IR channel. Empty when the room is OFF. For the tests.
    std::vector<std::vector<double>> wetResponse(double sampleRate) const;

    std::string name() const override { return "space"; }
    void prepare(std::uint32_t sampleRate, std::uint16_t channels) override;
    bool isIdentity() const noexcept override { return mixTarget_ == 0.0 && settled_; }
    // "hall;20" (room; MIX %), with ";user" when the room's IR is the user's.
    std::string displayDetail() const override;
    void process(std::span<double> samples, std::uint16_t channels) noexcept override;

private:
    using KernelPtr = std::shared_ptr<const ConvolutionKernel>;
    static bool kernelChanged(const Params& a, const Params& b);
    std::vector<std::vector<double>> buildTaps(const Params& p, double rate, std::size_t latency) const;
    KernelPtr buildKernel(const Params& p, double rate) const;
    void retire(KernelPtr& k) noexcept;

    mutable std::mutex mutex_; // params_, pending_, trash_, rate_ (the playback thread only try-locks)
    mutable std::mutex buildMutex_; // the caches below
    Params params_;
    std::uint64_t generation_ = 0; // counts kernel changes
    KernelPtr pending_;
    std::array<KernelPtr, 8> trash_; // released on the setting thread
    double rate_ = 0.0;
    mutable std::shared_ptr<const ImpulseResponse> cacheSource_; // what the cache was made from
    mutable SpaceRoom cacheRoom_ = SpaceRoom::Off;
    mutable double cacheRate_ = 0.0;
    mutable ImpulseResponse cache_; // the room's IR at cacheRate_

    std::atomic<SpaceRoom> room_{SpaceRoom::Off};
    std::atomic<double> mixTarget_{0.0}; // 0 when OFF
    std::atomic<bool> settled_{true};    // MIX is at 0 and the stage need not run
    std::atomic<std::size_t> latency_{128};
    std::atomic<double> preparedRate_{48000.0};

    // Playback thread only.
    std::uint16_t channels_ = 0;
    PartitionedConvolver convolver_;
    KernelPtr current_, next_;
    std::array<KernelPtr, 8> retired_;
    std::vector<double> wet_;
    double mixNow_ = 0.0;
    double mixStep_ = 1.0 / 960.0;
};

} // namespace ap::engine
