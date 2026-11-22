# ap_app_logic: what the app decides without JUCE (task 7-1: where the article
# reader may go; task 11-3: the codes it tells the page, and the texts it draws
# before the page exists; where a queue starts). A library of its own, so the tests link it in
# builds that leave the application out (AP_BUILD_APP=OFF, AddressSanitizer).

add_library(ap_app_logic STATIC
    ArticlePolicy.cpp
    ArticlePolicy.h
    NativeMessages.h
    QueueStart.h
    StartupTexts.cpp
    StartupTexts.h)
target_include_directories(ap_app_logic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ap_app_logic PUBLIC cxx_std_20)
ap_apply_compiler_settings(ap_app_logic)
set_target_properties(ap_app_logic PROPERTIES FOLDER "app")
