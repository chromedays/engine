// The stress sample's scene and workloads (docs/specs/samples.md, docs/specs/stress.md): what loads the engine with many objects, so
// the frame time can be measured as they grow. A grid of cubes, a deep chain, a crowd of animated characters and fire effects, set
// by the benchmark's steps, the fixed table at the end of this file.

#include "stress.h"

#include <engine/mesh.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RGBA(r, g, b, a) {r, g, b, a}

// The scene's fixed values (they were sandbox/data/stress.nvscene's).
#define GRID_COLUMNS 128
#define GRID_SPACING 0.9f
#define GRID_HEIGHT 0.25f
#define GRID_SCALE 0.5f
#define CHAIN_STRIDE 7 // palette colors between one link and the next
#define CHAIN_TWIST 0.25f  // radians a link turns from the last
#define CHAIN_WOBBLE 0.04f // radians the twist swings by
#define CROWD_COLUMNS 20
#define CROWD_SPACING 1.6f
#define CROWD_OFFSET 0.37f // seconds between one character's clip time and the next
#define EFFECTS_AREA_X 16.0f // half the field the effects fall in, meters
#define EFFECTS_AREA_Z 12.0f
#define CHARACTER_FILE "/assets/quaternius/character.glb"
#define CLIPS_FILE "/assets/quaternius/clips.glb"


//
// Effects: what the workload fires. Colors are linear and may exceed 1, which is what bloom spreads.
//

// Embers thrown up and falling back, fired in bursts to hold a number of live particles. Small, additive, bouncing on the ground.
internal NvVfxEffectId make_swarm(NvVfx* vfx)
{
    NvVfxEffectDesc effect = {.name = "Swarm", .emitter_count = 1};
    effect.emitters[0] = (NvVfxEmitterDesc){
        .count = 512, .life_min = 2.0f, .life_max = 3.0f, .speed_min = 2.0f, .speed_max = 10.0f, .cone = 1.2f,
        .gravity = 6.0f, .drag = 0.3f, .use_ground = 1, .ground = 0.02f, .restitution = 0.4f, .friction = 0.3f,
        .size_start = 0.05f, .size_end = 0.03f,
        .colors = {RGBA(3.0f, 1.4f, 0.4f, 1.0f), RGBA(1.5f, 0.5f, 0.1f, 0.8f), RGBA(0.3f, 0.08f, 0.02f, 0.0f)},
        .shape = NV_VFX_SHAPE_DISC, .blend = NV_VFX_BLEND_ADD};
    return nv_vfx_add_effect(vfx, &effect);
}

internal NvVfxEffectId make_explosion(NvVfx* vfx)
{
    NvVfxEffectDesc effect = {.name = "Explosion", .emitter_count = 5};
    // The flash, and the shockwave ring behind it.
    effect.emitters[0] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 0.25f, .life_max = 0.25f, .size_start = 0.6f, .size_end = 3.2f,
        .colors = {RGBA(7.0f, 4.5f, 1.5f, 1.0f), RGBA(3.0f, 1.4f, 0.4f, 0.8f), RGBA(1.0f, 0.3f, 0.1f, 0.0f)},
        .shape = NV_VFX_SHAPE_DISC, .blend = NV_VFX_BLEND_ADD};
    effect.emitters[1] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 0.55f, .life_max = 0.55f, .size_start = 0.3f, .size_end = 4.8f,
        .colors = {RGBA(1.6f, 1.3f, 0.9f, 0.7f), RGBA(0.6f, 0.45f, 0.3f, 0.4f), RGBA(0.1f, 0.1f, 0.1f, 0.0f)},
        .shape = NV_VFX_SHAPE_RING, .blend = NV_VFX_BLEND_ADD};
    // Sparks thrown in every direction, falling, bouncing on the ground.
    effect.emitters[2] = (NvVfxEmitterDesc){
        .count = 140, .life_min = 0.5f, .life_max = 1.3f, .speed_min = 4.0f, .speed_max = 15.0f, .cone = NV_PI,
        .gravity = 9.0f, .drag = 1.1f, .use_ground = 1, .ground = 0.02f, .restitution = 0.4f, .friction = 0.3f,
        .size_start = 0.07f, .size_end = 0.02f, .stretch = 0.3f,
        .colors = {RGBA(9.0f, 5.5f, 1.6f, 1.0f), RGBA(4.0f, 1.5f, 0.3f, 1.0f), RGBA(0.7f, 0.1f, 0.02f, 0.0f)},
        .shape = NV_VFX_SHAPE_STREAK, .blend = NV_VFX_BLEND_ADD};
    // Smoke that rises and swirls.
    effect.emitters[3] = (NvVfxEmitterDesc){
        .count = 30, .delay = 0.05f, .life_min = 1.6f, .life_max = 3.2f, .speed_min = 0.5f, .speed_max = 3.0f, .cone = 1.2f,
        .gravity = -0.6f, .drag = 0.8f, .turbulence = 1.4f, .turbulence_scale = 0.7f,
        .size_start = 0.4f, .size_end = 1.5f, .spin = 1.0f,
        .colors = {RGBA(0.9f, 0.55f, 0.25f, 0.85f), RGBA(0.22f, 0.2f, 0.18f, 0.7f), RGBA(0.05f, 0.05f, 0.05f, 0.0f)},
        .shape = NV_VFX_SHAPE_PUFF, .blend = NV_VFX_BLEND_ALPHA};
    // Dark debris.
    effect.emitters[4] = (NvVfxEmitterDesc){
        .count = 24, .life_min = 1.5f, .life_max = 3.0f, .speed_min = 5.0f, .speed_max = 12.0f, .cone = 1.1f,
        .gravity = 12.0f, .drag = 0.2f, .use_ground = 1, .ground = 0.05f, .restitution = 0.45f, .friction = 0.4f,
        .size_start = 0.08f, .size_end = 0.08f,
        .colors = {RGBA(0.16f, 0.11f, 0.08f, 1.0f), RGBA(0.14f, 0.1f, 0.07f, 1.0f), RGBA(0.1f, 0.07f, 0.05f, 0.0f)},
        .shape = NV_VFX_SHAPE_DISC, .blend = NV_VFX_BLEND_ALPHA};
    return nv_vfx_add_effect(vfx, &effect);
}

// What a missile leaves along its flight: a hot flame and smoke.
internal NvVfxEffectId make_missile(NvVfx* vfx)
{
    NvVfxEffectDesc effect = {.name = "Missile", .emitter_count = 2};
    effect.emitters[0] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 0.25f, .life_max = 0.45f, .speed_min = 0.0f, .speed_max = 0.6f, .cone = NV_PI,
        .size_start = 0.16f, .size_end = 0.03f,
        .colors = {RGBA(8.0f, 4.0f, 1.0f, 1.0f), RGBA(3.0f, 1.0f, 0.2f, 0.9f), RGBA(0.5f, 0.1f, 0.02f, 0.0f)},
        .shape = NV_VFX_SHAPE_DISC, .blend = NV_VFX_BLEND_ADD};
    effect.emitters[1] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 1.4f, .life_max = 2.4f, .speed_min = 0.1f, .speed_max = 0.7f, .cone = NV_PI,
        .gravity = -0.3f, .drag = 0.6f, .turbulence = 0.7f, .turbulence_scale = 1.0f,
        .size_start = 0.14f, .size_end = 0.6f, .spin = 1.0f,
        .colors = {RGBA(0.7f, 0.65f, 0.6f, 0.45f), RGBA(0.35f, 0.34f, 0.33f, 0.35f), RGBA(0.1f, 0.1f, 0.1f, 0.0f)},
        .shape = NV_VFX_SHAPE_PUFF, .blend = NV_VFX_BLEND_ALPHA};
    return nv_vfx_add_effect(vfx, &effect);
}

// The strips and the mark: a thin hot trail behind a missile, a laser that flickers and runs, and a scorch mark on the ground.
internal void make_styles(Workloads* workloads)
{
    workloads->missile_trail = (NvVfxLineStyle){
        .life = 0.9f, .width_start = 0.09f, .width_end = 0.0f,
        .colors = {RGBA(4.0f, 2.2f, 0.7f, 0.9f), RGBA(0.6f, 0.2f, 0.05f, 0.0f)}};
    workloads->laser = (NvVfxLineStyle){
        .life = 0.4f, .width_start = 0.12f, .width_end = 0.05f,
        .colors = {RGBA(0.6f, 3.0f, 8.0f, 1.0f), RGBA(0.1f, 0.5f, 2.0f, 0.0f)}, .flicker = 0.35f, .scroll = 5.0f};
    workloads->scorch = (NvVfxDecalStyle){
        .life = 12.0f, .fade = 0.4f, .color = RGBA(0.02f, 0.015f, 0.01f, 0.8f), .shape = NV_VFX_SHAPE_PUFF};
}

//
// Building
//

internal NvMeshId make_box_mesh(Stress* stress, f32 half)
{
    NvVertex vertices[NV_MESH_BOX_VERTICES];
    u32 indices[NV_MESH_BOX_INDICES];
    NvMeshBuilder mesh = {.vertices = vertices, .vertex_capacity = NV_MESH_BOX_VERTICES, .indices = indices,
                          .index_capacity = NV_MESH_BOX_INDICES};
    nv_mesh_append_box(&mesh, nv_vec3(0, 0, 0), nv_vec3(half, half, half));
    NvMeshData data = nv_mesh_builder_data(&mesh);
    return nv_renderer_add_mesh(&stress->renderer, &data);
}

// A color wheel, so neighboring colors differ.
internal void color_wheel(u32 i, u32 count, f32* rgb)
{
    f32 h = (f32)i / (f32)count * 6.0f;
    f32 x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);
    f32 r = 0, g = 0, b = 0;
    switch ((u32)h % 6) {
    case 0: r = 1; g = x; break;
    case 1: r = x; g = 1; break;
    case 2: g = 1; b = x; break;
    case 3: g = x; b = 1; break;
    case 4: r = x; b = 1; break;
    default: r = 1; b = x; break;
    }
    rgb[0] = 0.15f + 0.8f * r;
    rgb[1] = 0.15f + 0.8f * g;
    rgb[2] = 0.15f + 0.8f * b;
}

internal b32 is_loop(NvClipId clip)
{
    return strstr(nv_anim_clip_name(clip), "_Loop") != NULL;
}

// The character the crowd copies: loaded once, under the crowd's node, hidden and still. Its clips that loop are the crowd's.
internal void load_character(Stress* stress)
{
    Workloads* workloads = &stress->workloads;
    NvGltfLoad load = nv_gltf_load_model(CHARACTER_FILE, stress->scene, workloads->crowd, &stress->renderer, &stress->permanent,
                                         &stress->scratch);
    if (!load.ok) {
        nv_log(NV_LOG_ERROR, "stress", "cannot load %s: the crowd stays empty", CHARACTER_FILE);
        workloads->character_failed = 1;
        return;
    }
    workloads->character = load.model;
    workloads->character_loaded = 1;
    for (u32 m = 0; m < load.model.mesh_node_count; ++m) {
        NvNode* node = nv_scene_get(stress->scene, load.model.mesh_nodes[m]);
        workloads->character_meshes[m] = node->mesh;
        node->mesh = (NvMeshId){0};
    }
    if (!load.model.animator.index)
        return;
    nv_anim_get(load.model.animator)->scene = NULL;
    NvClipId clips[STRESS_MAX_CLIPS];
    u32 count = nv_gltf_load_clips(CLIPS_FILE, load.model.skeleton, NULL, &stress->scratch, clips, STRESS_MAX_CLIPS).count;
    for (u32 c = 0; c < count; ++c)
        if (is_loop(clips[c]))
            workloads->loops[workloads->loop_count++] = clips[c];
}

void workloads_build(Stress* stress)
{
    Workloads* workloads = &stress->workloads;
    NvScene* scene = stress->scene;
    NvRenderer* renderer = &stress->renderer;

    workloads->cube_mesh = make_box_mesh(stress, 0.5f);
    workloads->link_mesh = make_box_mesh(stress, 0.06f);
    NvMaterialDesc base = {.base_color = {0.6f, 0.62f, 0.66f, 1.0f}};
    workloads->palette[0] = nv_renderer_add_material(renderer, &base);
    for (u32 i = 1; i < STRESS_PALETTE_COLORS; ++i) {
        NvMaterialDesc desc = {.base_color = {0, 0, 0, 1}};
        color_wheel(i, STRESS_PALETTE_COLORS, desc.base_color);
        workloads->palette[i] = nv_renderer_add_material(renderer, &desc);
    }

    NvNodeId camera = nv_scene_add_node(scene, (NvNodeId){0}, "camera");
    nv_scene_get(scene, camera)->camera = (NvCamera){
        .projection = NV_PROJECTION_PERSPECTIVE, .fov_y = 50.0f * NV_PI / 180.0f, .near_z = 0.1f, .far_z = 300.0f};
    scene->active_camera = camera;
    stress->camera = camera;
    stress->orbit = (NvOrbitCamera){.yaw = 0.45f, .pitch = 0.42f, .distance = 28.0f, .min_pitch = -10.0f * NV_PI / 180.0f,
                                    .max_pitch = 80.0f * NV_PI / 180.0f, .min_distance = 1.0f, .max_distance = 100.0f};

    // 0.5 rad about Y, then -0.8 rad about X.
    NvNodeId sun = nv_scene_add_node(scene, (NvNodeId){0}, "sun");
    NvNode* sun_node = nv_scene_get(scene, sun);
    sun_node->rotation = (NvQuat){-0.377312273f, 0.227874145f, 0.0963436365f, 0.892427444f};
    sun_node->light = (NvLight){.type = NV_LIGHT_DIRECTIONAL, .color = nv_vec3(1.0f, 0.96f, 0.9f), .intensity = 1.1f};

    NvNodeId ground = nv_scene_add_node(scene, (NvNodeId){0}, "ground");
    NvNode* ground_node = nv_scene_get(scene, ground);
    ground_node->position = nv_vec3(0.0f, -0.05f, -50.0f);
    ground_node->scale = nv_vec3(260.0f, 0.1f, 260.0f);
    ground_node->mesh = workloads->cube_mesh;
    ground_node->material = nv_renderer_add_material(renderer, &(NvMaterialDesc){.base_color = {0.28f, 0.3f, 0.34f, 1.0f}});

    // The grid grows away from the camera, the crowd stands in front of it, the chain to its right.
    workloads->grid = nv_scene_add_node(scene, (NvNodeId){0}, "grid");
    workloads->chain = nv_scene_add_node(scene, (NvNodeId){0}, "chain");
    nv_scene_get(scene, workloads->chain)->position = nv_vec3(14.0f, 0.5f, 8.0f);
    workloads->crowd = nv_scene_add_node(scene, (NvNodeId){0}, "crowd");
    nv_scene_get(scene, workloads->crowd)->position = nv_vec3(0.0f, 0.0f, 4.0f);
    for (u32 i = 0; i < 3; ++i)
        renderer->ambient[i] = 0.15f;

    workloads->characters = NV_PUSH_ARRAY(&stress->permanent, STRESS_MAX_CHARACTERS, NvGltfModel);
    workloads->flights = NV_PUSH_ARRAY(&stress->permanent, STRESS_MAX_MISSILES, Flight);
    load_character(stress);
    workloads->fixed_nodes = stress_live_nodes(scene);

    make_styles(workloads);
    workloads->swarm = make_swarm(&stress->vfx);
    workloads->explosion = make_explosion(&stress->vfx);
    workloads->missile = make_missile(&stress->vfx);
}

// No workload, an empty grid and chain, the characters' clips from their start and no effects: the same frames follow at the
// same dt. The characters stay made (an animator cannot be destroyed).
void workloads_reset(Stress* stress)
{
    Workloads* workloads = &stress->workloads;
    NvScene* scene = stress->scene;
    workloads->settings = (WorkloadSettings){0};
    while (workloads->cubes_built)
        nv_scene_remove_node(scene, workloads->cubes[--workloads->cubes_built]);
    while (workloads->links_built)
        nv_scene_remove_node(scene, workloads->links[--workloads->links_built]);
    workloads->cubes_painted = 0;
    workloads->churn_cursor = 0;
    workloads->chain_time = 0.0f;
    for (u32 i = 0; i < workloads->characters_made; ++i) {
        NvAnimatorId id = workloads->characters[i].animator;
        if (!id.index || !workloads->loop_count)
            continue;
        NvAnimator* animator = nv_anim_get(id);
        NvClipId clip = workloads->loops[i % workloads->loop_count];
        nv_anim_play(animator, clip, 0.0f, 1);
        animator->layers[0].time = fmodf((f32)i * CROWD_OFFSET, nv_anim_clip_duration(clip));
    }
    nv_random_seed(&workloads->rng, 0x9E3779B9u, 1);
    workloads->flight_count = 0;
    workloads->swarm_carry = workloads->explosion_carry = workloads->decal_carry = 0.0f;
}

//
// Workloads
//

// Cube `i` of the grid, a child of the grid's node.
internal void add_cube(Stress* stress, u32 i)
{
    Workloads* workloads = &stress->workloads;
    NvNodeId id = nv_scene_add_node(stress->scene, workloads->grid, "cube");
    NvNode* node = nv_scene_get(stress->scene, id);
    u32 column = i % GRID_COLUMNS;
    u32 row = i / GRID_COLUMNS;
    node->position = nv_vec3(((f32)column - (f32)(GRID_COLUMNS - 1) * 0.5f) * GRID_SPACING, GRID_HEIGHT, -(f32)row * GRID_SPACING);
    node->scale = nv_vec3(GRID_SCALE, GRID_SCALE, GRID_SCALE);
    node->mesh = workloads->cube_mesh;
    node->material = workloads->palette[workloads->cubes_painted ? 1 + i % workloads->cubes_painted : 0];
    workloads->cubes[i] = id;
}

// Makes the grid hold `target` cubes painted with `colors` colors (0 = the base alone).
internal void update_grid(Stress* stress, u32 target, u32 colors)
{
    Workloads* workloads = &stress->workloads;
    if (colors != workloads->cubes_painted) {
        workloads->cubes_painted = colors;
        for (u32 i = 0; i < workloads->cubes_built; ++i)
            nv_scene_get(stress->scene, workloads->cubes[i])->material = workloads->palette[colors ? 1 + i % colors : 0];
    }
    while (workloads->cubes_built > target)
        nv_scene_remove_node(stress->scene, workloads->cubes[--workloads->cubes_built]);
    while (workloads->cubes_built < target)
        add_cube(stress, workloads->cubes_built++);
}

// Removes and re-adds cubes, so node slots keep being freed and reused.
internal void churn_grid(Stress* stress, u32 count)
{
    Workloads* workloads = &stress->workloads;
    if (count > workloads->cubes_built)
        count = workloads->cubes_built;
    for (u32 k = 0; k < count; ++k) {
        u32 i = workloads->churn_cursor++ % workloads->cubes_built;
        nv_scene_remove_node(stress->scene, workloads->cubes[i]);
        add_cube(stress, i);
    }
}

// A helix: each link is the child of the last, so moving the first moves them all.
internal void resize_chain(Stress* stress, u32 target)
{
    Workloads* workloads = &stress->workloads;
    while (workloads->links_built > target)
        nv_scene_remove_node(stress->scene, workloads->links[--workloads->links_built]);
    while (workloads->links_built < target) {
        u32 i = workloads->links_built++;
        NvNodeId parent = i ? workloads->links[i - 1] : workloads->chain;
        workloads->links[i] = nv_scene_add_node(stress->scene, parent, "link");
        NvNode* node = nv_scene_get(stress->scene, workloads->links[i]);
        node->position = i ? nv_vec3(0.12f, 0.03f, 0.0f) : nv_vec3(0, 0, 0);
        node->mesh = workloads->link_mesh;
        node->material = workloads->palette[1 + (i * CHAIN_STRIDE) % (STRESS_PALETTE_COLORS - 1)];
    }
}

// The links twist by an angle that wobbles with the chain's time.
internal void wobble_chain(Stress* stress, f32 dt)
{
    Workloads* workloads = &stress->workloads;
    workloads->chain_time += dt;
    NvQuat twist = nv_quat_axis_angle(nv_vec3(0, 1, 0), CHAIN_TWIST + CHAIN_WOBBLE * sinf(workloads->chain_time));
    for (u32 i = 0; i < workloads->links_built; ++i)
        nv_scene_get(stress->scene, workloads->links[i])->rotation = twist;
}

// Makes characters up to `target` and shows that many; the others are hidden and paused.
internal void update_crowd(Stress* stress, u32 target)
{
    Workloads* workloads = &stress->workloads;
    NvScene* scene = stress->scene;
    while (workloads->characters_made < target && workloads->character_loaded) {
        u32 i = workloads->characters_made++;
        NvGltfModel* made = &workloads->characters[i];
        nv_gltf_instantiate(&workloads->character, scene, workloads->crowd, made);
        NvNode* root = nv_scene_get(scene, made->root);
        u32 column = i % CROWD_COLUMNS;
        u32 row = i / CROWD_COLUMNS;
        root->position = nv_vec3(((f32)column - (f32)(CROWD_COLUMNS - 1) * 0.5f) * CROWD_SPACING, 0.0f, (f32)row * CROWD_SPACING);
        snprintf(root->name, sizeof(root->name), "character %u", i + 1);
        // Every character loops its own clip from its own point in time.
        if (workloads->loop_count && made->animator.index) {
            NvAnimator* animator = nv_anim_get(made->animator);
            NvClipId clip = workloads->loops[i % workloads->loop_count];
            nv_anim_play(animator, clip, 0.0f, 1);
            animator->layers[0].time = fmodf((f32)i * CROWD_OFFSET, nv_anim_clip_duration(clip));
        }
    }
    for (u32 i = 0; i < workloads->characters_made; ++i) {
        const NvGltfModel* made = &workloads->characters[i];
        b32 shown = i < target;
        if (made->animator.index)
            nv_anim_get(made->animator)->scene = shown ? scene : NULL;
        for (u32 m = 0; m < made->mesh_node_count; ++m)
            nv_scene_get(scene, made->mesh_nodes[m])->mesh = shown ? workloads->character_meshes[m] : (NvMeshId){0};
    }
    workloads->characters_shown = target < workloads->characters_made ? target : workloads->characters_made;
}

// A point on the ground in the field around the origin.
internal NvVec3 field_point(Workloads* workloads, f32 height)
{
    return nv_vec3((nv_random_f32(&workloads->rng) * 2.0f - 1.0f) * EFFECTS_AREA_X, height,
                   (nv_random_f32(&workloads->rng) * 2.0f - 1.0f) * EFFECTS_AREA_Z);
}

internal void new_flight(Workloads* workloads, Flight* flight)
{
    f32 angle = nv_random_f32(&workloads->rng) * 6.2831853f;
    f32 speed = 6.0f + nv_random_f32(&workloads->rng) * 4.0f;
    flight->pos = field_point(workloads, 0.3f);
    flight->vel = nv_vec3(cosf(angle) * speed, 3.0f + nv_random_f32(&workloads->rng) * 2.0f, sinf(angle) * speed);
    flight->life = 1.2f + nv_random_f32(&workloads->rng) * 0.8f;
}

// Keeps the asked numbers of missiles, beams, particles, explosions and decals going.
internal void update_effects(Stress* stress, f32 dt)
{
    Workloads* workloads = &stress->workloads;
    const WorkloadSettings* s = &workloads->settings;
    NvVfx* vfx = &stress->vfx;
    u32 missiles = s->missiles < STRESS_MAX_MISSILES ? s->missiles : STRESS_MAX_MISSILES;
    u32 beams = s->beams;

    // Missiles that fly, leave a trail and smoke, and start again from another point when their time is up.
    if (workloads->flight_count > missiles)
        workloads->flight_count = missiles;
    while (workloads->flight_count < missiles) {
        Flight* flight = &workloads->flights[workloads->flight_count++];
        new_flight(workloads, flight);
        flight->life *= nv_random_f32(&workloads->rng); // out of step with each other
    }
    for (u32 i = 0; i < workloads->flight_count; ++i) {
        Flight* flight = &workloads->flights[i];
        NvVec3 before = flight->pos;
        flight->vel.y -= 4.0f * dt;
        flight->pos = nv_vec3_add(flight->pos, nv_vec3_scale(flight->vel, dt));
        flight->life -= dt;
        if (dt > 0.0f) {
            NvVec3 step = nv_vec3_sub(flight->pos, before);
            // A thin smoke: 2,000 missiles would fill the particle buffer with a thicker one.
            nv_vfx_emit(vfx, workloads->missile, before, flight->pos, (u32)(sqrtf(nv_vec3_dot(step, step)) * 12.0f) + 1);
            nv_vfx_trail(vfx, &workloads->missile_trail, before, flight->pos);
        }
        if (flight->life <= 0.0f || flight->pos.y < 0.05f)
            new_flight(workloads, flight);
    }

    // Beams: each is held by calling again every frame with a short life, between fixed points of its own.
    for (u32 i = 0; i < beams; ++i) {
        u32 h = i * 2654435761u + 12345u;
        f32 ax = (f32)(h & 1023u) / 1023.0f, az = (f32)((h >> 10) & 1023u) / 1023.0f;
        f32 bx = (f32)((h >> 20) & 1023u) / 1023.0f, bz = (f32)((h * 40503u >> 8) & 1023u) / 1023.0f;
        NvVec3 from = nv_vec3((ax * 2.0f - 1.0f) * EFFECTS_AREA_X, 7.0f, (az * 2.0f - 1.0f) * EFFECTS_AREA_Z);
        NvVec3 to = nv_vec3((bx * 2.0f - 1.0f) * EFFECTS_AREA_X, 0.05f, (bz * 2.0f - 1.0f) * EFFECTS_AREA_Z);
        nv_vfx_beam(vfx, &workloads->laser, from, to, dt * 1.5f + 0.02f);
    }

    // Particles: embers in bursts of 512, at the rate that replaces what dies (they live about 2.5 s).
    f32 rate = (f32)s->particles / 2.5f;
    workloads->swarm_carry += rate * dt / 512.0f;
    u32 limit = 4000; // bursts a frame
    while (workloads->swarm_carry >= 1.0f) {
        workloads->swarm_carry -= 1.0f;
        if (limit) {
            nv_vfx_burst(vfx, workloads->swarm, field_point(workloads, 0.05f), nv_vec3(0.0f, 1.0f, 0.0f), 1.0f);
            --limit;
        }
    }
    workloads->explosion_carry += (f32)s->explosions * dt;
    for (u32 n = 0; workloads->explosion_carry >= 1.0f && n < 300; ++n) {
        workloads->explosion_carry -= 1.0f;
        nv_vfx_burst(vfx, workloads->explosion, field_point(workloads, 0.05f), nv_vec3(0.0f, 1.0f, 0.0f), 1.0f);
    }
    if (workloads->explosion_carry > 1.0f)
        workloads->explosion_carry = 0.0f;
    workloads->decal_carry += (f32)s->decals * dt;
    for (u32 n = 0; workloads->decal_carry >= 1.0f && n < 2000; ++n) {
        workloads->decal_carry -= 1.0f;
        NvVec3 point = field_point(workloads, 0.02f);
        f32 angle = nv_random_f32(&workloads->rng) * 6.2831853f;
        nv_vfx_decal(vfx, &workloads->scorch, point, angle, 0.6f + nv_random_f32(&workloads->rng) * 1.2f);
    }
}

// Every joint's line to its parent, for each character shown.
internal void draw_bones(Stress* stress)
{
    Workloads* workloads = &stress->workloads;
    for (u32 i = 0; i < workloads->characters_shown; ++i) {
        NvAnimatorId id = workloads->characters[i].animator;
        if (!id.index)
            continue;
        NvAnimator* animator = nv_anim_get(id);
        const NvJointDesc* joints = nv_anim_joints(animator->skeleton);
        NvMat4 root = nv_scene_get(stress->scene, animator->owner)->world;
        for (u32 j = 0; j < animator->joint_count; ++j) {
            if (joints[j].parent < 0)
                continue;
            NvVec3 a = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[joints[j].parent]));
            NvVec3 b = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[j]));
            nv_renderer_debug_line(&stress->renderer, a, b, nv_vec3(1.0f, 0.8f, 0.2f));
        }
    }
}

internal u32 at_most(u32 value, u32 max)
{
    return value < max ? value : max;
}

// The workloads' counts in the node budget: the crowd first, then the chain, then the grid, each holding what the nodes and
// animators left allow. Shrinking comes before growing, so the node count never passes the limit on the way.
void workloads_update(Stress* stress, f32 dt)
{
    Workloads* workloads = &stress->workloads;
    const WorkloadSettings* s = &workloads->settings;

    s32 nodes_left = (s32)(NV_MAX_NODES - 1 - workloads->fixed_nodes);
    u32 per_character = workloads->character_loaded ? workloads->character.mesh_node_count + 1 : 1; // its root and its meshes

    u32 characters = at_most(s->characters, STRESS_MAX_CHARACTERS);
    u32 can_make = workloads->characters_made + nv_anim_free_animators();
    if (characters > can_make)
        characters = can_make;
    u32 held = characters > workloads->characters_made ? characters : workloads->characters_made;
    nodes_left -= (s32)(held * per_character);
    if (nodes_left < 0)
        nodes_left = 0;
    u32 links = at_most(s->links, STRESS_MAX_LINKS);
    if (links > (u32)nodes_left)
        links = (u32)nodes_left;
    nodes_left -= (s32)links;
    u32 cubes = at_most(s->cubes, STRESS_MAX_CUBES);
    if (cubes > (u32)nodes_left)
        cubes = (u32)nodes_left;

    u32 colors = at_most(s->colors, STRESS_PALETTE_COLORS - 1);
    if (cubes < workloads->cubes_built)
        update_grid(stress, cubes, workloads->cubes_painted);
    if (links < workloads->links_built)
        resize_chain(stress, links);
    update_crowd(stress, characters);
    resize_chain(stress, links);
    wobble_chain(stress, dt);
    update_grid(stress, cubes, colors);
    if (s->churn && dt > 0.0f)
        churn_grid(stress, s->churn);
    update_effects(stress, dt);
}

// After nv_scene_update: what is drawn from the world matrices.
void workloads_after_update(Stress* stress)
{
    if (stress->workloads.settings.show_bones)
        draw_bones(stress);
}

//
// Benchmark
//

// Each step loads the engine one way; what a step leaves out is 0, so off.
typedef struct StressStep {
    const char* name;
    WorkloadSettings settings;
} StressStep;

global const StressStep stress_steps[] = {
    {"Cubes 250", {.cubes = 250}},
    {"Cubes 1000", {.cubes = 1000}},
    {"Cubes 4000", {.cubes = 4000}},
    {"Cubes 8000", {.cubes = 8000}},
    {"Cubes 16000", {.cubes = 16000}},
    {"Colors 199", {.cubes = 4000, .colors = 199}},
    {"Churn 256", {.cubes = 4000, .churn = 256}},
    {"Chain 1000", {.links = 1000}},
    {"Crowd 8", {.characters = 8}},
    {"Crowd 32", {.characters = 32}},
    {"Crowd 60", {.characters = 60}},
    {"Crowd 120", {.characters = 120}},
    {"Crowd 200", {.characters = 200}},
    {"Bones 200", {.characters = 200, .show_bones = 1}},
    {"Particles 250k", {.particles = 250000}},
    {"Particles 1M", {.particles = 1000000}},
    {"Particles 2M", {.particles = 2000000}},
    {"Explosions 200", {.explosions = 200}},
    {"Missiles 500", {.missiles = 500}},
    {"Missiles 2000", {.missiles = 2000}},
    {"Beams 500", {.beams = 500}},
    {"Decals 2000", {.decals = 2000}},
};

u32 workloads_step_count(void)
{
    return (u32)NV_ARRAY_COUNT(stress_steps);
}

const char* workloads_step_name(u32 step)
{
    return stress_steps[step].name;
}

void workloads_apply_step(Stress* stress, u32 step)
{
    stress->workloads.settings = stress_steps[step].settings;
}
