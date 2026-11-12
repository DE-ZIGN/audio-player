// Task 6-6: a region of a track played over and over for the ABX, its end and
// start joined by a short crossfade; and the 16-bit requantization that the
// ABX can compare with the original.

#include <ap/engine/dsp/requantize.h>
#include <ap/engine/dsp/sample_convert.h>
#include <ap/engine/looping_source.h>
#include <ap/engine/pcm_source.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

using namespace ap::engine;

namespace {

PcmFormat int16(std::uint16_t channels = 2)
{
    return PcmFormat{.sampleRate = 48000, .channels = channels, .containerBits = 16, .validBits = 16, .type = SampleType::Int};
}

// Frame i holds the value i (left) and -i (right): every frame can be told apart.
std::vector<std::byte> ramp(int frames)
{
    std::vector<std::byte> out;
    for (int i = 0; i < frames; ++i)
        for (const int v : {i, -i})
        {
            const auto s = static_cast<std::int16_t>(v);
            out.push_back(static_cast<std::byte>(s & 0xff));
            out.push_back(static_cast<std::byte>((s >> 8) & 0xff));
        }
    return out;
}

std::vector<std::int16_t> samples(const std::vector<std::byte>& bytes)
{
    std::vector<std::int16_t> out(bytes.size() / 2);
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return out;
}

std::vector<std::byte> readFrames(PcmSource& s, std::size_t frames, std::size_t chunk = 37)
{
    std::vector<std::byte> out;
    const auto bpf = s.format().bytesPerFrame();
    std::vector<std::byte> buf(chunk * bpf);
    while (out.size() < frames * bpf)
    {
        const auto want = std::min(chunk, frames - out.size() / bpf);
        const auto got = s.read(std::span(buf).first(want * bpf));
        REQUIRE(got > 0);
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(got * bpf));
    }
    return out;
}

} // namespace

TEST_CASE("Looping: the region again and again, untouched away from the join", "[loop]")
{
    const int start = 1000, length = 500, fade = 48;
    LoopingSource loop(std::make_unique<MemoryPcmSource>(int16(), ramp(4000)), start, length, fade);
    CHECK(loop.format() == int16());
    CHECK(loop.totalFrames() == 4000); // the whole track (for the seek bar)
    CHECK(loop.position() == start);
    CHECK(loop.loopStart() == start);
    CHECK(loop.loopLength() == length);

    // The file from the start of the region to `fade` before its end; then the
    // last `fade` frames mixed with the first `fade`; then on from start + fade.
    struct Expect
    {
        int exact = -1;      // the frame's value, or
        int tail = 0, head = 0; // a mix between these
    };
    std::vector<Expect> want;
    for (int f = start; f < start + length - fade; ++f)
        want.push_back({f});
    for (int pass = 0; pass < 3; ++pass)
    {
        for (int j = 0; j < fade; ++j)
            want.push_back({-1, start + length - fade + j, start + j});
        for (int f = start + fade; f < start + length - fade; ++f)
            want.push_back({f});
    }
    const auto got = samples(readFrames(loop, want.size()));
    for (std::size_t k = 0; k < want.size(); ++k)
    {
        const int left = got[2 * k], right = got[2 * k + 1];
        CAPTURE(k);
        if (want[k].exact >= 0)
            CHECK(left == want[k].exact);
        else
        {
            CHECK(left <= want[k].tail);
            CHECK(left >= want[k].head);
        }
        CHECK(right == -left);
    }
    // The mix goes from the tail to the head.
    CHECK(got[2 * (length - fade)] >= start + length - fade - 1);
    CHECK(got[2 * (length - 1)] <= start + fade);
}

TEST_CASE("Looping: the join of a steady signal stays steady (no click)", "[loop]")
{
    // A constant value: the crossfade's weights add up to one, so it stays.
    std::vector<std::byte> dc;
    for (int i = 0; i < 2000 * 2; ++i)
        dc.insert(dc.end(), {std::byte{0x34}, std::byte{0x12}}); // 0x1234
    LoopingSource loop(std::make_unique<MemoryPcmSource>(int16(), dc), 100, 300, 64);
    for (const auto v : samples(readFrames(loop, 2000)))
        CHECK(v == 0x1234);
}

TEST_CASE("Looping: a sine joined at an arbitrary point moves no more than a sine can", "[loop]")
{
    std::vector<double> x(48000 * 2);
    for (std::size_t i = 0; i < x.size() / 2; ++i)
        x[2 * i] = x[2 * i + 1] = 0.5 * std::sin(2 * std::numbers::pi * 997.0 * static_cast<double>(i) / 48000.0);
    std::vector<std::byte> bytes(x.size() * 2);
    dsp::fromDouble(x, int16(), bytes);
    // 12345 frames is not a whole number of periods: without the fade the join would jump.
    LoopingSource loop(std::make_unique<MemoryPcmSource>(int16(), bytes), 1000, 12345, 240);
    const auto got = samples(readFrames(loop, 12345 * 3));
    int worst = 0;
    for (std::size_t k = 2; k < got.size(); k += 2)
        worst = std::max(worst, std::abs(got[k] - got[k - 2]));
    // The largest step of a 0.5 sine at 997 Hz is 0.5 * 2 pi 997 / 48000 of
    // full scale; the fade adds at most its slope (pi / (2 * 240) per frame)
    // times the difference of the two sines (at most 1). Without the fade the
    // join could jump by up to full scale.
    const double bound = (0.5 * 2 * std::numbers::pi * 997 / 48000 + std::numbers::pi / (2 * 240)) * 32768;
    CHECK(worst <= static_cast<int>(bound) + 2);
    CHECK(worst < 4000);
}

TEST_CASE("Looping: seeking stays in the region; a short region has a short fade", "[loop]")
{
    LoopingSource loop(std::make_unique<MemoryPcmSource>(int16(), ramp(4000)), 1000, 500, 48);
    CHECK(loop.seek(1200));
    CHECK(loop.position() == 1200);
    CHECK(samples(readFrames(loop, 1))[0] == 1200);
    CHECK(loop.seek(10));
    CHECK(loop.position() == 1000);
    CHECK(loop.seek(3000));
    CHECK(loop.position() == 1000);

    // The fade is at most a quarter of the region.
    LoopingSource tiny(std::make_unique<MemoryPcmSource>(int16(), ramp(4000)), 100, 40, 48);
    CHECK(tiny.fadeFrames() == 10);
    // A region past the end of the track is cut to the track.
    LoopingSource late(std::make_unique<MemoryPcmSource>(int16(), ramp(4000)), 3900, 500, 48);
    CHECK(late.loopLength() == 100);
    readFrames(late, 1000); // keeps going
}

TEST_CASE("Requantize to 16 bit: values on the 16-bit grid, TPDF dither, identity when off", "[dsp][requantize]")
{
    RequantizeStage q;
    CHECK(q.isIdentity());
    CHECK(q.bits() == 0);
    q.setBits(16);
    CHECK_FALSE(q.isIdentity());
    q.prepare(48000, 2);

    std::vector<double> x(48000 * 2);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = 0.25 * std::sin(static_cast<double>(i) * 0.001) + 1e-7; // finer than 16 bit
    const auto in = x;
    q.process(x, 2);
    double sumErr = 0.0, sumErr2 = 0.0, worst = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double code = x[i] * 32768.0;
        CHECK(code == std::round(code)); // on the grid
        const double e = (x[i] - in[i]) * 32768.0; // in LSB
        sumErr += e;
        sumErr2 += e * e;
        worst = std::max(worst, std::abs(e));
    }
    const double n = static_cast<double>(x.size());
    CHECK(std::abs(sumErr / n) < 0.02);               // no offset
    CHECK(std::abs(sumErr2 / n - 0.25) < 0.02);       // TPDF (1/6) + rounding (1/12) = 1/4 LSB^2
    CHECK(worst <= 1.5);                              // dither +-1 LSB plus rounding

    // Full scale is clamped to the largest code.
    std::vector<double> loud{1.0, -1.0};
    q.process(loud, 2);
    CHECK(loud[0] <= 32767.0 / 32768.0);
    CHECK(loud[1] >= -1.0);

    q.setBits(0);
    CHECK(q.isIdentity());
}
