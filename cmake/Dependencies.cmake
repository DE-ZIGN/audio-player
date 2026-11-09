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
