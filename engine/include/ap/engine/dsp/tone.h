#pragma once

// The tone controls (task 9-2): BASS and TREBLE, as on the amp view's panel.
// Two gentle shelves (the Cookbook's low and high shelf, Q 0.5) at fixed
// frequencies, separate from the parametric EQ, run by an EQ stage of their
// own: the same automatic preamp (the largest boost is taken off, so the tone
// controls never lift anything above 0 dB), the same 10 ms crossfade when a
// control moves, and both at 0 dB is the identity (the direct path).

#include <ap/engine/dsp/eq.h>

#include <atomic>
#include <mutex>

namespace ap::engine {

class ToneStage final : public ProcessingStage
{
public:
    static constexpr double kMaxDb = 10.0;
    static constexpr double kBassHz = 150.0;    // the low shelf's midpoint
    static constexpr double kTrebleHz = 4500.0; // the high shelf's midpoint
    static constexpr double kShelfQ = 0.5;

    // Thread-safe; clamped to +-kMaxDb (not a number: 0).
    void setBassDb(double db);
    void setTrebleDb(double db);
    double bassDb() const noexcept { return bass_; }
    double trebleDb() const noexcept { return treble_; }
    std::vector<EqBand> bands() const;
    // The preamp in use (dB), at the last prepared sample rate.
    double preampDb() const noexcept { return eq_.preampDb(); }

    std::string name() const override { return "tone"; }
    void prepare(std::uint32_t sampleRate, std::uint16_t channels) override { eq_.prepare(sampleRate, channels); }
    bool isIdentity() const noexcept override { return eq_.isIdentity(); }
    std::optional<double> displayGainDb() const noexcept override { return eq_.preampDb(); }
    void process(std::span<double> samples, std::uint16_t channels) noexcept override { eq_.process(samples, channels); }

private:
    void update();

    std::mutex mutex_; // the setters
    std::atomic<double> bass_{0.0}, treble_{0.0};
    EqStage eq_;
};

} // namespace ap::engine
