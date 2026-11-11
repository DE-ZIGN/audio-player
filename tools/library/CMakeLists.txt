# ap_lib: tries the library database on real folders (tasks 5-2, 5-3).

add_executable(ap_lib main.cpp)
target_link_libraries(ap_lib PRIVATE ap_library)
ap_apply_compiler_settings(ap_lib)
set_target_properties(ap_lib PROPERTIES FOLDER "tools")
