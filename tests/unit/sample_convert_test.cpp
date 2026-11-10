// Task 3-1: conversion between the PCM formats and the 64-bit processing
// format. Every integer and float format must survive the round trip
// PCM -> double -> PCM unchanged, so a processing path whose stages leave the
// samples alone cannot alter them.

#include <ap/engine/dsp/sample_convert.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace ap::engine;
using ap::engine::dsp::fromDouble;
using ap::engine::dsp::toDouble;

namespace {

PcmFormat intFormat(std::uint16_t container, std::uint16_t valid)
{
    return PcmFormat{48000, 2, container, valid, SampleType::Int, 0};
}

PcmFormat floatFormat(std::uint16_t bits)
{
    return PcmFormat{48000, 2, bits, bits, SampleType::Float, 0};
}

// Little-endian bytes of left-justified integer codes: `code` holds the
// valid bits; the low (container - valid) bits of the slot are zero.
std::vector<std::byte> intBytes(const std::vector<std::int64_t>& codes, const PcmFormat& f)
{
    const unsigned bytes = f.containerBits / 8u;
    const unsigned shift = f.containerBits - f.validBits;
    std::vector<std::byte> out;
    for (auto c : codes)
    {
        const auto u = static_cast<std::uint64_t>(c) << shift;
        for (unsigned k = 0; k < bytes; ++k)
            out.push_back(static_cast<std::byte>(u >> (8 * k)));
    }
    return out;
}

// Codes that cover every bit: 0, each single bit (both signs), the extremes,
// and alternating patterns.
std::vector<std::int64_t> codesFor(unsigned valid)
{
    const std::int64_t max = (std::int64_t{1} << (valid - 1)) - 1;
    const std::int64_t min = -max - 1;
    std::vector<std::int64_t> c{0, 1, -1, max, min, max - 1, min + 1};
    for (unsigned b = 0; b + 1 < valid; ++b)
    {
        c.push_back(std::int64_t{1} << b);
        c.push_back(-(std::int64_t{1} << b));
    }
    std::int64_t alt = 0;
    for (unsigned b = 0; b + 1 < valid; b += 2)
        alt |= std::int64_t{1} << b;
    c.push_back(alt);
    c.push_back(-alt - 1);
    return c;
}

} // namespace

TEST_CASE("Integer PCM survives PCM -> double -> PCM unchanged", "[dsp][convert]")
{
    const auto [container, valid] = GENERATE(table<std::uint16_t, std::uint16_t>(
        {{16, 16}, {24, 24}, {32, 32}, {32, 24}, {24, 16}, {32, 16}, {24, 20}}));
    CAPTURE(container, valid);
    const auto f = intFormat(container, valid);
    const auto in = intBytes(codesFor(valid), f);
    const std::size_t samples = in.size() / f.bytesPerSample();

    std::vector<double> d(samples);
    toDouble(in, f, d);
    std::vector<std::byte> out(in.size());
    const auto overs = fromDouble(d, f, out);

    CHECK(out == in);
    CHECK(overs == 0u);
}

TEST_CASE("Integer full scale maps to +-1.0", "[dsp][convert]")
{
    const auto f = intFormat(24, 24);
    const auto in = intBytes({-(std::int64_t{1} << 23), (std::int64_t{1} << 22), 1}, f);
    std::vector<double> d(3);
    toDouble(in, f, d);
    CHECK(d[0] == -1.0);
    CHECK(d[1] == 0.5);
    CHECK(d[2] == std::ldexp(1.0, -23));

    // A 16-bit value in a 32-bit slot has the same value as in a 16-bit slot.
    const auto f16 = intFormat(16, 16);
    const auto f1632 = intFormat(32, 16);
    std::vector<double> a(1), b(1);
    toDouble(intBytes({12345}, f16), f16, a);
    toDouble(intBytes({12345}, f1632), f1632, b);
    CHECK(a[0] == b[0]);
}

TEST_CASE("Float PCM survives PCM -> double -> PCM unchanged", "[dsp][convert]")
{
    SECTION("32-bit float, including values beyond full scale and denormals")
    {
        const std::vector<float> v{0.0f, -0.0f, 1.0f, -1.0f, 1.26f, -3.5f, 1e-30f,
                                   std::numeric_limits<float>::denorm_min(), 0.1f, -0.333333f};
        std::vector<std::byte> in(v.size() * 4);
        std::memcpy(in.data(), v.data(), in.size());
        std::vector<double> d(v.size());
        toDouble(in, floatFormat(32), d);
        std::vector<std::byte> out(in.size());
        fromDouble(d, floatFormat(32), out);
        CHECK(out == in);
    }
    SECTION("64-bit float")
    {
        const std::vector<double> v{0.0, 1.0, -1.0, 0.1, 1.0 / 3.0, 2.5, 1e-300};
        std::vector<std::byte> in(v.size() * 8);
        std::memcpy(in.data(), v.data(), in.size());
        std::vector<double> d(v.size());
        toDouble(in, floatFormat(64), d);
        std::vector<std::byte> out(in.size());
        fromDouble(d, floatFormat(64), out);
        CHECK(out == in);
    }
}

TEST_CASE("Integer output rounds to the nearest code and clamps at full scale", "[dsp][convert]")
{
    const auto f = intFormat(16, 16);
    const double lsb = std::ldexp(1.0, -15);
    const std::vector<double> d{0.4 * lsb, 0.6 * lsb, -0.6 * lsb, 1.0, 1.5, -1.0, -2.0, 100.25 * lsb};
    std::vector<std::byte> out(d.size() * 2);
    const auto overs = fromDouble(d, f, out);

    std::vector<std::int16_t> v(d.size());
    std::memcpy(v.data(), out.data(), out.size());
    CHECK(v[0] == 0);
    CHECK(v[1] == 1);
    CHECK(v[2] == -1);
    CHECK(v[3] == 32767);  // +1.0 is one step above the largest code
    CHECK(v[4] == 32767);
    CHECK(v[5] == -32768); // -1.0 is the smallest code exactly
    CHECK(v[6] == -32768);
    CHECK(v[7] == 100);
    CHECK(overs == 3u); // +1.0, +1.5, -2.0
}

TEST_CASE("Integer output in a wider container keeps the low bits zero", "[dsp][convert]")
{
    const auto f = intFormat(32, 24);
    const std::vector<double> d{0.3, -0.7, std::ldexp(1.0, -30)}; // the last is below 24-bit resolution
    std::vector<std::byte> out(d.size() * 4);
    fromDouble(d, f, out);
    std::vector<std::int32_t> v(d.size());
    std::memcpy(v.data(), out.data(), out.size());
    for (auto x : v)
        CHECK((x & 0xFF) == 0);
    CHECK(v[2] == 0);
}

TEST_CASE("Float output passes values beyond full scale and counts them", "[dsp][convert]")
{
    const std::vector<double> d{0.5, 1.25, -1.0, -1.5, std::nan("")};
    std::vector<std::byte> out(d.size() * 4);
    const auto overs = fromDouble(d, floatFormat(32), out);
    std::vector<float> v(d.size());
    std::memcpy(v.data(), out.data(), out.size());
    CHECK(v[1] == 1.25f);
    CHECK(v[3] == -1.5f);
    CHECK(v[4] == 0.0f); // NaN never reaches the device
    CHECK(overs == 3u);  // 1.25, -1.5, NaN
}

TEST_CASE("NaN becomes silence in integer output", "[dsp][convert]")
{
    const std::vector<double> d{std::nan(""), std::numeric_limits<double>::infinity()};
    std::vector<std::byte> out(d.size() * 3);
    const auto overs = fromDouble(d, intFormat(24, 24), out);
    CHECK(out[0] == std::byte{0});
    CHECK(out[1] == std::byte{0});
    CHECK(out[2] == std::byte{0});
    CHECK(out[5] == std::byte{0x7F}); // +inf clamps to the largest code
    CHECK(overs == 2u);
}
