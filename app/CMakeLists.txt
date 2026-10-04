add_executable(app main.c console.c effects.c save.c search.c selection.c shortcuts.c strings.c stress.c textures.c ui.c ui_desktop.c ui_phone.c undo.c)
target_link_libraries(app PRIVATE nv)
nv_setup_executable(app ROOT ASSETS "${PROJECT_SOURCE_DIR}/assets")

# The build label and the benchmark report name the commit the build comes from and its build type (nv_version.h, NV_BUILD_NAME).
nv_add_version(app)
