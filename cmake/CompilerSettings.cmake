# Compiler settings applied to our own targets only (never to third-party code).
#
# Floating point: /fp:precise is stated explicitly and /fp:fast must not be used.
# The acceptance tests compare engine output byte-for-byte, which requires
# reproducible floating-point behaviour.

# Settings for plain C++ targets (engine, tests).
function(ap_apply_compiler_settings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /WX
            /permissive-
            /utf-8
            /EHsc
            /fp:precise
            /Zc:__cplusplus
            /Zc:preprocessor
            /external:anglebrackets
            /external:W0
            /MP)
        target_compile_definitions(${target} PRIVATE
            UNICODE _UNICODE
            NOMINMAX
            WIN32_LEAN_AND_MEAN)
    endif()
endfunction()

# Settings for JUCE application targets. JUCE module sources are compiled into
# the consuming target, so we keep to options JUCE itself is clean under and let
# juce::juce_recommended_warning_flags set the warning level.
function(ap_apply_juce_target_settings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /WX
            /permissive-
            /utf-8
            /fp:precise
            /Zc:__cplusplus
            /MP)
    endif()
endfunction()
