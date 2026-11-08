#pragma once

#include <ap/engine/pcm_source.h>

#include <filesystem>
#include <memory>

namespace ap::engine {

// Opens a file by its content: WAV as PCM, DSF/DFF as DoP (DopSource).
// Throws WavFormatError or DsdFormatError (both std::runtime_error).
std::unique_ptr<PcmSource> openAudioFile(const std::filesystem::path& path);

// Extensions the player opens, for file dialogs: "*.wav;*.dsf;*.dff".
const char* supportedAudioFilePattern() noexcept;

} // namespace ap::engine
