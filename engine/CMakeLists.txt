# ap_engine: output layer, decoders and signal processing.
# Must not depend on the UI or on JUCE (plain C++20 + Win32).

add_library(ap_engine STATIC
    src/version.cpp
    src/pcm_format.cpp
    src/pcm_source.cpp
    src/wav_file_source.cpp
    src/repack.cpp
    src/playback.cpp
    src/player.cpp
    src/sha256.cpp
    src/output/dummy_output.cpp
    src/output/format_probe.cpp
    src/output/device_selection.cpp
    src/output/device_watcher.cpp
    src/output/wasapi_exclusive_output.cpp)

target_link_libraries(ap_engine PRIVATE bcrypt ole32 avrt)

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
