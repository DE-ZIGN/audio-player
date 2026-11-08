// Task 1-3 completion criterion: under load, dropouts are recorded.
// Dropouts are provoked deterministically (a stalled playback thread, a
// stalled disk), and a CPU-load run checks that the counters and the output
// agree: no dropouts reported means the output equals the input.

#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>
#include <ap/engine/prefetching_source.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

using namespace ap::engine;
using namespace std::chrono_literals;

namespace {

constexpr std::uint32_t kRate = 48000;
const PcmFormat kFormat{kRate, 2, 24, 24, SampleType::Int, 3};
constexpr std::size_t kBpf = 6;

std::vector<std::byte> pattern(std::size_t frames, std::uint32_t seed)
{
    std::vector<std::byte> v(frames * kBpf);
    std::uint32_t x = seed;
    for (auto& b : v)
    {
        x = x * 1664525u + 1013904223u;
        b = static_cast<std::byte>((x >> 24) | 1u);
    }
    return v;
}

// Sleeps once inside read() when the position reaches `at`.
class StallingSource final : public PcmSource
{
public:
    StallingSource(std::vector<std::byte> data, std::uint64_t at, std::chrono::milliseconds stall)
        : inner_(kFormat, std::move(data)), at_(at), stall_(stall)
    {
    }
    const PcmFormat& format() const noexcept override { return inner_.format(); }
    std::uint64_t totalFrames() const noexcept override { return inner_.totalFrames(); }
    std::uint64_t position() const noexcept override { return inner_.position(); }
    bool seek(std::uint64_t f) override { return inner_.seek(f); }
    std::size_t read(std::span<std::byte> dst) override
    {
        if (!done_ && inner_.position() >= at_)
        {
            done_ = true;
            std::this_thread::sleep_for(stall_);
        }
        return inner_.read(dst);
    }

private:
    MemoryPcmSource inner_;
    std::uint64_t at_;
    std::chrono::milliseconds stall_;
    bool done_ = false;
};

std::vector<std::byte> readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<std::byte> out(raw.size());
    if (!raw.empty())
        std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

Player::OutputFactory pacedDummy(const std::filesystem::path& path)
{
    return [path] {
        return std::make_unique<DummyOutput>(
            DummyOutput::Options{.path = path, .periodFrames = {480}, .check = {}, .paceInRealTime = true});
    };
}

// Keeps every core busy while alive.
class CpuLoad
{
public:
    explicit CpuLoad(unsigned threads)
    {
        for (unsigned i = 0; i < threads; ++i)
            workers_.emplace_back([this] {
                volatile std::uint64_t x = 0;
                while (!stop_)
                    x = x * 6364136223846793005ull + 1442695040888963407ull;
            });
    }
    ~CpuLoad()
    {
        stop_ = true;
        for (auto& w : workers_)
            w.join();
    }

private:
    std::atomic<bool> stop_{false};
    std::vector<std::thread> workers_;
};

} // namespace

TEST_CASE("A stalled playback thread is recorded as a late buffer; the data is unchanged", "[engine][dropout]")
{
    ap::test::TempDir dir;
    const auto data = pattern(kRate / 2, 1); // 0.5 s
    Player player(pacedDummy(dir.path() / "out.raw"));
    // The read happens on the playback thread, so a stall there delays a buffer.
    player.setQueue({{"a", [data] { return std::make_unique<StallingSource>(data, kRate / 10, 60ms); }}});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(s.dropouts.lateBuffers >= 1u);
    CHECK(s.dropouts.readStarvations == 0u);
    CHECK(readFile(dir.path() / "out.raw") == data);
}

TEST_CASE("A disk stall longer than the read-ahead is recorded as a read starvation", "[engine][dropout]")
{
    ap::test::TempDir dir;
    const auto data = pattern(kRate / 2, 2);
    Player player(pacedDummy(dir.path() / "out.raw"));
    player.setQueue({{"a", [data] {
                          return std::make_unique<PrefetchingSource>(
                              std::make_unique<StallingSource>(data, kRate / 10, 200ms), 2400); // 50 ms ahead
                      }}});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(s.dropouts.readStarvations >= 1u);
    CHECK(s.dropouts.starvedFrames > 0u);
    // (lateBuffers is not checked: other load on the machine can delay the
    // normal-priority dummy output thread independently of this stall.)
    // Silence was inserted, never hidden: the output is longer by exactly the starved frames.
    CHECK(readFile(dir.path() / "out.raw").size() == (data.size() / kBpf + s.dropouts.starvedFrames) * kBpf);
}

TEST_CASE("Under CPU load the counters and the output agree", "[engine][dropout][load]")
{
    ap::test::TempDir dir;
    const auto a = pattern(kRate / 2, 3);
    const auto b = pattern(kRate / 2, 4);

    CpuLoad load(std::max(2u, std::thread::hardware_concurrency()));

    Player player(pacedDummy(dir.path() / "out.raw"));
    auto prefetched = [](std::vector<std::byte> d) {
        return [d] { return std::make_unique<PrefetchingSource>(std::make_unique<MemoryPcmSource>(kFormat, d)); };
    };
    player.setQueue({{"a", prefetched(a)}, {"b", prefetched(b)}});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    std::vector<std::byte> expected = a;
    expected.insert(expected.end(), b.begin(), b.end());
    const auto out = readFile(dir.path() / "out.raw");

    INFO("late buffers " << s.dropouts.lateBuffers << ", read starvations " << s.dropouts.readStarvations);
    CHECK(out.size() == (expected.size() / kBpf + s.dropouts.starvedFrames) * kBpf);
    if (s.dropouts.readStarvations == 0)
        CHECK(out == expected); // late buffers delay data but never change it
}

TEST_CASE("Dropout counters start from zero for a new queue", "[engine][dropout]")
{
    ap::test::TempDir dir;
    const auto data = pattern(kRate / 4, 5);
    Player player(pacedDummy(dir.path() / "out.raw"));
    player.setQueue({{"a", [data] { return std::make_unique<StallingSource>(data, 2400, 60ms); }}});
    player.play();
    player.waitUntilIdle();
    REQUIRE(player.status().dropouts.lateBuffers >= 1u);

    player.setQueue({{"b", [data] { return std::make_unique<MemoryPcmSource>(kFormat, data); }}});
    CHECK(player.status().dropouts.total() == 0u);
}
