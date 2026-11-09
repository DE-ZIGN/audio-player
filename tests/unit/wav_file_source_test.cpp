#include <ap/engine/pcm_source.h>
#include <ap/engine/wav_file_source.h>
#include <ap/signalgen/signals.h>
#include <ap/signalgen/wav_writer.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstring>
#include <vector>

using namespace ap::engine;
namespace sg = ap::signalgen;

namespace {

void put16(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    put16(b, v);
    put16(b, v >> 16);
}

void putTag(std::vector<std::uint8_t>& b, const char* tag)
{
    b.insert(b.end(), tag, tag + 4);
}

void putChunk(std::vector<std::uint8_t>& b, const char* tag, const std::vector<std::uint8_t>& body,
              std::uint32_t declaredSize)
{
    putTag(b, tag);
    put32(b, declaredSize);
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() % 2 == 1)
        b.push_back(0);
}

void putChunk(std::vector<std::uint8_t>& b, const char* tag, const std::vector<std::uint8_t>& body)
{
    putChunk(b, tag, body, static_cast<std::uint32_t>(body.size()));
}

std::vector<std::uint8_t> extensibleFmt(std::uint16_t channels, std::uint32_t rate, std::uint16_t container,
                                        std::uint16_t valid, std::uint16_t subformat)
{
    std::vector<std::uint8_t> f;
    put16(f, 0xFFFE);
    put16(f, channels);
    put32(f, rate);
    put32(f, rate * channels * container / 8);
    put16(f, static_cast<std::uint16_t>(channels * container / 8));
    put16(f, container);
    put16(f, 22);
    put16(f, valid);
    put32(f, channels == 2 ? 3u : 0u);
    put16(f, subformat);
    for (std::uint8_t v : std::initializer_list<std::uint8_t>{
             0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71})
        f.push_back(v);
    return f;
}

std::vector<std::uint8_t> riff(const std::vector<std::uint8_t>& chunks)
{
    std::vector<std::uint8_t> b;
    putTag(b, "RIFF");
    put32(b, static_cast<std::uint32_t>(chunks.size() + 4));
    putTag(b, "WAVE");
    b.insert(b.end(), chunks.begin(), chunks.end());
    return b;
}

std::vector<std::uint8_t> readAllBytes(PcmSource& src, std::size_t chunkFrames)
{
    std::vector<std::uint8_t> out;
    std::vector<std::byte> buf(chunkFrames * src.format().bytesPerFrame());
    while (const auto frames = src.read(buf))
    {
        const auto bytes = frames * src.format().bytesPerFrame();
        const auto* p = reinterpret_cast<const std::uint8_t*>(buf.data());
        out.insert(out.end(), p, p + bytes);
    }
    return out;
}

std::vector<std::uint8_t> dataChunkOf(const std::vector<std::uint8_t>& wav)
{
    for (std::size_t pos = 12; pos + 8 <= wav.size();)
    {
        const std::uint32_t size = wav[pos + 4] | (wav[pos + 5] << 8) | (wav[pos + 6] << 16)
                                 | (static_cast<std::uint32_t>(wav[pos + 7]) << 24);
        if (std::memcmp(&wav[pos], "data", 4) == 0)
            return {wav.begin() + static_cast<std::ptrdiff_t>(pos + 8),
                    wav.begin() + static_cast<std::ptrdiff_t>(pos + 8 + size)};
        pos += 8 + size + (size & 1u);
    }
    return {};
}

} // namespace

TEST_CASE("WavFileSource delivers the data chunk bytes unchanged", "[engine][wav]")
{
    const auto format = GENERATE(sg::SampleFormat::Int16, sg::SampleFormat::Int24, sg::SampleFormat::Int32,
                                 sg::SampleFormat::Float32, sg::SampleFormat::Float64);
    const auto channels = GENERATE(std::uint16_t{1}, std::uint16_t{2});
    const auto chunkFrames = GENERATE(std::size_t{1}, std::size_t{441}, std::size_t{100000});
    CAPTURE(sg::formatName(format), channels, chunkFrames);

    const sg::Layout layout{96000, channels, 2000};
    const auto pcm = sg::isInteger(format)
                         ? sg::bitPattern(layout, format)
                         : sg::quantize(sg::sine(layout, 1000.0, -1.0), layout.sampleRate, channels, format);
    const auto wav = sg::encodeWav(pcm);

    ap::test::TempDir dir;
    WavFileSource src(dir.write("in.wav", wav));

    const auto& f = src.format();
    CHECK(f.sampleRate == 96000u);
    CHECK(f.channels == channels);
    CHECK(f.containerBits == sg::bitsPerSample(format));
    CHECK(f.validBits == sg::bitsPerSample(format));
    CHECK(f.type == (sg::isInteger(format) ? SampleType::Int : SampleType::Float));
    CHECK(src.totalFrames() == 2000u);

    CHECK(readAllBytes(src, chunkFrames) == dataChunkOf(wav));
    std::vector<std::byte> extra(64);
    CHECK(src.read(extra) == 0);
}

TEST_CASE("WavFileSource skips unknown and odd-sized chunks", "[engine][wav]")
{
    const std::vector<std::uint8_t> data{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    std::vector<std::uint8_t> chunks;
    putChunk(chunks, "junk", {0xAA, 0xBB, 0xCC}); // odd size, padded
    putChunk(chunks, "fmt ", extensibleFmt(2, 44100, 24, 24, 1));
    putChunk(chunks, "LIST", {'I', 'N', 'F', 'O'});
    putChunk(chunks, "data", data);
    putChunk(chunks, "id3 ", {0, 1, 2, 3, 4});

    ap::test::TempDir dir;
    WavFileSource src(dir.write("in.wav", riff(chunks)));
    CHECK(src.format().containerBits == 24);
    CHECK(src.format().channelMask == 3u);
    CHECK(src.totalFrames() == 2u);
    CHECK(readAllBytes(src, 7) == data);
}

TEST_CASE("WavFileSource keeps 24-in-32 containers as they are", "[engine][wav]")
{
    // Two stereo frames, 24 valid bits left-justified in 32-bit slots.
    const std::vector<std::uint8_t> data{0x00, 0x01, 0x02, 0x03, 0x00, 0x04, 0x05, 0x06,
                                         0x00, 0x07, 0x08, 0x09, 0x00, 0x0A, 0x0B, 0x0C};
    std::vector<std::uint8_t> chunks;
    putChunk(chunks, "fmt ", extensibleFmt(2, 192000, 32, 24, 1));
    putChunk(chunks, "data", data);

    ap::test::TempDir dir;
    WavFileSource src(dir.write("in.wav", riff(chunks)));
    CHECK(src.format().containerBits == 32);
    CHECK(src.format().validBits == 24);
    CHECK(readAllBytes(src, 1) == data);
}

TEST_CASE("WavFileSource clamps a data chunk that overruns the file", "[engine][wav]")
{
    std::vector<std::uint8_t> chunks;
    putChunk(chunks, "fmt ", extensibleFmt(2, 48000, 16, 16, 1));
    putChunk(chunks, "data", {1, 2, 3, 4, 5, 6, 7, 8, 9}, 0xFFFFFFFFu); // 2 whole frames + 1 stray byte

    ap::test::TempDir dir;
    WavFileSource src(dir.write("in.wav", riff(chunks)));
    CHECK(src.totalFrames() == 2u);
    CHECK(readAllBytes(src, 16) == std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8});
}

TEST_CASE("WavFileSource rejects unsupported and malformed files", "[engine][wav]")
{
    ap::test::TempDir dir;

    SECTION("not RIFF")
    {
        const std::vector<std::uint8_t> junk(64, 0x20);
        CHECK_THROWS_AS(WavFileSource(dir.write("a.wav", junk)), WavFormatError);
    }
    SECTION("compressed format tag")
    {
        std::vector<std::uint8_t> fmt;
        put16(fmt, 0x0002); // MS ADPCM
        put16(fmt, 2);
        put32(fmt, 44100);
        put32(fmt, 44100 * 4);
        put16(fmt, 4);
        put16(fmt, 4);
        std::vector<std::uint8_t> chunks;
        putChunk(chunks, "fmt ", fmt);
        putChunk(chunks, "data", {0, 0, 0, 0});
        CHECK_THROWS_AS(WavFileSource(dir.write("b.wav", riff(chunks))), WavFormatError);
    }
    SECTION("valid bits larger than the container")
    {
        std::vector<std::uint8_t> chunks;
        putChunk(chunks, "fmt ", extensibleFmt(2, 44100, 24, 32, 1));
        putChunk(chunks, "data", {0, 0, 0, 0, 0, 0});
        CHECK_THROWS_AS(WavFileSource(dir.write("c.wav", riff(chunks))), WavFormatError);
    }
    SECTION("missing data chunk")
    {
        std::vector<std::uint8_t> chunks;
        putChunk(chunks, "fmt ", extensibleFmt(2, 44100, 24, 24, 1));
        CHECK_THROWS_AS(WavFileSource(dir.write("d.wav", riff(chunks))), WavFormatError);
    }
    SECTION("file does not exist")
    {
        CHECK_THROWS_AS(WavFileSource(dir.path() / "missing.wav"), WavFormatError);
    }
}

TEST_CASE("WavFileSource reads RF64 and BW64 (sizes from the ds64 chunk)", "[engine][wav]")
{
    const auto magic = GENERATE(std::string("RF64"), std::string("BW64"));
    CAPTURE(magic);
    const auto pcm = sg::bitPattern(sg::Layout{48000, 2, 3000}, sg::SampleFormat::Int24);
    const auto data = dataChunkOf(sg::encodeWav(pcm));

    std::vector<std::uint8_t> ds64;
    const std::uint64_t riffSize = 4 + (8 + 28) + (8 + 40) + (8 + data.size());
    put32(ds64, static_cast<std::uint32_t>(riffSize));
    put32(ds64, static_cast<std::uint32_t>(riffSize >> 32));
    put32(ds64, static_cast<std::uint32_t>(data.size()));
    put32(ds64, 0);
    put32(ds64, 3000); // sample count
    put32(ds64, 0);
    put32(ds64, 0); // table length

    std::vector<std::uint8_t> b;
    putTag(b, magic.c_str());
    put32(b, 0xFFFFFFFFu);
    putTag(b, "WAVE");
    putChunk(b, "ds64", ds64);
    putChunk(b, "fmt ", extensibleFmt(2, 48000, 24, 24, 1));
    putChunk(b, "data", data, 0xFFFFFFFFu); // the real size is in ds64

    ap::test::TempDir dir;
    WavFileSource src(dir.write("big.wav", b));
    CHECK(src.totalFrames() == 3000u);
    CHECK(readAllBytes(src, 512) == data);
}

TEST_CASE("MemoryPcmSource::loadAll copies the remaining bytes", "[engine][source]")
{
    const sg::Layout layout{44100, 2, 1234};
    const auto wav = sg::encodeWav(sg::bitPattern(layout, sg::SampleFormat::Int24));
    ap::test::TempDir dir;
    WavFileSource file(dir.write("in.wav", wav));

    auto mem = MemoryPcmSource::loadAll(file);
    CHECK(mem.format() == file.format());
    CHECK(mem.totalFrames() == 1234u);
    CHECK(readAllBytes(mem, 100) == dataChunkOf(wav));
}
