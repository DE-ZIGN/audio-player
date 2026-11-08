# Third-party dependencies.
#
# Each dependency is pinned to a commit and verified with SHA-256. Every entry
# here must also be listed in THIRD_PARTY_NOTICES.md.
#
# To use a local checkout instead of downloading, set e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=C:/src/JUCE

include(FetchContent)

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
