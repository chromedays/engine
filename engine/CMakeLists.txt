add_library(nv STATIC
    src/gpu.c
    src/imgui.c
    src/scene.c
    src/window.c)

target_include_directories(nv PUBLIC include)
target_link_libraries(nv PUBLIC webgpu cimgui)
