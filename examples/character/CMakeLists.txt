add_executable(character main.c)
target_link_libraries(character PRIVATE nv)
nv_setup_executable(character ASSETS "${PROJECT_SOURCE_DIR}/assets/quaternius")
