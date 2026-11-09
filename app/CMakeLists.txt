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
    src/UiResources.cpp
    src/UiResources.h)

target_compile_definitions(ap_app PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ap_app,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ap_app,JUCE_VERSION>"
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_ASIO=0             # our own ASIO implementation lives in engine/
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(ap_app
    PRIVATE
        ap_engine
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

ap_apply_juce_target_settings(ap_app)
ap_copy_engine_dlls(ap_app) # mpg123.dll next to the program
set_target_properties(ap_app PROPERTIES FOLDER "app")

# The built UI is served from <exe dir>/ui by the resource provider.
if(TARGET ap_ui)
    add_dependencies(ap_app ap_ui)
    add_custom_command(TARGET ap_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:ap_app>/ui"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${AP_UI_DIST_DIR}" "$<TARGET_FILE_DIR:ap_app>/ui"
        COMMENT "Copying the UI next to the app"
        VERBATIM)
endif()
