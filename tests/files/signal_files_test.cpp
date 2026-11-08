// Verifies the WAV files written by `ap_signalgen all` (CTest fixture
// "signal_files"). Reads them with the test-side WAV reader, which is
// independent of the generator's writer.

#include "wav_reader.h"

#include <ap/signalgen/signals.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

using namespace ap::signalgen;
using Catch::Generators::from_range;

namespace {

// Set by tests/CMakeLists.txt; must match the fixture's ap_signalgen arguments.
constexpr std::uint32_t kRates[] = {AP_SIGNAL_TEST_RATES};
constexpr int kBits[] = {AP_SIGNAL_TEST_BITS};
constexpr double kSeconds = AP_SIGNAL_TEST_SECONDS;
constexpr std::uint16_t kChannels = 2;

const std::filesystem::path kDir{AP_SIGNAL_DIR};

SampleFormat formatForBits(int bits)
{
    return bits == 16 ? SampleFormat::Int16 : bits == 24 ? SampleFormat::Int24 : SampleFormat::Int32;
}

ap::test::WavFile load(const std::string& name, std::uint32_t rate, int bits)
{
    const auto path = kDir / name;
    INFO("file: " << path.string());
    REQUIRE(std::filesystem::exists(path));
    auto wav = ap::test::readWavFile(path);
    REQUIRE(wav.sampleFormat == 1);
    REQUIRE(wav.sampleRate == rate);
    REQUIRE(wav.bitsPerSample == bits);
    REQUIRE(wav.channels == kChannels);
    REQUIRE(wav.frameCount() == static_cast<std::size_t>(std::llround(kSeconds * rate)));
    return wav;
}

} // namespace

TEST_CASE("Generated ISP files hold +FS,+FS,-FS,-FS in both channels", "[files][isp]")
{
    const auto rate = GENERATE(from_range(std::begin(kRates), std::end(kRates)));
    const auto bits = GENERATE(from_range(std::begin(kBits), std::end(kBits)));
    const auto format = formatForBits(bits);

    const auto wav = load(std::format("isp_fs4_45deg_0dBFS_{}Hz_{}_2ch.wav", rate, formatName(format)), rate, bits);
    const auto s = wav.intSamples();
    const auto fs = maxCode(format);
    for (std::size_t n = 0; n < wav.frameCount(); ++n)
    {
        const std::int32_t expected = (n % 4 < 2) ? fs : -fs;
        REQUIRE(s[n * 2] == expected);
        REQUIRE(s[n * 2 + 1] == expected);
    }
}

TEST_CASE("Generated 1 kHz -90 dBFS files have the expected peak code", "[files][sine]")
{
    const auto rate = GENERATE(from_range(std::begin(kRates), std::end(kRates)));
    const auto bits = GENERATE(from_range(std::begin(kBits), std::end(kBits)));
    const auto format = formatForBits(bits);

    const auto wav = load(std::format("sine_1000Hz_-90dBFS_{}Hz_{}_2ch.wav", rate, formatName(format)), rate, bits);
    const auto s = wav.intSamples();

    // Expected peak code: round(maxCode * 10^(-90/20)), reached within 0.25 s at these rates.
    const auto expectedPeak = std::llround(maxCode(format) * dbToLinear(-90.0));
    long long peak = 0;
    for (std::size_t n = 0; n < wav.frameCount(); ++n)
    {
        REQUIRE(s[n * 2] == s[n * 2 + 1]);
        peak = std::max(peak, std::llabs(s[n * 2]));
    }
    CHECK(peak == expectedPeak);
}

TEST_CASE("Generated bit-pattern files match the sequence exactly", "[files][bitpattern]")
{
    const auto rate = GENERATE(from_range(std::begin(kRates), std::end(kRates)));
    const auto bits = GENERATE(from_range(std::begin(kBits), std::end(kBits)));
    const auto format = formatForBits(bits);

    const auto wav = load(std::format("bitpattern_{}Hz_{}_2ch.wav", rate, formatName(format)), rate, bits);
    const auto s = wav.intSamples();
    const auto seq = bitPatternSequence(format);
    for (std::size_t n = 0; n < wav.frameCount(); ++n)
    {
        REQUIRE(s[n * 2] == seq[n % seq.size()]);
        REQUIRE(s[n * 2 + 1] == ~seq[n % seq.size()]);
    }
}

TEST_CASE("Generated sweep files stay within -6 dBFS and start at zero", "[files][sweep]")
{
    const auto rate = GENERATE(from_range(std::begin(kRates), std::end(kRates)));
    const auto bits = GENERATE(from_range(std::begin(kBits), std::end(kBits)));
    const auto format = formatForBits(bits);

    const auto wav = load(std::format("sweep_20-20000Hz_-6dBFS_{}Hz_{}_2ch.wav", rate, formatName(format)), rate, bits);
    const auto s = wav.intSamples();
    const auto limit = std::llround(maxCode(format) * dbToLinear(-6.0));

    CHECK(s[0] == 0);
    CHECK(s[1] == 0);
    for (auto v : s)
        REQUIRE(std::llabs(v) <= limit);
}
