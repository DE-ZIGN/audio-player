#include <ap/engine/audio_file.h>

#include <ap/engine/aiff_file_source.h>
#include <ap/engine/alac_file_source.h>
#include <ap/engine/mp3_file_source.h>
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

// Position after any ID3v2 tags at the start (FLAC and MP3 files may have them).
std::uint64_t skipId3v2(std::ifstream& f)
{
    std::uint64_t pos = 0;
    for (int tags = 0; tags < 4; ++tags)
    {
        std::array<std::uint8_t, 10> h{};
        f.clear();
        f.seekg(static_cast<std::streamoff>(pos));
        if (!f.read(reinterpret_cast<char*>(h.data()), 10) || std::memcmp(h.data(), "ID3", 3) != 0)
            break;
        // Synchsafe size (7 bits per byte), plus a 10-byte footer when flagged.
        const std::uint64_t size = (std::uint64_t{h[6]} << 21) | (std::uint64_t{h[7]} << 14) | (std::uint64_t{h[8]} << 7) | h[9];
        pos += 10 + size + ((h[5] & 0x10) ? 10 : 0);
    }
    return pos;
}

bool looksLikeFlac(std::ifstream& f)
{
    std::array<char, 4> m{};
    f.clear();
    f.seekg(static_cast<std::streamoff>(skipId3v2(f)));
    return f.read(m.data(), 4) && std::memcmp(m.data(), "fLaC", 4) == 0;
}

// An MPEG audio frame header (MPEG-1/2/2.5, Layer I/II/III) with valid fields.
bool looksLikeMpegAudio(std::ifstream& f)
{
    std::array<std::uint8_t, 4> h{};
    f.clear();
    f.seekg(static_cast<std::streamoff>(skipId3v2(f)));
    if (!f.read(reinterpret_cast<char*>(h.data()), 4))
        return false;
    const bool sync = h[0] == 0xFF && (h[1] & 0xE0) == 0xE0;
    const unsigned version = (h[1] >> 3) & 3; // 1 = reserved
    const unsigned layer = (h[1] >> 1) & 3;   // 0 = reserved
    const unsigned bitrate = h[2] >> 4;       // 15 = invalid
    const unsigned rate = (h[2] >> 2) & 3;    // 3 = reserved
    return sync && version != 1 && layer != 0 && bitrate != 15 && rate != 3;
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
    if (looksLikeMpegAudio(f))
        return AudioFileKind::Mp3;
    throw UnsupportedAudioFileError("unsupported file type: " + path.string());
}

std::unique_ptr<PcmSource> openAudioFile(const std::filesystem::path& path)
{
    switch (detectAudioFile(path))
    {
        case AudioFileKind::Wav:  return std::make_unique<WavFileSource>(path);
        case AudioFileKind::Flac: return std::make_unique<FlacFileSource>(path);
        case AudioFileKind::Mp3:  return std::make_unique<Mp3FileSource>(path);
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
    return "*.wav;*.flac;*.aif;*.aiff;*.aifc;*.m4a;*.mp3;*.dsf;*.dff";
}

} // namespace ap::engine
