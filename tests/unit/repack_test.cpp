#include <ap/engine/repack.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace ap::engine;

namespace {

PcmFormat intFormat(std::uint16_t container, std::uint16_t valid)
{
    return PcmFormat{44100, 2, container, valid, SampleType::Int, 3};
}

std::vector<std::byte> bytes(std::initializer_list<int> v)
{
    std::vector<std::byte> out;
    for (int x : v)
        out.push_back(static_cast<std::byte>(x));
    return out;
}

} // namespace

TEST_CASE("Repack candidates are ordered: wider container first, then zero padding", "[engine][repack]")
{
    CHECK(repackCandidates(intFormat(24, 24)) == std::vector<PcmFormat>{intFormat(32, 24), intFormat(32, 32)});
    CHECK(repackCandidates(intFormat(16, 16))
          == std::vector<PcmFormat>{intFormat(24, 16), intFormat(32, 16), intFormat(24, 24), intFormat(32, 24),
                                    intFormat(32, 32)});
    CHECK(repackCandidates(intFormat(32, 32)).empty());
    CHECK(repackCandidates(intFormat(32, 24)).empty());

    PcmFormat f32{48000, 2, 32, 32, SampleType::Float, 3};
    CHECK(repackCandidates(f32).empty());
}

TEST_CASE("Repack candidates keep rate, channels and channel mask", "[engine][repack]")
{
    const PcmFormat src{352800, 6, 24, 24, SampleType::Int, 0x3F};
    for (const auto& c : repackCandidates(src))
    {
        CHECK(c.sampleRate == src.sampleRate);
        CHECK(c.channels == src.channels);
        CHECK(c.channelMask == src.channelMask);
        CHECK(c.type == SampleType::Int);
    }
}

TEST_CASE("classifyRepack names the kind of repack", "[engine][repack]")
{
    CHECK(classifyRepack(intFormat(24, 24), intFormat(24, 24)) == Repack::None);
    CHECK(classifyRepack(intFormat(24, 24), intFormat(32, 24)) == Repack::WiderContainer);
    CHECK(classifyRepack(intFormat(24, 24), intFormat(32, 32)) == Repack::ZeroPadded);
    CHECK(classifyRepack(intFormat(16, 16), intFormat(24, 24)) == Repack::ZeroPadded);
}

TEST_CASE("repackSamples left-justifies with zero low bytes", "[engine][repack]")
{
    // Two 24-bit samples: 0x030201 and 0xFEDCBA (negative).
    const auto src = bytes({0x01, 0x02, 0x03, 0xBA, 0xDC, 0xFE});
    std::vector<std::byte> dst(8, std::byte{0x55});
    repackSamples(src, 3, dst, 4, 2);
    CHECK(dst == bytes({0x00, 0x01, 0x02, 0x03, 0x00, 0xBA, 0xDC, 0xFE}));

    // 16 -> 32
    const auto src16 = bytes({0x34, 0x12});
    std::vector<std::byte> dst32(4);
    repackSamples(src16, 2, dst32, 4, 1);
    CHECK(dst32 == bytes({0x00, 0x00, 0x34, 0x12}));
}
