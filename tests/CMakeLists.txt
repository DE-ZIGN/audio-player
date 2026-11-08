include(Catch)

# Test labels:
#   unit        fast unit tests
#   files       checks on files written by tools (CTest fixtures)
#   acceptance  CLAUDE.md chapter 5 acceptance criteria (byte-exact output checks)
#   smoke       the application starts and exits cleanly

# ---------------------------------------------------------------------------
# Shared test helpers
# ---------------------------------------------------------------------------
add_library(ap_test_support STATIC
    support/wav_reader.cpp)
target_include_directories(ap_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_compile_features(ap_test_support PUBLIC cxx_std_20)
ap_apply_compiler_settings(ap_test_support)
set_target_properties(ap_test_support PROPERTIES FOLDER "tests")

# ---------------------------------------------------------------------------
# Engine unit tests
# ---------------------------------------------------------------------------
add_executable(ap_engine_unit_tests
    unit/version_test.cpp
    unit/sha256_test.cpp
    unit/repack_test.cpp)

target_link_libraries(ap_engine_unit_tests PRIVATE ap_engine Catch2::Catch2WithMain)
target_compile_definitions(ap_engine_unit_tests PRIVATE
    AP_EXPECTED_VERSION="${PROJECT_VERSION}"
    AP_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    AP_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    AP_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH})
ap_apply_compiler_settings(ap_engine_unit_tests)
set_target_properties(ap_engine_unit_tests PROPERTIES FOLDER "tests")

catch_discover_tests(ap_engine_unit_tests
    PROPERTIES LABELS "unit")

# ---------------------------------------------------------------------------
# Engine tests that use generated signals as input
# ---------------------------------------------------------------------------
if(TARGET ap_signalgen_lib)
    add_executable(ap_engine_signal_tests
        unit/wav_file_source_test.cpp
        unit/player_test.cpp)
    target_link_libraries(ap_engine_signal_tests PRIVATE
        ap_engine ap_signalgen_lib ap_test_support Catch2::Catch2WithMain)
    ap_apply_compiler_settings(ap_engine_signal_tests)
    set_target_properties(ap_engine_signal_tests PROPERTIES FOLDER "tests")

    catch_discover_tests(ap_engine_signal_tests
        PROPERTIES LABELS "unit")

    # CLAUDE.md chapter 5 acceptance criteria
    add_executable(ap_acceptance_tests
        acceptance/bitperfect_test.cpp)
    target_link_libraries(ap_acceptance_tests PRIVATE
        ap_engine ap_signalgen_lib ap_test_support Catch2::Catch2WithMain)
    ap_apply_compiler_settings(ap_acceptance_tests)
    set_target_properties(ap_acceptance_tests PROPERTIES FOLDER "tests")

    catch_discover_tests(ap_acceptance_tests
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

    catch_discover_tests(ap_tools_unit_tests
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

    catch_discover_tests(ap_signal_file_tests
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
