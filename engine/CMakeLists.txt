add_library(nv STATIC
    src/anim.cpp
    src/chunk.c
    src/gltf.c
    src/gpu.c
    src/imgui.c
    src/renderer.c
    src/scene.c
    src/storage.c
    src/window.c)

target_include_directories(nv PUBLIC include)
# IDBFS keeps nv/storage.h's files in IndexedDB.
target_link_options(nv PUBLIC -lidbfs.js)
target_link_libraries(nv PUBLIC webgpu cimgui PRIVATE cgltf ozz_animation_offline ozz_animation ozz_base)
