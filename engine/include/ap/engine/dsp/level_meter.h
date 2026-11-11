#pragma once

// True Peak and loudness meter (task 4-4): what the meters on the output
// check screen show, measured on a running signal.
//
// - True Peak: the same 8x interpolator as the analysis (task 3-3), per
//   channel. "Recent" is the largest value of about the last 0.1 s (for a bar
//   that moves); "track max" is the largest since the track started.
// - Loudness (ITU-R BS.1770 / EBU R128, libebur128): momentary (400 ms) and
//   short-term (3 s) windows that slide across track boundaries, and the
//   integrated loudness (gated) since the track started. Mono is measured as
//   dual mono, as in the analysis (task 3-4).
//
// Measures only. Single-threaded: the caller feeds and reads it from one thread.

#include <cstdint>
#include <span>
#include <vector>

namespace ap::engine {

struct LevelReading
{
    std::uint16_t channels = 0;
    std::vector<double> truePeakRecent; // per channel, linear: about the last 0.1 s
    std::vector<double> truePeakMax;    // per channel, linear: since the track started
    double momentaryLufs = 0.0;         // -infinity for silence
    double shortTermLufs = 0.0;
    double integratedLufs = 0.0;        // since the track started; -infinity until something passes the gate
    std::uint64_t trackFrames = 0;      // measured since the track started
};

class LevelMeter
{
public:
    static constexpr double kRecentSeconds = 0.1;

    LevelMeter(std::uint32_t sampleRate, std::uint16_t channels);
    ~LevelMeter();
    LevelMeter(const LevelMeter&) = delete;
    LevelMeter& operator=(const LevelMeter&) = delete;

    std::uint32_t sampleRate() const noexcept { return rate_; }
    std::uint16_t channels() const noexcept { return channels_; }

    // Interleaved samples, full scale = +-1.0; samples.size() is a multiple of channels().
    void process(std::span<const double> samples);

    // The next sample is a new track's first: its maximum and integrated
    // loudness start from nothing. The momentary and short-term windows go on
    // (they hold what was just heard). Interpolated values that lie before
    // this point still count for the previous track, so they are not counted.
    void startTrack();

    // The signal stops here (the output closes): lets the interpolator run on
    // as if silence followed, so a peak in the last samples is not missed.
    void finish();

    LevelReading reading() const;

private:
    struct Channel
    {
        std::vector<double> history; // newest first, twice the window (always contiguous)
        std::size_t pos = 0;
        double recentPrev = 0.0; // the last complete block
        double recentNow = 0.0;  // the block being filled
        double trackMax = 0.0;
    };

    void push(Channel& c, double x) noexcept;
    void finishFrame() noexcept;

    std::uint32_t rate_ = 0;
    std::uint16_t channels_ = 0;
    std::vector<Channel> ch_;
    std::uint64_t blockFrames_ = 0; // frames in a "recent" block
    std::uint64_t blockFill_ = 0;
    // Interpolated values lag the input by the interpolator's delay; after
    // startTrack() this many frames still belong to the previous track.
    std::uint64_t carryOver_ = 0;
    std::uint64_t trackFrames_ = 0;
    std::vector<double> silence_; // one frame of zeros for finish()
    void* windows_ = nullptr; // ebur128_state, momentary and short-term (runs on)
    void* track_ = nullptr;   // ebur128_state, integrated (per track)
};

} // namespace ap::engine
