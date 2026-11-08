// The visual regression test's app (docs/specs/visual_regression.md): renders one case, a triangle
// scene, in a Wayland window for a few frames, writes the last one as a PNG, and exits.
//   visual_test <output directory>     writes <output directory>/triangle.png
// The comparison with the baseline is tools/visual/compare.mjs (odiff); this program only captures.
// It is a Debug-build program: the screenshot it asks for (nv_gpu_debug_screenshot) exists there only.

#include <engine/gpu.h>
#include <engine/log.h>
#include <engine/renderer.h>
#include <engine/scene.h>
#include <engine/window.h>
#include <engine/window_linux.h> // engine-private: the fixed size this test asks for

#include <stdio.h>

#define CAPTURE_FRAME 3 // frames 1 and 2 let the pipelines and targets finish being made
#define PATH_CAPACITY 520

global u8 permanent_memory[NV_MEGABYTES(48)];

typedef struct VisualTest {
    NvArena permanent;
    NvWindow window;
    NvGpu gpu;
    NvRenderer renderer;
    NvScene* scene; // about 1 MB: pushed from the arena, not a field of this struct's stack copies
    u32 frames;
    char screenshot_path[PATH_CAPACITY];
} VisualTest;

global VisualTest test_state;

// One triangle in the XY plane, its face toward +Z (the camera), counter-clockwise from there.
global const NvVertex triangle_vertices[] = {
    {{-0.8f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{ 0.8f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
    {{ 0.0f,  0.7f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
};
global const u32 triangle_indices[] = {0, 1, 2};

internal void build_scene(VisualTest* test)
{
    NvScene* scene = test->scene;
    NvMeshData triangle = {.vertices = triangle_vertices, .vertex_count = NV_ARRAY_COUNT(triangle_vertices),
                           .indices = triangle_indices, .index_count = NV_ARRAY_COUNT(triangle_indices)};
    NvMeshId mesh = nv_renderer_add_mesh(&test->renderer, &triangle);
    NvMaterialId material = nv_renderer_add_material(
        &test->renderer, &(NvMaterialDesc){.base_color = {0.9f, 0.35f, 0.1f, 1.0f}, .double_sided = 1});

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
    VisualTest* test = userdata;
    nv_log_pump();
    NvGpuFrame gpu_frame = nv_gpu_begin_frame(&test->gpu);
    if (!gpu_frame.ok)
        return; // a surface that is not ready yet: this frame does not count
    nv_scene_update(test->scene);
    // A zeroed output is the whole canvas at its own resolution: the window's fixed 1280x720.
    nv_renderer_draw(&test->renderer, test->scene, NULL, (NvSceneOutput){0}, gpu_frame.view);
    ++test->frames;
#if NV_DEBUG && NV_PLATFORM_LINUX
    if (test->frames == CAPTURE_FRAME)
        nv_gpu_debug_screenshot(&test->gpu, test->screenshot_path, 0, 0, 0, 0);
#endif
    nv_gpu_end_frame(&test->gpu);
    nv_renderer_end_frame(&test->renderer);
    if (test->frames >= CAPTURE_FRAME)
        nv_quit();
}

internal b32 file_exists(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return 0;
    fclose(file);
    return 1;
}

int main(int argc, char** argv)
{
#if !NV_DEBUG
    (void)argc, (void)argv;
    nv_log(NV_LOG_ERROR, "visual", "visual_test needs a Debug build: the GPU screenshot exists there only");
    return 2;
#else
    if (argc != 2) {
        nv_log(NV_LOG_ERROR, "visual", "usage: visual_test <output directory>");
        return 2;
    }
    VisualTest* test = &test_state;
    snprintf(test->screenshot_path, sizeof(test->screenshot_path), "%s/triangle.png", argv[1]);
    nv_arena_init(&test->permanent, permanent_memory, sizeof(permanent_memory));
    test->scene = NV_PUSH_STRUCT(&test->permanent, NvScene);

    nv_window_create(&test->window, "visual_test");
    // The baseline's size, at scale 1, whatever the compositor makes of the window (docs/specs/linux_wayland.md, "고정 크기").
    nv_window_linux_set_fixed_size(&test->window, 1280, 720);
    if (!nv_gpu_create(&test->gpu, &test->window)) {
        nv_log(NV_LOG_ERROR, "visual", "fatal: failed to initialize the GPU");
        return 1;
    }
    // The renderer's defaults are the case's settings: MSAA 1, Clamp, exposure 1, no bloom, no shadows.
    nv_renderer_init(&test->renderer, &test->gpu, &test->permanent);
    build_scene(test);

    nv_window_run(&test->window, frame, test);
    if (!file_exists(test->screenshot_path)) {
        nv_log(NV_LOG_ERROR, "visual", "no screenshot at %s (%u frames drawn)", test->screenshot_path, test->frames);
        return 1;
    }
    nv_log(NV_LOG_INFO, "visual", "captured %s after %u frames", test->screenshot_path, test->frames);
    return 0;
#endif
}
