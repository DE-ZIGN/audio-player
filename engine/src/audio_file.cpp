#include <ap/engine/audio_file.h>

#include <ap/engine/dop_source.h>
#include <ap/engine/dsd_source.h>
#include <ap/engine/wav_file_source.h>

namespace ap::engine {

std::unique_ptr<PcmSource> openAudioFile(const std::filesystem::path& path)
{
    if (looksLikeDsdFile(path))
        return std::make_unique<DopSource>(openDsdFile(path));
    return std::make_unique<WavFileSource>(path);
}

const char* supportedAudioFilePattern() noexcept
{
    return "*.wav;*.dsf;*.dff";
}

} // namespace ap::engine
