// Task 1-3: read-ahead. Bytes are delivered unchanged; when the read-ahead
// falls behind (e.g. a disk stall) the dropout is filled with silence and
// counted, never hidden.

#include <ap/engine/prefetching_source.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace ap::engine;
using namespace std::chrono_literals;

namespace {

constexpr std::uint32_t kRate = 48000;
const PcmFormat kFormat{kRate, 2, 24, 24, SampleType::Int, 3};
constexpr std::size_t kBpf = 6;

std::vector<std::byte> pattern(std::size_t frames)
{
    std::vector<std::byte> v(frames * kBpf);
    std::uint32_t x = 0x12345678u;
    for (auto& b : v)
    {
        x = x * 1664525u + 1013904223u; // deterministic, never all-zero
        b = static_cast<std::byte>((x >> 24) | 1u);
    }
    return v;
}

// Wraps a memory source and stalls once, for `stall`, when the read position
// reaches `stallAt` (a slow disk or network share).
class StallingSource final : public PcmSource
{
public:
    StallingSource(std::vector<std::byte> data, std::uint64_t stallAt, std::chrono::milliseconds stall)
        : inner_(kFormat, std::move(data)), stallAt_(stallAt), stall_(stall)
    {
    }
    const PcmFormat& format() const noexcept override { return inner_.format(); }
    std::uint64_t totalFrames() const noexcept override { return inner_.totalFrames(); }
    std::uint64_t position() const noexcept override { return inner_.position(); }
    bool seek(std::uint64_t f) override { return inner_.seek(f); }
    std::size_t read(std::span<std::byte> dst) override
    {
        if (!stalled_ && inner_.position() >= stallAt_)
        {
            stalled_ = true;
            std::this_thread::sleep_for(stall_);
        }
        return inner_.read(dst);
    }

private:
    MemoryPcmSource inner_;
    std::uint64_t stallAt_;
    std::chrono::milliseconds stall_;
    bool stalled_ = false;
};

// Reads like a device: `period` frames every period, in real time.
std::vector<std::byte> consumeRealtime(PcmSource& src, std::size_t period)
{
    std::vector<std::byte> out;
    std::vector<std::byte> buf(period * kBpf);
    auto next = std::chrono::steady_clock::now();
    for (;;)
    {
        const auto n = src.read(buf);
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n * kBpf));
        if (n < period)
            break;
        next += std::chrono::microseconds(1'000'000ull * period / kRate);
        std::this_thread::sleep_until(next);
    }
    return out;
}

std::vector<std::byte> consumeFast(PcmSource& src, std::size_t chunk)
{
    std::vector<std::byte> out;
    std::vector<std::byte> buf(chunk * kBpf);
    for (;;)
    {
        const auto n = src.read(buf);
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n * kBpf));
        if (n < chunk)
            break;
    }
    return out;
}

} // namespace

TEST_CASE("Prefetching delivers the inner bytes unchanged", "[engine][prefetch]")
{
    const auto data = pattern(30000);
    // The buffer holds the whole source, which is read ahead before the constructor returns.
    PrefetchingSource src(std::make_unique<MemoryPcmSource>(kFormat, data), 32768);
    CHECK(src.format() == kFormat);
    CHECK(src.totalFrames() == 30000u);
    CHECK(src.bufferedFrames() == 30000u);

    CHECK(consumeFast(src, 441) == data);
    CHECK(src.position() == 30000u);
    CHECK(src.health().starvations == 0u);

    std::vector<std::byte> more(64);
    CHECK(src.read(more) == 0);
}

TEST_CASE("The default buffer fills completely and the constructor returns promptly", "[engine][prefetch]")
{
    // 96 kHz: the default buffer (2 s = 192000 frames) is not a multiple of the read chunk.
    const PcmFormat f96{96000, 2, 24, 24, SampleType::Int, 3};
    std::vector<std::byte> data(96000 * 10 * kBpf, std::byte{1}); // 10 s
    const auto t0 = std::chrono::steady_clock::now();
    PrefetchingSource src(std::make_unique<MemoryPcmSource>(f96, data));
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    CHECK(src.bufferFrames() == 192000u);
    CHECK(src.bufferedFrames() == src.bufferFrames());
    CHECK(elapsed < 1s);

    // Same after a seek while not playing.
    src.seek(500000);
    const auto t1 = std::chrono::steady_clock::now();
    src.waitUntilReady();
    CHECK(std::chrono::steady_clock::now() - t1 < 1s);
    CHECK(src.bufferedFrames() == src.bufferFrames());
}

TEST_CASE("A small buffer read flat out: any shortfall is counted, never hidden", "[engine][prefetch]")
{
    const std::size_t total = 200000;
    const auto data = pattern(total);
    PrefetchingSource src(std::make_unique<MemoryPcmSource>(kFormat, data), 2048);
    const auto out = consumeFast(src, 441);
    const auto h = src.health();

    // Every frame of the source arrives, plus one frame of silence per starved frame.
    CHECK(out.size() == (total + h.starvedFrames) * kBpf);
    if (h.starvations == 0)
        CHECK(out == data);
}

TEST_CASE("A disk stall shorter than the buffer causes no dropout", "[engine][prefetch][dropout]")
{
    const auto data = pattern(kRate); // 1 s
    PrefetchingSource src(std::make_unique<StallingSource>(data, kRate / 10, 200ms), kRate / 2); // 0.5 s buffer

    const auto out = consumeRealtime(src, 480);
    CHECK(src.health().starvations == 0u);
    CHECK(out == data);
}

TEST_CASE("A disk stall longer than the buffer is counted as a dropout", "[engine][prefetch][dropout]")
{
    const auto data = pattern(kRate / 2); // 0.5 s
    PrefetchingSource src(std::make_unique<StallingSource>(data, kRate / 10, 200ms), 2400); // 50 ms buffer

    const auto out = consumeRealtime(src, 480);
    const auto h = src.health();
    CHECK(h.starvations >= 1u);
    CHECK(h.starvedFrames > 0u);
    CHECK(out.size() == (data.size() / kBpf + h.starvedFrames) * kBpf);
}

TEST_CASE("Seek refills from the new position", "[engine][prefetch]")
{
    const auto data = pattern(40000);
    PrefetchingSource src(std::make_unique<MemoryPcmSource>(kFormat, data), 8192);

    std::vector<std::byte> buf(1000 * kBpf);
    REQUIRE(src.read(buf) == 1000);

    CHECK(src.seek(25000));
    CHECK(src.position() == 25000u);
    std::this_thread::sleep_for(50ms); // let the reader refill

    REQUIRE(src.read(buf) == 1000);
    CHECK(std::memcmp(buf.data(), data.data() + 25000 * kBpf, buf.size()) == 0);
    CHECK(src.position() == 26000u);
    CHECK(src.health().starvations == 0u);
}

TEST_CASE("Reading right after a seek returns silence counted as seek fill, not as a dropout", "[engine][prefetch]")
{
    const auto data = pattern(40000);
    PrefetchingSource src(std::make_unique<StallingSource>(data, 30000, 100ms), 8192);
    std::vector<std::byte> buf(480 * kBpf);
    REQUIRE(src.read(buf) == 480);

    src.seek(30000); // the reader stalls for 100 ms at this position
    REQUIRE(src.read(buf) == 480);
    const auto h = src.health();
    CHECK(h.seekFillFrames == 480u);
    CHECK(h.starvations == 0u);
    CHECK(std::all_of(buf.begin(), buf.end(), [](std::byte b) { return b == std::byte{0}; }));
}

TEST_CASE("Seek to the end, then end of stream", "[engine][prefetch]")
{
    const auto data = pattern(5000);
    PrefetchingSource src(std::make_unique<MemoryPcmSource>(kFormat, data), 8192);
    src.seek(4900);
    std::this_thread::sleep_for(30ms);
    std::vector<std::byte> buf(480 * kBpf);
    CHECK(src.read(buf) == 100);
    CHECK(src.read(buf) == 0);
}
