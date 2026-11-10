// Task 3-2: digital volume, and how the player moves between the direct path
// and the processing path while playing.
//
//  - Volume 0 dB (100%): the stage is identity and playback is bit-perfect.
//  - Below 0 dB: the processing path (64-bit, TPDF dither when quantizing).
//  - Changing the volume ramps over 10 ms (no step in the waveform).
//  - Leaving or returning to 0 dB while playing reopens the output at the
//    current position (decided with the user; no frame is lost or repeated).
//  - Float sources (MP3, AAC) on an output without float are quantized
//    through the processing path even at 0 dB; integer sources never are.

#include <ap/engine/dsp/volume.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>

using namespace ap::engine;

namespace {

constexpr std::uint32_t kRate = 48000;

PcmFormat int16()
{
    return PcmFormat{kRate, 2, 16, 16, SampleType::Int, 0};
}

PcmFormat int32()
{
    return PcmFormat{kRate, 2, 32, 32, SampleType::Int, 0};
}

PcmFormat float32()
{
    return PcmFormat{kRate, 2, 32, 32, SampleType::Float, 0};
}

std::vector<std::int16_t> ramp16Values(std::size_t frames)
{
    std::vector<std::int16_t> v(frames * 2);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<std::int16_t>(static_cast<int>((i * 7919) % 60000) - 30000);
    return v;
}

template <typename T>
std::vector<std::byte> bytesOf(const std::vector<T>& v)
{
    std::vector<std::byte> b(v.size() * sizeof(T));
    std::memcpy(b.data(), v.data(), b.size());
    return b;
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

// Calls `at` once when reading passes `frame` (to change settings at a known point).
class TriggerSource final : public PcmSource
{
public:
    TriggerSource(std::unique_ptr<PcmSource> inner, std::vector<std::pair<std::uint64_t, std::function<void()>>> at)
        : inner_(std::move(inner)), at_(std::move(at))
    {
    }
    const PcmFormat& format() const noexcept override { return inner_->format(); }
    std::uint64_t totalFrames() const noexcept override { return inner_->totalFrames(); }
    std::size_t read(std::span<std::byte> dst) override
    {
        const auto n = inner_->read(dst);
        for (auto& [frame, fn] : at_)
            if (fn && inner_->position() >= frame)
            {
                fn();
                fn = nullptr;
            }
        return n;
    }
    bool seek(std::uint64_t f) override { return inner_->seek(f); }
    std::uint64_t position() const noexcept override { return inner_->position(); }
    std::optional<DopInfo> dop() const override { return dop_; }
    std::optional<DopInfo> dop_;

private:
    std::unique_ptr<PcmSource> inner_;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> at_;
};

Player::OutputFactory dummyFactory(const std::filesystem::path& p, std::function<bool(const PcmFormat&)> accepts,
                                   std::shared_ptr<std::vector<PcmFormat>> log = {})
{
    return [p, accepts, log] {
        return std::make_unique<DummyOutput>(
            DummyOutput::Options{.path = p, .periodFrames = {480}, .accepts = accepts, .sessionLog = log});
    };
}

PlaybackOptions withVolume(std::shared_ptr<VolumeStage> v, bool allowDop = false)
{
    PlaybackOptions o;
    o.allowDop = allowDop;
    o.processing = std::make_shared<ProcessingChain>(std::vector<std::shared_ptr<ProcessingStage>>{std::move(v)});
    return o;
}

std::vector<double> process(VolumeStage& v, std::size_t frames, double value = 1.0)
{
    std::vector<double> s(frames * 2, value);
    v.process(s, 2);
    return s;
}

} // namespace

TEST_CASE("Volume: identity only at 0 dB", "[dsp][volume]")
{
    VolumeStage v;
    CHECK(v.isIdentity());
    CHECK(v.name() == "volume");
    v.setGainDb(-6.0);
    CHECK_FALSE(v.isIdentity());
    v.setGainDb(+3.0); // digital gain above 0 dB is not offered (it would clip)
    CHECK(v.gainDb() == 0.0);
    v.prepare(kRate, 2);
    CHECK(v.isIdentity());
}

TEST_CASE("Volume: the gain from the start of a session, without a ramp", "[dsp][volume]")
{
    VolumeStage v;
    v.setGainDb(20.0 * std::log10(0.5));
    v.prepare(kRate, 2);
    const auto s = process(v, 100);
    for (auto x : s)
        CHECK(std::abs(x - 0.5) < 1e-15);
}

TEST_CASE("Volume: a change ramps linearly over 10 ms", "[dsp][volume]")
{
    VolumeStage v;
    v.setGainDb(20.0 * std::log10(0.5));
    v.prepare(kRate, 2);
    process(v, 64);
    v.setGainDb(20.0 * std::log10(0.25));
    const auto s = process(v, 1000);

    const std::size_t rampFrames = kRate / 100; // 480
    double prev = 0.5;
    for (std::size_t f = 0; f < 1000; ++f)
    {
        CHECK(s[2 * f] == s[2 * f + 1]); // both channels follow the same gain
        CHECK(s[2 * f] <= prev);         // monotonic, no overshoot
        CHECK(prev - s[2 * f] <= 0.25 / rampFrames + 1e-12); // no step larger than one ramp increment
        prev = s[2 * f];
    }
    CHECK(s[2 * (rampFrames - 1)] == 0.25); // exactly the target at the end of the ramp
    CHECK(s.back() == 0.25);
}

TEST_CASE("Volume: mute gives silence", "[dsp][volume]")
{
    VolumeStage v;
    v.setGainDb(-std::numeric_limits<double>::infinity());
    v.prepare(kRate, 2);
    for (auto x : process(v, 10, 0.7))
        CHECK(x == 0.0);
}

TEST_CASE("Volume: back to 0 dB is identity only once the ramp has finished", "[dsp][volume]")
{
    VolumeStage v;
    v.setGainDb(-6.0);
    v.prepare(kRate, 2);
    v.setGainDb(0.0);
    CHECK_FALSE(v.isIdentity()); // still at -6 dB until processed
    process(v, 100);
    CHECK_FALSE(v.isIdentity()); // halfway
    process(v, 1000);
    CHECK(v.isIdentity());
}

TEST_CASE("Player: volume at 100% is bit-perfect", "[dsp][volume][player]")
{
    ap::test::TempDir dir;
    const auto data = bytesOf(ramp16Values(5000));
    auto vol = std::make_shared<VolumeStage>();
    Player player(dummyFactory(dir.path() / "o.raw", {}), withVolume(vol));
    player.load(std::make_unique<MemoryPcmSource>(int16(), data));
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    REQUIRE(s.path.has_value());
    CHECK_FALSE(s.path->processing);
    CHECK(readFile(dir.path() / "o.raw") == data);
}

TEST_CASE("Player: leaving and returning to 100% reopens at the position, nothing lost", "[dsp][volume][player]")
{
    ap::test::TempDir dir;
    constexpr std::size_t total = 30000;
    const auto values = ramp16Values(total);
    auto vol = std::make_shared<VolumeStage>();
    auto log = std::make_shared<std::vector<PcmFormat>>();
    Player player(dummyFactory(dir.path() / "o.raw", {}, log), withVolume(vol));

    auto src = std::make_unique<TriggerSource>(
        std::make_unique<MemoryPcmSource>(int16(), bytesOf(values)),
        std::vector<std::pair<std::uint64_t, std::function<void()>>>{
            {5000, [vol] { vol->setGainDb(20.0 * std::log10(0.5)); }},
            {20000, [vol] { vol->setGainDb(0.0); }}});
    player.load(std::move(src));
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    // direct (16-bit) -> processing (32-bit) -> direct (16-bit)
    REQUIRE(log->size() == 3);
    CHECK((*log)[0] == int16());
    CHECK((*log)[1] == int32());
    CHECK((*log)[2] == int16());
    REQUIRE(s.transitions.size() == 2);
    for (const auto& t : s.transitions)
    {
        CHECK(t.kind == TransitionKind::ProcessingChanged);
        CHECK(t.fromIndex == 0);
        CHECK(t.toIndex == 0);
    }
    REQUIRE(s.path.has_value());
    CHECK_FALSE(s.path->processing); // back to the direct path at the end

    // Split the output into the three sessions. Every frame is there exactly once.
    // The trigger fires in the read that passes frame 5000; the next read ends
    // the session, so the direct part is whole periods of 480 frames.
    const auto out = readFile(dir.path() / "o.raw");
    const std::size_t directA = (5000 + 479) / 480 * 480;
    // Second session length: whatever remains after the 16-bit parts, at 8 bytes per frame.
    // out = directA*4 + mid*8 + (total - directA - mid)*4  =>  mid = (out - total*4) / 4
    REQUIRE(out.size() >= total * 4);
    const std::size_t mid = (out.size() - total * 4) / 4;
    REQUIRE(directA + mid <= total);
    REQUIRE(mid > 480);

    std::vector<std::int16_t> head(directA * 2);
    std::memcpy(head.data(), out.data(), head.size() * 2);
    CHECK(std::equal(head.begin(), head.end(), values.begin()));

    std::vector<std::int32_t> middle(mid * 2);
    std::memcpy(middle.data(), out.data() + directA * 4, middle.size() * 4);
    // The session starts at the new gain (0.5). Its last 480 frames are the
    // 10 ms ramp back to 1.0, after which the stage is identity again.
    bool halved = true;
    for (std::size_t i = 0; i < (mid - 480) * 2; ++i)
    {
        const double expected = values[directA * 2 + i] * 0.5 * 65536.0;
        halved = halved && std::abs(middle[i] - expected) <= 2.0; // TPDF dither: +-1 LSB of 32 bits
    }
    CHECK(halved);
    // The last frame of the ramp is at gain 1.0 exactly (within the dither).
    CHECK(std::abs(middle[(mid - 1) * 2] - values[(directA + mid - 1) * 2] * 65536.0) <= 2.0);

    const std::size_t tailFrames = total - directA - mid;
    std::vector<std::int16_t> tail(tailFrames * 2);
    std::memcpy(tail.data(), out.data() + directA * 4 + mid * 8, tail.size() * 2);
    CHECK(std::equal(tail.begin(), tail.end(), values.begin() + static_cast<std::ptrdiff_t>((directA + mid) * 2)));
}

TEST_CASE("Player: a float source on an integer-only output is quantized with dither even at 100%", "[dsp][volume][player]")
{
    ap::test::TempDir dir;
    std::vector<float> v(4000);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<float>(0.8 * std::sin(0.01 * static_cast<double>(i)));
    auto vol = std::make_shared<VolumeStage>();
    Player player(dummyFactory(dir.path() / "o.raw", [](const PcmFormat& f) { return f.type == SampleType::Int; }),
                  withVolume(vol));
    player.load(std::make_unique<MemoryPcmSource>(float32(), bytesOf(v)));
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    REQUIRE(s.path.has_value());
    CHECK(s.path->processing);
    CHECK(s.path->dither);
    CHECK(s.path->processingStages.empty());
    CHECK(s.path->output.format == int32());
    CHECK(s.transitions.empty());
    const auto out = readFile(dir.path() / "o.raw");
    std::vector<std::int32_t> codes(out.size() / 4);
    std::memcpy(codes.data(), out.data(), out.size());
    REQUIRE(codes.size() == v.size());
    for (std::size_t i = 0; i < v.size(); ++i)
        CHECK(std::abs(codes[i] - v[i] * 2147483648.0) <= 2.0);
}

TEST_CASE("Player: a float source on a float output stays bit-perfect", "[dsp][volume][player]")
{
    ap::test::TempDir dir;
    const auto data = bytesOf(std::vector<float>{0.5f, -0.5f, 1.26f, 0.1f, 0.0f, -1.0f});
    Player player(dummyFactory(dir.path() / "o.raw", {}), withVolume(std::make_shared<VolumeStage>()));
    player.load(std::make_unique<MemoryPcmSource>(float32(), data));
    player.play();
    player.waitUntilIdle();
    REQUIRE(player.status().path.has_value());
    CHECK_FALSE(player.status().path->processing);
    CHECK(readFile(dir.path() / "o.raw") == data);
}

TEST_CASE("Player: an integer source the output cannot take is still refused", "[dsp][volume][player]")
{
    ap::test::TempDir dir;
    Player player(dummyFactory(dir.path() / "o.raw", [](const PcmFormat& f) { return f.containerBits == 32; }),
                  withVolume(std::make_shared<VolumeStage>()));
    player.load(std::make_unique<MemoryPcmSource>(PcmFormat{kRate, 2, 16, 16, SampleType::Int, 0},
                                                  bytesOf(ramp16Values(100))));
    player.play();
    player.waitUntilIdle();
    // 16-bit fits a 32-bit container losslessly (repack), so this plays unchanged...
    CHECK(player.status().state == PlayerState::Ended);
    CHECK_FALSE(player.status().path->processing);

    // ...but a rate the output does not run at is never converted.
    Player strict(dummyFactory(dir.path() / "o2.raw", [](const PcmFormat& f) { return f.sampleRate == 96000; }),
                  withVolume(std::make_shared<VolumeStage>()));
    strict.load(std::make_unique<MemoryPcmSource>(int16(), bytesOf(ramp16Values(100))));
    strict.play();
    strict.waitUntilIdle();
    CHECK(strict.status().state == PlayerState::Error);
    CHECK(strict.status().error.kind == PlayerErrorKind::FormatNotSupported);
}

TEST_CASE("Player: a volume change never switches a DoP track to processing", "[dsp][volume][player][dop]")
{
    ap::test::TempDir dir;
    std::vector<std::byte> data;
    for (int i = 0; i < 20000; ++i)
        for (int ch = 0; ch < 2; ++ch)
            data.insert(data.end(), {std::byte{0x69}, std::byte{0x96}, (i % 2) ? std::byte{0xFA} : std::byte{0x05}});
    auto vol = std::make_shared<VolumeStage>();
    auto log = std::make_shared<std::vector<PcmFormat>>();
    auto src = std::make_unique<TriggerSource>(
        std::make_unique<MemoryPcmSource>(PcmFormat{176400, 2, 24, 24, SampleType::Int, 0}, data),
        std::vector<std::pair<std::uint64_t, std::function<void()>>>{{2000, [vol] { vol->setGainDb(-10.0); }}});
    src->dop_ = DopInfo{2822400, "DSF"};
    Player player(dummyFactory(dir.path() / "o.raw", {}, log), withVolume(vol, true));
    player.load(std::move(src));
    player.play();
    player.waitUntilIdle();

    CHECK(player.status().state == PlayerState::Ended);
    CHECK(log->size() == 1);
    CHECK(player.status().transitions.empty());
    CHECK(readFile(dir.path() / "o.raw") == data);
}
