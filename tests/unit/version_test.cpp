#include <ap/engine/version.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("Engine version matches the CMake project version", "[version]")
{
    const auto v = ap::engine::version();

    CHECK(v.major == AP_EXPECTED_VERSION_MAJOR);
    CHECK(v.minor == AP_EXPECTED_VERSION_MINOR);
    CHECK(v.patch == AP_EXPECTED_VERSION_PATCH);
    CHECK(std::string(ap::engine::versionString()) == AP_EXPECTED_VERSION);
}
