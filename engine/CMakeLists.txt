add_library(nv STATIC
    anim.cpp
    camera.c
    chunk.c
    file.c
    gltf.c
    gpu.c
    imgui.c
    log.c
    mesh.c
    renderer.c
    renderer_cpu.c
    scene.c
    storage.c
    strings.c
    vfx.c
    vfx_cpu.c
    window.c)

# Engine headers are included as <engine/name.h>, from the repository root.
target_include_directories(nv PUBLIC ${PROJECT_SOURCE_DIR})
# IDBFS keeps engine/storage.h's files in IndexedDB.
target_link_options(nv PUBLIC -lidbfs.js)
target_link_libraries(nv PUBLIC webgpu cimgui PRIVATE cgltf ozz_animation_offline ozz_animation ozz_base)
