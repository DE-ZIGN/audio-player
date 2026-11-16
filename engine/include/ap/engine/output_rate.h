#pragma once

// The output rate on the processing path (task 10-2, decided 2026-11-16).
// In 音作りモード only; 原音モード never changes the rate (CLAUDE.md
// chapter 4). The default is the track's own rate.
//   - Track:    the track's rate.
//   - Fixed:    every track at `rate` (higher ones lowered too).
//   - AtLeast:  tracks below `rate` raised to it; the others as they are.
//   - Multiple: the track's rate times `multiple` (2, 4, 8): the family
//               (44.1 or 48 kHz) stays.
// When the output refuses the rate wanted, the next ones are tried: for
// Multiple the smaller multiples, then (every mode) the track's own rate,
// then the rates of the track's family (higher first, then lower), then the
// other family (higher first). Nothing here claims a higher rate sounds better.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ap::engine {

enum class OutputRateMode
{
    Track,
    Fixed,
    AtLeast,
    Multiple,
};

struct OutputRateChoice
{
    OutputRateMode mode = OutputRateMode::Track;
    std::uint32_t rate = 96000;  // Fixed and AtLeast
    std::uint32_t multiple = 2;  // Multiple: 2, 4 or 8

    bool operator==(const OutputRateChoice&) const = default;
};

// "track", "fixed", "atleast", "multiple": in the settings file and the page.
const char* outputRateModeKey(OutputRateMode mode) noexcept;
std::optional<OutputRateMode> outputRateModeFrom(std::string_view key) noexcept;
// The rates offered for Fixed and AtLeast (44.1 to 768 kHz; 705.6 and 768 added
// in task 10-5), and the multiples.
bool isOutputRateChoiceRate(std::uint32_t rate) noexcept;
bool isOutputRateMultiple(std::uint32_t multiple) noexcept;
inline constexpr std::uint32_t kOutputRateChoiceRates[] = {44100,  48000,  88200,  96000, 176400,
                                                           192000, 352800, 384000, 705600, 768000};

// The rates to try for a track at `sourceRate`, the one wanted first.
std::vector<std::uint32_t> outputRateCandidates(std::uint32_t sourceRate, const OutputRateChoice& choice);

// A conversion on the processing path, for the path display.
struct Resampling
{
    enum class Reason
    {
        Setting,     // the output rate chosen in the settings
        Unsupported, // the output does not take the rate wanted
    };
    std::uint32_t from = 0, to = 0;
    Reason reason = Reason::Setting;

    bool operator==(const Resampling&) const = default;
};

} // namespace ap::engine
