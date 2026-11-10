// Task 3-2: TPDF dither, applied only when quantizing to an integer format.
//
// With TPDF dither of +-1 LSB, the total quantization error has mean 0 and
// variance 1/4 LSB^2 (dither 1/6 + rounding 1/12) whatever the input value,
// so the error does not depend on the signal. Without dither both depend on
// where the input lies between two codes.

#include <ap/engine/dsp/dither.h>
#include <ap/engine/dsp/sample_convert.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstring>
#include <vector>

using namespace ap::engine;
using namespace ap::engine::dsp;

namespace {

PcmFormat int16()
{
    return PcmFormat{48000, 2, 16, 16, SampleType::Int, 0};
}

struct Moments
{
    double mean = 0;
    double variance = 0;
    double maxAbs = 0;
};

// Quantizes `n` copies of `lsbValue` (in 16-bit LSB units) and returns the
// moments of the error (output code - input), in LSB.
Moments errorOf(double lsbValue, std::size_t n, TpdfDither* dither)
{
    const double x = lsbValue / 32768.0;
    std::vector<double> src(n, x);
    std::vector<std::byte> out(n * 2);
    fromDouble(src, int16(), out, dither);
    std::vector<std::int16_t> codes(n);
    std::memcpy(codes.data(), out.data(), out.size());
    Moments m;
    for (auto c : codes)
    {
        const double e = c - lsbValue;
        m.mean += e;
        m.variance += e * e;
        m.maxAbs = std::max(m.maxAbs, std::abs(e));
    }
    m.mean /= static_cast<double>(n);
    m.variance = m.variance / static_cast<double>(n) - m.mean * m.mean;
    return m;
}

} // namespace

TEST_CASE("TPDF dither values lie in (-1, 1) with a triangular distribution", "[dsp][dither]")
{
    TpdfDither d;
    constexpr int n = 1'000'000;
    int bins[4] = {}; // (-1,-0.5) [-0.5,0) [0,0.5) [0.5,1)
    double sum = 0, sum2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double v = d.next();
        REQUIRE(v > -1.0);
        REQUIRE(v < 1.0);
        sum += v;
        sum2 += v * v;
        ++bins[v < -0.5 ? 0 : v < 0.0 ? 1 : v < 0.5 ? 2 : 3];
    }
    CHECK(std::abs(sum / n) < 0.005);
    CHECK(std::abs(sum2 / n - 1.0 / 6.0) < 0.002); // variance of TPDF on (-1, 1)
    // Triangular: the outer halves hold 1/8 each, the inner halves 3/8 each.
    CHECK(std::abs(bins[0] / double(n) - 0.125) < 0.003);
    CHECK(std::abs(bins[1] / double(n) - 0.375) < 0.003);
    CHECK(std::abs(bins[2] / double(n) - 0.375) < 0.003);
    CHECK(std::abs(bins[3] / double(n) - 0.125) < 0.003);
}

TEST_CASE("Dithered quantization error: mean 0 and variance 1/4 LSB^2 whatever the input", "[dsp][dither]")
{
    const double fraction = GENERATE(0.0, 0.25, 0.5, 0.75, 0.9);
    CAPTURE(fraction);
    TpdfDither d;
    const auto m = errorOf(100.0 + fraction, 400'000, &d);
    CHECK(std::abs(m.mean) < 0.005);
    CHECK(std::abs(m.variance - 0.25) < 0.005);
    CHECK(m.maxAbs <= 1.5 + 1e-9); // at most one code beyond plain rounding
}

TEST_CASE("Without dither the error depends on the input (the reason for dither)", "[dsp][dither]")
{
    const auto onCode = errorOf(100.0, 1000, nullptr);
    const auto quarter = errorOf(100.25, 1000, nullptr);
    CHECK(onCode.mean == 0.0);
    CHECK(quarter.mean == -0.25); // always rounds down: a constant error correlated with the signal
    CHECK(quarter.variance == 0.0);
}

TEST_CASE("Dither does not make full-scale samples count as overs", "[dsp][dither]")
{
    TpdfDither d;
    std::vector<double> src(10000, 32767.0 / 32768.0); // exactly the largest code
    std::vector<std::byte> out(src.size() * 2);
    CHECK(fromDouble(src, int16(), out, &d) == 0u);
    std::vector<std::int16_t> codes(src.size());
    std::memcpy(codes.data(), out.data(), out.size());
    for (auto c : codes)
        CHECK(c >= 32766);
}

TEST_CASE("Dither is at the output's own resolution", "[dsp][dither]")
{
    // 24-bit in a 32-bit container: the dither is +-1 LSB of 24 bits, the low byte stays zero.
    const PcmFormat f{48000, 2, 32, 24, SampleType::Int, 0};
    TpdfDither d;
    std::vector<double> src(1000, 0.1234567);
    std::vector<std::byte> out(src.size() * 4);
    fromDouble(src, f, out, &d);
    std::vector<std::int32_t> v(src.size());
    std::memcpy(v.data(), out.data(), out.size());
    const double exact = 0.1234567 * 8388608.0;
    for (auto x : v)
    {
        CHECK((x & 0xFF) == 0);
        CHECK(std::abs((x >> 8) - exact) <= 1.5);
    }
}

TEST_CASE("Float output is never dithered", "[dsp][dither]")
{
    const PcmFormat f{48000, 2, 32, 32, SampleType::Float, 0};
    TpdfDither d;
    std::vector<double> src{0.5, -0.25, 0.1};
    std::vector<std::byte> out(src.size() * 4);
    fromDouble(src, f, out, &d);
    std::vector<float> v(src.size());
    std::memcpy(v.data(), out.data(), out.size());
    CHECK(v == std::vector<float>{0.5f, -0.25f, 0.1f});
}

TEST_CASE("The same seed gives the same dither", "[dsp][dither]")
{
    TpdfDither a(42), b(42), c(43);
    bool differs = false;
    for (int i = 0; i < 100; ++i)
    {
        const double x = a.next();
        CHECK(x == b.next());
        differs = differs || x != c.next();
    }
    CHECK(differs);
}
