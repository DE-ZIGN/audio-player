# Third-party dependencies.
#
# Each dependency is pinned to a commit and verified with SHA-256. Every entry
# here must also be listed in THIRD_PARTY_NOTICES.md.
#
# To use a local checkout instead of downloading, set e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=C:/src/JUCE

include(FetchContent)

# Steinberg ASIO SDK 2.3.4 headers, vendored unmodified in third_party/asiosdk
# (used under GPLv3; see third_party/asiosdk/VENDORED.md).
add_library(ap_asiosdk INTERFACE)
target_include_directories(ap_asiosdk SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/third_party/asiosdk/common")

# Apple ALAC decoder (macosforge/alac c38887c), Apache-2.0, vendored with
# hardening patches in third_party/alac (see VENDORED.md).
add_library(ap_alac STATIC
    "${CMAKE_SOURCE_DIR}/third_party/alac/codec/ALACDecoder.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/alac/codec/ALACBitUtilities.c"
    "${CMAKE_SOURCE_DIR}/third_party/alac/codec/EndianPortable.c"
    "${CMAKE_SOURCE_DIR}/third_party/alac/codec/ag_dec.c"
    "${CMAKE_SOURCE_DIR}/third_party/alac/codec/dp_dec.c"
    "${CMAKE_SOURCE_DIR}/third_party/alac/codec/matrix_dec.c")
target_include_directories(ap_alac SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/third_party/alac/codec")
# The byte-order helpers look for __x86_64__ (not defined by MSVC); this selects little-endian.
target_compile_definitions(ap_alac PRIVATE TARGET_OS_WIN32=1)
if(MSVC)
    target_compile_options(ap_alac PRIVATE /W0)
endif()
set_target_properties(ap_alac PROPERTIES FOLDER "third_party")

# libFLAC 1.5.0 (official release tarball), BSD-3-Clause (COPYING.Xiph).
# Only the C library is built: no programs (GPL), libFLAC++, Ogg, tests,
# examples or docs. The options are set inside a block so they do not leak
# into this project (FLAC requires CMake >= 3.12, so option() honours them).
block(SCOPE_FOR VARIABLES)
    set(BUILD_CXXLIBS OFF)
    set(BUILD_PROGRAMS OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_DOCS OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(WITH_OGG OFF)
    set(INSTALL_MANPAGES OFF)
    set(INSTALL_PKGCONFIG_MODULES OFF)
    set(INSTALL_CMAKE_CONFIG_MODULE OFF)
    set(ENABLE_MULTITHREADING OFF)
    FetchContent_Declare(flac
        URL      https://github.com/xiph/flac/releases/download/1.5.0/flac-1.5.0.tar.xz
        URL_HASH SHA256=f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(flac)
endblock()
set_target_properties(FLAC PROPERTIES FOLDER "third_party")

# mpg123 1.33.7 (official release tarball), LGPL-2.1. libmpg123 is built as a
# DLL (CLAUDE.md: MP3 via mpg123 as a DLL) from its CMake port (ports/cmake).
# Only the decoding library: no libout123, programs or network support.
# No assembler: the portable C decoder ("generic"), so results are the same on
# every CPU and the tests can check values. mpg123 needs CMP0077 to honour the
# options set here (its minimum is 3.12).
block(SCOPE_FOR VARIABLES)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS ON)
    set(BUILD_LIBOUT123 OFF)
    set(BUILD_PROGRAMS OFF)
    set(NETWORK OFF)
    set(NO_ICY ON)
    set(YASM_ASSEMBLER "YASM_ASSEMBLER-NOTFOUND")
    FetchContent_Declare(mpg123
        URL         https://www.mpg123.de/download/mpg123-1.33.7.tar.bz2
        URL_HASH    SHA256=31d0e35a4ca567ec9b5ebda6c3062bb4435d6d3eacd6ef0d95cadd7854dc03ee
        SOURCE_SUBDIR ports/cmake
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(mpg123)
endblock()
set_target_properties(libmpg123 PROPERTIES FOLDER "third_party")
# mpg123.h is in the source tree; the CMake port only exports its build folder (for installs).
# (mpg123's project() redefines mpg123_SOURCE_DIR as ports/cmake, so ask FetchContent for the root.)
FetchContent_GetProperties(mpg123 SOURCE_DIR AP_MPG123_ROOT)
target_include_directories(libmpg123 SYSTEM INTERFACE "$<BUILD_INTERFACE:${AP_MPG123_ROOT}/src/include>")

# libebur128 1.2.6 (release tag archive), MIT (queue.h: BSD-3-Clause).
# Loudness by ITU-R BS.1770 / EBU R128 for ReplayGain 2.0 (task 3-4). Only
# its one source file is built here, as a static library; its own CMake
# (shared library, tests) is not used. Its peak functions are not used: peaks
# come from the engine's own True Peak meter (CLAUDE.md chapter 4).
FetchContent_Declare(ebur128
    URL      https://github.com/jiixyj/libebur128/archive/refs/tags/v1.2.6.tar.gz
    URL_HASH SHA256=baa7fc293a3d4651e244d8022ad03ab797ca3c2ad8442c43199afe8059faa613
    SOURCE_SUBDIR not-used # no CMakeLists.txt there: FetchContent only downloads
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(ebur128)
add_library(ap_ebur128 STATIC "${ebur128_SOURCE_DIR}/ebur128/ebur128.c")
target_include_directories(ap_ebur128 SYSTEM PUBLIC "${ebur128_SOURCE_DIR}/ebur128")
target_include_directories(ap_ebur128 PRIVATE "${ebur128_SOURCE_DIR}/ebur128/queue")
target_compile_definitions(ap_ebur128 PRIVATE _USE_MATH_DEFINES)
set_target_properties(ap_ebur128 PROPERTIES FOLDER "third_party")

# SQLite 3.50.4 (amalgamation), public domain. The library database (task
# 5-1, CLAUDE.md chapter 7). Built here as a static library with FTS5 (the
# trigram tokenizer for Japanese substring search, task 5-4).
FetchContent_Declare(sqlite
    URL      https://sqlite.org/2025/sqlite-amalgamation-3500400.zip
    URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032
    SOURCE_SUBDIR not-used # no CMakeLists.txt there: FetchContent only downloads
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite)
add_library(ap_sqlite STATIC "${sqlite_SOURCE_DIR}/sqlite3.c")
target_include_directories(ap_sqlite SYSTEM PUBLIC "${sqlite_SOURCE_DIR}")
target_compile_definitions(ap_sqlite
    PUBLIC  SQLITE_OMIT_LOAD_EXTENSION # no run-time extensions
    PRIVATE SQLITE_ENABLE_FTS5 SQLITE_DQS=0 SQLITE_DEFAULT_FOREIGN_KEYS=1 SQLITE_THREADSAFE=1)
target_compile_options(ap_sqlite PRIVATE /W0)
set_target_properties(ap_sqlite PROPERTIES FOLDER "third_party")

# TagLib 2.1.1 (release archive), LGPL-2.1 or MPL-1.1; its bundled utfcpp is
# BSL-1.0. Reads the tags of the music files (task 5-3); the application never
# writes them. A static library (the source is public: the LGPL's relinking
# is possible). Formats the application does not play are left out; zlib
# (compressed ID3v2 frames, rare) is not used.
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(WITH_ASF OFF CACHE BOOL "" FORCE)
set(WITH_MOD OFF CACHE BOOL "" FORCE)
set(WITH_SHORTEN OFF CACHE BOOL "" FORCE)
set(WITH_TRUEAUDIO OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_utf8cpp TRUE) # the copy in the archive, not one installed elsewhere
FetchContent_Declare(taglib
    URL      https://github.com/taglib/taglib/releases/download/v2.1.1/taglib-2.1.1.tar.gz
    URL_HASH SHA256=3716d31f7c83cbf17b67c8cf44dd82b2a2f17e6780472287a16823e70305ddba
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
FetchContent_MakeAvailable(taglib)
set_target_properties(tag PROPERTIES FOLDER "third_party")
# TagLib publishes its header folders only for an installed copy; in this
# build they are given here (the folders of the formats that are built).
foreach(dir . toolkit mpeg mpeg/id3v1 mpeg/id3v2 mpeg/id3v2/frames ogg flac ape mp4 riff riff/aiff riff/wav dsf dsdiff)
    target_include_directories(tag SYSTEM INTERFACE "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/${dir}>")
endforeach()
target_include_directories(tag SYSTEM INTERFACE "$<BUILD_INTERFACE:${taglib_BINARY_DIR}>") # taglib_config.h

# Copies the mpg123 DLL next to an executable that uses the engine.
function(ap_copy_engine_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:libmpg123> $<TARGET_FILE_DIR:${target}>
        VERBATIM)
endfunction()

# Microsoft WebView2 SDK 1.0.3485.44 (NuGet package), BSD-3-Clause.
# JUCE links its static loader (WebView2LoaderStatic.lib); the WebView2
# runtime itself is the one installed in Windows. JUCE's FindWebView2 looks
# for a "*Microsoft.Web.WebView2*" folder under JUCE_WEBVIEW2_PACKAGE_LOCATION.
if(AP_BUILD_APP)
    set(AP_WEBVIEW2_VERSION 1.0.3485.44)
    set(AP_WEBVIEW2_PARENT "${FETCHCONTENT_BASE_DIR}/webview2-package")
    FetchContent_Declare(webview2
        URL           https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${AP_WEBVIEW2_VERSION}
        URL_HASH      SHA256=bc09150b179246ac90189649b13be8e6b11b3ac200e817e18df106e1f3cf489e
        DOWNLOAD_NAME microsoft.web.webview2.${AP_WEBVIEW2_VERSION}.zip
        SOURCE_DIR    "${AP_WEBVIEW2_PARENT}/Microsoft.Web.WebView2.${AP_WEBVIEW2_VERSION}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(webview2)
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${AP_WEBVIEW2_PARENT}")
endif()

# JUCE 8.0.17 (tag 8.0.17-security), used under AGPLv3.
if(AP_BUILD_APP)
    FetchContent_Declare(JUCE
        URL      https://github.com/juce-framework/JUCE/archive/e548e97a54a083acbfc330d584419fd074409045.tar.gz
        URL_HASH SHA256=313d076c2398157555077215fc875b59c3dde5be61606a6882816027246fbcb3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

    set(JUCE_BUILD_EXTRAS   OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(JUCE)
endif()

# Catch2 v3.16.1, BSL-1.0. Test-only; not part of the distributed application.
if(AP_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL      https://github.com/catchorg/Catch2/archive/08092139210881f01a34e60c5f2fd6afa0ea4024.tar.gz
        URL_HASH SHA256=c66daf9f31712f673ebdffb074c90b5d7e61ea50adc6a1d2db95e798af027940
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

    set(CATCH_INSTALL_DOCS   OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(Catch2)
    # Provides catch_discover_tests(). This file is include()d from the root,
    # so the change is visible to tests/.
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
