# ap_app: JUCE application shell. Hosts the web UI in WebView2 and bridges it
# to the engine.
#
# JUCE's device layer (juce_audio_devices / AudioDeviceManager) is deliberately
# not linked: the output layer is our own implementation in engine/.

juce_add_gui_app(ap_app
    PRODUCT_NAME   "${AP_PRODUCT_NAME}"
    COMPANY_NAME   "${AP_COMPANY_NAME}"
    BUNDLE_ID      "${AP_BUNDLE_ID}"
    VERSION        "${PROJECT_VERSION}"
    NEEDS_WEBVIEW2 TRUE)

target_sources(ap_app PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/MainComponent.h
    src/PlayerBridge.cpp
    src/PlayerBridge.h
    src/SettingsBridge.cpp
    src/EqVar.cpp
    src/SettingsBridge.h
    src/AsioCheck.cpp
    src/AsioCheck.h
    src/LibraryBridge.cpp
    src/LibraryBridge.h
    src/NativeVar.h
    src/UiResources.cpp
    src/UiResources.h
    src/ArticleWindow.cpp
    src/ArticleWindow.h
    src/ArticleSelfTest.cpp
    src/ArticleSelfTest.h)

target_compile_definitions(ap_app PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ap_app,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ap_app,JUCE_VERSION>"
    AP_DISPLAY_NAME_STRING="${AP_DISPLAY_NAME}" # the name shown (the window's title); UTF-8
    AP_DATA_FOLDER_STRING="${AP_DATA_FOLDER}"   # the folder under %LOCALAPPDATA% (task 8-3a)
    # The executable's version information is ours (resources/version.rc.in),
    # so that it describes the product by the name shown.
    JUCE_USER_DEFINED_RC_FILE="ap_version.rc"
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_ASIO=0             # our own ASIO implementation lives in engine/
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(ap_app
    PRIVATE
        ap_engine
        ap_library
        ap_app_logic
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# The version information JUCE compiles in place of its own (task 8-3a), and
# the executable's icon (task 8-3b; made by tools/make_icon.py).
set(AP_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.ico")
configure_file(resources/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/resources/ap_version.rc" @ONLY)
target_include_directories(ap_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/resources")

ap_apply_juce_target_settings(ap_app)
ap_copy_engine_dlls(ap_app) # mpg123.dll next to the program
set_target_properties(ap_app PROPERTIES FOLDER "app")

# The licenses screen's data (task 8-2): the components' list and license
# texts, made from third_party/components.json (cmake/AboutData.cmake).
include(AboutData)
set(AP_ABOUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/about")
ap_make_about_data("${AP_ABOUT_DIR}")

# The built UI is served from <exe dir>/ui by the resource provider; the
# licenses screen's data goes beside the page, in ui/about.
if(TARGET ap_ui)
    add_dependencies(ap_app ap_ui)
    add_custom_command(TARGET ap_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:ap_app>/ui"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${AP_UI_DIST_DIR}" "$<TARGET_FILE_DIR:ap_app>/ui"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${AP_ABOUT_DIR}" "$<TARGET_FILE_DIR:ap_app>/ui/about"
        COMMENT "Copying the UI next to the app"
        VERBATIM)
endif()
