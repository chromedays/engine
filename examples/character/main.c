#include <nv/anim.h>
#include <nv/gltf.h>
#include <nv/gpu.h>
#include <nv/imgui.h>
#include <nv/renderer.h>
#include <nv/scene.h>
#include <nv/window.h>

#include <stdio.h>

typedef struct App {
    NvWindow window;
    NvGpu gpu;
    NvArena permanent;
    NvArena scratch;
    NvScene* scene;
    NvRenderer renderer;
    NvImgui imgui;

    NvGltfModel character;
    f64 last_time;
} App;

// IMPORTANT: Global rather than on main's stack: main returns before the first frame runs.
global App app_state;
global u8 permanent_memory[NV_MEGABYTES(48)];
global u8 scratch_memory[NV_MEGABYTES(32)];

internal void build_scene(App* app)
{
    NvScene* scene = app->scene;
    NvNodeId none = {0};

    NvNodeId camera = nv_scene_add_node(scene, none, "camera");
    NvNode* camera_node = nv_scene_get(scene, camera);
    camera_node->position = nv_vec3(0.0f, 1.3f, 3.4f);
    camera_node->rotation = nv_quat_axis_angle(nv_vec3(1, 0, 0), -0.12f);
    camera_node->camera = (NvCamera){
        .projection = NV_PROJECTION_PERSPECTIVE,
        .fov_y = 45.0f * NV_PI / 180.0f,
        .near_z = 0.05f,
        .far_z = 100.0f,
    };
    scene->active_camera = camera;

    NvNodeId sun = nv_scene_add_node(scene, none, "sun");
    NvNode* sun_node = nv_scene_get(scene, sun);
    sun_node->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), 0.5f),
                                     nv_quat_axis_angle(nv_vec3(1, 0, 0), -0.8f));
    sun_node->light = (NvLight){.type = NV_LIGHT_DIRECTIONAL, .color = nv_vec3(1.0f, 0.96f, 0.9f), .intensity = 1.1f};
}

internal void frame(void* userdata)
{
    App* app = userdata;
    f64 now = nv_time_seconds();
    f32 dt = (f32)(now - app->last_time);
    app->last_time = now;

    WGPUTextureView target = nv_gpu_begin_frame(&app->gpu);
    if (!target)
        return;

    nv_imgui_new_frame(&app->imgui, dt);
    igText("%.0f FPS", igGetIO_Nil()->Framerate);

    nv_scene_update(app->scene);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->gpu.device, NULL);
    nv_renderer_draw(&app->renderer, app->scene, NULL, encoder, target);
    nv_imgui_render(&app->imgui, encoder, target);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(app->gpu.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    nv_gpu_end_frame(&app->gpu);
}

int main(void)
{
    App* app = &app_state;
    nv_arena_init(&app->permanent, permanent_memory, sizeof(permanent_memory));
    nv_arena_init(&app->scratch, scratch_memory, sizeof(scratch_memory));
    app->scene = NV_PUSH_STRUCT(&app->permanent, NvScene);

    nv_window_create(&app->window, "nv - character");
    if (!nv_gpu_create(&app->gpu, &app->window)) {
        fprintf(stderr, "fatal: failed to initialize WebGPU\n");
        return 1;
    }
    nv_renderer_init(&app->renderer, &app->gpu, &app->permanent);
    nv_imgui_init(&app->imgui, &app->gpu, &app->window, &app->permanent);

    build_scene(app);
    if (!nv_gltf_load_model("/assets/character.glb", app->scene, &app->renderer, &app->permanent, &app->scratch, &app->character)) {
        fprintf(stderr, "fatal: failed to load the character\n");
        return 1;
    }
    printf("[character] %u mesh nodes, %u joints\n", app->character.mesh_node_count, app->character.joint_count);

    app->last_time = nv_time_seconds();
    nv_window_run(&app->window, frame, app);
    return 0;
}
