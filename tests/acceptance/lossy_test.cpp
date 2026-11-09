// CLAUDE.md chapter 5:
//   非可逆デコード：リファレンスデコーダーとの差が許容範囲内で、クリップがない
//
// Reference: the Windows MP3 decoder (Media Foundation), an implementation
// independent of mpg123. Tolerance (measured, then given a margin): the RMS
// of the difference below -90 dBFS and its peak below -70 dBFS, after lining
// the two up (Windows does not remove the encoder delay). No clipping: the
// player's output keeps values beyond full scale where the decoded waveform
// overshoots.

#include <ap/engine/mp3_file_source.h>

#include "mf_audio.h"
#include "mp3_writer.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ap::engine;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> signal(const std::string& kind, std::uint32_t rate, std::size_t frames)
{
    std::vector<float> v(frames * 2);
    std::uint64_t x = 1;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        double l = 0, r = 0;
        if (i < rate / 20)
        {
            // 50 ms of noise first: makes the alignment with the reference unambiguous
            // (periodic signals line up at every period).
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            l = r = 0.1 * (static_cast<double>(x >> 40) / static_cast<double>(1ull << 24) - 0.5);
        }
        else if (kind == "music")
        {
            const double env = 0.6 + 0.4 * std::sin(2 * kPi * 1.3 * t);
            l = env * (std::sin(2 * kPi * 440 * t) + 0.3 * std::sin(2 * kPi * 1188 * t) + 0.2 * std::sin(2 * kPi * 2244 * t)) / 3;
            r = env * (std::sin(2 * kPi * 660 * t) + 0.3 * std::sin(2 * kPi * 1782 * t)) / 3;
        }
        else if (kind == "loud") // a square wave near full scale: the decoded waveform overshoots 1.0
        {
            l = r = (static_cast<std::size_t>(t * 441 * 2) % 2) ? 0.99 : -0.99;
        }
        else // "quiet": -60 dBFS sine
        {
            l = r = 0.001 * std::sin(2 * kPi * 1000 * t);
        }
        v[i * 2] = static_cast<float>(l);
        v[i * 2 + 1] = static_cast<float>(r);
    }
    return v;
}

std::vector<float> decodeAll(Mp3FileSource& src)
{
    std::vector<float> out;
    std::vector<std::byte> buf(4096 * src.format().bytesPerFrame());
    while (const auto n = src.read(buf))
    {
        const auto* f = reinterpret_cast<const float*>(buf.data());
        out.insert(out.end(), f, f + n * 2);
    }
    return out;
}

long bestLag(const std::vector<float>& a, const std::vector<float>& b, long maxLag)
{
    const long frames = static_cast<long>(std::min(a.size(), b.size()) / 2);
    long best = 0;
    double bestErr = 1e300;
    for (long lag = 0; lag <= maxLag; ++lag)
    {
        double err = 0;
        for (long i = 0; i + lag < frames; i += 5)
        {
            const double d = a[static_cast<std::size_t>(i) * 2] - b[static_cast<std::size_t>(i + lag) * 2];
            err += d * d;
        }
        if (err < bestErr)
        {
            bestErr = err;
            best = lag;
        }
    }
    return best;
}

} // namespace

TEST_CASE("Lossy decoding: the difference from the reference decoder is within tolerance", "[acceptance][lossy][mp3]")
{
    struct Encoding
    {
        const char* name;
        ap::test::Mp3Options options;
    };
    const auto rate = GENERATE(32000u, 44100u, 48000u);
    const auto enc = GENERATE(Encoding{"CBR 128", {.bitrateKbps = 128}}, Encoding{"CBR 320", {.bitrateKbps = 320}},
                              Encoding{"VBR V2", {.vbr = true, .vbrQuality = 2.0f}});
    const auto kind = GENERATE(std::string("music"), std::string("loud"), std::string("quiet"));
    CAPTURE(rate, enc.name, kind);

    ap::test::TempDir dir;
    const auto path = dir.path() / "in.mp3";
    ap::test::writeMp3(path, rate, 2, signal(kind, rate, rate * 2), enc.options);

    Mp3FileSource src(path);
    const auto ours = decodeAll(src);
    const auto ref = ap::test::mfDecodeFloat(path);
    REQUIRE(ref.channels == 2);
    const long lag = bestLag(ours, ref.samples, 3000);

    const float refPeak = *std::max_element(ref.samples.begin(), ref.samples.end(),
                                            [](float a, float b) { return std::abs(a) < std::abs(b); });
    const bool refClips = std::abs(refPeak) <= 1.0f && kind == "loud";
    double sum = 0, peak = 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < ours.size() && i + static_cast<std::size_t>(lag) * 2 < ref.samples.size(); ++i)
    {
        // Where the reference stops at full scale (it clips), compare with our value limited the same way.
        const double o = refClips ? std::clamp(double{ours[i]}, -1.0, 1.0) : double{ours[i]};
        const double d = std::abs(o - ref.samples[i + static_cast<std::size_t>(lag) * 2]);
        sum += d * d;
        peak = std::max(peak, d);
        ++n;
    }
    REQUIRE(n > ours.size() * 9 / 10);
    const double rmsDb = 10 * std::log10(sum / static_cast<double>(n) + 1e-30);
    const double peakDb = 20 * std::log10(peak + 1e-30);
    std::printf("%-7s %5u Hz %-5s: lag %ld, difference RMS %6.1f dBFS, peak %6.1f dBFS, reference peak %.4f%s\n", enc.name,
                rate, kind.c_str(), lag, rmsDb, peakDb, std::abs(refPeak), refClips ? " (clips)" : "");
    CHECK(rmsDb < -90.0);
    CHECK(peakDb < -70.0);

    if (kind == "loud")
    {
        // No clipping: where the decoded waveform goes beyond full scale, it stays beyond it.
        const auto over = std::count_if(ours.begin(), ours.end(), [](float x) { return std::abs(x) > 1.0f; });
        CHECK(over > 0);
    }
}
