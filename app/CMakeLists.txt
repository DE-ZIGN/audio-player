# ap_app: JUCE application shell. Bridges the engine and the UI.
#
# JUCE's device layer (juce_audio_devices / AudioDeviceManager) is deliberately
# not linked: the output layer is our own implementation in engine/.

juce_add_gui_app(ap_app
    PRODUCT_NAME "${AP_PRODUCT_NAME}"
    COMPANY_NAME "${AP_COMPANY_NAME}"
    BUNDLE_ID    "${AP_BUNDLE_ID}"
    VERSION      "${PROJECT_VERSION}")

target_sources(ap_app PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/MainComponent.h)

target_compile_definitions(ap_app PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ap_app,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ap_app,JUCE_VERSION>"
    JUCE_WEB_BROWSER=0      # enabled in step S3 (WebView2)
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
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

ap_apply_juce_target_settings(ap_app)
set_target_properties(ap_app PROPERTIES FOLDER "app")
