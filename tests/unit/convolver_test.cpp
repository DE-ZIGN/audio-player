// Task 9-6: the FFT and the partitioned convolution against direct
// computation - a known kernel gives the same output as the convolution sum,
// whatever the kernel's length and the chunk sizes; replacing the kernel
// crossfades and then gives exactly the new kernel's convolution.

#include <ap/engine/dsp/convolver.h>
#include <ap/engine/dsp/fft.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

using namespace ap::engine;

namespace {

std::vector<double> noise(std::size_t n, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> d(0.0, 0.3);
    std::vector<double> v(n);
    for (auto& x : v)
        x = d(rng);
    return v;
}

// out[t] = sum_j k[j] x[t - delay - j], one channel.
std::vector<double> direct(const std::vector<double>& x, const std::vector<double>& k, std::size_t delay)
{
    std::vector<double> y(x.size(), 0.0);
    for (std::size_t t = 0; t < x.size(); ++t)
    {
        double s = 0.0;
        for (std::size_t j = 0; j < k.size() && j + delay <= t; ++j)
            s += k[j] * x[t - delay - j];
        y[t] = s;
    }
    return y;
}

// Runs the convolver over interleaved x in chunks of varying sizes.
std::vector<double> run(PartitionedConvolver& c, const std::vector<double>& x, std::size_t channels, unsigned seed)
{
    std::vector<double> y(x.size());
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> size(1, 700);
    const std::size_t frames = x.size() / channels;
    for (std::size_t f = 0; f < frames;)
    {
        const std::size_t n = std::min(size(rng), frames - f);
        c.process(x.data() + f * channels, y.data() + f * channels, n);
        f += n;
    }
    return y;
}

double maxDiff(const std::vector<double>& a, const std::vector<double>& b)
{
    double m = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        m = std::max(m, std::abs(a[i] - b[i]));
    return m;
}

std::vector<double> channel(const std::vector<double>& x, std::size_t channels, std::size_t c)
{
    std::vector<double> v(x.size() / channels);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = x[i * channels + c];
    return v;
}

} // namespace

TEST_CASE("RealFft matches the DFT and inverts", "[fft]")
{
    const std::size_t n = GENERATE(4u, 8u, 64u, 256u, 4096u);
    CAPTURE(n);
    const auto x = noise(n, 7);
    RealFft fft(n);
    std::vector<double> re(fft.bins()), im(fft.bins());
    fft.forward(x.data(), re.data(), im.data());
    double err = 0.0;
    for (std::size_t k = 0; k < fft.bins(); k += (n > 64 ? 17 : 1))
    {
        std::complex<double> s = 0.0;
        for (std::size_t j = 0; j < n; ++j)
            s += x[j] * std::polar(1.0, -2.0 * std::numbers::pi * static_cast<double>(k * j) / static_cast<double>(n));
        err = std::max(err, std::abs(s - std::complex<double>(re[k], im[k])));
    }
    CHECK(err < 1e-10);
    std::vector<double> back(n);
    fft.inverse(re.data(), im.data(), back.data());
    for (auto& v : back)
        v /= static_cast<double>(n);
    CHECK(maxDiff(back, x) < 1e-13);
}

TEST_CASE("The layout keeps the latency about 2.7-2.9 ms at every rate", "[convolver]")
{
    for (const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 352800.0, 384000.0})
    {
        const auto l = ConvolverLayout::forRate(rate);
        CAPTURE(rate);
        const double ms = 1000.0 * static_cast<double>(l.head) / rate;
        CHECK(ms > 2.6);
        CHECK(ms < 2.95);
        CHECK((l.tail() & (l.tail() - 1)) == 0);
    }
}

TEST_CASE("The convolver's output is the convolution sum with a known kernel", "[convolver]")
{
    const ConvolverLayout layout{32, 7}; // small blocks: tail partitions of 256
    // Kernel lengths: inside the head, exactly the head, one tail part, across parts.
    const std::size_t len = GENERATE(1u, 50u, 224u, 225u, 480u, 1000u, 3001u);
    CAPTURE(len);
    const auto k = noise(len, 11);
    const auto x = noise(6000, 13);
    PartitionedConvolver c;
    c.prepare(layout, 1, 4000);
    const auto kernel = ConvolutionKernel::make(layout, {k});
    c.setKernel(kernel.get());
    const auto y = run(c, x, 1, 5);
    CHECK(c.latencyFrames() == 32);
    CHECK(maxDiff(y, direct(x, k, 32)) < 1e-12);
}

TEST_CASE("Each channel uses its kernel channel; a mono kernel serves both", "[convolver]")
{
    const ConvolverLayout layout{32, 7};
    const auto kl = noise(900, 1), kr = noise(700, 2);
    const auto x = noise(2 * 5000, 3);
    PartitionedConvolver c;
    c.prepare(layout, 2, 1000);

    const auto stereo = ConvolutionKernel::make(layout, {kl, kr});
    c.setKernel(stereo.get());
    auto y = run(c, x, 2, 9);
    CHECK(maxDiff(channel(y, 2, 0), direct(channel(x, 2, 0), kl, 32)) < 1e-12);
    CHECK(maxDiff(channel(y, 2, 1), direct(channel(x, 2, 1), kr, 32)) < 1e-12);

    const auto mono = ConvolutionKernel::make(layout, {kl});
    c.prepare(layout, 2, 1000);
    c.setKernel(mono.get());
    y = run(c, x, 2, 10);
    CHECK(maxDiff(channel(y, 2, 1), direct(channel(x, 2, 1), kl, 32)) < 1e-12);
}

TEST_CASE("At the real layout, a long kernel matches the sum", "[convolver]")
{
    const auto layout = ConvolverLayout::forRate(48000);
    const auto k = noise(9000, 21); // head (1920) and four tail parts
    const auto x = noise(20000, 22);
    PartitionedConvolver c;
    c.prepare(layout, 1, 48000);
    const auto kernel = ConvolutionKernel::make(layout, {k});
    c.setKernel(kernel.get());
    CHECK(maxDiff(run(c, x, 1, 23), direct(x, k, layout.head)) < 1e-11);
}

TEST_CASE("Replacing the kernel crossfades over one tail block, keeping the input history", "[convolver]")
{
    const ConvolverLayout layout{32, 7};
    const std::size_t T = layout.tail();
    const auto ka = noise(1500, 31), kb = noise(1100, 32);
    const auto x = noise(8000, 33);
    const auto ya = direct(x, ka, 32), yb = direct(x, kb, 32);
    const auto a = ConvolutionKernel::make(layout, {ka});
    const auto b = ConvolutionKernel::make(layout, {kb});

    PartitionedConvolver c;
    c.prepare(layout, 1, 2000);
    c.setKernel(a.get());
    std::vector<double> y(x.size());
    const std::size_t ask = 1000; // asked mid-block: takes over at 1024
    c.process(x.data(), y.data(), ask);
    REQUIRE(c.crossfadeTo(b.get()));
    CHECK_FALSE(c.crossfadeTo(a.get())); // busy
    c.process(x.data() + ask, y.data() + ask, x.size() - ask);
    CHECK(c.kernel() == b.get());
    CHECK_FALSE(c.crossfading());

    const std::size_t start = (ask / T + 1) * T;
    double before = 0.0, after = 0.0;
    for (std::size_t i = 0; i < start; ++i)
        before = std::max(before, std::abs(y[i] - ya[i]));
    for (std::size_t i = start + T; i < x.size(); ++i)
        after = std::max(after, std::abs(y[i] - yb[i]));
    CHECK(before < 1e-12);
    CHECK(after < 1e-12); // the new kernel's own convolution of the whole input
    // In between: between the two, moving monotonically from one to the other.
    bool between = true;
    for (std::size_t i = start; i < start + T; ++i)
    {
        const double lo = std::min(ya[i], yb[i]) - 1e-12, hi = std::max(ya[i], yb[i]) + 1e-12;
        between = between && y[i] >= lo && y[i] <= hi;
    }
    CHECK(between);
}

TEST_CASE("No kernel gives silence; a first kernel starts from silence", "[convolver]")
{
    const ConvolverLayout layout{32, 7};
    const auto k = noise(400, 41);
    auto x = noise(3000, 42);
    PartitionedConvolver c;
    c.prepare(layout, 1, 1000);
    std::vector<double> y(x.size());
    c.process(x.data(), y.data(), 1500);
    for (std::size_t i = 0; i < 1500; ++i)
        REQUIRE(y[i] == 0.0);
    const auto kernel = ConvolutionKernel::make(layout, {k});
    c.setKernel(kernel.get());
    c.process(x.data() + 1500, y.data() + 1500, 1500);
    // As if the input had started at 1500.
    std::vector<double> later(x.begin() + 1500, x.end());
    const auto expect = direct(later, k, 32);
    double m = 0.0;
    for (std::size_t i = 0; i < later.size(); ++i)
        m = std::max(m, std::abs(y[1500 + i] - expect[i]));
    CHECK(m < 1e-12);
}
