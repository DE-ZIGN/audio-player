#pragma once

#include <ap/engine/pcm_source.h>

#include <filesystem>
#include <memory>
#include <stdexcept>

namespace ap::engine {

class UnsupportedAudioFileError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

enum class AudioFileKind
{
    Wav,  // RIFF, RF64, BW64
    Flac, // also behind ID3v2 tags
    Mp3,  // MPEG audio (Layer I/II/III), also behind ID3v2 tags
    Aiff, // AIFF, AIFC
    Mp4,  // .m4a / .mp4: ALAC or AAC
    Dsf,
    Dff,
};

// Decides the format from the file's content, not its extension.
// Throws UnsupportedAudioFileError.
AudioFileKind detectAudioFile(const std::filesystem::path& path);

// Opens a file by its content: WAV, FLAC and AIFF as PCM, DSF/DFF as DoP
// (DopSource). Throws UnsupportedAudioFileError, or the reader's error
// (WavFormatError, FlacFormatError, AiffFormatError, DsdFormatError; all std::runtime_error).
std::unique_ptr<PcmSource> openAudioFile(const std::filesystem::path& path);

// Extensions the player opens, for file dialogs.
const char* supportedAudioFilePattern() noexcept;

} // namespace ap::engine
