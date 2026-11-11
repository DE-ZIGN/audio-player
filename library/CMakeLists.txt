# ap_library: the library database (tasks 5-1 to 5-5): files, tracks, the
# three layers of displayed values, analysis values, play history.
# Must not depend on the UI or on JUCE (plain C++20 + SQLite).

add_library(ap_library STATIC
    src/sqlite.cpp
    src/migration.cpp
    src/library_schema.cpp
    src/library_db.cpp)

target_link_libraries(ap_library PRIVATE ap_sqlite)

target_include_directories(ap_library PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(ap_library PUBLIC cxx_std_20)

ap_apply_compiler_settings(ap_library)
set_target_properties(ap_library PROPERTIES FOLDER "library")
