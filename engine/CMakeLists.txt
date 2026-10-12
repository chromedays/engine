add_library(nv STATIC
    anim.cpp
    camera.c
    chunk.c
    file.c
    gltf.c
    gpu.c
    imgui.c
    local_file.c
    log.c
    mesh.c
    renderer.c
    renderer_cpu.c
    scene.c
    storage.c
    strings.c
    text.c
    vfx.c
    vfx_cpu.c
    window.c)

# The shaders (engine/shaders/*.slang): each becomes a header of its WGSL in the build directory, included as
# "shaders/<name>_wgsl.h" (docs/specs/slang_shader_port.md).
nv_add_shader(imgui imgui.slang NV_IMGUI_WGSL)
nv_add_shader(vfx_compute vfx_compute.slang NV_VFX_COMPUTE_WGSL)
target_sources(nv PRIVATE ${NV_IMGUI_WGSL} ${NV_VFX_COMPUTE_WGSL})
target_include_directories(nv PRIVATE ${CMAKE_BINARY_DIR})

# Engine headers are included as <engine/name.h>, from the repository root.
target_include_directories(nv PUBLIC ${PROJECT_SOURCE_DIR})
# IDBFS keeps engine/storage.h's files in IndexedDB.
target_link_options(nv PUBLIC -lidbfs.js)
target_link_libraries(nv PUBLIC webgpu cimgui PRIVATE cgltf ozz_animation_offline ozz_animation ozz_base)
