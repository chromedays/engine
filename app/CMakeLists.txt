add_executable(app main.c console.c save.c stress.c textures.c ui.c ui_desktop.c ui_phone.c undo.c)
target_link_libraries(app PRIVATE nv)
nv_setup_executable(app ROOT ASSETS "${PROJECT_SOURCE_DIR}/assets/quaternius")

# The benchmark report names the commit it was built from (as of the last configure).
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE NV_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NV_GIT_COMMIT)
    target_compile_definitions(app PRIVATE NV_GIT_COMMIT="${NV_GIT_COMMIT}")
endif()
# Shown in the View tab and in benchmark reports, so Debug numbers are not taken for Release ones.
target_compile_definitions(app PRIVATE NV_BUILD_NAME="${CMAKE_BUILD_TYPE}")
