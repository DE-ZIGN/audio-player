#include <ap/engine/dsp/dither.h>

namespace ap::engine::dsp {

namespace {
// splitmix64: spreads one seed over the four state words (never all zero).
std::uint64_t splitmix(std::uint64_t& x) noexcept
{
    std::uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
} // namespace

TpdfDither::TpdfDither(std::uint64_t seed) noexcept
{
    for (auto& s : s_)
        s = splitmix(seed);
}

} // namespace ap::engine::dsp
