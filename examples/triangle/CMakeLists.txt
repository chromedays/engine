add_executable(triangle main.c)
target_link_libraries(triangle PRIVATE engine)
engine_setup_executable(triangle)
