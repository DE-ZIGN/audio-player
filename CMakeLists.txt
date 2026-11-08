cmake_minimum_required(VERSION 3.25)

project(audio_player
    VERSION 0.1.0
    DESCRIPTION "Windows audio player with a bit-perfect path and a 64-bit float processing path"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Product identity (provisional; change here only)
# ---------------------------------------------------------------------------
set(AP_PRODUCT_NAME "AudioPlayer" CACHE STRING "Provisional product name shown to users")
set(AP_COMPANY_NAME "AudioPlayer Project" CACHE STRING "Provisional company / publisher name")
set(AP_BUNDLE_ID    "com.audioplayer.app" CACHE STRING "Provisional reverse-DNS application ID")

option(AP_BUILD_APP   "Build the JUCE application (downloads JUCE)" ON)
option(AP_BUILD_TESTS "Build unit and acceptance tests (downloads Catch2)" ON)
option(AP_BUILD_TOOLS "Build developer tools (test-signal generator)" ON)

# ---------------------------------------------------------------------------
# Platform checks
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR "This project targets Windows 10/11 x64 only.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only x64 builds are supported. Configure with -A x64 (or use CMakePresets.json).")
endif()

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dynamic CRT (/MD, /MDd) — JUCE's default. Revisit for MSIX in step S2.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerSettings)
include(Dependencies)

# ---------------------------------------------------------------------------
# Sub-projects
# ---------------------------------------------------------------------------
add_subdirectory(engine)

if(AP_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(AP_BUILD_TESTS)
    enable_testing()
endif()

if(AP_BUILD_APP)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()

if(AP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
