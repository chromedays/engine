add_library(nv STATIC
    src/anim.cpp
    src/gltf.c
    src/gpu.c
    src/imgui.c
    src/renderer.c
    src/scene.c
    src/window.c)

target_include_directories(nv PUBLIC include)
target_link_libraries(nv PUBLIC webgpu cimgui PRIVATE cgltf ozz_animation_offline ozz_animation ozz_base)
