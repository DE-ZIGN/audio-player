// Task 1-1: unsupported combinations are refused with a reason (never
// converted). The fake devices copy what real devices reported in step S1
// (docs/phase1-tasks.md, "記録").

#include <ap/engine/output/format_probe.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace ap::engine;

namespace {

PcmFormat intFormat(std::uint32_t rate, std::uint16_t channels, std::uint16_t container, std::uint16_t valid)
{
    return PcmFormat{rate, channels, container, valid, SampleType::Int, 0};
}

PcmFormat floatFormat(std::uint32_t rate, std::uint16_t channels)
{
    return PcmFormat{rate, channels, 32, 32, SampleType::Float, 0};
}

bool isInt(const PcmFormat& f, std::uint16_t c, std::uint16_t v)
{
    return f.type == SampleType::Int && f.containerBits == c && f.validBits == v;
}

// SSL 2 MKII with its control panel at 96 kHz: 96 kHz stereo only;
// 16-bit, 24-in-32 and 32-bit, but not packed 24-bit.
const AcceptsFormat ssl96 = [](const PcmFormat& f) {
    return f.sampleRate == 96000 && f.channels == 2 && (isInt(f, 16, 16) || isInt(f, 32, 24) || isInt(f, 32, 32));
};

// HDMI monitor: 44.1 / 48 kHz, 16-bit stereo only.
const AcceptsFormat hdmi = [](const PcmFormat& f) {
    return (f.sampleRate == 44100 || f.sampleRate == 48000) && f.channels == 2 && isInt(f, 16, 16);
};

// A device that takes only 32-bit float at 44.1 kHz.
const AcceptsFormat floatOnly = [](const PcmFormat& f) {
    return f.sampleRate == 44100 && f.channels == 2 && f.type == SampleType::Float && f.containerBits == 32;
};

} // namespace

TEST_CASE("Standard rates cover 44.1 to 384 kHz", "[engine][format]")
{
    CHECK(standardSampleRates()
          == std::vector<std::uint32_t>{44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000});
    CHECK(standardIntLayouts(96000, 2).size() == 4);
}

TEST_CASE("Accepted rates are what the device takes now", "[engine][format]")
{
    CHECK(acceptedSampleRates(ssl96, 2) == std::vector<std::uint32_t>{96000});
    CHECK(acceptedSampleRates(hdmi, 2) == std::vector<std::uint32_t>{44100, 48000});
    CHECK(acceptedSampleRates(floatOnly, 2) == std::vector<std::uint32_t>{44100});
    CHECK(acceptedSampleRates(ssl96, 1).empty());
}

TEST_CASE("A rate the device does not run at is reported as SampleRate", "[engine][format]")
{
    CHECK(classifyRejection(intFormat(44100, 2, 24, 24), ssl96) == FormatRejection::SampleRate);
    CHECK(classifyRejection(intFormat(192000, 2, 32, 24), ssl96) == FormatRejection::SampleRate);
    CHECK(classifyRejection(intFormat(96000, 2, 16, 16), hdmi) == FormatRejection::SampleRate);
}

TEST_CASE("A layout the device does not take at a working rate is reported as BitDepth", "[engine][format]")
{
    CHECK(classifyRejection(intFormat(96000, 2, 24, 24), ssl96) == FormatRejection::BitDepth);
    CHECK(classifyRejection(intFormat(48000, 2, 24, 24), hdmi) == FormatRejection::BitDepth);
    CHECK(classifyRejection(intFormat(48000, 2, 32, 32), hdmi) == FormatRejection::BitDepth);
}

TEST_CASE("Float versus integer mismatches are reported as SampleType", "[engine][format]")
{
    CHECK(classifyRejection(floatFormat(96000, 2), ssl96) == FormatRejection::SampleType);
    CHECK(classifyRejection(intFormat(44100, 2, 24, 24), floatOnly) == FormatRejection::SampleType);
}

TEST_CASE("A channel count the device does not take is reported as Channels", "[engine][format]")
{
    CHECK(classifyRejection(intFormat(96000, 1, 16, 16), ssl96) == FormatRejection::Channels);
    CHECK(classifyRejection(intFormat(48000, 6, 16, 16), hdmi) == FormatRejection::Channels);
    // Mono at a rate the device does not run at: the rate is the problem.
    CHECK(classifyRejection(intFormat(44100, 1, 16, 16), ssl96) == FormatRejection::SampleRate);
}

TEST_CASE("checkFormatWith returns the reason and the accepted rates", "[engine][format]")
{
    const auto ok = checkFormatWith(intFormat(96000, 2, 32, 24), ssl96);
    CHECK(ok.supported);
    CHECK(ok.reason == FormatRejection::None);

    const auto rejected = checkFormatWith(intFormat(44100, 2, 24, 24), ssl96);
    CHECK_FALSE(rejected.supported);
    CHECK(rejected.reason == FormatRejection::SampleRate);
    CHECK(rejected.acceptedRates == std::vector<std::uint32_t>{96000});

    const auto badLayout = checkFormatWith(intFormat(48000, 2, 24, 24), hdmi);
    CHECK(badLayout.reason == FormatRejection::BitDepth);
    CHECK(badLayout.acceptedRates == std::vector<std::uint32_t>{44100, 48000});
}

TEST_CASE("Invalid formats are refused as Other", "[engine][format]")
{
    PcmFormat bad = intFormat(96000, 2, 20, 20);
    const auto c = checkFormatWith(bad, [](const PcmFormat&) { return true; });
    CHECK_FALSE(c.supported);
    CHECK(c.reason == FormatRejection::Other);
}
