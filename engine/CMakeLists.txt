add_library(nv STATIC
    anim.cpp
    camera.c
    chunk.c
    file.c
    gltf.c
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
if(NV_WEB)
    target_sources(nv PRIVATE
        gpu_webgpu.c
        image_web.c
        imgui_web.c
        local_file_web.c
        storage_web.c
        window_web.c)
else()
    target_sources(nv PRIVATE
        debug_script.c
        gpu_d3d12.c
        image_win32.c
        imgui_win32.c
        local_file_win32.c
        storage_win32.c
        window_win32.c)
endif()

# The shaders (engine/shaders/*.slang): each becomes a header in the build directory, included as "shaders/<name>_shader.h", which
# defines `<name>_shader` for the GPU API: its WGSL on the web, its DXIL on Windows (docs/specs/slang_shader_port.md, d3d12_port.md).
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
if(NV_WEB)
    # IDBFS keeps engine/storage.h's files in IndexedDB.
    target_link_options(nv PUBLIC -lidbfs.js)
    target_link_libraries(nv PUBLIC webgpu cimgui PRIVATE cgltf ozz_animation_offline ozz_animation ozz_base)
else()
    # The Agility SDK's headers come before the Windows SDK's, so they match the runtime the executables carry; the loader reads
    # the version from the symbols gpu_d3d12.c exports.
    target_include_directories(nv BEFORE PRIVATE ${NV_AGILITY_INCLUDE})
    target_compile_definitions(nv PRIVATE NV_AGILITY_SDK_VERSION=${NV_AGILITY_SDK_VERSION})
    target_link_libraries(nv PUBLIC cimgui
        PRIVATE cgltf ozz_animation_offline ozz_animation ozz_base
                d3d12 dxgi dxguid uuid windowscodecs ole32 user32 gdi32 imm32 comdlg32 advapi32 shell32)
endif()
