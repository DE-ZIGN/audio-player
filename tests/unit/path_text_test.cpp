// A file's name in the engine's texts is UTF-8, whatever the system's code
// page (2026-11-20). The texts were built with path::string(), which throws
// for a name the code page lacks: opening such a file that is not there, or
// is not audio, gave the conversion's std::system_error in place of the
// reader's own error. Found when the tests first ran on a Windows that is
// not Japanese (the CI runner, code page 1252).

#include <ap/engine/aac_file_source.h>
#include <ap/engine/aiff_file_source.h>
#include <ap/engine/alac_file_source.h>
#include <ap/engine/audio_file.h>
#include <ap/engine/dsd_source.h>
#include <ap/engine/dsp/eq.h>
#include <ap/engine/flac_file_source.h>
#include <ap/engine/mp4_reader.h>
#include <ap/engine/output/dummy_output.h>
#include <ap/engine/path_text.h>
#include <ap/engine/sha256.h>
#include <ap/engine/wav_file_source.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <system_error>

using namespace ap::engine;

namespace {

// In neither the Japanese code page (no "é", no Hangul) nor the Western one
// (no Japanese, no Hangul): so the test means the same on the developer's
// machine and on the CI runner.
const std::u8string kName = u8"曲 é 한글";

std::string utf8(const std::u8string& s)
{
    return {s.begin(), s.end()};
}

// `open` fails with an error of the engine's own that names the file in UTF-8.
template <typename Open>
void failsNamingTheFile(const char* what, Open&& open)
{
    INFO(what);
    try
    {
        open();
        FAIL("no error");
    }
    catch (const std::system_error& e) // (a runtime_error too: first)
    {
        FAIL("the path could not be turned into text: " << e.what());
    }
    catch (const std::runtime_error& e)
    {
        CHECK(std::string(e.what()).find(utf8(kName)) != std::string::npos);
    }
}

} // namespace

TEST_CASE("pathUtf8 gives a path as UTF-8 text", "[engine][pathtext]")
{
    const std::filesystem::path p = std::filesystem::path(u8"音楽") / (kName + u8".flac");
    CHECK(pathUtf8(p.filename()) == utf8(kName + u8".flac"));
    CHECK(pathUtf8(p.parent_path()) == utf8(u8"音楽"));
    CHECK(pathUtf8(p).size() == utf8(u8"音楽").size() + 1 + utf8(kName + u8".flac").size()); // the two, and the separator
    CHECK(pathUtf8({}).empty());
}

TEST_CASE("A missing file with a name the code page lacks: each reader's own error, naming it", "[engine][pathtext]")
{
    ap::test::TempDir dir;
    const auto missing = dir.path() / (kName + u8".dat");

    CHECK_THROWS_AS(openAudioFile(missing), UnsupportedAudioFileError);
    CHECK_THROWS_AS(detectAudioFile(missing), UnsupportedAudioFileError);
    failsNamingTheFile("openAudioFile", [&] { openAudioFile(missing); });
    failsNamingTheFile("WAV", [&] { WavFileSource s(missing); });
    failsNamingTheFile("AIFF", [&] { AiffFileSource s(missing); });
    failsNamingTheFile("FLAC", [&] { FlacFileSource s(missing); });
    failsNamingTheFile("ALAC", [&] { AlacFileSource s(missing); });
    failsNamingTheFile("AAC", [&] { AacFileSource s(missing); });
    failsNamingTheFile("MP4", [&] { readMp4AudioTrack(missing); });
    failsNamingTheFile("DSF", [&] { DsfFileSource s(missing); });
    failsNamingTheFile("DFF", [&] { DffFileSource s(missing); });
    failsNamingTheFile("DSD", [&] { openDsdFile(missing); });
    failsNamingTheFile("EQ preset", [&] { loadEqPreset(missing); });
    failsNamingTheFile("SHA-256", [&] { sha256File(missing); });
}

TEST_CASE("A file that is not audio, with such a name: refused as unsupported, naming it", "[engine][pathtext]")
{
    ap::test::TempDir dir;
    const auto text = dir.path() / (kName + u8".flac");
    {
        std::ofstream out(text, std::ios::binary);
        out << "this is not audio, whatever the name says, and it is long enough to be looked at";
    }
    CHECK_THROWS_AS(openAudioFile(text), UnsupportedAudioFileError);
    failsNamingTheFile("openAudioFile", [&] { openAudioFile(text); });
    failsNamingTheFile("DSD", [&] { openDsdFile(text); });
}

TEST_CASE("The dummy output names its file in UTF-8", "[engine][pathtext]")
{
    ap::test::TempDir dir;
    DummyOutput out(DummyOutput::Options{.path = dir.path() / (kName + u8".raw")});
    CHECK(out.deviceName().find(utf8(kName)) != std::string::npos);
}
