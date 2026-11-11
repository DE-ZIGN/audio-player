// Task 5-6: measuring the tracks of the library. True Peak and loudness come
// from the engine's decoders and meters (the same values as the analysis of
// tasks 3-3 and 3-4); albums are gated together; what changed is measured
// again; DSD and undecodable files are recorded and not retried.

#include <ap/library/analysis.h>
#include <ap/library/library_db.h>

#include <ap/engine/dsp/loudness.h>
#include <ap/engine/dsp/replay_gain.h>
#include <ap/engine/pcm_source.h>

#include "dsd_test_files.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

using namespace ap::library;
using namespace std::chrono_literals;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;
using Strings = std::vector<std::string>;

namespace {

constexpr std::uint32_t kRate = 48000;

// 16-bit stereo WAV of a 1 kHz tone at `dbfs` (in phase on both channels: dbfs LUFS).
std::vector<std::int16_t> tone(double seconds, double dbfs)
{
    const auto frames = static_cast<std::size_t>(seconds * kRate);
    const double a = std::pow(10.0, dbfs / 20.0) * 32767.0;
    std::vector<std::int16_t> v(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
        v[2 * i] = v[2 * i + 1] =
            static_cast<std::int16_t>(std::lround(a * std::sin(2 * std::numbers::pi * 1000.0 * i / kRate)));
    return v;
}

// The ISP pattern (+, +, -, -) at full scale: inter-sample peaks of +3 dB.
std::vector<std::int16_t> isp(double seconds)
{
    const auto frames = static_cast<std::size_t>(seconds * kRate);
    std::vector<std::int16_t> v(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
        v[2 * i] = v[2 * i + 1] = (i % 4 < 2) ? 32767 : -32767;
    return v;
}

fs::path writeWav(const fs::path& p, const std::vector<std::int16_t>& samples)
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
    out.write("RIFF", 4);
    u32(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(kRate);
    u32(kRate * 4);
    u16(4);
    u16(16);
    out.write("data", 4);
    u32(bytes);
    out.write(reinterpret_cast<const char*>(samples.data()), bytes);
    return p;
}

double db(double x)
{
    return 20.0 * std::log10(x);
}

struct Fixture
{
    ap::test::TempDir dir;
    LibraryDb lib = LibraryDb::openInMemory();

    std::int64_t add(const std::string& name, const std::vector<std::int16_t>& samples, const std::string& album = {},
                     const std::string& albumArtist = {})
    {
        FileFacts f;
        f.path = writeWav(dir.path() / name, samples);
        f.size = fs::file_size(f.path);
        const auto t = lib.addTrack(f);
        lib.database().prepare("UPDATE tracks SET fingerprint = ? WHERE id = ?").bind(1, "fp-" + name).bind(2, t).run();
        if (!album.empty())
            lib.setValues(t, Layer::Tag, "album", Strings{album});
        if (!albumArtist.empty())
            lib.setValues(t, Layer::Tag, "albumartist", Strings{albumArtist});
        return t;
    }

    double real(const std::string& sql)
    {
        auto st = lib.database().prepare(sql);
        REQUIRE(st.step());
        REQUIRE_FALSE(st.isNull(0));
        return st.real(0);
    }

    std::string text(const std::string& sql)
    {
        auto st = lib.database().prepare(sql);
        REQUIRE(st.step());
        return st.text(0);
    }
};

// The engine's own analysis of the same samples, for comparison.
ap::engine::TrackAnalysis engineAnalysis(const std::vector<std::int16_t>& samples)
{
    std::vector<std::byte> b(samples.size() * 2);
    std::memcpy(b.data(), samples.data(), b.size());
    ap::engine::MemoryPcmSource src(ap::engine::PcmFormat{kRate, 2, 16, 16, ap::engine::SampleType::Int, 0},
                                    std::move(b));
    return ap::engine::analyzeTrack(src);
}

} // namespace

TEST_CASE("Analysis: a track's true peak, loudness and exact format", "[library][analysis]")
{
    Fixture f;
    const auto samples = tone(5.0, -20.0);
    const auto t = f.add("a.wav", samples);
    CHECK(pendingAnalysis(f.lib) == 1);

    const auto r = analyzePending(f.lib);
    CHECK(r.tracks == 1);
    CHECK(r.albums == 0); // no album
    CHECK(pendingAnalysis(f.lib) == 0);

    const auto expected = engineAnalysis(samples);
    const auto id = std::to_string(t);
    CHECK(f.text("SELECT status FROM track_analysis WHERE track_id = " + id) == "ok");
    CHECK(f.real("SELECT loudness_lufs FROM track_analysis WHERE track_id = " + id) == expected.integratedLufs);
    CHECK(f.real("SELECT true_peak FROM track_analysis WHERE track_id = " + id) == expected.peaks.truePeakMax);
    CHECK(f.real("SELECT sample_peak FROM track_analysis WHERE track_id = " + id) == expected.peaks.samplePeakMax);
    CHECK_THAT(f.real("SELECT loudness_lufs FROM track_analysis WHERE track_id = " + id), WithinAbs(-20.0, 0.1));
    CHECK_THAT(db(f.real("SELECT true_peak FROM track_analysis WHERE track_id = " + id)), WithinAbs(-20.0, 0.05));
    // The decoder's exact facts.
    CHECK(f.text("SELECT container FROM tracks WHERE id = " + id) == "WAV");
    CHECK(f.real("SELECT sample_rate FROM tracks WHERE id = " + id) == kRate);
    CHECK(f.real("SELECT bits FROM tracks WHERE id = " + id) == 16);
    CHECK(f.real("SELECT frames FROM tracks WHERE id = " + id) == 5 * kRate);
}

TEST_CASE("Analysis: inter-sample peaks are measured, not the sample peak", "[library][analysis]")
{
    Fixture f;
    const auto t = f.add("isp.wav", isp(1.0));
    analyzePending(f.lib);
    const auto tp = f.real("SELECT true_peak FROM track_analysis WHERE track_id = " + std::to_string(t));
    const auto sp = f.real("SELECT sample_peak FROM track_analysis WHERE track_id = " + std::to_string(t));
    CHECK(db(tp) > db(sp) + 3.0); // +3.01 dB, and a little more at the abrupt ends
}

TEST_CASE("Analysis: an album is gated as a whole", "[library][analysis]")
{
    Fixture f;
    const auto loud = tone(5.0, -20.0);
    const auto quiet = tone(5.0, -30.0);
    const auto a = f.add("1.wav", loud, "Album", "Artist");
    const auto b = f.add("2.wav", quiet, "Album", "Artist");
    const auto other = f.add("3.wav", quiet, "Another Album", "Artist");
    const auto r = analyzePending(f.lib);
    CHECK(r.tracks == 3);
    CHECK(r.albums == 2);

    const std::vector<ap::engine::TrackAnalysis> members{engineAnalysis(loud), engineAnalysis(quiet)};
    const auto album = ap::engine::analyzeAlbum(members);
    const auto rgA = replayGainOf(f.lib, a);
    const auto rgB = replayGainOf(f.lib, b);
    REQUIRE(rgA.has_value());
    REQUIRE(rgB.has_value());
    REQUIRE(rgA->albumGainDb.has_value());
    CHECK(*rgA->albumGainDb == ap::engine::peakLimitedGainDb(ap::engine::replayGainDb(album.integratedLufs), album.truePeak));
    CHECK(rgA->albumGainDb == rgB->albumGainDb);
    CHECK(rgA->albumPeak == album.truePeak);
    CHECK_THAT(rgA->trackGainDb, WithinAbs(2.0, 0.1));  // -18 - (-20)
    CHECK_THAT(rgB->trackGainDb, WithinAbs(12.0, 0.1)); // -18 - (-30)
    CHECK_THAT(*rgA->albumGainDb, WithinAbs(-18.0 - 10 * std::log10((0.01 + 0.001) / 2), 0.1)); // not an average of dB
    CHECK(replayGainOf(f.lib, other)->albumGainDb == replayGainOf(f.lib, other)->trackGainDb); // an album of one
}

TEST_CASE("Analysis: albums are grouped by album artist (or artist) and album", "[library][analysis]")
{
    Fixture f;
    const auto a = f.add("1.wav", tone(1.0, -20.0), "Greatest Hits", "Artist A");
    const auto b = f.add("2.wav", tone(1.0, -20.0), "Greatest Hits", "Artist B"); // the same title, another album
    f.lib.setValues(b, Layer::Tag, "artist", Strings{"ignored when there is an album artist"});
    const auto c = f.add("3.wav", tone(1.0, -20.0), "Greatest Hits");
    f.lib.setValues(c, Layer::Tag, "artist", Strings{"Artist A"}); // no album artist: the artist
    const auto lone = f.add("4.wav", tone(1.0, -20.0)); // no album
    const auto r = analyzePending(f.lib);
    CHECK(r.tracks == 4);
    CHECK(r.albums == 2); // {a, c} and {b}
    auto key = [&](std::int64_t t) {
        auto st = f.lib.database().prepare("SELECT album_key FROM track_analysis WHERE track_id = ?");
        st.bind(1, t);
        REQUIRE(st.step());
        return st.isNull(0) ? std::string("(none)") : st.text(0);
    };
    CHECK(key(a) == key(c));
    CHECK(key(a) != key(b));
    CHECK(key(lone) == "(none)");
}

TEST_CASE("Analysis: what changed is measured again, nothing else", "[library][analysis]")
{
    Fixture f;
    const auto a = f.add("1.wav", tone(1.0, -20.0), "Album", "Artist");
    f.add("2.wav", tone(1.0, -20.0), "Album", "Artist");
    analyzePending(f.lib);
    CHECK(analyzePending(f.lib).tracks == 0); // nothing waits

    // The file changed (its fingerprint): the track, and with it its album.
    f.lib.database().prepare("UPDATE tracks SET fingerprint = 'changed' WHERE id = ?").bind(1, a).run();
    CHECK(pendingAnalysis(f.lib) == 1);
    auto r = analyzePending(f.lib);
    CHECK(r.tracks == 2); // the album is gated as a whole, so both are decoded
    CHECK(r.albums == 1);

    // A new track in the album: the album again.
    f.add("3.wav", tone(1.0, -30.0), "Album", "Artist");
    r = analyzePending(f.lib);
    CHECK(r.tracks == 3);
    CHECK(r.albums == 1);

    // A newer analyser.
    f.lib.database().exec("UPDATE track_analysis SET analyzer_version = 0");
    CHECK(pendingAnalysis(f.lib) == 3);
}

TEST_CASE("Analysis: DSD and files that do not decode are recorded and not retried", "[library][analysis]")
{
    Fixture f;
    const std::vector<std::uint8_t> dsdBytes(4096, 0x69); // DSD silence
    const auto dsf = ap::test::makeDsf({dsdBytes, dsdBytes}, 2822400);
    const auto dsfPath = f.dir.path() / "a.dsf";
    std::ofstream(dsfPath, std::ios::binary).write(reinterpret_cast<const char*>(dsf.data()),
                                                  static_cast<std::streamsize>(dsf.size()));
    FileFacts d;
    d.path = dsfPath;
    const auto dsd = f.lib.addTrack(d);
    FileFacts bad;
    bad.path = f.dir.path() / "bad.flac";
    std::ofstream(bad.path) << "not audio";
    const auto broken = f.lib.addTrack(bad);

    const auto r = analyzePending(f.lib);
    CHECK(r.notPcm == 1);
    CHECK(r.failed == 1);
    CHECK(r.errors.size() == 1);
    CHECK(f.text("SELECT status FROM track_analysis WHERE track_id = " + std::to_string(dsd)) == "not_pcm");
    CHECK(f.text("SELECT status FROM track_analysis WHERE track_id = " + std::to_string(broken)) == "failed");
    CHECK(f.text("SELECT container FROM tracks WHERE id = " + std::to_string(dsd)) == "DSF");
    CHECK_FALSE(replayGainOf(f.lib, dsd).has_value());
    CHECK(pendingAnalysis(f.lib) == 0); // not retried
    f.lib.database().prepare("UPDATE tracks SET fingerprint = 'fixed' WHERE id = ?").bind(1, broken).run();
    CHECK(pendingAnalysis(f.lib) == 1); // until the file changes
}

TEST_CASE("Analysis: a stop request is honoured", "[library][analysis]")
{
    Fixture f;
    f.add("1.wav", tone(5.0, -20.0));
    std::atomic<bool> stop{true};
    const auto r = analyzePending(f.lib, &stop);
    CHECK(r.stopped);
    CHECK(r.tracks == 0);
    CHECK(pendingAnalysis(f.lib) == 1);
}

TEST_CASE("Analysis: the background worker measures what is added", "[library][analysis]")
{
    ap::test::TempDir dir;
    const auto dbFile = dir.path() / "library.db";
    {
        auto lib = LibraryDb::open(dbFile);
        for (int i = 0; i < 3; ++i)
        {
            FileFacts f;
            f.path = writeWav(dir.path() / ("t" + std::to_string(i) + ".wav"), tone(2.0, -20.0 - i));
            lib.addTrack(f);
        }
    }
    AnalysisWorker worker(dbFile);
    REQUIRE(worker.waitUntilIdle(30s));
    CHECK(worker.total().tracks == 3);
    {
        auto lib = LibraryDb::open(dbFile);
        CHECK(pendingAnalysis(lib) == 0);
        FileFacts f;
        f.path = writeWav(dir.path() / "late.wav", tone(1.0, -25.0));
        lib.addTrack(f);
    }
    worker.wake();
    std::this_thread::sleep_for(50ms);
    REQUIRE(worker.waitUntilIdle(30s));
    CHECK(worker.total().tracks == 4);
}
