# The samples (docs/specs/samples.md): one feature each, each a standalone executable made of one file (<name>.c), linked to the
# engine alone. Each builds into a folder of its own (<binary directory>/<name>), which holds its page and its files on the web
# and its executable natively. A sample packs only the assets it reads.
#   add_sample(<name> [PRELOAD <dir>@<path> ...])
function(add_sample name)
    cmake_parse_arguments(ARG "" "" "PRELOAD" ${ARGN})
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nv)
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${name}")
    nv_setup_executable(${name} PRELOAD ${ARG_PRELOAD})
    # The build label (and the stress benchmark's table) name the commit and the build type (nv_version.h, NV_BUILD_NAME).
    nv_add_version(${name})
endfunction()

# One triangle: the smallest program the engine draws.
add_sample(triangle PRELOAD "${PROJECT_SOURCE_DIR}/assets/fonts@/assets/fonts")
# Loads the engine with many objects and measures it: the UI font and the character.
add_sample(stress PRELOAD "${PROJECT_SOURCE_DIR}/assets/fonts@/assets/fonts" "${PROJECT_SOURCE_DIR}/assets/quaternius@/assets/quaternius")
