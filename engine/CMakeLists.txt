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
    src/dsp/level_meter.cpp
    src/dsp/replay_gain.cpp
    src/dsp/isp_safe.cpp
    src/dsp/limiter.cpp
    src/dsp/eq.cpp
    src/dsp/comparison.cpp
    src/dsp/tube.cpp
    src/dsp/tube_auto.cpp
    src/dsp/tone.cpp
    src/dsp/crossfeed.cpp
    src/dsp/fft.cpp
    src/dsp/convolver.cpp
    src/dsp/room_ir.cpp
    src/dsp/space.cpp
    src/dsp/resampler.cpp
    src/listening_settings.cpp
    src/looping_source.cpp
    src/dsp/requantize.cpp
    src/output/dummy_output.cpp
    src/output/delivery_check.cpp
    src/output/output_meter.cpp
    src/output/format_probe.cpp
    src/output/device_selection.cpp
    src/output/device_watcher.cpp
    src/output/asio_sample_types.cpp
    src/output/asio_output.cpp
    src/output/asio_probe.cpp
    src/output/wasapi_exclusive_output.cpp)

# The bundled IRs (task 9-6), built into the engine (cmake/EmbedFile.cmake).
set(AP_IR_MAIR ${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/mair_st_pauls_ab_s01.wav)
set(AP_IR_MAIR_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/ir_mair_st_pauls_ab_s01.cpp)
add_custom_command(OUTPUT ${AP_IR_MAIR_CPP}
    COMMAND ${CMAKE_COMMAND} -DIN=${AP_IR_MAIR} -DOUT=${AP_IR_MAIR_CPP} -DNAME=kMairStPaulsAbS01
            -P ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS ${AP_IR_MAIR} ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
    COMMENT "Embedding the St Paul's Hall IR (MAIR, CC BY 4.0)"
    VERBATIM)
target_sources(ap_engine PRIVATE ${AP_IR_MAIR_CPP})

target_link_libraries(ap_engine PRIVATE bcrypt ole32 avrt user32 advapi32 ap_asiosdk FLAC::FLAC ap_alac libmpg123 ap_ebur128 ap_r8brain mfplat mfuuid)

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
