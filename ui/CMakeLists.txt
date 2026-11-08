# Builds the web UI (Vite + TypeScript) with npm and exposes:
#   target ap_ui           - runs "npm install" (when the lock file changes) and "npm run build"
#   AP_UI_DIST_DIR         - folder with the built files (copied next to the app as ui/)
# Node.js is needed only to build; nothing from node_modules is distributed
# except what Vite bundles from src/.

option(AP_BUILD_UI "Build the web UI with npm" ON)
find_program(AP_NPM NAMES npm.cmd npm)

if(NOT AP_BUILD_UI)
    return()
endif()
if(NOT AP_NPM)
    message(WARNING "npm was not found; the UI will not be built (install Node.js LTS).")
    return()
endif()

set(ui_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(npm_stamp "${CMAKE_CURRENT_BINARY_DIR}/npm-ci.stamp")
file(GLOB_RECURSE ui_sources CONFIGURE_DEPENDS "${ui_dir}/src/*")

# "npm install" rather than "npm ci": it leaves an up-to-date node_modules
# alone, so it also works while the Vite dev server holds files open.
# (CI uses a clean checkout, where the two behave the same.)
add_custom_command(
    OUTPUT "${npm_stamp}"
    COMMAND "${AP_NPM}" install --no-audit --no-fund
    COMMAND "${CMAKE_COMMAND}" -E touch "${npm_stamp}"
    WORKING_DIRECTORY "${ui_dir}"
    DEPENDS "${ui_dir}/package.json" "${ui_dir}/package-lock.json"
    COMMENT "UI: installing dependencies (npm install)"
    VERBATIM)

add_custom_command(
    OUTPUT "${ui_dir}/dist/index.html"
    COMMAND "${AP_NPM}" run build
    WORKING_DIRECTORY "${ui_dir}"
    DEPENDS "${npm_stamp}" ${ui_sources} "${ui_dir}/index.html" "${ui_dir}/vite.config.ts" "${ui_dir}/tsconfig.json"
    COMMENT "UI: building (vite)"
    VERBATIM)

add_custom_target(ap_ui ALL DEPENDS "${ui_dir}/dist/index.html")
set_target_properties(ap_ui PROPERTIES FOLDER "ui")
set(AP_UI_DIST_DIR "${ui_dir}/dist" PARENT_SCOPE)

if(AP_BUILD_TESTS)
    add_test(NAME ui_unit_tests COMMAND "${AP_NPM}" test WORKING_DIRECTORY "${ui_dir}")
    set_tests_properties(ui_unit_tests PROPERTIES LABELS "unit")
endif()
