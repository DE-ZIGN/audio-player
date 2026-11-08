// ASIO sample types: only value-preserving mappings are accepted, and the
// per-channel writes keep every value.

#include <ap/engine/output/asio_sample_types.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

using namespace ap::engine;
namespace a = ap::engine::asio;

namespace {

PcmFormat intFmt(std::uint16_t channels, std::uint16_t container, std::uint16_t valid)
{
    return PcmFormat{48000, channels, container, valid, SampleType::Int, 0};
}

PcmFormat floatFmt(std::uint16_t channels, std::uint16_t bits)
{
    return PcmFormat{48000, channels, bits, bits, SampleType::Float, 0};
}

std::vector<std::byte> bytes(std::initializer_list<int> v)
{
    std::vector<std::byte> out;
    for (int x : v)
        out.push_back(static_cast<std::byte>(x));
    return out;
}

} // namespace

TEST_CASE("ASIO sample types accept only value-preserving layouts", "[engine][asio]")
{
    CHECK(a::accepts(a::Int16LSB, intFmt(2, 16, 16)));
    CHECK_FALSE(a::accepts(a::Int16LSB, intFmt(2, 24, 24)));

    CHECK(a::accepts(a::Int24LSB, intFmt(2, 24, 24)));
    CHECK(a::accepts(a::Int24LSB, intFmt(2, 24, 16))); // 16 bits left-justified, low byte zero
    CHECK_FALSE(a::accepts(a::Int24LSB, intFmt(2, 16, 16)));

    CHECK(a::accepts(a::Int32LSB, intFmt(2, 32, 32)));
    CHECK(a::accepts(a::Int32LSB, intFmt(2, 32, 24)));
    CHECK(a::accepts(a::Int32LSB, intFmt(2, 32, 16)));
    CHECK_FALSE(a::accepts(a::Int32LSB, intFmt(2, 24, 24))); // needs the 24-in-32 repack first

    CHECK(a::accepts(a::Int32LSB24, intFmt(2, 24, 24)));
    CHECK(a::accepts(a::Int32LSB16, intFmt(2, 16, 16)));

    CHECK(a::accepts(a::Float32LSB, floatFmt(2, 32)));
    CHECK(a::accepts(a::Float64LSB, floatFmt(2, 64)));
}

TEST_CASE("Integer and float are never mixed", "[engine][asio]")
{
    CHECK_FALSE(a::accepts(a::Float32LSB, intFmt(2, 32, 32)));
    CHECK_FALSE(a::accepts(a::Float32LSB, intFmt(2, 24, 24)));
    CHECK_FALSE(a::accepts(a::Int32LSB, floatFmt(2, 32)));
    CHECK_FALSE(a::accepts(a::Float64LSB, floatFmt(2, 32)));
}

TEST_CASE("Big-endian and unknown types are refused", "[engine][asio]")
{
    CHECK_FALSE(a::accepts(0 /* ASIOSTInt16MSB */, intFmt(2, 16, 16)));
    CHECK_FALSE(a::accepts(2 /* ASIOSTInt32MSB */, intFmt(2, 32, 32)));
    CHECK(a::bytesPerSample(0) == 0);
    CHECK(a::bytesPerSample(a::Int24LSB) == 3);
    CHECK(a::bytesPerSample(a::Int32LSB24) == 4);
}

TEST_CASE("De-interleaving copies each channel's bytes unchanged", "[engine][asio]")
{
    // Two stereo frames of packed 24-bit: L0 R0 L1 R1.
    const auto src = bytes({0x01, 0x02, 0x03, 0x11, 0x12, 0x13, 0x21, 0x22, 0x23, 0x31, 0x32, 0x33});
    std::vector<std::byte> left(6), right(6);
    void* channels[] = {left.data(), right.data()};
    a::deinterleave(a::Int24LSB, intFmt(2, 24, 24), src, 2, channels);
    CHECK(left == bytes({0x01, 0x02, 0x03, 0x21, 0x22, 0x23}));
    CHECK(right == bytes({0x11, 0x12, 0x13, 0x31, 0x32, 0x33}));
}

TEST_CASE("Int32LSB takes 24-in-32 slots as they are", "[engine][asio]")
{
    const auto src = bytes({0x00, 0x01, 0x02, 0x03, 0x00, 0xBA, 0xDC, 0xFE});
    std::vector<std::byte> left(4), right(4);
    void* channels[] = {left.data(), right.data()};
    a::deinterleave(a::Int32LSB, intFmt(2, 32, 24), src, 1, channels);
    CHECK(left == bytes({0x00, 0x01, 0x02, 0x03}));
    CHECK(right == bytes({0x00, 0xBA, 0xDC, 0xFE}));
}

TEST_CASE("Right-aligned 32-bit types are sign-extended, keeping the value", "[engine][asio]")
{
    // 24-bit samples 0x030201 (positive) and 0xFEDCBA (negative).
    const auto src = bytes({0x01, 0x02, 0x03, 0xBA, 0xDC, 0xFE});
    std::vector<std::byte> left(4), right(4);
    void* channels[] = {left.data(), right.data()};
    a::deinterleave(a::Int32LSB24, intFmt(2, 24, 24), src, 1, channels);
    CHECK(left == bytes({0x01, 0x02, 0x03, 0x00}));
    CHECK(right == bytes({0xBA, 0xDC, 0xFE, 0xFF}));

    // 16-bit into Int32LSB16.
    const auto src16 = bytes({0x34, 0x12, 0xCD, 0xAB});
    void* ch16[] = {left.data(), right.data()};
    a::deinterleave(a::Int32LSB16, intFmt(2, 16, 16), src16, 1, ch16);
    CHECK(left == bytes({0x34, 0x12, 0x00, 0x00}));
    CHECK(right == bytes({0xCD, 0xAB, 0xFF, 0xFF}));
}

TEST_CASE("Sample type names", "[engine][asio]")
{
    CHECK(a::sampleTypeName(a::Int32LSB) == "Int32LSB");
    CHECK(a::sampleTypeName(a::Float32LSB) == "Float32LSB");
    CHECK(a::sampleTypeName(99) == "type 99");
}
