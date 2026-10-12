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
    strings.c
    text.c
    vfx.c
    vfx_cpu.c)

# The modules whose code depends on the platform have one source for each (docs/specs/d3d12_port.md, "Platform layer"): the
# header is shared, the source is picked here.
if(EMSCRIPTEN)
    target_sources(nv PRIVATE
        image_web.c
        imgui_web.c
        local_file_web.c
        storage_web.c
        window_web.c)
endif()

# The shaders (engine/shaders/*.slang): each becomes a header of its WGSL in the build directory, included as
# "shaders/<name>_wgsl.h" (docs/specs/slang_shader_port.md).
nv_add_shader(imgui imgui.slang NV_IMGUI_WGSL)
nv_add_shader(vfx_compute vfx_compute.slang NV_VFX_COMPUTE_WGSL)
nv_add_shader(vfx_particles vfx_particles.slang NV_VFX_PARTICLES_WGSL)
nv_add_shader(vfx_marks vfx_marks.slang NV_VFX_MARKS_WGSL)
foreach(shader mesh shadow debug_lines upscale bloom preview_color preview_depth preview_depth_ms)
    nv_add_shader(${shader} ${shader}.slang NV_${shader}_WGSL)
    list(APPEND NV_RENDERER_WGSL ${NV_${shader}_WGSL})
endforeach()
target_sources(nv PRIVATE ${NV_RENDERER_WGSL} ${NV_IMGUI_WGSL} ${NV_VFX_COMPUTE_WGSL} ${NV_VFX_PARTICLES_WGSL} ${NV_VFX_MARKS_WGSL})
target_include_directories(nv PRIVATE ${CMAKE_BINARY_DIR})

# Engine headers are included as <engine/name.h>, from the repository root.
target_include_directories(nv PUBLIC ${PROJECT_SOURCE_DIR})
# IDBFS keeps engine/storage.h's files in IndexedDB.
target_link_options(nv PUBLIC -lidbfs.js)
target_link_libraries(nv PUBLIC webgpu cimgui PRIVATE cgltf ozz_animation_offline ozz_animation ozz_base)
