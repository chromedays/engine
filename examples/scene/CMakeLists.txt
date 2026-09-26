add_executable(scene main.c)
target_link_libraries(scene PRIVATE nv)
nv_setup_executable(scene)
