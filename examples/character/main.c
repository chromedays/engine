#include <nv/anim.h>
#include <nv/gltf.h>
#include <nv/gpu.h>
#include <nv/imgui.h>
#include <nv/renderer.h>
#include <nv/scene.h>
#include <nv/window.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_CLIPS 16

typedef enum JumpPhase {
    JUMP_NONE,
    JUMP_START,
    JUMP_AIR,
    JUMP_LAND,
} JumpPhase;

typedef struct App {
    NvWindow window;
    NvGpu gpu;
    NvArena permanent;
    NvArena scratch;
    NvArena anim_memory;
    NvScene* scene;
    NvRenderer renderer;
    NvImgui imgui;

    NvNodeId camera;
    NvGltfModel character;
    NvSkeletonId skeleton;
    NvAnimatorId animator;
    NvClipId clips[MAX_CLIPS];
    const char* clip_names[MAX_CLIPS];
    u32 clip_count;
    NvClipId root_motion_clips[MAX_CLIPS];
    u32 root_motion_clip_count;

    // Playback controls
    f32 fade_seconds;
    s32 blend_clip; // index into clips; blended with the current clip by blend_weight
    f32 blend_weight;
    JumpPhase jump;
    f32 jump_air_time;
    NvClipId jump_return; // loop to go back to after landing

    // View
    bool show_bones; // bool because ImGui writes it through a bool*
    f32 camera_yaw;
    f64 last_time;
} App;

// IMPORTANT: Global rather than on main's stack: main returns before the first frame runs.
global App app_state;
global u8 permanent_memory[NV_MEGABYTES(48)];
global u8 scratch_memory[NV_MEGABYTES(32)];
global u8 anim_memory[NV_MEGABYTES(48)];

internal NvClipId find_clip(App* app, const char* name)
{
    for (u32 i = 0; i < app->clip_count; ++i) {
        if (strcmp(app->clip_names[i], name) == 0)
            return app->clips[i];
    }
    return (NvClipId){0};
}

internal b32 is_loop(NvClipId clip)
{
    return strstr(nv_anim_clip_name(clip), "_Loop") != NULL;
}

internal NvMeshId create_ground_mesh(NvRenderer* renderer)
{
    f32 h = 6.0f; // half size in meters
    NvVertex vertices[4] = {
        {{-h, 0, -h}, {0, 1, 0}, {0, 0}},
        {{-h, 0, h}, {0, 1, 0}, {0, 1}},
        {{h, 0, h}, {0, 1, 0}, {1, 1}},
        {{h, 0, -h}, {0, 1, 0}, {1, 0}},
    };
    u32 indices[6] = {0, 1, 2, 0, 2, 3};
    NvMeshData data = {.vertices = vertices, .vertex_count = 4, .indices = indices, .index_count = 6};
    return nv_renderer_add_mesh(renderer, &data);
}

internal void build_scene(App* app)
{
    NvScene* scene = app->scene;
    NvNodeId none = {0};

    app->camera = nv_scene_add_node(scene, none, "camera");
    nv_scene_get(scene, app->camera)->camera = (NvCamera){
        .projection = NV_PROJECTION_PERSPECTIVE,
        .fov_y = 45.0f * NV_PI / 180.0f,
        .near_z = 0.05f,
        .far_z = 100.0f,
    };
    scene->active_camera = app->camera;

    NvNodeId sun = nv_scene_add_node(scene, none, "sun");
    NvNode* sun_node = nv_scene_get(scene, sun);
    sun_node->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), 0.5f),
                                     nv_quat_axis_angle(nv_vec3(1, 0, 0), -0.8f));
    sun_node->light = (NvLight){.type = NV_LIGHT_DIRECTIONAL, .color = nv_vec3(1.0f, 0.96f, 0.9f), .intensity = 1.1f};

    NvNodeId ground = nv_scene_add_node(scene, none, "ground");
    NvNode* ground_node = nv_scene_get(scene, ground);
    ground_node->mesh = create_ground_mesh(&app->renderer);
    ground_node->material = nv_renderer_add_material(&app->renderer, &(NvMaterialDesc){.base_color = {0.32f, 0.34f, 0.38f, 1.0f}});
}

internal void play(App* app, NvClipId clip)
{
    nv_anim_play(nv_anim_get(app->animator), clip, app->fade_seconds, is_loop(clip));
}

// Jump is three clips chained: take-off, a short loop in the air, landing, then back to the loop
// that was playing before.
internal void update_jump(App* app, f32 dt)
{
    NvAnimLayer* layer = &nv_anim_get(app->animator)->layers[0];
    b32 finished = layer->clip.index && layer->time >= nv_anim_clip_duration(layer->clip) - 0.001f;
    switch (app->jump) {
    case JUMP_START:
        if (finished) {
            nv_anim_play(nv_anim_get(app->animator), find_clip(app, "Jump_Loop"), 0.05f, 1);
            app->jump_air_time = 0.0f;
            app->jump = JUMP_AIR;
        }
        break;
    case JUMP_AIR:
        app->jump_air_time += dt;
        if (app->jump_air_time >= 0.35f) {
            nv_anim_play(nv_anim_get(app->animator), find_clip(app, "Jump_Land"), 0.05f, 0);
            app->jump = JUMP_LAND;
        }
        break;
    case JUMP_LAND:
        if (finished) {
            nv_anim_play(nv_anim_get(app->animator), app->jump_return, 0.2f, 1);
            app->jump = JUMP_NONE;
        }
        break;
    case JUMP_NONE:
        break;
    }
}

// Blends a second clip into the current one, time-synchronized so feet stay in step.
internal void update_blend(App* app)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    NvAnimLayer* base = &animator->layers[0];
    NvAnimLayer* blend = &animator->layers[2];
    NvClipId other = app->clips[app->blend_clip];
    b32 active = app->blend_weight > 0.0f && base->clip.index && other.index != base->clip.index &&
                 animator->fade_duration == 0.0f && app->jump == JUMP_NONE;
    if (!active) {
        *blend = (NvAnimLayer){0};
        if (animator->fade_duration == 0.0f)
            base->weight = 1.0f;
        return;
    }
    f32 base_duration = nv_anim_clip_duration(base->clip);
    f32 other_duration = nv_anim_clip_duration(other);
    *blend = (NvAnimLayer){
        .clip = other,
        .time = base->time / base_duration * other_duration,
        .speed = base->speed * other_duration / base_duration,
        .weight = app->blend_weight,
        .loop = 1,
    };
    base->weight = 1.0f - app->blend_weight;
}

internal void build_ui(App* app)
{
    ImGuiIO* io = igGetIO_Nil();
    NvAnimator* animator = nv_anim_get(app->animator);
    f32 width = 300.0f * app->imgui.ui_scale;
    if (width > io->DisplaySize.x - 20.0f)
        width = io->DisplaySize.x - 20.0f;
    igSetNextWindowPos((ImVec2_c){10.0f, 10.0f}, ImGuiCond_FirstUseEver, (ImVec2_c){0.0f, 0.0f});
    igSetNextWindowSize((ImVec2_c){width, 0.0f}, ImGuiCond_FirstUseEver);
    if (igBegin("Animation", NULL, 0)) {
        igText("%.0f FPS", io->Framerate);

        igSeparatorText("Clips");
        for (u32 i = 0; i < app->clip_count; ++i) {
            bool current = animator->layers[0].clip.index == app->clips[i].index;
            if (igSelectable_Bool(app->clip_names[i], current, 0, (ImVec2_c){0, 0})) {
                app->jump = JUMP_NONE;
                play(app, app->clips[i]);
            }
        }
        if (igButton("Jump", (ImVec2_c){-1.0f, 0.0f}) && app->jump == JUMP_NONE) {
            app->jump_return = animator->layers[0].clip;
            nv_anim_play(animator, find_clip(app, "Jump_Start"), 0.1f, 0);
            app->jump = JUMP_START;
        }

        igSeparatorText("Playback");
        igSliderFloat("Speed", &animator->layers[0].speed, 0.0f, 2.0f, "%.2fx", 0);
        igSliderFloat("Fade", &app->fade_seconds, 0.0f, 1.0f, "%.2f s", 0);
        igCombo_Str_arr("Blend", &app->blend_clip, app->clip_names, (int)app->clip_count, -1);
        igSliderFloat("Weight", &app->blend_weight, 0.0f, 1.0f, "%.2f", 0);

        igSeparatorText("Layers");
        f32 total = 0.0f;
        for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
            if (animator->layers[l].clip.index)
                total += animator->layers[l].weight;
        }
        for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
            NvAnimLayer* layer = &animator->layers[l];
            if (!layer->clip.index)
                continue;
            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%s %.2f", nv_anim_clip_name(layer->clip), layer->weight);
            igProgressBar(total > 0.0f ? layer->weight / total : 0.0f, (ImVec2_c){-1.0f, 0.0f}, overlay);
        }

        igSeparatorText("View");
        igCheckbox("Show bones", &app->show_bones);
        igSliderAngle("Camera", &app->camera_yaw, -180.0f, 180.0f, "%.0f deg", 0);
    }
    igEnd();
}

internal void update_camera(App* app)
{
    f32 distance = 3.6f;
    NvNode* camera = nv_scene_get(app->scene, app->camera);
    camera->position = nv_vec3(sinf(app->camera_yaw) * distance, 1.35f, cosf(app->camera_yaw) * distance);
    camera->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), app->camera_yaw),
                                   nv_quat_axis_angle(nv_vec3(1, 0, 0), -0.12f));
}

internal void draw_bones(App* app)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    const NvJointDesc* joints = nv_anim_joints(app->skeleton);
    NvMat4 root = nv_scene_get(app->scene, app->character.root)->world;
    for (u32 j = 0; j < animator->joint_count; ++j) {
        if (joints[j].parent < 0)
            continue;
        NvVec3 a = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[joints[j].parent]));
        NvVec3 b = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[j]));
        nv_renderer_debug_line(&app->renderer, a, b, nv_vec3(1.0f, 0.8f, 0.2f));
    }
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
    build_ui(app);

    update_jump(app, dt);
    update_blend(app);
    nv_anim_update(nv_anim_get(app->animator), dt);
    update_camera(app);
    nv_scene_update(app->scene);
    if (app->show_bones)
        draw_bones(app);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->gpu.device, NULL);
    nv_renderer_draw(&app->renderer, app->scene, nv_anim_skins(), encoder, target);
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
    nv_arena_init(&app->anim_memory, anim_memory, sizeof(anim_memory));
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

    nv_anim_init(&app->anim_memory);
    app->skeleton = nv_anim_create_skeleton(app->character.joints, app->character.joint_count, app->character.inverse_bind);
    app->clip_count = nv_gltf_load_clips("/assets/clips.glb", app->skeleton, NULL, &app->scratch, app->clips, MAX_CLIPS);
    for (u32 i = 0; i < app->clip_count; ++i)
        app->clip_names[i] = nv_anim_clip_name(app->clips[i]);
    app->root_motion_clip_count = nv_gltf_load_clips("/assets/clips_rm.glb", app->skeleton, "root", &app->scratch,
                                                     app->root_motion_clips, MAX_CLIPS);

    app->animator = nv_anim_create_animator(app->skeleton);
    for (u32 i = 0; i < app->character.mesh_node_count; ++i)
        nv_scene_get(app->scene, app->character.mesh_nodes[i])->animator = app->animator;

    app->fade_seconds = 0.3f;
    nv_anim_play(nv_anim_get(app->animator), find_clip(app, "Idle_Loop"), 0.0f, 1);
    app->last_time = nv_time_seconds();
    nv_window_run(&app->window, frame, app);
    return 0;
}
