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

    // Attachment: a sword held in the right hand.
    bool attach_sword;
    NvNodeId sword;
    NvMeshId sword_mesh;
    s32 hand_joint;
    NvMat4 sword_offset; // hand joint space -> sword space

    // Root motion: the character moves by what the clip's root joint does, turning as it goes.
    bool root_motion;
    f32 turn_rate; // radians per second

    // Aim IK: the head follows a target.
    bool look_at;
    NvNodeId target;
    NvMeshId target_mesh;
    s32 head_joint;
    f64 time;

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

internal NvClipId find_root_motion_clip(App* app, const char* name)
{
    for (u32 i = 0; i < app->root_motion_clip_count; ++i) {
        if (strcmp(nv_anim_clip_name(app->root_motion_clips[i]), name) == 0)
            return app->root_motion_clips[i];
    }
    return (NvClipId){0};
}

internal b32 is_loop(NvClipId clip)
{
    return strstr(nv_anim_clip_name(clip), "_Loop") != NULL;
}

// Appends an axis-aligned box to vertex and index arrays.
internal void append_box(NvVertex* vertices, u32* vertex_count, u32* indices, u32* index_count, NvVec3 center, NvVec3 half)
{
    // Each face: normal n and in-plane axes u, v with u x v = n (counter-clockwise from outside).
    local_persist const f32 faces[6][3][3] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    local_persist const f32 corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const f32 c[3] = {center.x, center.y, center.z};
    const f32 h[3] = {half.x, half.y, half.z};
    for (u32 face = 0; face < 6; ++face) {
        u32 base = *vertex_count;
        for (u32 corner = 0; corner < 4; ++corner) {
            NvVertex* vertex = &vertices[(*vertex_count)++];
            *vertex = (NvVertex){0};
            for (u32 axis = 0; axis < 3; ++axis) {
                f32 p = faces[face][0][axis] + corners[corner][0] * faces[face][1][axis] + corners[corner][1] * faces[face][2][axis];
                vertex->position[axis] = c[axis] + p * h[axis];
                vertex->normal[axis] = faces[face][0][axis];
            }
        }
        u32* out = &indices[*index_count];
        out[0] = base; out[1] = base + 1; out[2] = base + 2;
        out[3] = base; out[4] = base + 2; out[5] = base + 3;
        *index_count += 6;
    }
}

// A sword along +Y: the grip is centered on the origin so it sits in the fist.
internal NvMeshId create_sword_mesh(NvRenderer* renderer)
{
    NvVertex vertices[24 * 3];
    u32 indices[36 * 3];
    u32 vertex_count = 0;
    u32 index_count = 0;
    append_box(vertices, &vertex_count, indices, &index_count, nv_vec3(0, 0, 0), nv_vec3(0.018f, 0.09f, 0.018f));
    append_box(vertices, &vertex_count, indices, &index_count, nv_vec3(0, 0.1f, 0), nv_vec3(0.09f, 0.012f, 0.025f));
    append_box(vertices, &vertex_count, indices, &index_count, nv_vec3(0, 0.52f, 0), nv_vec3(0.025f, 0.41f, 0.006f));
    NvMeshData data = {.vertices = vertices, .vertex_count = vertex_count, .indices = indices, .index_count = index_count};
    return nv_renderer_add_mesh(renderer, &data);
}

internal NvMeshId create_ground_mesh(NvRenderer* renderer)
{
    f32 h = 12.0f; // half size in meters
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

// Plays `clip`, or its root-motion version while root motion is on.
internal void play(App* app, NvClipId clip)
{
    NvClipId moving = find_root_motion_clip(app, nv_anim_clip_name(clip));
    if (app->root_motion && moving.index)
        clip = moving;
    nv_anim_play(nv_anim_get(app->animator), clip, app->fade_seconds, is_loop(clip));
}

// The regular clip with the same name as `clip` (which may be a root-motion clip).
internal NvClipId regular_clip(App* app, NvClipId clip)
{
    NvClipId regular = find_clip(app, nv_anim_clip_name(clip));
    return regular.index ? regular : clip;
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
            nv_anim_play(nv_anim_get(app->animator), app->jump_return, 0.2f, is_loop(app->jump_return));
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

internal void clips_tab(App* app)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    for (u32 i = 0; i < app->clip_count; ++i) {
        bool current = animator->layers[0].clip.index && regular_clip(app, animator->layers[0].clip).index == app->clips[i].index;
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
}

internal void playback_tab(App* app)
{
    NvAnimator* animator = nv_anim_get(app->animator);
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
}

internal void extras_tab(App* app)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    igCheckbox("Sword in hand", &app->attach_sword);
    if (igCheckbox("Root motion", &app->root_motion) && app->jump == JUMP_NONE)
        play(app, regular_clip(app, animator->layers[0].clip));
    igBeginDisabled(!app->root_motion);
    NvVec3 position = nv_scene_get(app->scene, app->character.root)->position;
    igText("Position %.2f, %.2f m", position.x, position.z);
    igSliderFloat("Turn", &app->turn_rate, -1.5f, 1.5f, "%.2f rad/s", 0);
    if (igButton("Back to center", (ImVec2_c){-1.0f, 0.0f})) {
        NvNode* root = nv_scene_get(app->scene, app->character.root);
        root->position = nv_vec3(0, 0, 0);
        root->rotation = nv_quat_identity();
    }
    igEndDisabled();
    igCheckbox("Look at target", &app->look_at);
}

internal void view_tab(App* app)
{
    ImGuiIO* io = igGetIO_Nil();
    igText("%.0f FPS", io->Framerate);
    igCheckbox("Show bones", &app->show_bones);
    igSliderAngle("Camera", &app->camera_yaw, -180.0f, 180.0f, "%.0f deg", 0);
}

internal void build_ui(App* app, NvRect panel)
{
    // NOTE: The panel is short on phones, so each group of controls gets its own tab.
    if (nv_imgui_begin_panel("Animation", panel) && igBeginTabBar("tabs", 0)) {
        if (igBeginTabItem("Clips", NULL, 0)) {
            clips_tab(app);
            igEndTabItem();
        }
        if (igBeginTabItem("Playback", NULL, 0)) {
            playback_tab(app);
            igEndTabItem();
        }
        if (igBeginTabItem("Extras", NULL, 0)) {
            extras_tab(app);
            igEndTabItem();
        }
        if (igBeginTabItem("View", NULL, 0)) {
            view_tab(app);
            igEndTabItem();
        }
        igEndTabBar();
    }
    igEnd();
}

internal void update_camera(App* app)
{
    f32 distance = 3.6f;
    NvVec3 focus = nv_scene_get(app->scene, app->character.root)->position;
    NvNode* camera = nv_scene_get(app->scene, app->camera);
    camera->position = nv_vec3(focus.x + sinf(app->camera_yaw) * distance, 1.35f, focus.z + cosf(app->camera_yaw) * distance);
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

    // Aim IK: the head's forward axis (green) against the direction to the target (red).
    if (app->look_at) {
        NvMat4 head = nv_mat4_mul(root, animator->joint_model[app->head_joint]);
        NvVec3 f = animator->look_at.forward;
        NvVec3 origin = nv_mat4_translation(head);
        NvVec3 forward = nv_vec3(head.e[0] * f.x + head.e[4] * f.y + head.e[8] * f.z,
                                 head.e[1] * f.x + head.e[5] * f.y + head.e[9] * f.z,
                                 head.e[2] * f.x + head.e[6] * f.y + head.e[10] * f.z);
        nv_renderer_debug_line(&app->renderer, origin, nv_vec3_add(origin, nv_vec3_scale(nv_vec3_normalize(forward), 0.6f)), nv_vec3(0.2f, 1.0f, 0.3f));
        nv_renderer_debug_line(&app->renderer, origin, nv_scene_get(app->scene, app->target)->position, nv_vec3(1.0f, 0.2f, 0.2f));
    }
}

// Moves the character by the root motion the animator accumulated, turning it as it walks.
internal void apply_root_motion(App* app, f32 dt)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    NvNode* root = nv_scene_get(app->scene, app->character.root);
    if (app->root_motion) {
        root->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), app->turn_rate * dt), root->rotation);
        root->position = nv_vec3_add(root->position, nv_quat_rotate(root->rotation, animator->root_motion));
    }
    animator->root_motion = nv_vec3(0, 0, 0);
}

// The target sweeps in front of the character; the head's aim IK follows it.
internal void update_look_at(App* app)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    NvNode* target = nv_scene_get(app->scene, app->target);
    target->mesh = app->look_at ? app->target_mesh : (NvMeshId){0};
    animator->look_at.enabled = app->look_at;
    if (!app->look_at)
        return;

    f32 t = (f32)app->time;
    f32 angle = sinf(t * 0.7f) * 1.3f;
    NvVec3 local = nv_vec3(sinf(angle) * 1.2f, 1.75f + 0.3f * sinf(t * 1.3f), cosf(angle) * 1.2f);
    NvNode* root = nv_scene_get(app->scene, app->character.root);
    target->position = nv_vec3_add(root->position, nv_quat_rotate(root->rotation, local));
    target->rotation = nv_quat_axis_angle(nv_vec3(0, 1, 0), t * 2.0f);

    // IK works in the character's model space.
    animator->look_at.target = local;
    animator->look_at.weight = 1.0f;
}

// The sword follows the hand: hand joint (model space) * the offset found in the rest pose.
internal void update_sword(App* app)
{
    NvNode* sword = nv_scene_get(app->scene, app->sword);
    sword->mesh = app->attach_sword ? app->sword_mesh : (NvMeshId){0};
    if (!app->attach_sword)
        return;
    NvAnimator* animator = nv_anim_get(app->animator);
    NvMat4 local = nv_mat4_mul(animator->joint_model[app->hand_joint], app->sword_offset);
    nv_mat4_decompose(local, &sword->position, &sword->rotation, &sword->scale);
}

// Returns the joint-space axis (+-X, +-Y or +-Z) that points most along `direction` in model space.
internal NvVec3 joint_axis_towards(NvMat4 joint_model, NvVec3 direction)
{
    NvVec3 best = nv_vec3(1, 0, 0);
    f32 best_dot = -2.0f;
    for (u32 axis = 0; axis < 3; ++axis) {
        NvVec3 column = nv_vec3_normalize(nv_vec3(joint_model.e[axis * 4], joint_model.e[axis * 4 + 1], joint_model.e[axis * 4 + 2]));
        f32 d = nv_vec3_dot(column, direction);
        for (s32 sign = -1; sign <= 1; sign += 2) {
            if (d * (f32)sign > best_dot) {
                best_dot = d * (f32)sign;
                best = nv_vec3(axis == 0 ? (f32)sign : 0.0f, axis == 1 ? (f32)sign : 0.0f, axis == 2 ? (f32)sign : 0.0f);
            }
        }
    }
    return best;
}

// Sets up the sword, the look-at target and the IK axes from the rest pose.
internal void setup_extras(App* app)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    NvMaterialId steel = nv_renderer_add_material(&app->renderer, &(NvMaterialDesc){.base_color = {0.75f, 0.77f, 0.8f, 1.0f}, .double_sided = 1});
    NvMaterialId gold = nv_renderer_add_material(&app->renderer, &(NvMaterialDesc){.base_color = {1.0f, 0.75f, 0.2f, 1.0f}});
    app->sword_mesh = create_sword_mesh(&app->renderer);

    app->sword = nv_scene_add_node(app->scene, app->character.root, "sword");
    nv_scene_get(app->scene, app->sword)->material = steel;
    app->target = nv_scene_add_node(app->scene, (NvNodeId){0}, "look target");
    NvNode* target = nv_scene_get(app->scene, app->target);
    target->material = gold;
    NvVertex box_vertices[24];
    u32 box_indices[36];
    u32 box_vertex_count = 0;
    u32 box_index_count = 0;
    append_box(box_vertices, &box_vertex_count, box_indices, &box_index_count, nv_vec3(0, 0, 0), nv_vec3(0.06f, 0.06f, 0.06f));
    app->target_mesh = nv_renderer_add_mesh(&app->renderer, &(NvMeshData){.vertices = box_vertices, .vertex_count = box_vertex_count,
                                                                           .indices = box_indices, .index_count = box_index_count});

    // The character faces +Z with +Y up. In the rest (T) pose the right fist's grip runs along
    // +Z, so the sword there points +Z with its grip a little past the wrist, toward the fist.
    app->hand_joint = nv_anim_find_joint(app->skeleton, "hand_r");
    NV_ASSERT(app->hand_joint >= 0);
    NvMat4 hand = animator->joint_model[app->hand_joint];
    NvVec3 grip = nv_vec3_add(nv_mat4_translation(hand), nv_vec3(-0.08f, -0.02f, 0.0f));
    NvMat4 desired = nv_mat4_trs(grip, nv_quat_axis_angle(nv_vec3(1, 0, 0), NV_PI * 0.5f), nv_vec3(1, 1, 1));
    app->sword_offset = nv_mat4_mul(nv_mat4_inverse(hand), desired);

    app->head_joint = nv_anim_find_joint(app->skeleton, "Head");
    NV_ASSERT(app->head_joint >= 0);
    NvMat4 head = animator->joint_model[app->head_joint];
    animator->look_at = (NvLookAt){
        .joint = app->head_joint,
        .forward = joint_axis_towards(head, nv_vec3(0, 0, 1)),
        .up = joint_axis_towards(head, nv_vec3(0, 1, 0)),
    };
    app->turn_rate = 0.6f;
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

    NvEditorLayout layout = nv_editor_layout(&app->gpu, NV_EDITOR_VIEWPORT_FRACTION);
    nv_imgui_new_frame(&app->imgui, dt);
    build_ui(app, layout.panel);

    app->time = now;
    update_jump(app, dt);
    update_blend(app);
    update_look_at(app);
    nv_anim_update(nv_anim_get(app->animator), dt);
    apply_root_motion(app, dt);
    update_sword(app);
    update_camera(app);
    nv_scene_update(app->scene);
    if (app->show_bones)
        draw_bones(app);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->gpu.device, NULL);
    nv_renderer_draw(&app->renderer, app->scene, nv_anim_skins(), layout.viewport, encoder, target);
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

    setup_extras(app); // before any clip plays: it reads the rest pose
    app->fade_seconds = 0.3f;
    nv_anim_play(nv_anim_get(app->animator), find_clip(app, "Idle_Loop"), 0.0f, 1);
    app->last_time = nv_time_seconds();
    nv_window_run(&app->window, frame, app);
    return 0;
}
