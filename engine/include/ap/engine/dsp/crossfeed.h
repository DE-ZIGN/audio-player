#pragma once

// Crossfeed for headphones (task 9-5): the amp view's CROSSFEED switch.
//
// The method of bs2b (Boris Mikhaylov's Bauer stereophonic-to-binaural DSP,
// http://bs2b.sourceforge.net, MIT licence), written here from its published
// design: each side reaches the other through a first-order low-pass (lower
// by the level's "feed" at low frequencies), and the direct path gets a
// first-order high-shelf so the two balance. With headphones, each ear then
// also hears the other side's low frequencies, a little later in phase (as
// with loudspeakers), at a level that falls with frequency.
//
// What it does to a centred (mono) sound: 0 dB at low frequencies, -1.75 dB
// at 15 kHz (the default level, at 48 kHz) - measured and said, not hidden.
//
// Stereo only: other channel counts pass as they are. Switching on, off or
// the level crossfades over 10 ms. No latency.

#include <ap/engine/dsp/processing.h>

#include <array>
#include <atomic>
#include <optional>
#include <string_view>

namespace ap::engine {

enum class CrossfeedLevel
{
    Default, // 700 Hz, feed 4.5 dB (bs2b's default)
    Cmoy,    // 700 Hz, 6.0 dB (bs2b's "Chu Moy")
    Jmeier,  // 650 Hz, 9.5 dB (bs2b's "Jan Meier")
};

// "default", "cmoy", "jmeier": in the settings file, the status and the page.
const char* crossfeedLevelKey(CrossfeedLevel level) noexcept;
std::optional<CrossfeedLevel> crossfeedLevelFrom(std::string_view key) noexcept;

class CrossfeedStage final : public ProcessingStage
{
public:
    static constexpr double kFadeSeconds = 0.010;

    // Thread-safe.
    void setEnabled(bool on) noexcept { enabled_ = on; }
    bool enabled() const noexcept { return enabled_; }
    void setLevel(CrossfeedLevel l) noexcept { level_ = l; }
    CrossfeedLevel level() const noexcept { return level_; }
    // The level's numbers: the low-pass's cutoff, and how much lower the other
    // side's low frequencies come than the direct ones.
    double cutoffHz() const noexcept;
    double feedDb() const noexcept;

    std::string name() const override { return "crossfeed"; }
    void prepare(std::uint32_t sampleRate, std::uint16_t channels) override;
    // Off and faded out, or not stereo.
    bool isIdentity() const noexcept override { return channels_ != 2 || (!enabled_ && settled_); }
    std::string displayDetail() const override { return crossfeedLevelKey(level_); }
    void process(std::span<double> samples, std::uint16_t channels) noexcept override;

private:
    struct Filter
    {
        // bs2b's coefficients and state (per channel).
        double a0Lo = 0, b1Lo = 0, a0Hi = 1, a1Hi = 0, b1Hi = 0, gain = 1;
        std::array<double, 2> lo{}, hi{}, asis{};
        void design(CrossfeedLevel level, double sampleRate) noexcept;
        void run(const double in[2], double out[2]) noexcept;
    };

    std::atomic<bool> enabled_{false};
    std::atomic<CrossfeedLevel> level_{CrossfeedLevel::Default};
    std::atomic<std::uint16_t> channels_{0};
    std::atomic<bool> settled_{true}; // off and faded out (the playback thread sets it)

    // Playback thread only.
    double rate_ = 48000;
    Filter current_, next_;
    CrossfeedLevel designed_ = CrossfeedLevel::Default;
    std::uint32_t levelFade_ = 0; // frames left of the crossfade to next_
    std::uint32_t fadeFrames_ = 480;
    double mix_ = 0.0; // 0 = dry, 1 = crossfed
};

} // namespace ap::engine
