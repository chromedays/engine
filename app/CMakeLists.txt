add_executable(app main.c console.c save.c search.c shortcuts.c strings.c stress.c textures.c ui.c ui_desktop.c ui_phone.c undo.c)
target_link_libraries(app PRIVATE nv)
nv_setup_executable(app ROOT ASSETS "${PROJECT_SOURCE_DIR}/assets")

# The build label and the benchmark report name the commit the build comes from: its hash and its
# subject line. The header is rewritten on every build (cmake/version.cmake), so it is never the
# commit of an older configure.
set(NV_VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/nv_version.h)
add_custom_target(app_version
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUTPUT=${NV_VERSION_HEADER}
            -P ${PROJECT_SOURCE_DIR}/cmake/version.cmake
    BYPRODUCTS ${NV_VERSION_HEADER}
    COMMENT "Recording the commit")
add_dependencies(app app_version)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# Shown in the View tab and in benchmark reports, so Debug numbers are not taken for Release ones.
target_compile_definitions(app PRIVATE NV_BUILD_NAME="${CMAKE_BUILD_TYPE}")
