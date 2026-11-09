# ap_play: step S1/S2 prototype player (bit-perfect WAV -> exclusive WASAPI / dummy output)
# with environment diagnostics for MSIX (package identity, ASIO driver loading).

add_executable(ap_play
    main.cpp
    environment_probe.cpp
    environment_probe.h)
target_link_libraries(ap_play PRIVATE ap_engine ole32)
ap_apply_compiler_settings(ap_play)
ap_copy_engine_dlls(ap_play) # mpg123.dll next to the program
set_target_properties(ap_play PROPERTIES FOLDER "tools")
