# ap_engine: output layer, decoders and signal processing.
# Must not depend on the UI or on JUCE (plain C++20 + Win32).

add_library(ap_engine STATIC
    src/version.cpp
    src/pcm_format.cpp
    src/pcm_source.cpp
    src/wav_file_source.cpp
    src/flac_file_source.cpp
    src/aiff_file_source.cpp
    src/mp4_reader.cpp
    src/alac_file_source.cpp
    src/mp3_file_source.cpp
    src/aac_file_source.cpp
    src/md5.cpp
    src/prefetching_source.cpp
    src/dsd_source.cpp
    src/dop_source.cpp
    src/audio_file.cpp
    src/repack.cpp
    src/playback.cpp
    src/player.cpp
    src/abx.cpp
    src/bit_perfect.cpp
    src/sha256.cpp
    src/dsp/sample_convert.cpp
    src/dsp/processing.cpp
    src/dsp/dither.cpp
    src/dsp/gain_ramp.cpp
    src/dsp/volume.cpp
    src/dsp/true_peak.cpp
    src/dsp/loudness.cpp
    src/dsp/replay_gain.cpp
    src/dsp/isp_safe.cpp
    src/dsp/limiter.cpp
    src/dsp/eq.cpp
    src/dsp/comparison.cpp
    src/output/dummy_output.cpp
    src/output/delivery_check.cpp
    src/output/format_probe.cpp
    src/output/device_selection.cpp
    src/output/device_watcher.cpp
    src/output/asio_sample_types.cpp
    src/output/asio_output.cpp
    src/output/asio_probe.cpp
    src/output/wasapi_exclusive_output.cpp)

target_link_libraries(ap_engine PRIVATE bcrypt ole32 avrt user32 advapi32 ap_asiosdk FLAC::FLAC ap_alac libmpg123 ap_ebur128 mfplat mfuuid)

target_include_directories(ap_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(ap_engine PUBLIC cxx_std_20)

target_compile_definitions(ap_engine PRIVATE
    AP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    AP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    AP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    AP_VERSION_STRING="${PROJECT_VERSION}")

ap_apply_compiler_settings(ap_engine)
set_target_properties(ap_engine PROPERTIES FOLDER "engine")
