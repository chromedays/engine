add_executable(app main.c ui.c)
target_link_libraries(app PRIVATE nv)
nv_setup_executable(app ROOT ASSETS "${PROJECT_SOURCE_DIR}/assets/quaternius")
