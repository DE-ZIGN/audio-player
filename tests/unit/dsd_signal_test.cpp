// The DSD test-signal generator: its files read back bit-exact through the
// engine's DSF/DFF readers, and the bit stream encodes the requested tone.

#include <ap/engine/dsd_source.h>
#include <ap/signalgen/dsd.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace ap::engine;
namespace sg = ap::signalgen;
using Catch::Matchers::WithinAbs;

namespace {

std::vector<std::uint8_t> readAll(DsdSource& s)
{
    std::vector<std::uint8_t> out;
    std::vector<std::byte> buf(4096 * s.format().channels);
    while (const auto n = s.read(buf))
    {
        const auto* p = reinterpret_cast<const std::uint8_t*>(buf.data());
        out.insert(out.end(), p, p + n * s.format().channels);
    }
    return out;
}

std::vector<std::uint8_t> interleave(const std::vector<std::vector<std::uint8_t>>& ch)
{
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < ch[0].size(); ++i)
        for (const auto& c : ch)
            out.push_back(c[i]);
    return out;
}

} // namespace

TEST_CASE("Generated DSF and DFF files read back bit-exact", "[signalgen][dsd]")
{
    const auto ch = sg::dsdSine(2822400, 2, 20000, 1000.0, -20.0);
    ap::test::TempDir dir;
    const auto dsfPath = dir.path() / "t.dsf";
    const auto dffPath = dir.path() / "t.dff";
    sg::writeBytes(dsfPath, sg::encodeDsf(ch, 2822400));
    sg::writeBytes(dffPath, sg::encodeDff(ch, 2822400));

    DsfFileSource dsf(dsfPath);
    DffFileSource dff(dffPath);
    CHECK(dsf.format().dsdRate == 2822400u);
    CHECK(dff.format().channels == 2);
    CHECK(readAll(dsf) == interleave(ch));
    CHECK(readAll(dff) == interleave(ch));
}

TEST_CASE("Generated DSD encodes a 1 kHz tone at the requested level", "[signalgen][dsd]")
{
    constexpr std::uint32_t rate = 2822400;
    const std::size_t bytes = rate / 8; // 1 s
    const auto ch = sg::dsdSine(rate, 1, bytes, 1000.0, -20.0);

    // +-1 per bit, correlated with the tone over exactly 1000 periods.
    std::complex<double> acc{};
    const double step = 2.0 * std::numbers::pi * 1000.0 / rate;
    for (std::size_t n = 0; n < bytes * 8; ++n)
    {
        const int bit = (ch[0][n / 8] >> (7 - n % 8)) & 1;
        acc += (bit ? 1.0 : -1.0) * std::polar(1.0, -step * static_cast<double>(n));
    }
    const double amplitude = 2.0 * std::abs(acc) / static_cast<double>(bytes * 8);
    // 0 dB = 50 % modulation, so -20 dB is an amplitude of 0.05.
    CHECK_THAT(20.0 * std::log10(amplitude / 0.5), WithinAbs(-20.0, 0.5));
}
