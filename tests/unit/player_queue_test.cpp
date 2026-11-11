// Task 1-2: per-track sample rate and a gapless play queue.
//   - consecutive tracks of the same format: no gap, no overlap
//     (the output is exactly the concatenation of the inputs)
//   - a format change reopens the output and is recorded

#include <ap/engine/output/dummy_output.h>
#include <ap/engine/player.h>
#include <ap/signalgen/signals.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ap::engine;
using namespace std::chrono_literals;
namespace sg = ap::signalgen;

namespace {

// Distinct content per track: a bit pattern shifted by `offset` frames.
std::vector<std::byte> trackData(std::uint32_t rate, std::size_t frames, std::size_t offset)
{
    const auto pcm = sg::bitPattern(sg::Layout{rate, 2, frames + offset}, sg::SampleFormat::Int24);
    std::vector<std::byte> bytes;
    for (std::size_t i = offset * 2; i < pcm.intSamples.size(); ++i)
    {
        const auto u = static_cast<std::uint32_t>(pcm.intSamples[i]);
        bytes.push_back(static_cast<std::byte>(u));
        bytes.push_back(static_cast<std::byte>(u >> 8));
        bytes.push_back(static_cast<std::byte>(u >> 16));
    }
    return bytes;
}

PcmFormat fmt(std::uint32_t rate)
{
    return PcmFormat{rate, 2, 24, 24, SampleType::Int, 3};
}

QueueItem item(const std::string& name, std::uint32_t rate, std::vector<std::byte> data)
{
    return {name, [rate, data] { return std::make_unique<MemoryPcmSource>(fmt(rate), data); }};
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

std::vector<std::byte> concat(std::initializer_list<const std::vector<std::byte>*> parts)
{
    std::vector<std::byte> out;
    for (const auto* p : parts)
        out.insert(out.end(), p->begin(), p->end());
    return out;
}

struct Rig
{
    ap::test::TempDir dir;
    std::shared_ptr<std::vector<PcmFormat>> sessions = std::make_shared<std::vector<PcmFormat>>();

    Player::OutputFactory factory(std::function<bool(const PcmFormat&)> accepts = {}, bool realtime = false)
    {
        return [this, accepts, realtime] {
            return std::make_unique<DummyOutput>(DummyOutput::Options{.path = dir.path() / "out.raw",
                                                                      .periodFrames = {441, 1000, 7},
                                                                      .check = {},
                                                                      .paceInRealTime = realtime,
                                                                      .accepts = accepts,
                                                                      .sessionLog = sessions});
        };
    }
    std::vector<std::byte> output() const { return readFile(dir.path() / "out.raw"); }
};

} // namespace

TEST_CASE("Same-format tracks play gaplessly: output is the exact concatenation", "[engine][queue][gapless]")
{
    Rig rig;
    const auto a = trackData(44100, 1000, 0);
    const auto b = trackData(44100, 1500, 3);
    const auto c = trackData(44100, 700, 11);

    Player player(rig.factory());
    player.setQueue({item("A", 44100, a), item("B", 44100, b), item("C", 44100, c)});
    CHECK(player.status().queueLength == 3);
    CHECK(player.status().trackName == "A");

    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(s.queueIndex == 2);
    CHECK(s.trackName == "C");
    CHECK(s.positionFrames == 700u);
    CHECK(rig.output() == concat({&a, &b, &c})); // no gap, no overlap
    CHECK(rig.sessions->size() == 1);            // the output was opened once

    REQUIRE(s.transitions.size() == 2);
    CHECK(s.transitions[0].kind == TransitionKind::Gapless);
    CHECK(s.transitions[0].fromIndex == 0);
    CHECK(s.transitions[0].toIndex == 1);
    CHECK(s.transitions[1].kind == TransitionKind::Gapless);
}

TEST_CASE("A sample-rate change reopens the output and is recorded", "[engine][queue][rate]")
{
    Rig rig;
    const auto a = trackData(44100, 1200, 0);
    const auto b = trackData(96000, 900, 5);

    Player player(rig.factory());
    player.setQueue({item("A", 44100, a), item("B", 96000, b)});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(rig.output() == concat({&a, &b})); // every frame of both, in order, nothing converted
    REQUIRE(rig.sessions->size() == 2);
    CHECK((*rig.sessions)[0].sampleRate == 44100u);
    CHECK((*rig.sessions)[1].sampleRate == 96000u);

    REQUIRE(s.transitions.size() == 1);
    const auto& t = s.transitions[0];
    CHECK(t.kind == TransitionKind::Reopened);
    CHECK(t.fromFormat.sampleRate == 44100u);
    CHECK(t.toFormat.sampleRate == 96000u);
    CHECK(t.switchMilliseconds >= 0.0);
}

TEST_CASE("Gapless and reopened transitions in one queue", "[engine][queue]")
{
    Rig rig;
    const auto a = trackData(48000, 800, 0);
    const auto b = trackData(48000, 800, 1);
    const auto c = trackData(96000, 800, 2);
    const auto d = trackData(96000, 800, 3);

    Player player(rig.factory());
    player.setQueue({item("A", 48000, a), item("B", 48000, b), item("C", 96000, c), item("D", 96000, d)});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(rig.output() == concat({&a, &b, &c, &d}));
    CHECK(rig.sessions->size() == 2);
    REQUIRE(s.transitions.size() == 3);
    CHECK(s.transitions[0].kind == TransitionKind::Gapless);
    CHECK(s.transitions[1].kind == TransitionKind::Reopened);
    CHECK(s.transitions[2].kind == TransitionKind::Gapless);
}

TEST_CASE("A next track the device cannot take stops the queue with the reason", "[engine][queue][rate]")
{
    Rig rig;
    const auto a = trackData(48000, 1000, 0);
    const auto b = trackData(96000, 1000, 1);
    const auto only48k = [](const PcmFormat& f) { return f.sampleRate == 48000; };

    Player player(rig.factory(only48k));
    player.setQueue({item("A", 48000, a), item("B", 96000, b)});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Error);
    CHECK(s.queueIndex == 1);
    CHECK(s.error.kind == PlayerErrorKind::FormatNotSupported);
    CHECK(s.error.formatCheck.reason == FormatRejection::SampleRate);
    CHECK(s.error.formatCheck.acceptedRates == std::vector<std::uint32_t>{48000});
    CHECK(rig.output() == a); // the first track played completely
}

TEST_CASE("A track that cannot be opened ends the queue with a load error", "[engine][queue]")
{
    Rig rig;
    const auto a = trackData(48000, 1000, 0);
    QueueItem broken{"broken.wav", [] () -> std::unique_ptr<PcmSource> { throw std::runtime_error("bad file"); }};

    Player player(rig.factory());
    player.setQueue({item("A", 48000, a), broken});
    player.play();
    player.waitUntilIdle();

    const auto s = player.status();
    CHECK(s.state == PlayerState::Error);
    CHECK(s.error.kind == PlayerErrorKind::Load);
    CHECK(s.queueIndex == 1);
    CHECK(rig.output() == a);
}

TEST_CASE("A first track that cannot be opened is reported by setQueue", "[engine][queue]")
{
    Rig rig;
    QueueItem broken{"broken.wav", [] () -> std::unique_ptr<PcmSource> { throw std::runtime_error("bad file"); }};
    Player player(rig.factory());
    player.setQueue({broken});
    const auto s = player.status();
    CHECK(s.state == PlayerState::Error);
    CHECK(s.error.kind == PlayerErrorKind::Load);
}

TEST_CASE("Stop and play continue in the same track", "[engine][queue]")
{
    Rig rig;
    const auto a = trackData(48000, 4800, 0);   // 0.1 s
    const auto b = trackData(48000, 48000, 1);  // 1 s

    Player player(rig.factory({}, true));
    player.setQueue({item("A", 48000, a), item("B", 48000, b)});
    player.play();
    std::this_thread::sleep_for(300ms); // into track B
    player.stop();

    auto s = player.status();
    CHECK(s.state == PlayerState::Stopped);
    CHECK(s.queueIndex == 1);
    const auto stoppedAt = s.positionFrames;
    CHECK(stoppedAt > 0u);

    player.play();
    player.waitUntilIdle();
    s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(s.queueIndex == 1);

    // The second run continues track B where it stopped.
    const auto out = rig.output();
    const auto rest = std::vector<std::byte>(b.begin() + static_cast<std::ptrdiff_t>(stoppedAt * 6), b.end());
    CHECK(out == rest);
}

TEST_CASE("Play after the end of the queue starts again from the first track", "[engine][queue]")
{
    Rig rig;
    const auto a = trackData(48000, 500, 0);
    const auto b = trackData(48000, 500, 1);

    Player player(rig.factory());
    player.setQueue({item("A", 48000, a), item("B", 48000, b)});
    player.play();
    player.waitUntilIdle();
    REQUIRE(player.status().state == PlayerState::Ended);

    player.play();
    player.waitUntilIdle();
    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(rig.output() == concat({&a, &b}));
}

TEST_CASE("Starting the queue at a later track", "[engine][queue]")
{
    Rig rig;
    const auto a = trackData(48000, 500, 0);
    const auto b = trackData(48000, 500, 1);
    Player player(rig.factory());
    player.setQueue({item("A", 48000, a), item("B", 48000, b)}, 1);
    CHECK(player.status().trackName == "B");
    player.play();
    player.waitUntilIdle();
    CHECK(rig.output() == b);
}

// --- Task 6-2: moving in the queue and changing what comes next ---------------

TEST_CASE("playAt: stopped, the queue waits at that track", "[engine][queue][edit]")
{
    Rig rig;
    const auto a = trackData(48000, 500, 0);
    const auto b = trackData(48000, 500, 1);
    const auto c = trackData(48000, 500, 2);
    Player player(rig.factory());
    player.setQueue({item("A", 48000, a), item("B", 48000, b), item("C", 48000, c)});
    player.playAt(2);
    auto s = player.status();
    CHECK(s.state == PlayerState::Stopped);
    CHECK(s.queueIndex == 2);
    CHECK(s.trackName == "C");
    CHECK(s.positionFrames == 0u);
    player.play();
    player.waitUntilIdle();
    CHECK(rig.output() == c);
    player.playAt(7); // past the end: nothing changes
    CHECK(player.status().queueIndex == 2);
}

TEST_CASE("playAt: playing, it goes on from the start of that track", "[engine][queue][edit]")
{
    Rig rig;
    const auto a = trackData(48000, 48000, 0); // 1 s each
    const auto b = trackData(48000, 48000, 1);
    const auto c = trackData(48000, 4800, 2);
    Player player(rig.factory({}, true));
    player.setQueue({item("A", 48000, a), item("B", 48000, b), item("C", 48000, c)});
    player.play();
    std::this_thread::sleep_for(200ms);
    player.playAt(2);
    CHECK(player.status().state == PlayerState::Playing);
    player.waitUntilIdle();
    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(s.queueIndex == 2);
    // The second output session (after the jump) is exactly track C.
    CHECK(rig.output() == c);
}

TEST_CASE("replaceUpcoming: the current track plays on, the new ones follow gaplessly", "[engine][queue][edit]")
{
    Rig rig;
    const auto a = trackData(48000, 24000, 0); // 0.5 s
    const auto b = trackData(48000, 4800, 1);
    const auto c = trackData(48000, 4800, 2);
    const auto d = trackData(48000, 4800, 3);
    Player player(rig.factory({}, true));
    player.setQueue({item("A", 48000, a), item("B", 48000, b)});
    player.play();
    std::this_thread::sleep_for(100ms); // in A
    player.replaceUpcoming({item("C", 48000, c), item("D", 48000, d)});
    CHECK(player.status().queueLength == 3);
    CHECK(player.status().trackName == "A");
    player.waitUntilIdle();
    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(s.trackName == "D");
    CHECK(rig.output() == concat({&a, &c, &d})); // B is gone; no gap, no overlap
    CHECK(rig.sessions->size() == 1);
}

TEST_CASE("replaceUpcoming: stopped, and with nothing after", "[engine][queue][edit]")
{
    Rig rig;
    const auto a = trackData(48000, 500, 0);
    const auto b = trackData(48000, 500, 1);
    const auto c = trackData(48000, 500, 2);
    Player player(rig.factory());
    player.setQueue({item("A", 48000, a), item("B", 48000, b)});
    player.replaceUpcoming({item("C", 48000, c)});
    player.play();
    player.waitUntilIdle();
    CHECK(rig.output() == concat({&a, &c}));

    Rig rig2;
    Player alone(rig2.factory());
    alone.setQueue({item("A", 48000, a), item("B", 48000, b)});
    alone.replaceUpcoming({});
    CHECK(alone.status().queueLength == 1);
    alone.play();
    alone.waitUntilIdle();
    CHECK(rig2.output() == a);
}

TEST_CASE("replaceUpcoming many times while playing never mixes tracks up", "[engine][queue][edit]")
{
    // Each replacement changes what follows A while the next track may be
    // loading; whatever follows A must be one whole track of the last list.
    Rig rig;
    const auto a = trackData(48000, 9600, 0); // 0.2 s
    std::vector<std::vector<std::byte>> others;
    for (int i = 0; i < 20; ++i)
        others.push_back(trackData(48000, 480, 10 + i));
    Player player(rig.factory({}, true));
    player.setQueue({item("A", 48000, a), item("X", 48000, others[0])});
    player.play();
    for (int i = 1; i < 20; ++i)
    {
        player.replaceUpcoming({item("N" + std::to_string(i), 48000, others[i])});
        std::this_thread::sleep_for(5ms);
    }
    player.waitUntilIdle();
    const auto s = player.status();
    CHECK(s.state == PlayerState::Ended);
    CHECK(s.trackName == "N19");
    CHECK(rig.output() == concat({&a, &others[19]}));
}
