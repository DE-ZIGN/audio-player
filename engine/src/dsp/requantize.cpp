#include <ap/engine/dsp/requantize.h>

#include <algorithm>
#include <cmath>

namespace ap::engine {

void RequantizeStage::setBits(int bits) noexcept
{
    bits_ = bits <= 0 ? 0 : std::clamp(bits, 8, 24);
}

void RequantizeStage::process(std::span<double> samples, std::uint16_t) noexcept
{
    const int bits = bits_;
    if (bits == 0)
        return;
    const double scale = std::ldexp(1.0, bits - 1); // 2^(bits-1) codes per unit
    const double lo = -scale, hi = scale - 1.0;
    for (auto& x : samples)
    {
        const double code = std::nearbyint(x * scale + dither_.next());
        x = std::clamp(code, lo, hi) / scale;
    }
}

} // namespace ap::engine
