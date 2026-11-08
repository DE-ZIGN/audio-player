# ap_play: step S1 prototype player (bit-perfect WAV -> exclusive WASAPI / dummy output).

add_executable(ap_play main.cpp)
target_link_libraries(ap_play PRIVATE ap_engine)
ap_apply_compiler_settings(ap_play)
set_target_properties(ap_play PROPERTIES FOLDER "tools")
