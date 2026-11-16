#include <ap/engine/output_rate.h>

#include <algorithm>
#include <tuple>

namespace ap::engine {

const char* outputRateModeKey(OutputRateMode mode) noexcept
{
    switch (mode)
    {
        case OutputRateMode::Track:    return "track";
        case OutputRateMode::Fixed:    return "fixed";
        case OutputRateMode::AtLeast:  return "atleast";
        case OutputRateMode::Multiple: return "multiple";
    }
    return "track";
}

std::optional<OutputRateMode> outputRateModeFrom(std::string_view key) noexcept
{
    for (const auto m : {OutputRateMode::Track, OutputRateMode::Fixed, OutputRateMode::AtLeast, OutputRateMode::Multiple})
        if (key == outputRateModeKey(m))
            return m;
    return std::nullopt;
}

bool isOutputRateChoiceRate(std::uint32_t rate) noexcept
{
    return std::find(std::begin(kOutputRateChoiceRates), std::end(kOutputRateChoiceRates), rate)
        != std::end(kOutputRateChoiceRates);
}

bool isOutputRateMultiple(std::uint32_t multiple) noexcept
{
    return multiple == 2 || multiple == 4 || multiple == 8;
}

namespace {

// One rate is the other times a power of two: the same family.
bool sameFamily(std::uint32_t a, std::uint32_t b) noexcept
{
    const auto hi = std::max(a, b), lo = std::min(a, b);
    if (lo == 0 || hi % lo != 0)
        return false;
    const auto k = hi / lo;
    return (k & (k - 1)) == 0;
}

constexpr std::uint32_t kKnownRates[] = {8000,   11025,  16000,  22050,  24000,  32000,  44100,
                                         48000,  88200,  96000,  176400, 192000, 352800, 384000,
                                         705600, 768000};

} // namespace

std::vector<std::uint32_t> outputRateCandidates(std::uint32_t sourceRate, const OutputRateChoice& choice)
{
    std::vector<std::uint32_t> out;
    auto add = [&out](std::uint64_t r) {
        if (r > 0 && r <= 0xFFFFFFFFu && std::find(out.begin(), out.end(), static_cast<std::uint32_t>(r)) == out.end())
            out.push_back(static_cast<std::uint32_t>(r));
    };
    switch (choice.mode)
    {
        case OutputRateMode::Track:
            add(sourceRate);
            break;
        case OutputRateMode::Fixed:
            add(choice.rate);
            break;
        case OutputRateMode::AtLeast:
            add(sourceRate < choice.rate ? choice.rate : sourceRate);
            break;
        case OutputRateMode::Multiple:
            for (std::uint32_t k = isOutputRateMultiple(choice.multiple) ? choice.multiple : 2; k >= 2; k /= 2)
                add(static_cast<std::uint64_t>(sourceRate) * k);
            break;
    }
    // When refused: the track's own rate, its family (higher first, then
    // lower, the nearest first), then the other rates (higher first).
    add(sourceRate);
    std::vector<std::uint32_t> rest(std::begin(kKnownRates), std::end(kKnownRates));
    std::sort(rest.begin(), rest.end(), [sourceRate](std::uint32_t a, std::uint32_t b) {
        auto key = [sourceRate](std::uint32_t r) {
            const bool up = r >= sourceRate;
            return std::tuple(sameFamily(r, sourceRate) ? 0 : 1, up ? 0 : 1,
                              up ? static_cast<std::int64_t>(r) : -static_cast<std::int64_t>(r));
        };
        return key(a) < key(b);
    });
    for (const auto r : rest)
        add(r);
    return out;
}

} // namespace ap::engine
