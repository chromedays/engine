#include "app.h"

#include <cimguizmo.h>
#include <emscripten/emscripten.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// IMPORTANT: Global rather than on main's stack: main returns before the first frame runs.
global App app_state;
global u8 permanent_memory[NV_MEGABYTES(48)];
global u8 scratch_memory[NV_MEGABYTES(32)];
global u8 anim_memory[NV_MEGABYTES(48)];

//
// Clips
//

NvClipId app_find_clip(App* app, const char* name)
{
    for (u32 i = 0; i < app->clip_count; ++i) {
        if (strcmp(nv_anim_clip_name(app->clips[i]), name) == 0)
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

NvClipId app_regular_clip(App* app, NvClipId clip)
{
    NvClipId regular = app_find_clip(app, nv_anim_clip_name(clip));
    return regular.index ? regular : clip;
}

void app_play(App* app, NvClipId clip)
{
    // Root motion moves the node, which only happens while playing; Edit mode previews in place.
    NvClipId moving = find_root_motion_clip(app, nv_anim_clip_name(clip));
    if (app->root_motion && app->playing && moving.index)
        clip = moving;
    app->jump = JUMP_NONE;
    nv_anim_play(nv_anim_get(app->animator), clip, app->fade_seconds, is_loop(clip));
}

// Jump is three clips chained: take-off, a short loop in the air, landing, then back to the loop
// that was playing before.
void app_jump(App* app)
{
    if (app->jump != JUMP_NONE || !app->playing)
        return;
    NvAnimator* animator = nv_anim_get(app->animator);
    app->jump_return = animator->layers[0].clip;
    nv_anim_play(animator, app_find_clip(app, "Jump_Start"), 0.1f, 0);
    app->jump = JUMP_START;
}

internal void update_jump(App* app, f32 dt)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    NvAnimLayer* layer = &animator->layers[0];
    b32 finished = layer->clip.index && layer->time >= nv_anim_clip_duration(layer->clip) - 0.001f;
    switch (app->jump) {
    case JUMP_START:
        if (finished) {
            nv_anim_play(animator, app_find_clip(app, "Jump_Loop"), 0.05f, 1);
            app->jump_air_time = 0.0f;
            app->jump = JUMP_AIR;
        }
        break;
    case JUMP_AIR:
        app->jump_air_time += dt;
        if (app->jump_air_time >= 0.35f) {
            nv_anim_play(animator, app_find_clip(app, "Jump_Land"), 0.05f, 0);
            app->jump = JUMP_LAND;
        }
        break;
    case JUMP_LAND:
        if (finished) {
            nv_anim_play(animator, app->jump_return, 0.2f, is_loop(app->jump_return));
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

//
// Scene
//

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

NvMeshId app_box_mesh(App* app, NvVec3 half)
{
    NvVertex vertices[24];
    u32 indices[36];
    u32 vertex_count = 0;
    u32 index_count = 0;
    append_box(vertices, &vertex_count, indices, &index_count, nv_vec3(0, 0, 0), half);
    NvMeshData data = {.vertices = vertices, .vertex_count = vertex_count, .indices = indices, .index_count = index_count};
    return nv_renderer_add_mesh(&app->renderer, &data);
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
    f32 h = 30.0f; // half size in meters
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

internal NvMaterialId add_color(App* app, f32 r, f32 g, f32 b, b32 double_sided)
{
    NvMaterialDesc desc = {.base_color = {r, g, b, 1.0f}, .double_sided = double_sided};
    return nv_renderer_add_material(&app->renderer, &desc);
}

internal void build_world(App* app)
{
    NvScene* scene = app->scene;
    NvNodeId none = {0};

    NvNodeId camera = nv_scene_add_node(scene, none, "camera");
    nv_scene_get(scene, camera)->camera = (NvCamera){
        .projection = NV_PROJECTION_PERSPECTIVE,
        .fov_y = 45.0f * NV_PI / 180.0f,
        .near_z = 0.05f,
        .far_z = 100.0f,
    };
    scene->active_camera = camera;
    app->views[SCENE_SHOWCASE] = (SceneView){
        .scene = scene,
        .camera = camera,
        .camera_yaw = 0.35f,
        .camera_pitch = 0.12f,
        .camera_distance = 5.0f,
        .follow_selection = false,
        .orbit_point = {0.0f, 0.92f, 0.0f}, // the character's head height, where following would look
    };

    NvNodeId sun = nv_scene_add_node(scene, none, "sun");
    NvNode* sun_node = nv_scene_get(scene, sun);
    sun_node->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), 0.5f),
                                     nv_quat_axis_angle(nv_vec3(1, 0, 0), -0.8f));
    sun_node->light = (NvLight){.type = NV_LIGHT_DIRECTIONAL, .color = nv_vec3(1.0f, 0.96f, 0.9f), .intensity = 1.1f};

    NvNodeId ground = nv_scene_add_node(scene, none, "ground");
    NvNode* ground_node = nv_scene_get(scene, ground);
    ground_node->mesh = create_ground_mesh(&app->renderer);
    ground_node->material = add_color(app, 0.32f, 0.34f, 0.38f, 0);

    // The planet floats beside the character, far enough that the moon's orbit misses it.
    NvMeshId cube = app_box_mesh(app, nv_vec3(0.5f, 0.5f, 0.5f));
    app->planet = nv_scene_add_node(scene, none, "planet");
    NvNode* planet = nv_scene_get(scene, app->planet);
    planet->position = nv_vec3(-4.5f, 1.6f, -4.0f);
    planet->mesh = cube;
    planet->material = add_color(app, 0.95f, 0.55f, 0.25f, 0);

    app->moon = nv_scene_add_node(scene, app->planet, "moon");
    NvNode* moon = nv_scene_get(scene, app->moon);
    moon->position = nv_vec3(2.4f, 0.0f, 0.0f);
    moon->scale = nv_vec3(0.45f, 0.45f, 0.45f);
    moon->mesh = cube;
    moon->material = add_color(app, 0.55f, 0.70f, 0.95f, 0);
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

// Loads the character and its clips, and sets up the sword, the look-at target and the IK axes
// from the rest pose (so before any clip plays).
internal b32 build_character(App* app)
{
    if (!nv_gltf_load_model("/assets/character.glb", app->scene, &app->renderer, &app->permanent, &app->scratch, &app->character))
        return 0;
    NV_ASSERT(app->character.animator.index);
    NvNode* root = nv_scene_get(app->scene, app->character.root);
    snprintf(root->name, sizeof(root->name), "character");
    app->animator = app->character.animator;
    NvSkeletonId skeleton = app->character.skeleton;
    app->clip_count = nv_gltf_load_clips("/assets/clips.glb", skeleton, NULL, &app->scratch, app->clips, APP_MAX_CLIPS);
    app->root_motion_clip_count = nv_gltf_load_clips("/assets/clips_rm.glb", skeleton, "root", &app->scratch,
                                                     app->root_motion_clips, APP_MAX_CLIPS);
    NvAnimator* animator = nv_anim_get(app->animator);

    // The character faces +Z with +Y up. In the rest (T) pose the right fist's grip runs along
    // +Z, so the sword there points +Z with its grip a little past the wrist, toward the fist.
    s32 hand_joint = nv_anim_find_joint(skeleton, "hand_r");
    NV_ASSERT(hand_joint >= 0);
    NvMat4 hand = animator->joint_model[hand_joint];
    NvVec3 grip = nv_vec3_add(nv_mat4_translation(hand), nv_vec3(-0.08f, -0.02f, 0.0f));
    NvMat4 desired = nv_mat4_trs(grip, nv_quat_axis_angle(nv_vec3(1, 0, 0), NV_PI * 0.5f), nv_vec3(1, 1, 1));
    app->sword_mesh = create_sword_mesh(&app->renderer);
    app->sword = nv_scene_add_node(app->scene, app->character.root, "sword");
    NvNode* sword = nv_scene_get(app->scene, app->sword);
    sword->material = add_color(app, 0.75f, 0.77f, 0.8f, 1);
    sword->attach = (NvJointAttach){.animator = app->animator, .joint = (u32)hand_joint};
    nv_mat4_decompose(nv_mat4_mul(nv_mat4_inverse(hand), desired), &sword->position, &sword->rotation, &sword->scale);

    app->target_mesh = app_box_mesh(app, nv_vec3(0.06f, 0.06f, 0.06f));
    app->target = nv_scene_add_node(app->scene, (NvNodeId){0}, "look target");
    NvNode* target = nv_scene_get(app->scene, app->target);
    target->material = add_color(app, 1.0f, 0.75f, 0.2f, 0);
    // In Edit mode it stays where it is put, so it starts in front of the head.
    target->position = nv_vec3(0.0f, 1.75f, 1.2f);

    s32 head_joint = nv_anim_find_joint(skeleton, "Head");
    NV_ASSERT(head_joint >= 0);
    NvMat4 head = animator->joint_model[head_joint];
    animator->look_at = (NvLookAt){
        .joint = head_joint,
        .forward = joint_axis_towards(head, nv_vec3(0, 0, 1)),
        .up = joint_axis_towards(head, nv_vec3(0, 1, 0)),
        .weight = 1.0f,
        .target_node = app->target,
    };
    return 1;
}

NvAnimatorId app_node_animator(NvScene* scene, NvNodeId id)
{
    NvNode* node = nv_scene_get(scene, id);
    if (node->animator.index)
        return node->animator;
    for (u32 child = node->first_child; child; child = scene->nodes[child].next_sibling) {
        if (scene->nodes[child].animator.index)
            return scene->nodes[child].animator;
    }
    return (NvAnimatorId){0};
}

SceneView* app_view(App* app)
{
    return &app->views[app->shown];
}

void app_show_scene(App* app, SceneKind kind)
{
    if (kind == SCENE_STRESS && !app->stress.built)
        stress_build(app);
    if (kind == SCENE_STRESS && app->shown != SCENE_STRESS)
        app->open_stress = 1;
    app->shown = kind;
}

void app_back_to_center(App* app)
{
    NvNode* root = nv_scene_get(app->scene, app->character.root);
    root->position = nv_vec3(0, 0, 0);
    root->rotation = nv_quat_identity();
}

//
// Frame
//

// Turns the character while root motion walks it (nv_anim_update_scene moves it).
internal void apply_turn(App* app, f32 dt)
{
    NvNode* root = nv_scene_get(app->scene, app->character.root);
    if (app->root_motion)
        root->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), app->turn_rate * dt), root->rotation);
}

// The head's aim IK follows the target. While playing, the target sweeps in front of the
// character; in Edit mode it stays where it was put.
internal void update_look_target(App* app)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    NvNode* target = nv_scene_get(app->scene, app->target);
    target->mesh = app->look_at ? app->target_mesh : (NvMeshId){0};
    animator->look_at.enabled = app->look_at;
    if (!app->look_at || !app->playing)
        return;
    f32 t = app->play_time;
    f32 angle = sinf(t * 0.7f) * 1.3f;
    NvVec3 local = nv_vec3(sinf(angle) * 1.2f, 1.75f + 0.3f * sinf(t * 1.3f), cosf(angle) * 1.2f);
    NvNode* root = nv_scene_get(app->scene, app->character.root);
    target->position = nv_vec3_add(root->position, nv_quat_rotate(root->rotation, local));
    target->rotation = nv_quat_axis_angle(nv_vec3(0, 1, 0), t * 2.0f);
}

#define ORBIT_RADIANS_PER_PIXEL 0.008f

// Mouse and touch input on the viewport: drags orbit and pan, the wheel and pinches zoom.
internal void apply_view_input(App* app, SceneView* view, NvRect viewport)
{
    const NvViewInput* in = &app->imgui.view;
    view->camera_yaw -= in->orbit_x * ORBIT_RADIANS_PER_PIXEL;
    view->camera_pitch += in->orbit_y * ORBIT_RADIANS_PER_PIXEL;
    view->camera_pitch = view->camera_pitch < CAMERA_MIN_PITCH ? CAMERA_MIN_PITCH : view->camera_pitch;
    view->camera_pitch = view->camera_pitch > CAMERA_MAX_PITCH ? CAMERA_MAX_PITCH : view->camera_pitch;
    view->camera_distance *= expf(in->dolly);
    view->camera_distance = view->camera_distance < CAMERA_MIN_DISTANCE ? CAMERA_MIN_DISTANCE : view->camera_distance;
    view->camera_distance = view->camera_distance > CAMERA_MAX_DISTANCE ? CAMERA_MAX_DISTANCE : view->camera_distance;

    if (view->selected.index != view->panned_for.index || view->selected.gen != view->panned_for.gen) {
        view->pan = nv_vec3(0, 0, 0);
        view->panned_for = view->selected;
    }
    if (in->pan_x != 0.0f || in->pan_y != 0.0f) {
        // Move the orbit point so the scene follows the finger: one pixel is the height the view
        // covers at the orbit point, divided by the viewport's height in pixels.
        NvNode* camera = nv_scene_get(view->scene, view->camera);
        f32 pixel_ratio = app->window.pixel_ratio > 0.0f ? app->window.pixel_ratio : 1.0f;
        f32 height = (f32)viewport.height / pixel_ratio;
        f32 meters = 2.0f * view->camera_distance * tanf(camera->camera.fov_y * 0.5f) / (height > 1.0f ? height : 1.0f);
        NvVec3 right = nv_quat_rotate(camera->rotation, nv_vec3(1, 0, 0));
        NvVec3 up = nv_quat_rotate(camera->rotation, nv_vec3(0, 1, 0));
        NvVec3 move = nv_vec3_add(nv_vec3_scale(right, -in->pan_x * meters), nv_vec3_scale(up, in->pan_y * meters));
        if (view->follow_selection)
            view->pan = nv_vec3_add(view->pan, move);
        else
            view->orbit_point = nv_vec3_add(view->orbit_point, move);
    }
}

// A tap in the viewport selects the mesh under it, or clears the selection. A character's meshes
// stand for the character, so they select its root.
internal void pick(App* app, NvRect viewport)
{
    const NvViewInput* in = &app->imgui.view;
    if (!in->tapped)
        return;
    // A tap on the build label's badge opens the Console tab and leaves the selection alone.
    const f32* badge = app->badge_box;
    if (badge[2] > badge[0] && in->tap_x >= badge[0] && in->tap_x < badge[2] && in->tap_y >= badge[1] &&
        in->tap_y < badge[3]) {
        app->open_console = 1;
        return;
    }
    SceneView* view = app_view(app);
    f32 pixel_ratio = app->window.pixel_ratio > 0.0f ? app->window.pixel_ratio : 1.0f;
    NvRay ray = nv_renderer_view_ray(view->scene, viewport, in->tap_x * pixel_ratio, in->tap_y * pixel_ratio);
    NvNodeId hit = nv_renderer_pick(&app->renderer, view->scene, nv_anim_skins(), ray, NULL);
    if (hit.index) {
        NvNode* node = nv_scene_get(view->scene, hit);
        if (node->animator.index && node->parent)
            hit = (NvNodeId){node->parent, view->scene->nodes[node->parent].gen};
        app->open_inspector = 1;
    }
    view->selected = hit;
}

// The selected node's mesh boxes, and those of its mesh children (a character's meshes).
internal void draw_selection(App* app)
{
    SceneView* view = app_view(app);
    if (!view->selected.index)
        return;
    NvScene* scene = view->scene;
    NvNode* selected = nv_scene_get(scene, view->selected);
    u32 nodes[17];
    u32 count = 0;
    nodes[count++] = view->selected.index;
    if (!selected->mesh.index) {
        for (u32 child = selected->first_child; child && count < NV_ARRAY_COUNT(nodes); child = scene->nodes[child].next_sibling)
            nodes[count++] = child;
    }
    for (u32 n = 0; n < count; ++n) {
        NvNode* node = &scene->nodes[nodes[n]];
        if (!node->mesh.index)
            continue;
        const NvSkin* skin = node->animator.index ? &nv_anim_skins()[node->animator.index] : NULL;
        NvBox box = nv_renderer_mesh_bounds(&app->renderer, node->mesh, skin);
        if (nv_box_is_empty(box))
            continue;
        NvVec3 lo = box.min;
        NvVec3 hi = box.max;
        NvVec3 corners[8];
        for (u32 c = 0; c < 8; ++c)
            corners[c] = nv_mat4_transform_point(node->world, nv_vec3(c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z));
        for (u32 c = 0; c < 8; ++c) {
            for (u32 bit = 1; bit < 8; bit <<= 1) {
                if (!(c & bit))
                    nv_renderer_debug_line(&app->renderer, corners[c], corners[c | bit], nv_vec3(0.3f, 0.85f, 1.0f));
            }
        }
    }
}

// Where the camera orbits when it follows: the selected node, or the view's focus. Cameras and
// lights are not worth orbiting, so they fall back to the focus. A character's origin is at its
// feet, so its focus is raised.
internal NvVec3 view_focus_point(SceneView* view)
{
    NvScene* scene = view->scene;
    NvNodeId focus = view->selected;
    if (!focus.index || nv_scene_get(scene, focus)->camera.projection || nv_scene_get(scene, focus)->light.type)
        focus = view->focus;
    NvVec3 point = nv_vec3(0, 0, 0);
    if (focus.index) {
        point = nv_mat4_translation(nv_scene_get(scene, focus)->world);
        if (app_node_animator(scene, focus).index)
            point.y += 0.92f;
    }
    return point;
}

void app_focus_selection(App* app)
{
    SceneView* view = app_view(app);
    if (!view->selected.index)
        return;
    // Following, the orbit point is the selection plus the pan, so an empty pan is what centers it.
    view->pan = nv_vec3(0, 0, 0);
    view->orbit_point = view_focus_point(view);
}

internal void update_camera(App* app, NvRect viewport)
{
    SceneView* view = app_view(app);
    NvScene* scene = view->scene;
    apply_view_input(app, view, viewport);
    // Following, the camera orbits the selection (or the view's focus). Not following, it stays
    // where it was and only moves by panning, whatever gets selected.
    NvVec3 point = view->orbit_point;
    // NOTE: While the gizmo drags the selection, the camera holds still: following it would move
    // the pointer's ray with the node, and the drag would run away.
    if (view->follow_selection && !ImGuizmo_IsUsingAny()) {
        point = nv_vec3_add(view_focus_point(view), view->pan);
        view->orbit_point = point;
    }

    f32 d = view->camera_distance;
    f32 cp = cosf(view->camera_pitch);
    NvNode* camera = nv_scene_get(scene, view->camera);
    camera->position = nv_vec3_add(point, nv_vec3(sinf(view->camera_yaw) * cp * d, sinf(view->camera_pitch) * d, cosf(view->camera_yaw) * cp * d));
    camera->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), view->camera_yaw),
                                   nv_quat_axis_angle(nv_vec3(1, 0, 0), -view->camera_pitch));
    // The camera is a top-level node and nv_scene_update already ran this frame.
    camera->world = nv_mat4_trs(camera->position, camera->rotation, camera->scale);
}

internal OPERATION gizmo_imguizmo_operation(App* app)
{
    switch (app->gizmo_operation) {
    case GIZMO_MOVE: return TRANSLATE;
    case GIZMO_ROTATE: return ROTATE_X | ROTATE_Y | ROTATE_Z; // no screen-space ring
    case GIZMO_SCALE: return SCALE;
    }
    NV_INVALID_CODE_PATH;
    return TRANSLATE;
}

// NvImgui.view_grab: a press on a gizmo handle drags the gizmo instead of the camera.
internal b32 gizmo_grab(void* data)
{
    App* app = data;
    return app->gizmo_shown && ImGuizmo_IsOver_OPERATION(gizmo_imguizmo_operation(app));
}

// The transform gizmo on the selected node. ImGuizmo edits the world matrix; the result goes back
// to the node's position, rotation and scale, which nv_scene_update turns into `world` next frame.
internal void draw_gizmo(App* app, NvRect viewport)
{
    SceneView* view = app_view(app);
    NvScene* scene = view->scene;
    app->gizmo_shown = 0;
    // The active camera is the orbit camera's to move.
    if (!view->selected.index || view->selected.index == scene->active_camera.index)
        return;
    NvNode* node = nv_scene_get(scene, view->selected);

    NvMat4 view_matrix, projection;
    nv_renderer_camera_matrices(scene, viewport, &view_matrix, &projection);
    // ImGui works in CSS pixels; the viewport is in framebuffer pixels.
    f32 ratio = igGetIO_Nil()->DisplayFramebufferScale.x;
    ImGuizmo_SetRect((f32)viewport.x / ratio, (f32)viewport.y / ratio, (f32)viewport.width / ratio, (f32)viewport.height / ratio);
    ImGuizmo_SetOrthographic(nv_scene_get(scene, scene->active_camera)->camera.projection == NV_PROJECTION_ORTHOGRAPHIC);
    // NOTE: ImGuizmo sizes the gizmo as a fraction of the viewport width, which leaves it tiny on a
    // phone. Its handles are hit within fixed pixel distances, so it is sized in pixels instead:
    // about 64 CSS pixels long, larger for fingers.
    f32 length = 64.0f * app->imgui.ui_scale;
    ImGuizmo_SetGizmoSizeClipSpace(length / ((f32)viewport.width / ratio * 0.5f));
    app->gizmo_shown = 1;

    // W, E and R pick the operation while the pointer is over the viewport, as in most editors.
    ImGuiIO* io = igGetIO_Nil();
    f32 px = io->MousePos.x * ratio;
    f32 py = io->MousePos.y * ratio;
    b32 pointer_in_view = px >= (f32)viewport.x && py >= (f32)viewport.y && px < (f32)(viewport.x + viewport.width) &&
                          py < (f32)(viewport.y + viewport.height);
    if (pointer_in_view && !io->WantTextInput && !ImGuizmo_IsUsingAny()) {
        if (igIsKeyPressed_Bool(ImGuiKey_W, false))
            app->gizmo_operation = GIZMO_MOVE;
        if (igIsKeyPressed_Bool(ImGuiKey_E, false))
            app->gizmo_operation = GIZMO_ROTATE;
        if (igIsKeyPressed_Bool(ImGuiKey_R, false))
            app->gizmo_operation = GIZMO_SCALE;
    }

    // Snap steps: half a meter, 15 degrees, a tenth of the scale.
    f32 snap[3] = {0.5f, 0.5f, 0.5f};
    if (app->gizmo_operation == GIZMO_ROTATE)
        snap[0] = 15.0f;
    else if (app->gizmo_operation == GIZMO_SCALE)
        snap[0] = 0.1f;

    NvMat4 world = node->world;
    MODE mode = (app->gizmo_local || app->gizmo_operation == GIZMO_SCALE) ? LOCAL : WORLD;
    if (!ImGuizmo_Manipulate(view_matrix.e, projection.e, gizmo_imguizmo_operation(app), mode, world.e, NULL,
                             app->gizmo_snap ? snap : NULL, NULL, NULL))
        return;

    // world = parent world * joint (for attached nodes) * local, as in nv_scene_update.
    NvMat4 parent = node->parent ? scene->nodes[node->parent].world : nv_mat4_identity();
    if (node->attach.animator.index)
        parent = nv_mat4_mul(parent, node->attach.joint_model);
    NvMat4 local = nv_mat4_mul(nv_mat4_inverse(parent), world);
    NvVec3 position, scale;
    NvQuat rotation;
    // A scale dragged to (almost) nothing cannot be split back into a rotation.
    f32 smallest = 1.0e-4f;
    for (u32 column = 0; column < 3; ++column) {
        const f32* c = &local.e[column * 4];
        if (c[0] * c[0] + c[1] * c[1] + c[2] * c[2] < smallest * smallest)
            return;
    }
    nv_mat4_decompose(local, &position, &rotation, &scale);
    node->position = position;
    node->rotation = rotation;
    node->scale = scale;
}

internal void draw_bones(App* app)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    const NvJointDesc* joints = nv_anim_joints(animator->skeleton);
    NvMat4 root = nv_scene_get(app->scene, animator->owner)->world;
    for (u32 j = 0; j < animator->joint_count; ++j) {
        if (joints[j].parent < 0)
            continue;
        NvVec3 a = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[joints[j].parent]));
        NvVec3 b = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[j]));
        nv_renderer_debug_line(&app->renderer, a, b, nv_vec3(1.0f, 0.8f, 0.2f));
    }

    // Aim IK: the head's forward axis (green) against the direction to the target (red).
    NvLookAt* look = &animator->look_at;
    if (look->enabled) {
        NvMat4 head = nv_mat4_mul(root, animator->joint_model[look->joint]);
        NvVec3 f = look->forward;
        NvVec3 origin = nv_mat4_translation(head);
        NvVec3 forward = nv_vec3(head.e[0] * f.x + head.e[4] * f.y + head.e[8] * f.z,
                                 head.e[1] * f.x + head.e[5] * f.y + head.e[9] * f.z,
                                 head.e[2] * f.x + head.e[6] * f.y + head.e[10] * f.z);
        nv_renderer_debug_line(&app->renderer, origin, nv_vec3_add(origin, nv_vec3_scale(nv_vec3_normalize(forward), 0.6f)), nv_vec3(0.2f, 1.0f, 0.3f));
        nv_renderer_debug_line(&app->renderer, origin, nv_mat4_translation(nv_scene_get(app->scene, app->target)->world), nv_vec3(1.0f, 0.2f, 0.2f));
    }
}

internal f64 now_ms(void)
{
    return nv_time_seconds() * 1000.0;
}

f64 app_load(const FrameTimes* t)
{
    f64 cpu = t->anim + t->scene + t->draw + t->ui;
    f64 gpu = t->gpu + t->gpu_shadow;
    f64 busy = cpu > gpu ? cpu : gpu;
    return t->frame > 0.0 ? busy / t->frame * 100.0 : 0.0;
}

// Averages the frame times over windows of about a second, for the stats and the benchmark.
internal void accumulate_times(App* app, f64 now)
{
    FrameTimes* t = &app->times;
    FrameTimes* sum = &app->window_sum;
    sum->frame += t->frame;
    sum->anim += t->anim;
    sum->scene += t->scene;
    sum->draw += t->draw;
    sum->ui += t->ui;
    sum->gpu += t->gpu;
    sum->gpu_shadow += t->gpu_shadow;
    if (t->frame > app->window_worst_frame)
        app->window_worst_frame = t->frame;
    ++app->window_frames;
    if (now - app->window_start < 1.0)
        return;
    f64 n = (f64)app->window_frames;
    app->shown_average = (FrameTimes){sum->frame / n, sum->anim / n, sum->scene / n, sum->draw / n, sum->ui / n, sum->gpu / n,
                                      sum->gpu_shadow / n};
    app->shown_worst_frame = app->window_worst_frame;
    *sum = (FrameTimes){0};
    app->window_worst_frame = 0.0;
    app->window_frames = 0;
    app->window_start = now;
}

// What always runs is the animation preview (with its crossfades and blend); what moves the scene
// runs only while playing.
internal void update_showcase(App* app, f32 dt)
{
    if (app->playing) {
        app->play_time += dt;
        app->orbit_angle += app->orbit_speed * dt;
        // The spins turn on top of the rotations authored in Edit mode.
        nv_scene_get(app->scene, app->planet)->rotation =
            nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), app->orbit_angle), app->planet_rotation);
        nv_scene_get(app->scene, app->moon)->rotation =
            nv_quat_mul(nv_quat_axis_angle(nv_vec3_normalize(nv_vec3(1, 1, 0)), app->play_time * 2.0f), app->moon_rotation);
        update_jump(app, dt);
        apply_turn(app, dt);
    }
    update_blend(app);
    update_look_target(app);
    nv_scene_get(app->scene, app->sword)->mesh = app->show_sword ? app->sword_mesh : (NvMeshId){0};
}

// Restarts the character's clip from its start with no crossfade, as a run begins and ends. With
// `playing` set, root motion picks the clip's moving copy.
internal void restart_clip(App* app)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    NvClipId clip = app->jump != JUMP_NONE ? app->jump_return : animator->layers[0].clip;
    clip = app_regular_clip(app, clip);
    f32 fade = app->fade_seconds;
    app->fade_seconds = 0.0f;
    animator->layers[0].clip = (NvClipId){0}; // so the same clip restarts too
    app_play(app, clip);
    app->fade_seconds = fade;
    animator->layers[0].time = 0.0f;
}

void app_start_playing(App* app)
{
    if (app->playing)
        return;
    app->play_snapshot_size = save_write(app, app->play_snapshot, SAVE_MAX_SIZE);
    NV_ASSERT(app->play_snapshot_size);
    app->planet_rotation = nv_scene_get(app->scene, app->planet)->rotation;
    app->moon_rotation = nv_scene_get(app->scene, app->moon)->rotation;
    app->play_time = 0.0f;
    app->orbit_angle = 0.0f;
    app->playing = 1;
    restart_clip(app);
}

void app_stop_playing(App* app)
{
    if (!app->playing)
        return;
    app->playing = 0;
    // The scene as it was at Play: nodes, character settings, scene settings. The view, the
    // selection and the editor settings stay as they are now.
    const char* problem = save_load_parts(app, app->play_snapshot, app->play_snapshot_size, SAVE_PART_SCENE);
    NV_ASSERT(!problem); // written by save_write at Play
    (void)problem;
    app->jump = JUMP_NONE;
    app->orbit_angle = 0.0f;
    restart_clip(app);
}

internal void frame(void* userdata)
{
    App* app = userdata;
    nv_log_pump();
    f64 now = nv_time_seconds();
    f32 dt = (f32)(now - app->last_time);
    app->last_time = now;
    app->time = now;

    WGPUTextureView target = nv_gpu_begin_frame(&app->gpu);
    if (!target)
        return;

    FrameTimes* times = &app->times;
    times->frame = (f64)dt * 1000.0;
    f64 t = now_ms();
    app_layout(app);
    const Layout* layout = &app->layout;
    app->imgui.view_rect = layout->viewport;
    NvRect* logged = &app->logged_viewport;
    if (logged->width != layout->viewport.width || logged->height != layout->viewport.height ||
        logged->x != layout->viewport.x || logged->y != layout->viewport.y) {
        *logged = layout->viewport;
        nv_log(NV_LOG_INFO, "app", "%s UI: scene viewport %ux%u at (%u, %u) of the %ux%u color target",
               app->ui_mode == UI_PHONE ? "phone" : "desktop", layout->viewport.width, layout->viewport.height,
               layout->viewport.x, layout->viewport.y, app->gpu.width, app->gpu.height);
    }
    nv_imgui_new_frame(&app->imgui, dt);
    // IMPORTANT: Before the panel, so ImGuizmo's full-screen window (created on the first frame)
    // stays behind the panel.
    ImGuizmo_BeginFrame();
    app_build_ui(app);
    times->ui = now_ms() - t;

    NvScene* scene = app_view(app)->scene;
    if (app->shown == SCENE_SHOWCASE)
        update_showcase(app, dt);
    else
        stress_update(app, dt);

    t = now_ms();
    nv_anim_update_scene(scene, dt);
    times->anim = now_ms() - t;
    t = now_ms();
    nv_scene_update(scene);
    times->scene = now_ms() - t;
    pick(app, layout->viewport);
    update_camera(app, layout->viewport);
    draw_gizmo(app, layout->viewport);
    undo_update(app);
    draw_selection(app);
    if (app->shown == SCENE_SHOWCASE && app->show_bones)
        draw_bones(app);
    if (app->shown == SCENE_STRESS && app->stress.want.show_bones)
        stress_draw_bones(app);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->gpu.device, NULL);
    t = now_ms();
    // The depth target is samplable while the Textures tab shows it (docs/specs/textures.md).
    app->renderer.depth_sampled = app->textures.shown_now;
    nv_renderer_draw(&app->renderer, scene, nv_anim_skins(), layout->viewport, encoder, target);
    times->draw = now_ms() - t;
    times->gpu = app->renderer.gpu_ms;
    times->gpu_shadow = app->renderer.shadows.size ? app->renderer.gpu_shadow_ms : 0.0;
    t = now_ms();
    nv_imgui_render(&app->imgui, encoder, target);
    times->ui += now_ms() - t;
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(app->gpu.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    nv_renderer_end_frame(&app->renderer);
    nv_gpu_end_frame(&app->gpu);

    accumulate_times(app, now);
    stress_after_frame(app);
    save_update(app);
}

#if !defined(NDEBUG)
// For tests: Module._app_debug_layout(region, component) is a region's rectangle in CSS pixels
// (region 0 viewport, 1 top bar, 2 left dock, 3 right dock, 4 bottom dock, 5 phone panel, 6 the
// Play button; component 0 x, 1 y, 2 width, 3 height). Module._app_debug_ui_mode() is 0 for the
// desktop UI and 1 for the phone UI, and Module._app_debug_dock(n) the wanted sizes and state
// (0 left width, 1 right width, 2 bottom height, 3 bottom open).
EMSCRIPTEN_KEEPALIVE float app_debug_layout(int region, int component)
{
    App* app = &app_state;
    f32 ratio = app->window.pixel_ratio > 0.0f ? app->window.pixel_ratio : 1.0f;
    const Layout* layout = &app->layout;
    const NvRect* rects[] = {&layout->viewport, &layout->top_bar, &layout->left, &layout->right, &layout->bottom, &layout->panel};
    if (region == 6) {
        const f32* box = app->play_box;
        f32 values[4] = {box[0], box[1], box[2] - box[0], box[3] - box[1]};
        return values[component];
    }
    const NvRect* r = rects[region];
    f32 values[4] = {(f32)r->x / ratio, (f32)r->y / ratio, (f32)r->width / ratio, (f32)r->height / ratio};
    return values[component];
}

EMSCRIPTEN_KEEPALIVE int app_debug_playing(void)
{
    return app_state.playing;
}

// The shown view: 0 yaw, 1 pitch, 2 distance, 3 to 5 orbit point x, y, z.
EMSCRIPTEN_KEEPALIVE float app_debug_view(int which)
{
    const SceneView* view = app_view(&app_state);
    f32 values[6] = {view->camera_yaw, view->camera_pitch, view->camera_distance, view->orbit_point.x, view->orbit_point.y, view->orbit_point.z};
    return values[which];
}

EMSCRIPTEN_KEEPALIVE int app_debug_ui_mode(void)
{
    return (int)app_state.ui_mode;
}

EMSCRIPTEN_KEEPALIVE float app_debug_dock(int which)
{
    const Docks* docks = &app_state.docks;
    f32 values[4] = {docks->left_width, docks->right_width, docks->bottom_height, (f32)docks->bottom_open};
    return values[which];
}

// For tests: Module._app_debug_save_round_trip() checks the save round trip at any moment.
EMSCRIPTEN_KEEPALIVE int app_debug_save_round_trip(void)
{
    return save_round_trip_matches(&app_state);
}

// For tests: Module._app_debug_undo_steps() is the steps held, and _app_debug_undo_done() how many
// of them are applied.
EMSCRIPTEN_KEEPALIVE int app_debug_undo_steps(void)
{
    return (int)app_state.undo.count;
}

EMSCRIPTEN_KEEPALIVE int app_debug_undo_done(void)
{
    return (int)app_state.undo.done;
}

// For tests, the Console tab (docs/specs/console.md). Module._app_debug_log(level, n) adds an "app"
// message "test message n", and _app_debug_log_lines(level, n) one of three lines; strings from the page
// go through Module.nvLog. _app_debug_wgpu_error() makes WebGPU report a validation error.
EMSCRIPTEN_KEEPALIVE void app_debug_log(int level, int number)
{
    nv_log((NvLogLevel)level, "app", "test message %d", number);
}

EMSCRIPTEN_KEEPALIVE void app_debug_log_lines(int level, int number)
{
    nv_log((NvLogLevel)level, "app", "first line %d\nsecond line\nthird line", number);
}

EMSCRIPTEN_KEEPALIVE void app_debug_wgpu_error(void)
{
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = 16; // no usage: not a valid buffer
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(app_state.gpu.device, &descriptor);
    if (buffer)
        wgpuBufferRelease(buffer);
}

EMSCRIPTEN_KEEPALIVE int app_debug_log_count(void)
{
    return (int)nv_log_ring.count;
}

// The i-th message held (0 = the oldest): its repeat count, and its level.
EMSCRIPTEN_KEEPALIVE int app_debug_log_repeat(int i)
{
    return (int)nv_log_message((u32)i)->repeat;
}

EMSCRIPTEN_KEEPALIVE int app_debug_log_level(int i)
{
    return (int)nv_log_message((u32)i)->level;
}

// The build label's tap box (component 0..3 is x0, y0, x1, y1), all zero when it has no badge.
EMSCRIPTEN_KEEPALIVE float app_debug_badge_box(int component)
{
    return app_state.badge_box[component];
}

// The average CPU time of building and recording the UI, in milliseconds.
EMSCRIPTEN_KEEPALIVE float app_debug_ui_ms(void)
{
    return (float)app_state.shown_average.ui;
}

// The shown scene's camera yaw, in radians.
EMSCRIPTEN_KEEPALIVE float app_debug_camera_yaw(void)
{
    return app_view(&app_state)->camera_yaw;
}

// Warnings and errors not yet seen (0 while the Console tab is shown).
EMSCRIPTEN_KEEPALIVE int app_debug_console_unseen(void)
{
    return (int)console_unseen(&app_state, NULL);
}

// Rows the Console tab's filters let through.
EMSCRIPTEN_KEEPALIVE int app_debug_console_rows(void)
{
    return (int)app_state.console.rows;
}

// Where an item of the Console tab was drawn last frame, in CSS pixels: id is a ConsoleRect,
// component 0..3 is x0, y0, x1, y1.
EMSCRIPTEN_KEEPALIVE float app_debug_console_rect(int id, int component)
{
    return app_state.console.rects[id][component];
}

// The message list's scroll (0) and the end of its range (1).
EMSCRIPTEN_KEEPALIVE float app_debug_console_scroll(int which)
{
    return which ? app_state.console.scroll_max : app_state.console.scroll_y;
}

// The number of the selected message + 1; 0 = none.
EMSCRIPTEN_KEEPALIVE int app_debug_console_selected(void)
{
    return (int)app_state.console.selected;
}

// For tests, the Textures tab (docs/specs/textures.md): where an item was drawn last frame (id is a
// TexturesRect, component 0..3 is x0, y0, x1, y1, CSS pixels), the rows listed, the pick
// (kind * 1000 + index, -1 = none), the preview slots in use, and whether the depth target is samplable.
EMSCRIPTEN_KEEPALIVE float app_debug_textures_rect(int id, int component)
{
    return app_state.textures.rects[id][component];
}

// Where the k-th listed row was drawn last frame (component 0..3); zero when it was not.
EMSCRIPTEN_KEEPALIVE float app_debug_textures_row(int k, int component)
{
    return app_state.textures.row_rects[k][component];
}

EMSCRIPTEN_KEEPALIVE int app_debug_textures_listed(void)
{
    return (int)app_state.textures.listed;
}

EMSCRIPTEN_KEEPALIVE int app_debug_textures_selected(void)
{
    TextureViewer* viewer = &app_state.textures;
    return viewer->has_selection ? (int)viewer->selected_kind * 1000 + (int)viewer->selected_index : -1;
}

EMSCRIPTEN_KEEPALIVE int app_debug_textures_mip(void)
{
    return (int)app_state.textures.mip;
}

EMSCRIPTEN_KEEPALIVE int app_debug_preview_slots(void)
{
    return (int)nv_imgui_preview_count(&app_state.imgui);
}

EMSCRIPTEN_KEEPALIVE int app_debug_depth_sampled(void)
{
    return app_state.renderer.depth_texture_sampled;
}

// Changes the shadow settings (size 0 = off; format 0 depth32float, 1 depth16unorm), as the View tab does.
EMSCRIPTEN_KEEPALIVE void app_debug_set_shadows(int size, int format)
{
    app_state.renderer.shadows.size = (u32)size;
    app_state.renderer.shadows.format = (NvShadowFormat)format;
}

// Shows a scene (0 showcase, 1 stress), as the View tab's picker does.
EMSCRIPTEN_KEEPALIVE void app_debug_show_scene(int kind)
{
    app_show_scene(&app_state, (SceneKind)kind);
}

// Renderer textures: how many, and the i-th's width, height and mip count (component 0, 1, 2).
EMSCRIPTEN_KEEPALIVE int app_debug_texture_count(void)
{
    return (int)app_state.renderer.texture_count;
}

EMSCRIPTEN_KEEPALIVE int app_debug_texture_info(int i, int component)
{
    WGPUTexture texture = app_state.renderer.textures[i].texture;
    return component == 0 ? (int)wgpuTextureGetWidth(texture)
         : component == 1 ? (int)wgpuTextureGetHeight(texture)
                          : (int)wgpuTextureGetMipLevelCount(texture);
}

// The k-th byte of the i-th renderer texture's name; 0 past its end.
EMSCRIPTEN_KEEPALIVE int app_debug_texture_name(int i, int k)
{
    const char* name = app_state.renderer.textures[i].name;
    return k < (int)strlen(name) ? (u8)name[k] : 0;
}

// The selected node's index in the shown scene; 0 = none.
EMSCRIPTEN_KEEPALIVE int app_debug_selected_node(void)
{
    return (int)app_view(&app_state)->selected.index;
}

// For tests: Module._app_debug_save_crc() is a CRC-32 of the save the state would write now, so a
// test can compare the state at two moments (before Play and after Stop).
EMSCRIPTEN_KEEPALIVE unsigned app_debug_save_crc(void)
{
    App* app = &app_state;
    umm mark = app->scratch.used;
    u8* bytes = NV_PUSH_ARRAY(&app->scratch, SAVE_MAX_SIZE, u8);
    u32 size = save_write(app, bytes, SAVE_MAX_SIZE);
    u32 crc = nv_crc32(bytes, size);
    app->scratch.used = mark;
    return crc;
}
#endif

int main(void)
{
    App* app = &app_state;
    nv_arena_init(&app->permanent, permanent_memory, sizeof(permanent_memory));
    nv_arena_init(&app->scratch, scratch_memory, sizeof(scratch_memory));
    nv_arena_init(&app->anim_memory, anim_memory, sizeof(anim_memory));
    app->scene = NV_PUSH_STRUCT(&app->permanent, NvScene);

    nv_window_create(&app->window, "nv");
    if (!nv_gpu_create(&app->gpu, &app->window)) {
        nv_log(NV_LOG_ERROR, "app", "fatal: failed to initialize WebGPU");
        return 1;
    }
    nv_renderer_init(&app->renderer, &app->gpu, &app->permanent);
    nv_imgui_init(&app->imgui, &app->gpu, &app->window, &app->permanent);
    // Shadows (docs/specs/shadows.md): lighter on touch screens, where the GPU is the limit.
    b32 touch = app->imgui.ui_scale > 1.0f;
    // One UI per device (docs/specs/layout.md): touch gets the phone UI, everything else the desktop's.
    app->ui_mode = touch ? UI_PHONE : UI_DESKTOP;
    app->docks = (Docks){
        .left_width = 260.0f,
        .right_width = 340.0f,
        .bottom_height = 220.0f,
        .bottom_open = 1,
        .show_left = 1,
        .show_right = 1,
        .show_bottom = 1,
    };
    app->renderer.shadows = (NvShadowSettings){
        .size = touch ? 1024 : 2048,
        .format = NV_SHADOW_FORMAT_DEPTH32F,
        .filter = touch ? NV_SHADOW_FILTER_LOW : NV_SHADOW_FILTER_HIGH,
        .distance = 30.0f,
    };
    app->imgui.view_grab = gizmo_grab;
    app->imgui.view_grab_data = app;
    // Thicker gizmo lines on touch screens, like the rest of the UI.
    Style* gizmo_style = ImGuizmo_GetStyle();
    f32 ui_scale = app->imgui.ui_scale;
    gizmo_style->TranslationLineThickness *= ui_scale;
    gizmo_style->TranslationLineArrowSize *= ui_scale;
    gizmo_style->RotationLineThickness *= ui_scale;
    gizmo_style->RotationOuterLineThickness *= ui_scale;
    gizmo_style->ScaleLineThickness *= ui_scale;
    gizmo_style->ScaleLineCircleSize *= ui_scale;
    gizmo_style->HatchedAxisLineThickness *= ui_scale;
    gizmo_style->CenterCircleSize *= ui_scale;
    nv_anim_init(&app->anim_memory);

    build_world(app);
    if (!build_character(app)) {
        nv_log(NV_LOG_ERROR, "app", "fatal: failed to load the character");
        return 1;
    }

    app->orbit_speed = 0.7f;
    app->fade_seconds = 0.3f;
    app->turn_rate = 0.6f;
    app->show_sword = true;
    app->views[SCENE_SHOWCASE].selected = app->character.root;
    app->views[SCENE_SHOWCASE].focus = app->character.root;
    app->autosave = true;
    app->console.auto_scroll = true;
    app->play_snapshot = NV_PUSH_ARRAY(&app->permanent, SAVE_MAX_SIZE, u8);
    // Taken before anything can rename or move nodes: saves apply to nodes by their place in this
    // tree, and only while it is the same tree.
    app->scene_layout = save_scene_layout(app->scene);
    app->window_start = nv_time_seconds();
    app_show_scene(app, SCENE_SHOWCASE);
    app_play(app, app_find_clip(app, "Idle_Loop"));
    NV_ASSERT(save_round_trip_matches(app));
    save_init(app);
    undo_init(app);
    app->last_time = nv_time_seconds();
    nv_window_run(&app->window, frame, app);
    return 0;
}
