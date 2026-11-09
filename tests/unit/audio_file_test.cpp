// Task 2-1: files are opened by content, whatever their extension.

#include <ap/engine/audio_file.h>
#include <ap/signalgen/dsd.h>
#include <ap/signalgen/flac_writer.h>
#include <ap/signalgen/signals.h>
#include <ap/signalgen/wav_writer.h>

#include "aiff_test_files.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ap::engine;
namespace sg = ap::signalgen;

TEST_CASE("Each supported format is recognised by its content", "[engine][audiofile]")
{
    ap::test::TempDir dir;
    const auto pcm = sg::bitPattern(sg::Layout{48000, 2, 480}, sg::SampleFormat::Int24);

    // Deliberately misleading extensions.
    const auto wav = dir.write("a.flac", sg::encodeWav(pcm));
    const auto flac = dir.path() / "b.aiff";
    sg::writeFlac(flac, pcm);
    const auto aiff = dir.write("c.wav", ap::test::makeAiff({.sampleSize = 24}, std::vector<std::uint8_t>(480 * 6)));
    const auto aifc = dir.write("d.dsf", ap::test::makeAiff({.aifc = true, .compression = "sowt", .sampleSize = 16},
                                                            std::vector<std::uint8_t>(480 * 4)));
    const auto dsf = dir.path() / "e.wav";
    sg::writeBytes(dsf, sg::encodeDsf(sg::dsdSine(2822400, 2, 4096, 1000.0, -20.0), 2822400));
    const auto dff = dir.path() / "f.flac";
    sg::writeBytes(dff, sg::encodeDff(sg::dsdSine(2822400, 2, 4096, 1000.0, -20.0), 2822400));

    CHECK(detectAudioFile(wav) == AudioFileKind::Wav);
    CHECK(detectAudioFile(flac) == AudioFileKind::Flac);
    CHECK(detectAudioFile(aiff) == AudioFileKind::Aiff);
    CHECK(detectAudioFile(aifc) == AudioFileKind::Aiff);
    CHECK(detectAudioFile(dsf) == AudioFileKind::Dsf);
    CHECK(detectAudioFile(dff) == AudioFileKind::Dff);

    CHECK(openAudioFile(wav)->containerName() == "WAV");
    CHECK(openAudioFile(flac)->containerName() == "FLAC");
    CHECK(openAudioFile(aiff)->containerName() == "AIFF");
    CHECK(openAudioFile(aifc)->containerName() == "AIFC");
    CHECK(openAudioFile(dsf)->containerName() == "DSF");
    CHECK(openAudioFile(dff)->containerName() == "DFF");
}

TEST_CASE("Unknown files are refused with a reason", "[engine][audiofile]")
{
    ap::test::TempDir dir;
    const auto mp3ish = dir.write("x.mp3", std::vector<std::uint8_t>{0xFF, 0xFB, 0x90, 0x64, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK_THROWS_AS(openAudioFile(mp3ish), UnsupportedAudioFileError);
    CHECK_THROWS_AS(openAudioFile(dir.path() / "missing.flac"), UnsupportedAudioFileError);
    // An ID3 tag that is not followed by FLAC.
    std::vector<std::uint8_t> id3{'I', 'D', '3', 3, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0xFF, 0xFB, 0x90, 0x64};
    CHECK_THROWS_AS(openAudioFile(dir.write("y.mp3", id3)), UnsupportedAudioFileError);
}

TEST_CASE("The file dialog pattern lists every supported extension", "[engine][audiofile]")
{
    const std::string p = supportedAudioFilePattern();
    for (const char* ext : {"*.wav", "*.flac", "*.aif", "*.aiff", "*.aifc", "*.dsf", "*.dff"})
        CHECK(p.find(ext) != std::string::npos);
}
