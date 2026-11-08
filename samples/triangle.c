// The triangle sample (docs/specs/samples.md): the smallest program the engine draws. One triangle, a camera and a light, made in
// C; it needs no input. Besides the scene it draws only the build label every app has.

#include <engine/gpu.h>
#include <engine/imgui.h>
#include <engine/log.h>
#include <engine/renderer.h>
#include <engine/scene.h>
#include <engine/window.h>

// The commit's hash and subject line and the build type (nv_version.h, written by cmake/version.cmake on every build).
#include "nv_version.h"

#include <stdio.h>

typedef struct Triangle {
    NvArena permanent;
    NvWindow window;
    NvGpu gpu;
    NvRenderer renderer;
    NvImgui imgui;
    NvScene* scene; // about 1 MB: pushed from the arena
    f64 last_time;
    u64 frames;
    char download_text[64];
} Triangle;

global Triangle triangle_state;
global u8 permanent_memory[NV_MEGABYTES(48)];

// One triangle in the XY plane, its face toward +Z (the camera), counter-clockwise from there.
global const NvVertex triangle_vertices[] = {
    {{-0.8f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{ 0.8f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
    {{ 0.0f,  0.7f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
};
global const u32 triangle_indices[] = {0, 1, 2};

internal void build_scene(Triangle* triangle)
{
    NvScene* scene = triangle->scene;
    NvMeshData data = {.vertices = triangle_vertices, .vertex_count = NV_ARRAY_COUNT(triangle_vertices),
                       .indices = triangle_indices, .index_count = NV_ARRAY_COUNT(triangle_indices)};
    NvMeshId mesh = nv_renderer_add_mesh(&triangle->renderer, &data);
    NvMaterialId material = nv_renderer_add_material(
        &triangle->renderer, &(NvMaterialDesc){.base_color = {0.9f, 0.35f, 0.1f, 1.0f}, .double_sided = 1});

    // At the origin looking along -Z, as a node with no rotation does.
    NvNodeId camera = nv_scene_add_node(scene, (NvNodeId){0}, "camera");
    nv_scene_get(scene, camera)->camera = (NvCamera){
        .projection = NV_PROJECTION_PERSPECTIVE, .fov_y = 45.0f * NV_PI / 180.0f, .near_z = 0.05f, .far_z = 100.0f};
    scene->active_camera = camera;

    // The same way (along its -Z, at the triangle's face).
    NvNodeId sun = nv_scene_add_node(scene, (NvNodeId){0}, "sun");
    nv_scene_get(scene, sun)->light =
        (NvLight){.type = NV_LIGHT_DIRECTIONAL, .color = nv_vec3(1.0f, 0.96f, 0.9f), .intensity = 1.1f};

    NvNodeId node = nv_scene_add_node(scene, (NvNodeId){0}, "triangle");
    NvNode* triangle_node = nv_scene_get(scene, node);
    triangle_node->position = nv_vec3(0.0f, 0.0f, -3.0f);
    triangle_node->mesh = mesh;
    triangle_node->material = material;
}

internal void frame(void* userdata)
{
    Triangle* triangle = userdata;
    nv_log_pump();
    f64 now = nv_time_seconds();
    f32 dt = (f32)(now - triangle->last_time);
    triangle->last_time = now;
    NvGpuFrame gpu_frame = nv_gpu_begin_frame(&triangle->gpu);
    if (!gpu_frame.ok)
        return; // a surface that is not ready yet

    // The whole canvas is the viewport.
    NvRect viewport = {0, 0, triangle->gpu.width, triangle->gpu.height};
    triangle->imgui.view_rect = viewport;
    nv_imgui_new_frame(&triangle->imgui, dt);
    char text[160];
    snprintf(text, sizeof(text), "%s build %s%s%s", NV_BUILD_NAME, NV_GIT_COMMIT, triangle->download_text[0] ? " \xC2\xB7 " : "",
             triangle->download_text);
    nv_imgui_draw_build_label(&triangle->imgui, viewport, text, 0.0f, "Commit: " NV_GIT_SUBJECT);

    nv_scene_update(triangle->scene);
    NvResolution resolution = {.mode = NV_RESOLUTION_SCALE, .divisor = 1};
    nv_renderer_draw(&triangle->renderer, triangle->scene, NULL, nv_renderer_scene_output(&resolution, viewport), gpu_frame.view);
    nv_imgui_render(&triangle->imgui, gpu_frame.view);
    nv_gpu_end_frame(&triangle->gpu);
    nv_renderer_end_frame(&triangle->renderer);
    ++triangle->frames;
}

#if NV_DEBUG
// Debug exports (docs/specs/samples.md): they read. 0 frames drawn, 1 warnings and errors in the log.
NV_DEBUG_EXPORT int triangle_debug_stat(int which)
{
    switch (which) {
    case 0: return (int)triangle_state.frames;
    case 1: return (int)(nv_log_ring.level_counts[NV_LOG_WARNING] + nv_log_ring.level_counts[NV_LOG_ERROR]);
    }
    return -1;
}
#endif

int main(void)
{
    Triangle* triangle = &triangle_state;
    nv_arena_init(&triangle->permanent, permanent_memory, sizeof(permanent_memory));
    triangle->scene = NV_PUSH_STRUCT(&triangle->permanent, NvScene);

    nv_window_create(&triangle->window, "nv triangle sample");
    if (!nv_gpu_create(&triangle->gpu, &triangle->window)) {
        nv_log(NV_LOG_ERROR, "triangle", "fatal: failed to initialize the GPU");
        return 1;
    }
    // The renderer's defaults: MSAA 1, Clamp, exposure 1, no bloom, no shadows.
    nv_renderer_init(&triangle->renderer, &triangle->gpu, &triangle->permanent);
    nv_imgui_init(&triangle->imgui, &triangle->gpu, &triangle->window, &triangle->permanent);
    nv_imgui_load_ui_font(&triangle->imgui, &triangle->permanent);
    nv_window_download_text(triangle->download_text, sizeof(triangle->download_text));
    if (!triangle->download_text[0]) // nothing was downloaded (a native build, whose files are on the disk): the label names the backend
        snprintf(triangle->download_text, sizeof(triangle->download_text), "%s", triangle->gpu.backend_name);
    build_scene(triangle);

    triangle->last_time = nv_time_seconds();
    nv_window_run(&triangle->window, frame, triangle);
    return 0;
}
