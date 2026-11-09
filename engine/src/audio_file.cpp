#include <ap/engine/audio_file.h>

#include <ap/engine/aiff_file_source.h>
#include <ap/engine/alac_file_source.h>
#include <ap/engine/mp4_reader.h>
#include <ap/engine/dop_source.h>
#include <ap/engine/dsd_source.h>
#include <ap/engine/flac_file_source.h>
#include <ap/engine/wav_file_source.h>

#include <array>
#include <cstring>
#include <fstream>

namespace ap::engine {

namespace {

// Skips ID3v2 tags (some FLAC files start with one) and tells whether "fLaC" follows.
bool looksLikeFlac(std::ifstream& f)
{
    std::uint64_t pos = 0;
    for (int tags = 0; tags < 4; ++tags)
    {
        std::array<std::uint8_t, 10> h{};
        f.clear();
        f.seekg(static_cast<std::streamoff>(pos));
        if (!f.read(reinterpret_cast<char*>(h.data()), 4))
            return false;
        if (std::memcmp(h.data(), "fLaC", 4) == 0)
            return true;
        if (std::memcmp(h.data(), "ID3", 3) != 0 || !f.read(reinterpret_cast<char*>(h.data()) + 4, 6))
            return false;
        // Synchsafe size (7 bits per byte), plus a 10-byte footer when flagged.
        const std::uint64_t size = (std::uint64_t{h[6]} << 21) | (std::uint64_t{h[7]} << 14) | (std::uint64_t{h[8]} << 7) | h[9];
        pos += 10 + size + ((h[5] & 0x10) ? 10 : 0);
    }
    return false;
}

} // namespace

AudioFileKind detectAudioFile(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw UnsupportedAudioFileError("cannot open file: " + path.string());
    std::array<char, 16> head{};
    f.read(head.data(), head.size());
    const auto got = f.gcount();

    if (got >= 12 && (std::memcmp(head.data(), "RIFF", 4) == 0 || std::memcmp(head.data(), "RF64", 4) == 0
                      || std::memcmp(head.data(), "BW64", 4) == 0)
        && std::memcmp(head.data() + 8, "WAVE", 4) == 0)
        return AudioFileKind::Wav;
    if (got >= 12 && std::memcmp(head.data(), "FORM", 4) == 0
        && (std::memcmp(head.data() + 8, "AIFF", 4) == 0 || std::memcmp(head.data() + 8, "AIFC", 4) == 0))
        return AudioFileKind::Aiff;
    if (got >= 8 && std::memcmp(head.data() + 4, "ftyp", 4) == 0)
        return AudioFileKind::Mp4;
    if (got >= 4 && std::memcmp(head.data(), "DSD ", 4) == 0)
        return AudioFileKind::Dsf;
    if (got >= 16 && std::memcmp(head.data(), "FRM8", 4) == 0 && std::memcmp(head.data() + 12, "DSD ", 4) == 0)
        return AudioFileKind::Dff;
    if (looksLikeFlac(f))
        return AudioFileKind::Flac;
    throw UnsupportedAudioFileError("unsupported file type: " + path.string());
}

std::unique_ptr<PcmSource> openAudioFile(const std::filesystem::path& path)
{
    switch (detectAudioFile(path))
    {
        case AudioFileKind::Wav:  return std::make_unique<WavFileSource>(path);
        case AudioFileKind::Flac: return std::make_unique<FlacFileSource>(path);
        case AudioFileKind::Aiff: return std::make_unique<AiffFileSource>(path);
        case AudioFileKind::Mp4:
        {
            const auto codec = readMp4AudioTrack(path).codec;
            if (codec == "alac")
                return std::make_unique<AlacFileSource>(path);
            if (codec == "mp4a")
                throw UnsupportedAudioFileError("AAC is not supported yet (task 2-4): " + path.string());
            throw UnsupportedAudioFileError("unsupported codec '" + codec + "' in " + path.string());
        }
        case AudioFileKind::Dsf:
        case AudioFileKind::Dff:  return std::make_unique<DopSource>(openDsdFile(path));
    }
    throw UnsupportedAudioFileError("unsupported file type: " + path.string());
}

const char* supportedAudioFilePattern() noexcept
{
    return "*.wav;*.flac;*.aif;*.aiff;*.aifc;*.m4a;*.dsf;*.dff";
}

} // namespace ap::engine
