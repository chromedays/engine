add_executable(triangle main.c)
target_link_libraries(triangle PRIVATE nv)
nv_setup_executable(triangle)
