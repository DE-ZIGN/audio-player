include(Catch)

# With AddressSanitizer the test programs need the ASan runtime DLL (next to cl.exe) on PATH,
# both when Catch2 lists their tests at build time and when ctest runs them.
set(AP_DISCOVER_ARGS)
if(AP_ASAN)
    set(AP_DISCOVER_ARGS DL_PATHS "${AP_ASAN_DLL_DIR}")
endif()
function(ap_asan_test_env)
    if(AP_ASAN)
        set_tests_properties(${ARGN} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${AP_ASAN_DLL_DIR}")
    endif()
endfunction()

# Test labels:
#   unit        fast unit tests
#   files       checks on files written by tools (CTest fixtures)
#   acceptance  CLAUDE.md chapter 5 acceptance criteria (byte-exact output checks)
#   smoke       the application starts and exits cleanly

# ---------------------------------------------------------------------------
# Shared test helpers
# ---------------------------------------------------------------------------
# Apple's ALAC encoder (Apache-2.0, third_party/alac), for making test files
# only; it is not part of the application.
add_library(ap_alac_encoder STATIC
    "${CMAKE_SOURCE_DIR}/third_party/alac/codec/ALACEncoder.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/alac/codec/ag_enc.c"
    "${CMAKE_SOURCE_DIR}/third_party/alac/codec/dp_enc.c"
    "${CMAKE_SOURCE_DIR}/third_party/alac/codec/matrix_enc.c")
target_link_libraries(ap_alac_encoder PUBLIC ap_alac)
target_compile_definitions(ap_alac_encoder PRIVATE TARGET_OS_WIN32=1)
target_compile_options(ap_alac_encoder PRIVATE /W0)
set_target_properties(ap_alac_encoder PROPERTIES FOLDER "tests")

# LAME 3.100 (LGPL), for making MP3 test files with a LAME tag (encoder delay
# and padding, needed for gapless playback); test-only, not part of the
# application. LAME has no CMake build: the library is built from its sources
# with its MSVC configuration header (configMS.h as config.h).
FetchContent_Declare(lame
    URL      https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
    URL_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(lame) # no CMakeLists.txt in LAME: this only downloads it
file(GLOB AP_LAME_SOURCES "${lame_SOURCE_DIR}/libmp3lame/*.c")
add_library(ap_lame STATIC ${AP_LAME_SOURCES} "${lame_SOURCE_DIR}/libmp3lame/vector/xmm_quantize_sub.c")
configure_file("${lame_SOURCE_DIR}/configMS.h" "${CMAKE_CURRENT_BINARY_DIR}/lame_config/config.h" COPYONLY)
target_include_directories(ap_lame
    PUBLIC "${lame_SOURCE_DIR}/include"
    PRIVATE "${lame_SOURCE_DIR}/libmp3lame" "${CMAKE_CURRENT_BINARY_DIR}/lame_config")
target_compile_definitions(ap_lame PRIVATE HAVE_CONFIG_H _CRT_SECURE_NO_WARNINGS)
target_compile_options(ap_lame PRIVATE /W0)
set_target_properties(ap_lame PROPERTIES FOLDER "tests")

add_library(ap_test_support STATIC
    support/wav_reader.cpp
    support/mf_audio.cpp
    support/alac_m4a_writer.cpp
    support/mp3_writer.cpp
    support/m4a_muxer.cpp)
target_include_directories(ap_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)
# Media Foundation: an independent ALAC encoder/decoder for making and checking test files.
target_link_libraries(ap_test_support PUBLIC mfplat mfreadwrite mfuuid ole32 ap_alac_encoder ap_lame ap_engine)
target_compile_features(ap_test_support PUBLIC cxx_std_20)
ap_apply_compiler_settings(ap_test_support)
set_target_properties(ap_test_support PROPERTIES FOLDER "tests")

# ---------------------------------------------------------------------------
# Engine unit tests
# ---------------------------------------------------------------------------
add_executable(ap_engine_unit_tests
    unit/version_test.cpp
    unit/sha256_test.cpp
    unit/repack_test.cpp
    unit/format_probe_test.cpp
    unit/device_selection_test.cpp
    unit/device_watcher_test.cpp
    unit/deadline_tracker_test.cpp
    unit/prefetching_source_test.cpp
    unit/asio_sample_types_test.cpp
    unit/asio_probe_test.cpp
    unit/dsd_source_test.cpp
    unit/dop_source_test.cpp
    unit/aiff_file_source_test.cpp)

target_link_libraries(ap_engine_unit_tests PRIVATE ap_engine ap_test_support Catch2::Catch2WithMain)
target_compile_definitions(ap_engine_unit_tests PRIVATE
    AP_EXPECTED_VERSION="${PROJECT_VERSION}"
    AP_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    AP_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    AP_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH})
ap_apply_compiler_settings(ap_engine_unit_tests)
ap_copy_engine_dlls(ap_engine_unit_tests) # before test discovery, which runs the program
set_target_properties(ap_engine_unit_tests PROPERTIES FOLDER "tests")

catch_discover_tests(ap_engine_unit_tests ${AP_DISCOVER_ARGS}
    PROPERTIES LABELS "unit")

# ---------------------------------------------------------------------------
# Engine tests that use generated signals as input
# ---------------------------------------------------------------------------
if(TARGET ap_signalgen_lib)
    add_executable(ap_engine_signal_tests
        unit/wav_file_source_test.cpp
        unit/player_test.cpp
        unit/player_queue_test.cpp
        unit/player_dropout_test.cpp
        unit/asio_output_test.cpp
        unit/dsd_signal_test.cpp
        unit/signal_path_test.cpp
        unit/flac_file_source_test.cpp
        unit/audio_file_test.cpp
        unit/alac_file_source_test.cpp
        unit/mp3_file_source_test.cpp
        unit/aac_file_source_test.cpp)
    target_link_libraries(ap_engine_signal_tests PRIVATE
        ap_engine ap_signalgen_lib ap_test_support ap_asiosdk ole32 Catch2::Catch2WithMain)
    ap_apply_compiler_settings(ap_engine_signal_tests)
ap_copy_engine_dlls(ap_engine_signal_tests) # before test discovery, which runs the program
    set_target_properties(ap_engine_signal_tests PROPERTIES FOLDER "tests")

    catch_discover_tests(ap_engine_signal_tests ${AP_DISCOVER_ARGS}
        PROPERTIES LABELS "unit")

    # CLAUDE.md chapter 5 acceptance criteria
    add_executable(ap_acceptance_tests
        acceptance/bitperfect_test.cpp
        acceptance/lossy_test.cpp)
    target_link_libraries(ap_acceptance_tests PRIVATE
        ap_engine ap_signalgen_lib ap_test_support Catch2::Catch2WithMain)
    ap_apply_compiler_settings(ap_acceptance_tests)
ap_copy_engine_dlls(ap_acceptance_tests) # before test discovery, which runs the program
    set_target_properties(ap_acceptance_tests PROPERTIES FOLDER "tests")

    catch_discover_tests(ap_acceptance_tests ${AP_DISCOVER_ARGS}
        PROPERTIES LABELS "acceptance")
endif()

# ---------------------------------------------------------------------------
# Tools: signal generator
# ---------------------------------------------------------------------------
if(TARGET ap_signalgen_lib)
    add_executable(ap_tools_unit_tests
        unit/signalgen_pcm_test.cpp
        unit/signalgen_signals_test.cpp
        unit/wav_writer_test.cpp)
    target_link_libraries(ap_tools_unit_tests PRIVATE
        ap_signalgen_lib ap_test_support Catch2::Catch2WithMain)
    ap_apply_compiler_settings(ap_tools_unit_tests)
    set_target_properties(ap_tools_unit_tests PROPERTIES FOLDER "tests")

    catch_discover_tests(ap_tools_unit_tests ${AP_DISCOVER_ARGS}
        PROPERTIES LABELS "unit")

    # Generate files with the real command-line tool, then verify them.
    set(AP_SIGNAL_DIR "${CMAKE_CURRENT_BINARY_DIR}/signal_files")
    set(AP_SIGNAL_TEST_RATES 44100 384000)
    set(AP_SIGNAL_TEST_BITS 16 24 32)
    set(AP_SIGNAL_TEST_SECONDS 0.25)
    string(JOIN "," rates_csv ${AP_SIGNAL_TEST_RATES})
    string(JOIN "," bits_csv ${AP_SIGNAL_TEST_BITS})

    add_test(NAME signal_files_clean
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${AP_SIGNAL_DIR}")
    set_tests_properties(signal_files_clean PROPERTIES
        LABELS "files"
        FIXTURES_SETUP signal_files_clean)

    add_test(NAME signal_files_generate
        COMMAND ap_signalgen all
            --out-dir "${AP_SIGNAL_DIR}"
            --rates ${rates_csv}
            --bits ${bits_csv}
            --seconds ${AP_SIGNAL_TEST_SECONDS})
    set_tests_properties(signal_files_generate PROPERTIES
        LABELS "files"
        FIXTURES_REQUIRED signal_files_clean
        FIXTURES_SETUP signal_files)
    ap_asan_test_env(signal_files_generate)

    add_executable(ap_signal_file_tests
        files/signal_files_test.cpp)
    target_link_libraries(ap_signal_file_tests PRIVATE
        ap_signalgen_lib ap_test_support Catch2::Catch2WithMain)
    target_compile_definitions(ap_signal_file_tests PRIVATE
        AP_SIGNAL_DIR="${AP_SIGNAL_DIR}"
        AP_SIGNAL_TEST_RATES=${rates_csv}
        AP_SIGNAL_TEST_BITS=${bits_csv}
        AP_SIGNAL_TEST_SECONDS=${AP_SIGNAL_TEST_SECONDS})
    ap_apply_compiler_settings(ap_signal_file_tests)
    set_target_properties(ap_signal_file_tests PROPERTIES FOLDER "tests")

    catch_discover_tests(ap_signal_file_tests ${AP_DISCOVER_ARGS}
        PROPERTIES
            LABELS "files"
            FIXTURES_REQUIRED signal_files)
endif()

# ---------------------------------------------------------------------------
# Application smoke test
# ---------------------------------------------------------------------------
if(AP_BUILD_APP)
    add_test(NAME app_smoke_test COMMAND $<TARGET_FILE:ap_app> --smoke-test)
    # Passes only when WebView2 starts, the built UI loads from <exe dir>/ui and
    # the page reaches the bridge (getStatus, uiReady).
    set_tests_properties(app_smoke_test PROPERTIES
        LABELS  "smoke"
        TIMEOUT 60)

    # End to end: the page plays, seeks and stops a 2 s file through the
    # native bridge into a dummy output; the app checks the bytes.
    if(TARGET ap_signalgen)
        set(self_test_dir "${CMAKE_CURRENT_BINARY_DIR}/app_self_test")
        add_test(NAME app_self_test_signal
            COMMAND ap_signalgen sine --rate 48000 --bits 24 --level -40 --seconds 2
                    --out "${self_test_dir}/selftest_48k_24bit.wav")
        set_tests_properties(app_self_test_signal PROPERTIES
            LABELS "smoke"
            FIXTURES_SETUP app_self_test_signal)

        add_test(NAME app_self_test
            COMMAND ap_app "--self-test=${self_test_dir}/selftest_48k_24bit.wav"
                           "--self-test-output=${self_test_dir}/out.raw")
        set_tests_properties(app_self_test PROPERTIES
            LABELS "smoke"
            TIMEOUT 60
            FIXTURES_REQUIRED app_self_test_signal)
    endif()
endif()
