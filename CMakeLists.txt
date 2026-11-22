cmake_minimum_required(VERSION 3.25)

project(audio_player
    VERSION 0.1.0
    DESCRIPTION "Windows audio player with a bit-perfect path and a 64-bit float processing path"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Product identity (change here only)
# ---------------------------------------------------------------------------
# The product's name, as shown to the user (decided 2026-11-18): the window's
# title, the page's title.
set(AP_DISPLAY_NAME "DE:ZIGN AUDIO PLAYER" CACHE STRING "The product's name as shown to users")
# The names used for files (decided 2026-11-21, task 8-3a): a colon cannot be
# in a file or folder name. The executable (DEZIGNAudioPlayer.exe), and the
# folder of the library, the settings and the web views' storage under
# %LOCALAPPDATA%. The library and settings of the provisional name
# (%LOCALAPPDATA%\AudioPlayer) are not moved (the user, 2026-11-21).
set(AP_PRODUCT_NAME "DEZIGNAudioPlayer" CACHE STRING "The executable's name (no extension)")
set(AP_DATA_FOLDER  "DEZIGN Audio Player" CACHE STRING "The data folder's name under %LOCALAPPDATA%")
# The publisher, as the Store shows it (it must match the Partner Center
# account's display name, task 8-5); also the executable's company name.
set(AP_COMPANY_NAME "DE:ZIGN" CACHE STRING "The publisher's display name")
set(AP_BUNDLE_ID    "io.github.de-zign.audioplayer" CACHE STRING "Reverse-DNS application ID")
# The MSIX package's identity (task 8-3c), as Partner Center gave it when the
# name was reserved (task 8-5e, 2026-11-22). A package to try at hand is
# signed with a test certificate, whose subject the build script puts in
# place of the Publisher; the Store signs the unsigned one itself.
set(AP_MSIX_IDENTITY_NAME "DEZIGN.DEZIGNAUDIOPLAYER" CACHE STRING "MSIX Identity Name (from Partner Center)")
set(AP_MSIX_PUBLISHER     "CN=955E5C32-0C31-4594-88D0-3D3367E883F3" CACHE STRING "MSIX Identity Publisher (from Partner Center)")

option(AP_BUILD_APP   "Build the JUCE application (downloads JUCE)" ON)
option(AP_BUILD_TESTS "Build unit and acceptance tests (downloads Catch2)" ON)
option(AP_BUILD_TOOLS "Build developer tools (test-signal generator)" ON)
option(AP_ASAN "Build everything with AddressSanitizer (preset vs2022-asan; for running the tests)" OFF)

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

# AddressSanitizer for every target, third-party code included (decoders read
# untrusted files). Incompatible with /RTC and incremental linking.
if(AP_ASAN)
    if(NOT MSVC)
        message(FATAL_ERROR "AP_ASAN is set up for MSVC only")
    endif()
    foreach(lang C CXX)
        foreach(cfg DEBUG RELWITHDEBINFO)
            string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_${lang}_FLAGS_${cfg} "${CMAKE_${lang}_FLAGS_${cfg}}")
        endforeach()
    endforeach()
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO /DEBUG)
    # The ASan runtime DLL sits next to cl.exe; tests need it on PATH.
    get_filename_component(AP_ASAN_DLL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
endif()

include(CompilerSettings)
include(Dependencies)

# ---------------------------------------------------------------------------
# Sub-projects
# ---------------------------------------------------------------------------
add_subdirectory(engine)
add_subdirectory(library)
add_subdirectory(app/logic) # no JUCE; built and tested without the application too

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
