// Task 3-1: the 64-bit processing path and the full-bypass decision.
//
// When every processing stage would leave the samples unchanged, play() uses
// the direct (bit-perfect) path. Otherwise it converts to 64-bit float, runs
// the stages, and quantizes to the output format with the most resolution the
// device accepts. DoP is never processed.

#include <ap/engine/dsp/processing.h>
#include <ap/engine/dsp/sample_convert.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/playback.h>
#include <ap/engine/player.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <xmmintrin.h>

using namespace ap::engine;

namespace {

// Multiplies every sample by a gain; identity at 1.0.
class GainStage final : public ProcessingStage
{
public:
    explicit GainStage(double gain) : gain_(gain) {}
    std::string name() const override { return "test-gain"; }
    void prepare(std::uint32_t, std::uint16_t) override { ++prepared; }
    bool isIdentity() const noexcept override { return gain_ == 1.0; }
    void process(std::span<double> samples, std::uint16_t) noexcept override
    {
        for (auto& s : samples)
            s *= gain_;
    }
    int prepared = 0;

private:
    double gain_;
};

// Claims to be active but changes nothing: forces the processing path so the
// conversions alone can be checked.
class ForcedStage final : public ProcessingStage
{
public:
    std::string name() const override { return "test-forced"; }
    void prepare(std::uint32_t, std::uint16_t) override {}
    bool isIdentity() const noexcept override { return false; }
    void process(std::span<double>, std::uint16_t) noexcept override
    {
        const unsigned csr = _mm_getcsr();
        ftz = (csr & 0x8000u) != 0;
        daz = (csr & 0x0040u) != 0;
        ++calls;
    }
    std::atomic<bool> ftz{false};
    std::atomic<bool> daz{false};
    std::atomic<int> calls{0};
};

PcmFormat intFormat(std::uint16_t container, std::uint16_t valid, std::uint32_t rate = 48000)
{
    return PcmFormat{rate, 2, container, valid, SampleType::Int, 0};
}

PcmFormat float32(std::uint32_t rate = 48000)
{
    return PcmFormat{rate, 2, 32, 32, SampleType::Float, 0};
}

std::vector<std::byte> ramp16(std::size_t frames)
{
    std::vector<std::byte> out;
    for (std::size_t i = 0; i < frames * 2; ++i)
    {
        const auto v = static_cast<std::uint16_t>(static_cast<std::int16_t>((i * 7919) % 65536 - 32768));
        out.push_back(static_cast<std::byte>(v));
        out.push_back(static_cast<std::byte>(v >> 8));
    }
    return out;
}

std::vector<std::byte> readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<std::byte> out(raw.size());
    if (!raw.empty())
        std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

DummyOutput::Options dummy(const std::filesystem::path& p, std::function<bool(const PcmFormat&)> accepts = {})
{
    return DummyOutput::Options{.path = p, .periodFrames = {441, 4096, 7, 1000}, .accepts = accepts};
}

// A source that says its samples are DoP.
class DopMemory final : public PcmSource
{
public:
    explicit DopMemory(std::vector<std::byte> d) : m_(intFormat(24, 24, 176400), std::move(d)) {}
    const PcmFormat& format() const noexcept override { return m_.format(); }
    std::uint64_t totalFrames() const noexcept override { return m_.totalFrames(); }
    std::size_t read(std::span<std::byte> dst) override { return m_.read(dst); }
    bool seek(std::uint64_t f) override { return m_.seek(f); }
    std::uint64_t position() const noexcept override { return m_.position(); }
    std::optional<DopInfo> dop() const override { return DopInfo{2822400, "DSF"}; }

private:
    MemoryPcmSource m_;
};

std::vector<std::byte> dopBytes(int frames)
{
    std::vector<std::byte> data;
    for (int i = 0; i < frames; ++i)
        for (int ch = 0; ch < 2; ++ch)
            data.insert(data.end(), {std::byte{0x69}, std::byte{0x96}, (i % 2) ? std::byte{0xFA} : std::byte{0x05}});
    return data;
}

std::shared_ptr<ProcessingChain> chainOf(std::vector<std::shared_ptr<ProcessingStage>> stages)
{
    return std::make_shared<ProcessingChain>(std::move(stages));
}

} // namespace

TEST_CASE("A chain is bypassed when it is empty or every stage is identity", "[dsp][chain]")
{
    CHECK(ProcessingChain{}.bypassed());
    CHECK(ProcessingChain({std::make_shared<GainStage>(1.0)}).bypassed());

    const ProcessingChain active({std::make_shared<GainStage>(1.0), std::make_shared<GainStage>(0.5)});
    CHECK_FALSE(active.bypassed());
    CHECK(active.activeStageNames() == std::vector<std::string>{"test-gain"});
}

TEST_CASE("A chain runs only its active stages", "[dsp][chain]")
{
    ProcessingChain chain({std::make_shared<GainStage>(1.0), std::make_shared<GainStage>(0.5),
                           std::make_shared<GainStage>(0.25)});
    std::vector<double> s{1.0, -1.0};
    chain.process(s, 2);
    CHECK(s == std::vector<double>{0.125, -0.125});
}

TEST_CASE("Processing format: the most resolution the output accepts, same rate and channels", "[dsp][format]")
{
    ap::test::TempDir dir;
    const auto src = intFormat(16, 16);

    SECTION("everything accepted: 32-bit integer")
    {
        DummyOutput out(dummy(dir.path() / "o.raw"));
        const auto n = negotiateProcessing(src, out);
        REQUIRE(n.plan);
        CHECK(n.plan->output == intFormat(32, 32));
        CHECK(n.plan->processing);
    }
    SECTION("24-bit in a 32-bit container before packed 24-bit")
    {
        DummyOutput out(dummy(dir.path() / "o.raw", [](const PcmFormat& f) {
            return f.type == SampleType::Int && f.validBits <= 24;
        }));
        const auto n = negotiateProcessing(src, out);
        REQUIRE(n.plan);
        CHECK(n.plan->output == intFormat(32, 24));
    }
    SECTION("16-bit only")
    {
        DummyOutput out(dummy(dir.path() / "o.raw", [](const PcmFormat& f) {
            return f.type == SampleType::Int && f.containerBits == 16;
        }));
        const auto n = negotiateProcessing(src, out);
        REQUIRE(n.plan);
        CHECK(n.plan->output == intFormat(16, 16));
    }
    SECTION("float only")
    {
        DummyOutput out(dummy(dir.path() / "o.raw", [](const PcmFormat& f) { return f.type == SampleType::Float; }));
        const auto n = negotiateProcessing(src, out);
        REQUIRE(n.plan);
        CHECK(n.plan->output == float32());
    }
    SECTION("the rate is never changed")
    {
        DummyOutput out(dummy(dir.path() / "o.raw", [](const PcmFormat& f) { return f.sampleRate == 96000; }));
        const auto n = negotiateProcessing(src, out);
        CHECK_FALSE(n.plan);
        CHECK(n.exactCheck.reason == FormatRejection::SampleRate);
    }
}

TEST_CASE("A bypassed chain plays through the direct path, unchanged", "[dsp][bypass]")
{
    ap::test::TempDir dir;
    const auto data = ramp16(5000);
    MemoryPcmSource src(intFormat(16, 16), data);
    DummyOutput out(dummy(dir.path() / "o.raw"));
    auto gain = std::make_shared<GainStage>(1.0);

    PlaybackOptions options;
    options.processing = chainOf({gain});
    const auto report = play(src, out, options);

    CHECK_FALSE(report.plan.processing);
    CHECK(report.plan.output == intFormat(16, 16));
    CHECK(report.plan.processingStages.empty());
    CHECK(gain->prepared == 0);
    CHECK(readFile(dir.path() / "o.raw") == data);
}

TEST_CASE("The processing path with no change to the samples gives back the same values", "[dsp][path]")
{
    ap::test::TempDir dir;
    const auto data = ramp16(5000);
    auto forced = std::make_shared<ForcedStage>();
    PlaybackOptions options;
    options.processing = chainOf({forced});

    SECTION("into the same format")
    {
        MemoryPcmSource src(intFormat(16, 16), data);
        DummyOutput out(dummy(dir.path() / "o.raw", [](const PcmFormat& f) { return f == intFormat(16, 16); }));
        const auto report = play(src, out, options);
        CHECK(report.plan.processing);
        CHECK(report.plan.processingStages == std::vector<std::string>{"test-forced"});
        CHECK(report.plan.output == intFormat(16, 16));
        CHECK(report.overs == 0u);
        CHECK(readFile(dir.path() / "o.raw") == data);
    }
    SECTION("into 32-bit: each 16-bit value in the upper bits, the low bits zero")
    {
        MemoryPcmSource src(intFormat(16, 16), data);
        DummyOutput out(dummy(dir.path() / "o.raw"));
        const auto report = play(src, out, options);
        CHECK(report.plan.output == intFormat(32, 32));
        std::vector<std::byte> expected;
        for (std::size_t i = 0; i < data.size(); i += 2)
        {
            expected.insert(expected.end(), {std::byte{0}, std::byte{0}});
            expected.insert(expected.end(), {data[i], data[i + 1]});
        }
        CHECK(readFile(dir.path() / "o.raw") == expected);
    }
    SECTION("float into float")
    {
        std::vector<float> v{0.5f, -0.25f, 1.26f, -1.0f, 1e-20f, 0.1f};
        std::vector<std::byte> bytes(v.size() * 4);
        std::memcpy(bytes.data(), v.data(), bytes.size());
        MemoryPcmSource src(float32(), bytes);
        DummyOutput out(dummy(dir.path() / "o.raw", [](const PcmFormat& f) { return f.type == SampleType::Float; }));
        const auto report = play(src, out, options);
        CHECK(report.plan.output == float32());
        CHECK(report.overs == 1u); // 1.26 passes unchanged and is counted
        CHECK(readFile(dir.path() / "o.raw") == bytes);
    }
    CHECK(forced->calls > 0);
}

TEST_CASE("An active stage changes the samples", "[dsp][path]")
{
    ap::test::TempDir dir;
    std::vector<std::int16_t> v{16384, -16384, 1000, -1000};
    std::vector<std::byte> bytes(v.size() * 2);
    std::memcpy(bytes.data(), v.data(), bytes.size());
    MemoryPcmSource src(intFormat(16, 16), bytes);
    DummyOutput out(dummy(dir.path() / "o.raw", [](const PcmFormat& f) { return f == intFormat(16, 16); }));
    auto gain = std::make_shared<GainStage>(0.5);
    PlaybackOptions options;
    options.processing = chainOf({gain});

    const auto report = play(src, out, options);
    CHECK(report.plan.processing);
    CHECK(gain->prepared == 1);
    const auto got = readFile(dir.path() / "o.raw");
    std::vector<std::int16_t> r(got.size() / 2);
    std::memcpy(r.data(), got.data(), got.size());
    CHECK(r == std::vector<std::int16_t>{8192, -8192, 500, -500});
}

TEST_CASE("Stages run with flush-to-zero and denormals-are-zero and the state is restored", "[dsp][path]")
{
    ap::test::TempDir dir;
    const unsigned before = _mm_getcsr();
    MemoryPcmSource src(intFormat(16, 16), ramp16(2000));
    DummyOutput out(dummy(dir.path() / "o.raw"));
    auto forced = std::make_shared<ForcedStage>();
    PlaybackOptions options;
    options.processing = chainOf({forced});
    play(src, out, options);

    CHECK(forced->ftz);
    CHECK(forced->daz);
    // Compare the control bits only: the low 6 bits are sticky exception
    // flags (e.g. "inexact") that any later arithmetic may set.
    constexpr unsigned kControl = ~0x3Fu;
    CHECK((_mm_getcsr() & kControl) == (before & kControl));
}

TEST_CASE("DoP is never processed", "[dsp][dop]")
{
    ap::test::TempDir dir;
    const auto data = dopBytes(1000);
    DopMemory src(data);
    DummyOutput out(dummy(dir.path() / "o.raw"));
    PlaybackOptions options;
    options.processing = chainOf({std::make_shared<GainStage>(0.5)});

    const auto report = play(src, out, options);
    CHECK_FALSE(report.plan.processing);
    CHECK(readFile(dir.path() / "o.raw") == data);
}

// --- Player -----------------------------------------------------------------

namespace {

Player::OutputFactory dummyFactory(const std::filesystem::path& p, std::function<bool(const PcmFormat&)> accepts = {})
{
    return [p, accepts] { return std::make_unique<DummyOutput>(dummy(p, accepts)); };
}

QueueItem memoryTrack(PcmFormat f, std::vector<std::byte> data)
{
    return {"", [f, data] { return std::make_unique<MemoryPcmSource>(f, data); }};
}

} // namespace

TEST_CASE("Player records the processing path and its active stages", "[dsp][player]")
{
    ap::test::TempDir dir;
    const auto data = ramp16(3000);
    auto gain = std::make_shared<GainStage>(1.0);
    auto forced = std::make_shared<ForcedStage>();
    PlaybackOptions options;
    options.processing = chainOf({gain, forced});
    Player player(dummyFactory(dir.path() / "o.raw"), options);

    player.load(std::make_unique<MemoryPcmSource>(intFormat(16, 16), data));
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    REQUIRE(s.path.has_value());
    CHECK(s.path->processing);
    CHECK(s.path->processingStages == std::vector<std::string>{"test-forced"});
    CHECK(s.path->output.format == intFormat(32, 32));
}

TEST_CASE("Player with a bypassed chain records the direct path, output unchanged", "[dsp][player][bypass]")
{
    ap::test::TempDir dir;
    const auto data = ramp16(3000);
    PlaybackOptions options;
    options.processing = chainOf({std::make_shared<GainStage>(1.0)});
    Player player(dummyFactory(dir.path() / "o.raw"), options);

    player.load(std::make_unique<MemoryPcmSource>(intFormat(16, 16), data));
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.path.has_value());
    CHECK_FALSE(s.path->processing);
    CHECK(s.path->processingStages.empty());
    CHECK(readFile(dir.path() / "o.raw") == data);
}

TEST_CASE("Same-format tracks stay gapless through the processing path", "[dsp][player][gapless]")
{
    ap::test::TempDir dir;
    const auto a = ramp16(2345);
    const auto b = ramp16(1111);
    PlaybackOptions options;
    options.processing = chainOf({std::make_shared<ForcedStage>()});
    // Only 16-bit accepted: the processing path gives back the same bytes.
    Player player(dummyFactory(dir.path() / "o.raw", [](const PcmFormat& f) { return f == intFormat(16, 16); }),
                  options);

    player.setQueue({memoryTrack(intFormat(16, 16), a), memoryTrack(intFormat(16, 16), b)});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    REQUIRE(s.transitions.size() == 1);
    CHECK(s.transitions[0].kind == TransitionKind::Gapless);
    auto expected = a;
    expected.insert(expected.end(), b.begin(), b.end());
    CHECK(readFile(dir.path() / "o.raw") == expected);
}

TEST_CASE("Player never processes a DoP track", "[dsp][player][dop]")
{
    ap::test::TempDir dir;
    const auto data = dopBytes(3000);
    PlaybackOptions options;
    options.allowDop = true;
    options.processing = chainOf({std::make_shared<GainStage>(0.5)});
    Player player(dummyFactory(dir.path() / "o.raw"), options);

    player.load(std::make_unique<DopMemory>(data));
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    REQUIRE(s.path.has_value());
    CHECK_FALSE(s.path->processing);
    CHECK(s.path->dop.has_value());
    CHECK(readFile(dir.path() / "o.raw") == data);
}
