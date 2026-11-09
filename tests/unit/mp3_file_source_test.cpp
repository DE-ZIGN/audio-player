// Task 2-3: MP3 via libmpg123, decoded to 32-bit float. Test files are made
// with LAME (test-only), which records the encoder delay and padding in the
// LAME tag; the decoded length must equal the encoded length (gapless). The
// output is compared with the Windows MP3 decoder, an independent
// implementation; the difference must stay within a measured tolerance.

#include <ap/engine/audio_file.h>
#include <ap/engine/mp3_file_source.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>

#include "mf_audio.h"
#include "mp3_writer.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace ap::engine;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A tone with a few partials and a slow level change: enough structure to find
// the alignment between two decodes, compressible enough for MP3.
std::vector<float> testSignal(std::uint32_t rate, std::uint16_t channels, std::size_t frames, double level = 0.5)
{
    std::vector<float> v(frames * channels);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const double env = 0.6 + 0.4 * std::sin(2 * kPi * 1.3 * t);
        for (std::uint16_t c = 0; c < channels; ++c)
        {
            const double f = 440.0 * (c + 1);
            const double s = std::sin(2 * kPi * f * t) + 0.3 * std::sin(2 * kPi * 2.7 * f * t) + 0.2 * std::sin(2 * kPi * 5.1 * f * t);
            v[i * channels + c] = static_cast<float>(level * env * s / 1.5);
        }
    }
    return v;
}

std::vector<float> readAllFloats(PcmSource& src, std::size_t chunkFrames = 1000)
{
    REQUIRE(src.format().type == SampleType::Float);
    REQUIRE(src.format().containerBits == 32);
    std::vector<float> out;
    std::vector<std::byte> buf(chunkFrames * src.format().bytesPerFrame());
    while (const auto n = src.read(buf))
    {
        const auto* f = reinterpret_cast<const float*>(buf.data());
        out.insert(out.end(), f, f + n * src.format().channels);
    }
    return out;
}

// The lag (in frames) of `b` relative to `a` with the smallest squared error, within +-maxLag.
long bestLag(const std::vector<float>& a, const std::vector<float>& b, std::uint16_t channels, long maxLag)
{
    const long frames = static_cast<long>(std::min(a.size(), b.size()) / channels);
    long best = 0;
    double bestErr = 1e300;
    for (long lag = -maxLag; lag <= maxLag; ++lag)
    {
        double err = 0;
        for (long i = maxLag; i + maxLag < frames; i += 7)
            for (std::uint16_t c = 0; c < channels; ++c)
            {
                const double d = a[static_cast<std::size_t>(i) * channels + c] - b[static_cast<std::size_t>(i + lag) * channels + c];
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

// Signal-to-error ratio in dB of `decoded` against `original`, over the common part.
double snrDb(const std::vector<float>& original, const std::vector<float>& decoded)
{
    const std::size_t n = std::min(original.size(), decoded.size());
    double s = 0, e = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        s += double{original[i]} * original[i];
        const double d = double{original[i]} - decoded[i];
        e += d * d;
    }
    return 10.0 * std::log10(s / std::max(e, 1e-30));
}

std::vector<std::uint8_t> fileBytes(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeBytes(const std::filesystem::path& p, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("MP3 with a LAME tag decodes to exactly the encoded length, aligned", "[engine][mp3]")
{
    struct Case
    {
        std::uint32_t rate;
        int kbps;
    };
    // MPEG-1, MPEG-2 and MPEG-2.5 sample rates.
    const auto c = GENERATE(Case{44100, 320}, Case{48000, 256}, Case{32000, 192}, Case{22050, 128}, Case{24000, 128},
                            Case{16000, 64}, Case{11025, 32}, Case{8000, 32});
    const auto channels = GENERATE(std::uint16_t{1}, std::uint16_t{2});
    CAPTURE(c.rate, c.kbps, channels);
    ap::test::TempDir dir;
    const std::size_t frames = c.rate * 2 + 345; // not a whole number of MP3 frames
    const auto pcm = testSignal(c.rate, channels, frames);
    const auto path = dir.path() / "in.mp3";
    ap::test::writeMp3(path, c.rate, channels, pcm, {.bitrateKbps = c.kbps});

    Mp3FileSource src(path);
    CHECK(src.format() == PcmFormat{c.rate, channels, 32, 32, SampleType::Float, channels == 1 ? 0x4u : 0x3u});
    CHECK(src.containerName() == "MP3");
    CHECK(src.gaplessInfo());
    CHECK(src.totalFrames() == frames);
    const auto out = readAllFloats(src, 777);
    REQUIRE(out.size() == frames * channels);
    CHECK(bestLag(pcm, out, channels, 1200) == 0); // the encoder delay is removed exactly
    const double snr = snrDb(pcm, out);
    CAPTURE(snr);
    CHECK(snr > 15.0); // a lossy codec; only shows that the right signal came back
}

TEST_CASE("MP3 VBR with a LAME tag: exact length", "[engine][mp3]")
{
    ap::test::TempDir dir;
    const std::size_t frames = 44100 * 3 + 7;
    const auto pcm = testSignal(44100, 2, frames);
    ap::test::writeMp3(dir.path() / "vbr.mp3", 44100, 2, pcm, {.vbr = true, .vbrQuality = 2.0f});
    Mp3FileSource src(dir.path() / "vbr.mp3");
    CHECK(src.totalFrames() == frames);
    CHECK(bestLag(pcm, readAllFloats(src), 2, 1200) == 0);
}

TEST_CASE("MP3 without a LAME tag: the encoder's delay and padding cannot be removed", "[engine][mp3]")
{
    ap::test::TempDir dir;
    const std::size_t frames = 44100 + 345;
    const auto pcm = testSignal(44100, 2, frames);
    const auto info = ap::test::writeMp3(dir.path() / "notag.mp3", 44100, 2, pcm, {.lameTag = false});
    Mp3FileSource src(dir.path() / "notag.mp3");
    CHECK_FALSE(src.gaplessInfo());
    CAPTURE(info.encoderDelay, info.encoderPadding, src.totalFrames());
    CHECK(src.totalFrames() > frames); // longer than the music: the file does not say what to drop
}

TEST_CASE("MP3 agrees with the Windows MP3 decoder within the measured tolerance", "[engine][mp3][reference]")
{
    const auto rate = GENERATE(44100u, 48000u);
    CAPTURE(rate);
    ap::test::TempDir dir;
    const auto pcm = testSignal(rate, 2, rate * 3);
    const auto path = dir.path() / "ref.mp3";
    ap::test::writeMp3(path, rate, 2, pcm, {.bitrateKbps = 320});

    Mp3FileSource src(path);
    const auto ours = readAllFloats(src);
    const auto theirs = ap::test::mfDecodeFloat(path);
    REQUIRE(theirs.channels == 2);
    // Windows may not remove the encoder delay; line the two up first.
    const long lag = bestLag(ours, theirs.samples, 2, 3000);
    REQUIRE(lag >= 0);
    double maxDiff = 0, sum = 0;
    std::size_t n = 0;
    for (std::size_t i = 2 * 4096; i + 2 * 4096 < ours.size() && i + static_cast<std::size_t>(lag) * 2 < theirs.samples.size(); ++i)
    {
        const double d = std::abs(double{ours[i]} - theirs.samples[i + static_cast<std::size_t>(lag) * 2]);
        maxDiff = std::max(maxDiff, d);
        sum += d * d;
        ++n;
    }
    REQUIRE(n > 0);
    const double rmsDb = 10.0 * std::log10(sum / static_cast<double>(n) + 1e-30);
    const double maxDb = 20.0 * std::log10(maxDiff + 1e-30);
    std::printf("MP3 vs Windows decoder (%u Hz): lag %ld frames, difference RMS %.1f dBFS, max %.1f dBFS\n", rate, lag,
                rmsDb, maxDb);
    CHECK(rmsDb < -90.0);
    CHECK(maxDb < -70.0);
}

TEST_CASE("MP3: values above full scale are kept (no clipping)", "[engine][mp3][clip]")
{
    // A loud signal with sharp edges: MP3's band limit makes the decoded waveform overshoot.
    ap::test::TempDir dir;
    const std::size_t frames = 44100;
    std::vector<float> pcm(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const float v = (i / 50) % 2 ? 0.99f : -0.99f; // 441 Hz square wave at -0.1 dBFS
        pcm[i * 2] = v;
        pcm[i * 2 + 1] = v;
    }
    ap::test::writeMp3(dir.path() / "loud.mp3", 44100, 2, pcm, {.bitrateKbps = 320});
    Mp3FileSource src(dir.path() / "loud.mp3");
    const auto out = readAllFloats(src);
    const auto over = std::count_if(out.begin(), out.end(), [](float x) { return std::abs(x) > 1.0f; });
    const float peak = *std::max_element(out.begin(), out.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
    CAPTURE(over, peak);
    CHECK(over > 0);                    // there are samples beyond full scale...
    CHECK(std::abs(peak) > 1.0f);       // ...and they were not cut at 1.0
}

// The position after a seek is exact. The values match a straight decode to
// within float rounding, not bit for bit: mpg123's synthesis filter writes to a
// ring buffer whose starting offset advances every frame, and after a seek it
// starts from another offset, so the same sums are added in another order
// (measured: at most 1.2e-7, about -138 dBFS, one 24-bit step).
TEST_CASE("MP3 seeks are sample-exact in position, values within float rounding", "[engine][mp3][seek]")
{
    ap::test::TempDir dir;
    const std::size_t frames = 44100 * 3 + 11;
    ap::test::writeMp3(dir.path() / "seek.mp3", 44100, 2, testSignal(44100, 2, frames));
    Mp3FileSource linear(dir.path() / "seek.mp3");
    const auto all = readAllFloats(linear);

    Mp3FileSource src(dir.path() / "seek.mp3");
    std::vector<std::byte> buf(500 * 8);
    for (const std::uint64_t target : {std::uint64_t{70001}, std::uint64_t{0}, std::uint64_t{1151}, std::uint64_t{1152},
                                       std::uint64_t{frames - 10}, std::uint64_t{33333}})
    {
        CAPTURE(target);
        REQUIRE(src.seek(target));
        CHECK(src.position() == target);
        const auto n = src.read(buf);
        REQUIRE(n == std::min<std::uint64_t>(500, frames - target));
        const auto* got = reinterpret_cast<const float*>(buf.data());
        double maxDiff = 0;
        for (std::size_t i = 0; i < n * 2; ++i)
            maxDiff = std::max(maxDiff, std::abs(double{got[i]} - all[target * 2 + i]));
        CAPTURE(maxDiff);
        CHECK(maxDiff <= std::ldexp(1.0, -22)); // a shifted position would differ by the signal itself
    }
    REQUIRE(src.seek(frames));
    CHECK(src.read(buf) == 0u);
}

// Where and how much a seek's output differs from a straight decode (run with "[.probe]").
TEST_CASE("Probe: MP3 seek differences", "[.probe]")
{
    ap::test::TempDir dir;
    const std::size_t frames = 44100 * 3 + 11;
    ap::test::writeMp3(dir.path() / "seek.mp3", 44100, 2, testSignal(44100, 2, frames));
    Mp3FileSource linear(dir.path() / "seek.mp3");
    const auto all = readAllFloats(linear);
    for (const std::uint64_t target : {std::uint64_t{0}, std::uint64_t{1152}, std::uint64_t{33333}, std::uint64_t{70001}})
    {
        Mp3FileSource src(dir.path() / "seek.mp3");
        std::vector<std::byte> warm(3000 * 8);
        src.read(warm); // decode something first, as the player would
        src.seek(target);
        std::vector<float> got(4000 * 2);
        const auto n = src.read(std::span(reinterpret_cast<std::byte*>(got.data()), got.size() * 4));
        double maxDiff = 0;
        std::size_t first = SIZE_MAX, last = 0, count = 0;
        for (std::size_t i = 0; i < n * 2; ++i)
        {
            const double d = std::abs(double{got[i]} - all[target * 2 + i]);
            if (d > 0)
            {
                maxDiff = std::max(maxDiff, d);
                first = std::min(first, i / 2);
                last = i / 2;
                ++count;
            }
        }
        std::printf("target %llu: %zu samples differ, frames %zu..%zu after the target, max %.3g\n",
                    static_cast<unsigned long long>(target), count, first == SIZE_MAX ? 0 : first, last, maxDiff);
    }
}

TEST_CASE("MP3 tracks join gaplessly in the player", "[engine][mp3][gapless]")
{
    // One continuous signal cut into two files; played in a row, the result has
    // the original length and the second part starts exactly where it should.
    ap::test::TempDir dir;
    const std::uint32_t rate = 44100;
    const std::size_t total = rate * 4;
    const std::size_t cut = rate * 2 + 1000;
    const auto pcm = testSignal(rate, 2, total);
    const std::vector<float> a(pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(cut * 2));
    const std::vector<float> b(pcm.begin() + static_cast<std::ptrdiff_t>(cut * 2), pcm.end());
    ap::test::writeMp3(dir.path() / "a.mp3", rate, 2, a);
    ap::test::writeMp3(dir.path() / "b.mp3", rate, 2, b);

    auto item = [](const std::filesystem::path& p) {
        return QueueItem{p.filename().string(), [p] { return openAudioFile(p); }};
    };
    const auto out = dir.path() / "out.raw";
    Player player([out] { return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out, .periodFrames = {4096}}); });
    player.setQueue({item(dir.path() / "a.mp3"), item(dir.path() / "b.mp3")});
    player.play();
    player.waitUntilIdle();
    const auto s = player.status();
    REQUIRE(s.state == PlayerState::Ended);
    REQUIRE(s.transitions.size() == 1u);
    CHECK(s.transitions[0].kind == TransitionKind::Gapless);

    const auto bytes = fileBytes(out);
    REQUIRE(bytes.size() == total * 2 * 4); // no gap, no overlap
    std::vector<float> joined(total * 2);
    std::memcpy(joined.data(), bytes.data(), bytes.size());
    const std::vector<float> second(joined.begin() + static_cast<std::ptrdiff_t>(cut * 2), joined.end());
    CHECK(bestLag(b, second, 2, 1200) == 0);
}

TEST_CASE("An integer-only output refuses MP3 with the reason, no conversion", "[engine][mp3]")
{
    ap::test::TempDir dir;
    ap::test::writeMp3(dir.path() / "x.mp3", 44100, 2, testSignal(44100, 2, 4410));
    Player player([out = dir.path() / "out.raw"] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{
            .path = out, .periodFrames = {480}, .accepts = [](const PcmFormat& f) { return f.type == SampleType::Int; }});
    });
    player.setQueue({{"x", [p = dir.path() / "x.mp3"] { return openAudioFile(p); }}});
    player.play();
    player.waitUntilIdle();
    const auto s = player.status();
    CHECK(s.state == PlayerState::Error);
    CHECK(s.error.kind == PlayerErrorKind::FormatNotSupported);
    CHECK(s.error.formatCheck.reason == FormatRejection::SampleType);
}

TEST_CASE("openAudioFile opens MP3 by content, with or without ID3v2, and Japanese paths", "[engine][mp3][audiofile]")
{
    ap::test::TempDir dir;
    const auto pcm = testSignal(44100, 2, 44100);
    const auto plain = dir.path() / u8"日本語の曲.mp3";
    ap::test::writeMp3(plain, 44100, 2, pcm);
    std::vector<std::uint8_t> tagged{'I', 'D', '3', 3, 0, 0, 0, 0, 0, 30};
    tagged.resize(tagged.size() + 30, 0);
    const auto mp3 = fileBytes(plain);
    tagged.insert(tagged.end(), mp3.begin(), mp3.end());
    writeBytes(dir.path() / "tagged.dat", tagged);

    for (const auto& p : {plain, dir.path() / "tagged.dat"})
    {
        CAPTURE(p.filename().string());
        CHECK(detectAudioFile(p) == AudioFileKind::Mp3);
        const auto src = openAudioFile(p);
        CHECK(src->containerName() == "MP3");
        CHECK(src->totalFrames() == 44100u);
    }
}

TEST_CASE("MP3: damaged and non-MP3 files never crash", "[engine][mp3][hardening]")
{
    ap::test::TempDir dir;
    const auto path = dir.path() / "ok.mp3";
    ap::test::writeMp3(path, 44100, 2, testSignal(44100, 2, 44100 * 2));
    const auto good = fileBytes(path);

    std::uint64_t x = 777;
    for (int round = 0; round < 150; ++round)
    {
        auto bad = good;
        for (int k = 0; k < 1 + round % 10; ++k)
        {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            bad[static_cast<std::size_t>((x >> 33) % bad.size())] ^= static_cast<std::uint8_t>(x >> 13);
        }
        writeBytes(dir.path() / "bad.mp3", bad);
        try
        {
            Mp3FileSource src(dir.path() / "bad.mp3");
            const auto out = readAllFloats(src, 4096);
            CHECK(out.size() == src.totalFrames() * src.format().channels);
        }
        catch (const Mp3FormatError&)
        {
        }
    }
    for (std::size_t cut : {std::size_t{4}, std::size_t{200}, good.size() / 2})
    {
        CAPTURE(cut);
        writeBytes(dir.path() / "cut.mp3", std::vector<std::uint8_t>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut)));
        try
        {
            Mp3FileSource src(dir.path() / "cut.mp3");
            readAllFloats(src);
        }
        catch (const Mp3FormatError&)
        {
        }
    }
    writeBytes(dir.path() / "junk.mp3", std::vector<std::uint8_t>(5000, 0x42));
    CHECK_THROWS(openAudioFile(dir.path() / "junk.mp3"));
}
