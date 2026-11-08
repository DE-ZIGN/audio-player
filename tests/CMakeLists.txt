include(Catch)

# Test labels:
#   unit        fast unit tests of engine pieces
#   acceptance  CLAUDE.md chapter 5 acceptance criteria (byte-exact output checks)
#   smoke       the application starts and exits cleanly

# ---------------------------------------------------------------------------
# Engine unit tests
# ---------------------------------------------------------------------------
add_executable(ap_engine_unit_tests
    unit/version_test.cpp)

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
# Application smoke test
# ---------------------------------------------------------------------------
if(AP_BUILD_APP)
    add_test(NAME app_smoke_test COMMAND $<TARGET_FILE:ap_app> --smoke-test)
    set_tests_properties(app_smoke_test PROPERTIES
        LABELS  "smoke"
        TIMEOUT 30)
endif()
