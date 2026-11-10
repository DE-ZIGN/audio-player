// Task 2-4: AAC in .m4a, decoded by the Windows AAC decoder (Media
// Foundation) to 32-bit float. Test files come from the Windows AAC encoder,
// which records neither an edit list nor iTunSMPB; the encoder's priming is
// measured here and written into rewritten copies, to check that the reader
// trims exactly what each kind of file says.

#include <ap/engine/aac_file_source.h>
#include <ap/engine/audio_file.h>
#include <ap/engine/mp4_reader.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>

#include "m4a_muxer.h"
#include "mf_audio.h"
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
using ap::test::MfPcm;

namespace {

constexpr double kPi = 3.14159265358979323846;

// 50 ms of noise (for an unambiguous alignment), then a tone with partials.
std::vector<float> testSignal(std::uint32_t rate, std::size_t frames, double level = 0.4)
{
    std::vector<float> v(frames * 2);
    std::uint64_t x = 1;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        double l, r;
        if (i < rate / 20)
        {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            l = r = 0.1 * (static_cast<double>(x >> 40) / static_cast<double>(1ull << 24) - 0.5);
        }
        else
        {
            const double env = 0.6 + 0.4 * std::sin(2 * kPi * 1.3 * t);
            l = level * env * (std::sin(2 * kPi * 440 * t) + 0.3 * std::sin(2 * kPi * 1188 * t)) / 1.3;
            r = level * env * (std::sin(2 * kPi * 660 * t) + 0.3 * std::sin(2 * kPi * 1782 * t)) / 1.3;
        }
        v[i * 2] = static_cast<float>(l);
        v[i * 2 + 1] = static_cast<float>(r);
    }
    return v;
}

MfPcm toPcm16(std::uint32_t rate, const std::vector<float>& f)
{
    MfPcm p{rate, 2, 16, std::vector<std::byte>(f.size() * 2)};
    for (std::size_t i = 0; i < f.size(); ++i)
    {
        const auto s = static_cast<std::int16_t>(std::lround(std::clamp(f[i], -1.0f, 32767.0f / 32768.0f) * 32768.0f));
        p.data[i * 2] = static_cast<std::byte>(s & 0xFF);
        p.data[i * 2 + 1] = static_cast<std::byte>((s >> 8) & 0xFF);
    }
    return p;
}

std::vector<float> fromPcm16(const MfPcm& p)
{
    std::vector<float> f(p.data.size() / 2);
    for (std::size_t i = 0; i < f.size(); ++i)
        f[i] = static_cast<float>(static_cast<std::int16_t>(std::to_integer<int>(p.data[i * 2]) | (std::to_integer<int>(p.data[i * 2 + 1]) << 8)) / 32768.0);
    return f;
}

std::vector<float> readAllFloats(PcmSource& src, std::size_t chunkFrames = 1000)
{
    REQUIRE(src.format().type == SampleType::Float);
    std::vector<float> out;
    std::vector<std::byte> buf(chunkFrames * src.format().bytesPerFrame());
    while (const auto n = src.read(buf))
    {
        const auto* f = reinterpret_cast<const float*>(buf.data());
        out.insert(out.end(), f, f + n * src.format().channels);
    }
    return out;
}

// Lag (frames) of `b` relative to `a` with the smallest error, in [minLag, maxLag].
long bestLag(const std::vector<float>& a, const std::vector<float>& b, long minLag, long maxLag)
{
    const long frames = static_cast<long>(std::min(a.size(), b.size()) / 2);
    long best = 0;
    double bestErr = 1e300;
    for (long lag = minLag; lag <= maxLag; ++lag)
    {
        double err = 0;
        for (long i = std::max(0L, -lag); i + std::max(0L, lag) < frames && i < 20000; i += 3)
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

struct Encoded
{
    std::vector<float> original; // after the 16-bit rounding the encoder was given
    std::filesystem::path plain; // as the Windows encoder wrote it (no trimming information)
    long priming = 0;            // measured: decoded frames before the first original frame
    std::uint64_t streamFrames = 0;
};

Encoded encode(const ap::test::TempDir& dir, const char* name, std::uint32_t rate, const std::vector<float>& signal)
{
    Encoded e;
    const auto pcm = toPcm16(rate, signal);
    e.original = fromPcm16(pcm);
    e.plain = dir.path() / name;
    ap::test::mfWriteAacM4a(e.plain, pcm);
    AacFileSource src(e.plain);
    REQUIRE(std::string(src.gaplessSource()).empty());
    const auto all = readAllFloats(src, 4096);
    e.streamFrames = all.size() / 2;
    e.priming = bestLag(e.original, all, 0, 4096);
    return e;
}

} // namespace

TEST_CASE("AAC from the Windows encoder: no trimming information, the whole stream plays", "[engine][aac]")
{
    const auto rate = GENERATE(44100u, 48000u);
    CAPTURE(rate);
    ap::test::TempDir dir;
    const std::size_t frames = rate * 2 + 345;
    const auto e = encode(dir, "plain.m4a", rate, testSignal(rate, frames));
    std::printf("Windows AAC encoder at %u Hz: %llu frames decoded for %zu encoded, priming %ld frames\n", rate,
                static_cast<unsigned long long>(e.streamFrames), frames, e.priming);
    CHECK(e.streamFrames > frames);     // longer than the music: padding at the end
    CHECK(e.streamFrames % 1024 == 0u); // whole packets
    // Measured: 0. Windows' encoder and decoder together put the first input
    // frame at the first output frame (no priming to remove at the start).
    CHECK(e.priming >= 0);
    CHECK(e.priming < 4096);
}

TEST_CASE("AAC: the edit list / iTunSMPB start and length select exactly those decoded frames", "[engine][aac][gapless]")
{
    // Independent of how much priming an encoder adds: compare with the
    // untrimmed decode of the same packets (the same decoder, bit for bit).
    const auto kind = GENERATE(std::string("elst"), std::string("iTunSMPB"));
    const auto start = GENERATE(std::uint64_t{1024}, std::uint64_t{2112}, std::uint64_t{1}, std::uint64_t{0});
    CAPTURE(kind, start);
    ap::test::TempDir dir;
    const auto e = encode(dir, "plain.m4a", 44100, testSignal(44100, 44100 * 2));
    AacFileSource plain(e.plain);
    const auto all = readAllFloats(plain, 4096);
    const std::uint64_t length = e.streamFrames - start - 777;

    const auto tagged = dir.path() / "tagged.m4a";
    if (kind == "elst")
        ap::test::rewriteAacM4a(e.plain, tagged, start, length, {});
    else
        ap::test::rewriteAacM4a(e.plain, tagged, 0, 0, ap::test::makeITunSmpb(start, 777, length));
    AacFileSource src(tagged);
    CHECK(src.totalFrames() == length);
    const auto out = readAllFloats(src, 999);
    REQUIRE(out.size() == length * 2);
    CHECK(std::memcmp(out.data(), all.data() + start * 2, out.size() * sizeof(float)) == 0);
}

TEST_CASE("AAC: an edit list or iTunSMPB trims priming and padding exactly", "[engine][aac][gapless]")
{
    const auto rate = GENERATE(44100u, 48000u);
    const auto kind = GENERATE(std::string("elst"), std::string("iTunSMPB"));
    CAPTURE(rate, kind);
    ap::test::TempDir dir;
    const std::size_t frames = rate * 2 + 345;
    const auto e = encode(dir, "plain.m4a", rate, testSignal(rate, frames));

    const auto tagged = dir.path() / "tagged.m4a";
    const auto padding = e.streamFrames - static_cast<std::uint64_t>(e.priming) - frames;
    if (kind == "elst")
        ap::test::rewriteAacM4a(e.plain, tagged, static_cast<std::uint64_t>(e.priming), frames, {});
    else
        ap::test::rewriteAacM4a(e.plain, tagged, 0, 0, ap::test::makeITunSmpb(static_cast<std::uint64_t>(e.priming), padding, frames));

    AacFileSource src(tagged);
    CHECK(std::string(src.gaplessSource()) == kind);
    CHECK(src.format() == PcmFormat{rate, 2, 32, 32, SampleType::Float, 0x3});
    CHECK(src.containerName() == "AAC");
    CHECK(src.totalFrames() == frames);
    const auto out = readAllFloats(src, 777);
    REQUIRE(out.size() == frames * 2);
    CHECK(bestLag(e.original, out, -1200, 1200) == 0);
    const double snr = snrDb(e.original, out);
    CAPTURE(snr);
    CHECK(snr > 15.0); // a lossy codec; shows the right signal came back in the right place
}

TEST_CASE("AAC: the decoded stream matches Windows' own file reader", "[engine][aac][reference]")
{
    // The same decoder behind both; this checks our container reading and
    // packet feeding against Media Foundation's own MP4 reader.
    ap::test::TempDir dir;
    const auto e = encode(dir, "plain.m4a", 44100, testSignal(44100, 44100 * 2));
    AacFileSource src(e.plain);
    const auto ours = readAllFloats(src, 4096);
    const auto theirs = ap::test::mfDecodeFloat(e.plain);
    const long lag = bestLag(ours, theirs.samples, -4096, 4096);
    double peak = 0;
    std::size_t n = 0;
    for (std::size_t i = 4096 * 2; i < ours.size(); ++i)
    {
        const long j = static_cast<long>(i) + lag * 2;
        if (j < 0 || static_cast<std::size_t>(j) >= theirs.samples.size())
            continue;
        peak = std::max(peak, std::abs(double{ours[i]} - theirs.samples[static_cast<std::size_t>(j)]));
        ++n;
    }
    std::printf("AAC vs Windows' own reader: %zu / %zu frames, offset %ld, peak difference %.3g\n", ours.size() / 2,
                theirs.samples.size() / 2, lag, peak);
    REQUIRE(n > 0);
    CHECK(peak < 1e-6);
}

// Checks a real AAC .m4a (%AP_PROBE_IN%) and, when %AP_PROBE_WAV% names the
// 16-bit stereo WAV it was made from, whether the trimming gives exactly the
// original length at offset 0: the only way to see that the Windows decoder's
// output starts where the encoder's priming count (iTunSMPB 2112, FFmpeg's
// edit list 1024) assumes. Run with "[.probe]".
TEST_CASE("Probe: a real AAC against Windows' reader and its source WAV", "[.probe]")
{
    auto env = [](const wchar_t* name) -> std::filesystem::path {
        wchar_t* v = nullptr;
        std::size_t len = 0;
        if (_wdupenv_s(&v, &len, name) != 0 || !v)
            return {};
        std::filesystem::path p(v);
        std::free(v);
        return p;
    };
    const auto m4a = env(L"AP_PROBE_IN");
    if (m4a.empty())
        return;
    const auto track = readMp4AudioTrack(m4a);
    std::printf("track: %u Hz, %u ch, %zu packets, %llu frames in the stream\n", track.sampleRate, track.channels,
                track.packets.size(), static_cast<unsigned long long>(track.totalFrames));
    if (track.hasEditList)
        std::printf("edit list: start %llu, %llu frames\n", static_cast<unsigned long long>(track.editStart),
                    static_cast<unsigned long long>(track.editFrames));
    else
        std::printf("edit list: none\n");
    if (track.itunSmpb)
        std::printf("iTunSMPB: priming %llu, padding %llu, original %llu frames\n",
                    static_cast<unsigned long long>(track.itunSmpb->priming),
                    static_cast<unsigned long long>(track.itunSmpb->padding),
                    static_cast<unsigned long long>(track.itunSmpb->originalFrames));
    else
        std::printf("iTunSMPB: none\n");

    AacFileSource src(m4a);
    REQUIRE(src.format().channels == 2);
    const auto ours = readAllFloats(src, 4096);
    std::printf("ours: %s, trimming from \"%s\", %llu frames after trimming, decoder errors %u\n",
                describe(src.format()).c_str(), src.gaplessSource(), static_cast<unsigned long long>(src.totalFrames()),
                src.integrity().decodeErrors);

    const auto theirs = ap::test::mfDecodeFloat(m4a);
    const long lag = bestLag(ours, theirs.samples, -4096, 4096);
    std::printf("Windows' own reader: %zu frames, offset %ld frames from ours\n", theirs.samples.size() / 2, lag);

    const auto wavPath = env(L"AP_PROBE_WAV");
    if (wavPath.empty())
        return;
    auto wav = openAudioFile(wavPath);
    REQUIRE(wav->format().containerBits == 16);
    REQUIRE(wav->format().channels == 2);
    std::vector<float> original;
    std::vector<std::byte> buf(65536 * wav->format().bytesPerFrame());
    while (const auto k = wav->read(buf))
    {
        const auto* s = reinterpret_cast<const std::int16_t*>(buf.data());
        for (std::size_t i = 0; i < k * 2; ++i)
            original.push_back(static_cast<float>(s[i] / 32768.0));
    }
    const long wlag = bestLag(original, ours, -4096, 4096);
    std::vector<float> aligned;
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        const long j = static_cast<long>(i) + wlag * 2;
        aligned.push_back(j >= 0 && static_cast<std::size_t>(j) < ours.size() ? ours[static_cast<std::size_t>(j)] : 0.0f);
    }
    float peak = 0;
    for (auto v : ours)
        peak = std::max(peak, std::abs(v));
    std::printf("source WAV: %zu frames; AAC is %lld frames longer (negative: shorter); offset %ld frames; SNR %.1f dB; "
                "AAC peak %.3f (%.2f dBFS)\n",
                original.size() / 2,
                static_cast<long long>(ours.size() / 2) - static_cast<long long>(original.size() / 2), wlag,
                snrDb(original, aligned), peak, 20 * std::log10(peak + 1e-30));
}

TEST_CASE("AAC: values beyond full scale (measured)", "[engine][aac][clip]")
{
    // A square wave near full scale: the decoded waveform overshoots. Whether
    // the values above 1.0 survive depends on Windows' decoder.
    ap::test::TempDir dir;
    std::vector<float> sq(44100 * 2);
    for (std::size_t i = 0; i < 44100; ++i)
        sq[i * 2] = sq[i * 2 + 1] = (i / 50) % 2 ? 0.99f : -0.99f;
    const auto path = dir.path() / "loud.m4a";
    ap::test::mfWriteAacM4a(path, toPcm16(44100, sq));
    AacFileSource src(path);
    const auto out = readAllFloats(src, 4096);
    const auto over = std::count_if(out.begin(), out.end(), [](float x) { return std::abs(x) > 1.0f; });
    const float peak = std::abs(*std::max_element(out.begin(), out.end(), [](float a, float b) { return std::abs(a) < std::abs(b); }));
    std::printf("AAC loud square: %lld samples beyond 1.0, peak %.4f\n", static_cast<long long>(over), peak);
    CHECK(over > 0);       // measured: Windows' AAC decoder keeps the overshoot (no clipping)
    CHECK(peak > 1.0f);
}

TEST_CASE("AAC seeks to an exact position", "[engine][aac][seek]")
{
    ap::test::TempDir dir;
    const std::size_t frames = 44100 * 3;
    const auto e = encode(dir, "plain.m4a", 44100, testSignal(44100, frames));
    const auto tagged = dir.path() / "tagged.m4a";
    ap::test::rewriteAacM4a(e.plain, tagged, static_cast<std::uint64_t>(e.priming), frames, {});
    AacFileSource linear(tagged);
    const auto all = readAllFloats(linear);

    AacFileSource src(tagged);
    std::vector<std::byte> buf(500 * 8);
    double worst = 0;
    for (const std::uint64_t target : {std::uint64_t{70001}, std::uint64_t{0}, std::uint64_t{1023}, std::uint64_t{1024},
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
        worst = std::max(worst, maxDiff);
        CAPTURE(maxDiff);
        CHECK(maxDiff <= 1e-6); // a shifted position would differ by the signal itself
    }
    std::printf("AAC seek: worst difference from a straight decode %.3g\n", worst);
    REQUIRE(src.seek(frames));
    CHECK(src.read(buf) == 0u);
}

TEST_CASE("AAC tracks with trimming information join gaplessly in the player", "[engine][aac][gapless]")
{
    ap::test::TempDir dir;
    const std::uint32_t rate = 44100;
    const std::size_t total = rate * 4, cut = rate * 2 + 1000;
    const auto signal = testSignal(rate, total);
    const std::vector<float> a(signal.begin(), signal.begin() + static_cast<std::ptrdiff_t>(cut * 2));
    const std::vector<float> b(signal.begin() + static_cast<std::ptrdiff_t>(cut * 2), signal.end());
    const auto ea = encode(dir, "a_plain.m4a", rate, a);
    const auto eb = encode(dir, "b_plain.m4a", rate, b);
    ap::test::rewriteAacM4a(ea.plain, dir.path() / "a.m4a", static_cast<std::uint64_t>(ea.priming), cut, {});
    ap::test::rewriteAacM4a(eb.plain, dir.path() / "b.m4a", static_cast<std::uint64_t>(eb.priming), total - cut, {});

    auto item = [](const std::filesystem::path& p) { return QueueItem{p.filename().string(), [p] { return openAudioFile(p); }}; };
    const auto out = dir.path() / "out.raw";
    Player player([out] { return std::make_unique<DummyOutput>(DummyOutput::Options{.path = out, .periodFrames = {4096}}); });
    player.setQueue({item(dir.path() / "a.m4a"), item(dir.path() / "b.m4a")});
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
    CHECK(bestLag(eb.original, second, -1200, 1200) == 0);
}

// Task 3-2: AAC decodes to float, which has no integer original; an output
// without float gets it quantized with TPDF dither on the processing path.
TEST_CASE("An integer-only output plays AAC quantized with dither on the processing path", "[engine][aac]")
{
    ap::test::TempDir dir;
    const auto e = encode(dir, "x.m4a", 44100, testSignal(44100, 4410));
    Player player([out = dir.path() / "out.raw"] {
        return std::make_unique<DummyOutput>(DummyOutput::Options{
            .path = out, .periodFrames = {480}, .accepts = [](const PcmFormat& f) { return f.type == SampleType::Int; }});
    });
    player.setQueue({{"x", [p = e.plain] { return openAudioFile(p); }}});
    player.play();
    player.waitUntilIdle();
    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    REQUIRE(s.path.has_value());
    CHECK(s.path->processing);
    CHECK(s.path->processingRequired);
    CHECK(s.path->dither);
    CHECK(s.path->output.format.type == SampleType::Int);
    CHECK(s.path->output.format.containerBits == 32);
}

TEST_CASE("AAC: damaged files never crash", "[engine][aac][hardening]")
{
    ap::test::TempDir dir;
    const auto e = encode(dir, "ok.m4a", 44100, testSignal(44100, 44100 * 2));
    const auto good = fileBytes(e.plain);
    const auto track = readMp4AudioTrack(e.plain);
    const auto first = track.packets.front().offset;
    const auto last = track.packets.back().offset + track.packets.back().size;

    std::uint64_t x = 4242;
    std::uint32_t withErrors = 0;
    for (int round = 0; round < 120; ++round)
    {
        auto bad = good;
        for (int k = 0; k < 1 + round % 8; ++k)
        {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            bad[static_cast<std::size_t>(first + (x >> 33) % (last - first))] ^= static_cast<std::uint8_t>(x >> 13);
        }
        writeBytes(dir.path() / "bad.m4a", bad);
        AacFileSource src(dir.path() / "bad.m4a");
        const auto out = readAllFloats(src, 4096);
        CHECK(out.size() <= track.totalFrames * 2);
        if (src.integrity().decodeErrors > 0)
            ++withErrors;
    }
    std::printf("AAC damaged files: decoder errors reported in %u of 120\n", withErrors);

    // A broken AudioSpecificConfig is refused.
    auto bad = good;
    for (std::size_t i = 4; i + 4 <= bad.size(); ++i)
        if (std::memcmp(&bad[i], "esds", 4) == 0)
        {
            std::fill(bad.begin() + static_cast<std::ptrdiff_t>(i + 4), bad.begin() + static_cast<std::ptrdiff_t>(i + 12), std::uint8_t{0});
            break;
        }
    writeBytes(dir.path() / "esds.m4a", bad);
    CHECK_THROWS(AacFileSource(dir.path() / "esds.m4a"));
}
