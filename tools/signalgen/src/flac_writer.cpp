#include <ap/signalgen/flac_writer.h>

#include <FLAC/metadata.h>
#include <FLAC/stream_encoder.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace ap::signalgen {

namespace {

struct EncoderGuard
{
    FLAC__StreamEncoder* encoder = FLAC__stream_encoder_new();
    FLAC__StreamMetadata* seekTable = nullptr;
    ~EncoderGuard()
    {
        if (encoder)
            FLAC__stream_encoder_delete(encoder);
        if (seekTable)
            FLAC__metadata_object_delete(seekTable);
    }
};

[[noreturn]] void fail(const std::string& what)
{
    throw std::runtime_error("FLAC encoder: " + what);
}

} // namespace

void writeFlac(const std::filesystem::path& path, std::uint32_t sampleRate, std::uint16_t channels,
               unsigned bitsPerSample, const std::vector<std::int32_t>& interleaved, const FlacOptions& options)
{
    if (channels == 0 || channels > 8 || interleaved.size() % channels != 0)
        throw std::invalid_argument("writeFlac: bad channel count or sample count");
    const auto frames = interleaved.size() / channels;

    EncoderGuard g;
    if (!g.encoder)
        fail("cannot create the encoder");
    auto* e = g.encoder;
    bool ok = FLAC__stream_encoder_set_verify(e, true);
    ok = ok && FLAC__stream_encoder_set_compression_level(e, options.compressionLevel);
    ok = ok && FLAC__stream_encoder_set_channels(e, channels);
    ok = ok && FLAC__stream_encoder_set_bits_per_sample(e, bitsPerSample);
    ok = ok && FLAC__stream_encoder_set_sample_rate(e, sampleRate);
    ok = ok && FLAC__stream_encoder_set_total_samples_estimate(e, frames);
    if (!ok)
        fail("cannot set the parameters");

    FLAC__StreamMetadata* metadata[1]{};
    if (options.seekPointSeconds > 0 && frames > 0)
    {
        g.seekTable = FLAC__metadata_object_new(FLAC__METADATA_TYPE_SEEKTABLE);
        if (!g.seekTable
            || !FLAC__metadata_object_seektable_template_append_spaced_points_by_samples(
                g.seekTable, sampleRate * options.seekPointSeconds, frames)
            || !FLAC__metadata_object_seektable_template_sort(g.seekTable, true))
            fail("cannot build the seek table");
        metadata[0] = g.seekTable;
        if (!FLAC__stream_encoder_set_metadata(e, metadata, 1))
            fail("cannot set the seek table");
    }

    FILE* file = nullptr; // seekable, so STREAMINFO and the seek table are filled in at the end
    if (_wfopen_s(&file, path.c_str(), L"w+b") != 0 || !file)
        fail("cannot create " + path.string());
    // The encoder owns the FILE from here on and closes it in finish().
    if (FLAC__stream_encoder_init_FILE(e, file, nullptr, nullptr) != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
        fail(std::string("init failed: ") + FLAC__stream_encoder_get_resolved_state_string(e));

    constexpr std::size_t kChunk = 4096;
    for (std::size_t done = 0; done < frames;)
    {
        const auto n = std::min(kChunk, frames - done);
        if (!FLAC__stream_encoder_process_interleaved(e, interleaved.data() + done * channels, static_cast<unsigned>(n)))
            fail(std::string("encode failed: ") + FLAC__stream_encoder_get_resolved_state_string(e));
        done += n;
    }
    if (!FLAC__stream_encoder_finish(e))
        fail(std::string("finish failed: ") + FLAC__stream_encoder_get_resolved_state_string(e));

    if (!options.storeMd5)
    {
        // An all-zero MD5 in STREAMINFO means "not stored" (as written by encoders that skip it).
        constexpr long kMd5Offset = 4 + 4 + 18; // "fLaC", block header, STREAMINFO fields before the MD5
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"r+b") != 0 || !f)
            fail("cannot reopen " + path.string());
        const unsigned char zeros[16]{};
        const bool written = std::fseek(f, kMd5Offset, SEEK_SET) == 0 && std::fwrite(zeros, 1, 16, f) == 16;
        std::fclose(f);
        if (!written)
            fail("cannot clear the MD5 in " + path.string());
    }
}

void writeFlac(const std::filesystem::path& path, const PcmData& pcm, const FlacOptions& options)
{
    if (!isInteger(pcm.format))
        throw std::invalid_argument("writeFlac: FLAC stores integer samples only");
    writeFlac(path, pcm.sampleRate, pcm.channels, static_cast<unsigned>(bitsPerSample(pcm.format)), pcm.intSamples,
              options);
}

} // namespace ap::signalgen
