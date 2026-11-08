# Test-signal generator: library (used by tests) and command-line tool.

add_library(ap_signalgen_lib STATIC
    src/pcm.cpp
    src/signals.cpp
    src/wav_writer.cpp)

target_include_directories(ap_signalgen_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ap_signalgen_lib PUBLIC cxx_std_20)
ap_apply_compiler_settings(ap_signalgen_lib)
set_target_properties(ap_signalgen_lib PROPERTIES FOLDER "tools")

add_executable(ap_signalgen
    src/main.cpp)
target_link_libraries(ap_signalgen PRIVATE ap_signalgen_lib)
ap_apply_compiler_settings(ap_signalgen)
set_target_properties(ap_signalgen PROPERTIES FOLDER "tools")
