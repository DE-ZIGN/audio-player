# The licenses screen's data (task 8-2; the settings' About tab).
#
# ap_make_about_data(<out_dir>) reads the list of the distributed components,
# third_party/components.json, and writes into <out_dir>:
#   about.json     - the list as the page reads it (ui/src/about.ts), with the
#                    app's name and version and the names of the copied texts
#   <id>-<n>.txt   - each component's license texts, copied out of its fetched
#                    source (FetchContent) or this repository; app-<n>.txt the app's
# The app copies <out_dir> next to its built page (app/CMakeLists.txt: ui/about).
#
# A listed text that is not there stops the configure step, with the file and
# the component named: an update of a dependency cannot drop a license quietly.
# The list and the repository's texts are configure dependencies, so a change
# to either makes the data again; the fetched sources change only with
# cmake/Dependencies.cmake, which is one already.
#
# Runs at configure time, after cmake/Dependencies.cmake (the fetched sources).

include(FetchContent)

# Copies the texts of one entry (its JSON) and returns their JSON array
# ([{"file": "<prefix>-<n>.txt", "name": "<the file's own name>"}, ...]).
function(_ap_about_texts out_var entry prefix out_dir)
    string(JSON count ERROR_VARIABLE err LENGTH "${entry}" texts)
    if(err OR count EQUAL 0)
        message(FATAL_ERROR "third_party/components.json: ${prefix} has no license texts (\"texts\").")
    endif()
    set(result "[]")
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
        string(JSON path GET "${entry}" texts ${i} path)
        string(JSON fetch ERROR_VARIABLE no_fetch GET "${entry}" texts ${i} fetch)
        if(no_fetch)
            set(root "${CMAKE_SOURCE_DIR}")
            set(where "this repository")
        else()
            FetchContent_GetProperties(${fetch} SOURCE_DIR root POPULATED populated)
            if(NOT populated)
                message(FATAL_ERROR "third_party/components.json: ${prefix} names the fetched source \"${fetch}\", "
                                    "which this build has not fetched (cmake/Dependencies.cmake).")
            endif()
            set(where "the fetched source \"${fetch}\" (${root})")
        endif()
        set(src "${root}/${path}")
        if(NOT EXISTS "${src}" OR IS_DIRECTORY "${src}")
            message(FATAL_ERROR "The licenses screen: the license text \"${path}\" of \"${prefix}\" is not in ${where}.\n"
                                "A dependency's update may have moved or removed it: find its license text and change "
                                "the entry in third_party/components.json (and THIRD_PARTY_NOTICES.md) to match.")
        endif()
        get_filename_component(name "${path}" NAME)
        if(NOT name MATCHES "^[A-Za-z0-9._-]+$")
            message(FATAL_ERROR "third_party/components.json: ${prefix}: an unexpected file name \"${name}\".")
        endif()
        math(EXPR number "${i} + 1")
        set(file "${prefix}-${number}.txt")
        file(COPY_FILE "${src}" "${out_dir}/${file}")
        set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${src}")
        string(JSON result SET "${result}" ${i} "{\"file\": \"${file}\", \"name\": \"${name}\"}")
    endforeach()
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

# A CMake string as a JSON string (quotes and backslashes escaped).
function(_ap_json_string out_var value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(${out_var} "\"${value}\"" PARENT_SCOPE)
endfunction()

function(ap_make_about_data out_dir)
    set(list_file "${CMAKE_SOURCE_DIR}/third_party/components.json")
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${list_file}")
    file(READ "${list_file}" list_json)

    # Made afresh each time: a text no longer listed does not stay behind.
    file(REMOVE_RECURSE "${out_dir}")
    file(MAKE_DIRECTORY "${out_dir}")

    string(JSON app GET "${list_json}" app)
    _ap_about_texts(app_texts "${app}" app "${out_dir}")
    _ap_json_string(app_name "${AP_DISPLAY_NAME}")
    _ap_json_string(app_version "${PROJECT_VERSION}")
    set(app "{}")
    string(JSON app SET "${app}" name "${app_name}")
    string(JSON app SET "${app}" version "${app_version}")
    string(JSON app SET "${app}" texts "${app_texts}")

    string(JSON components GET "${list_json}" components)
    string(JSON count LENGTH "${components}")
    math(EXPR last "${count} - 1")
    set(ids "")
    foreach(i RANGE ${last})
        string(JSON entry GET "${components}" ${i})
        string(JSON id GET "${entry}" id)
        if(NOT id MATCHES "^[a-z0-9]+(-[a-z0-9]+)*$" OR id STREQUAL "app" OR id IN_LIST ids)
            message(FATAL_ERROR "third_party/components.json: the id \"${id}\" is not a new lowercase name.")
        endif()
        list(APPEND ids "${id}")
        _ap_about_texts(texts "${entry}" "${id}" "${out_dir}")
        string(JSON entry SET "${entry}" texts "${texts}")
        # Where the build found it is the build's business, not the page's.
        string(JSON fetch ERROR_VARIABLE no_fetch GET "${entry}" fetch)
        if(NOT no_fetch)
            string(JSON entry REMOVE "${entry}" fetch)
        endif()
        string(JSON components SET "${components}" ${i} "${entry}")
    endforeach()

    set(data "{}")
    string(JSON data SET "${data}" app "${app}")
    string(JSON data SET "${data}" components "${components}")
    file(WRITE "${out_dir}/about.json" "${data}\n")
    list(LENGTH ids n)
    message(STATUS "Licenses screen: ${n} components, their texts in ${out_dir}")
endfunction()
