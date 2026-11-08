// The stress sample (docs/specs/samples.md, docs/specs/stress.md): a standalone executable that loads the engine with many objects
// and measures the frame time as they grow. Its scene and logic are fixed in C and it needs no input: it runs its benchmark by
// itself, again and again, and shows the stats and the results. It depends on the engine only, like every sample, and is one
// file: the state, then the scene and its workloads (a grid of cubes, a deep chain, a crowd of animated characters, fire effects,
// the benchmark's steps), then the shell (the frame, the camera, the panel, the benchmark, the read-only debug exports).

#include <engine/anim.h>
#include <engine/camera.h>
#include <engine/gltf.h>
#include <engine/gpu.h>
#include <engine/imgui.h>
#include <engine/log.h>
#include <engine/mesh.h>
#include <engine/renderer.h>
#include <engine/scene.h>
#include <engine/vfx.h>
#include <engine/window.h>

// The commit's hash and subject line and the build type (nv_version.h, written by cmake/version.cmake on every build).
#include "nv_version.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FIXED_DT (1.0f / 60.0f) // what the workloads, the animation and the effects advance by each frame

//
// The workloads' state
//

#define STRESS_MAX_CUBES 16000
#define STRESS_MAX_LINKS 1000
#define STRESS_MAX_CHARACTERS 200
#define STRESS_MAX_MISSILES 2000
#define STRESS_PALETTE_COLORS 200 // the base and 199 around the color wheel
#define STRESS_MAX_CLIPS 64

typedef struct Flight {
    NvVec3 pos, vel;
    f32 life;
} Flight;

// What a benchmark step sets (the steps are at the end of the workloads). A count of 0 is off.
typedef struct WorkloadSettings {
    u32 cubes;
    u32 colors; // palette colors the cubes take (0 = the base alone)
    u32 churn;  // cubes removed and added again a frame
    u32 links;
    u32 characters;
    u32 particles;  // live
    u32 explosions; // a second
    u32 missiles;   // in flight
    u32 beams;
    u32 decals; // a second
    b32 show_bones;
} WorkloadSettings;

typedef struct Workloads {
    WorkloadSettings settings;

    NvNodeId grid, chain, crowd; // the group nodes the workloads live under
    NvMeshId cube_mesh, link_mesh;
    NvMaterialId palette[STRESS_PALETTE_COLORS];
    u32 fixed_nodes; // live nodes besides the workloads': the camera, the sun, the ground, the groups, the loaded character

    NvNodeId cubes[STRESS_MAX_CUBES];
    u32 cubes_built;
    u32 cubes_painted; // colors the built cubes have (0 = the base)
    u32 churn_cursor;

    NvNodeId links[STRESS_MAX_LINKS];
    u32 links_built;
    f32 chain_time; // seconds the wobble has run

    // Characters are made on first use and then kept: an animator cannot be destroyed. The ones not asked for are hidden and paused.
    NvGltfModel character; // the loaded one, which the others copy; not a crowd member
    b32 character_loaded, character_failed;
    NvMeshId character_meshes[NV_GLTF_MAX_MESH_NODES];
    NvClipId loops[STRESS_MAX_CLIPS];
    u32 loop_count;
    NvGltfModel* characters; // [STRESS_MAX_CHARACTERS]
    u32 characters_made, characters_shown;

    // Effects (definitions in stress.c)
    NvRandom rng;
    NvVfxEffectId swarm, explosion, missile;
    NvVfxLineStyle missile_trail, laser;
    NvVfxDecalStyle scorch;
    Flight* flights; // [STRESS_MAX_MISSILES]
    u32 flight_count;
    f32 swarm_carry, explosion_carry, decal_carry; // fractions of a burst not yet fired
} Workloads;

//
// The shell's state
//

typedef struct FrameTimes {
    f64 frame; // from one frame to the next, ms
    f64 anim;  // nv_anim_update_scene
    f64 scene; // the workloads' update and nv_scene_update
    f64 draw;  // nv_renderer_draw
    f64 ui;    // building and recording ImGui
    f64 gpu;   // the scene pass on the GPU; 0 without timestamps
    f64 gpu_shadow;
    f64 gpu_upscale;
    f64 gpu_bloom;
    f64 gpu_particles;
} FrameTimes;

#define BENCHMARK_MAX_STEPS 32
#define BENCHMARK_WARMUP 1.0  // seconds before a step measures
#define BENCHMARK_MEASURE 3.0 // seconds a step measures
#define BENCHMARK_TABLE_SIZE 4096

typedef struct BenchmarkResult {
    u32 frames;
    FrameTimes average;
    f64 worst_frame;
    f64 particles; // alive, on average
} BenchmarkResult;

// The benchmark runs by itself from the start, step after step, and starts again at the end of the last.
typedef struct Benchmark {
    u32 runs; // finished
    u32 step;
    f64 step_start; // seconds
    BenchmarkResult results[BENCHMARK_MAX_STEPS];  // this run's, up to `step`
    BenchmarkResult finished[BENCHMARK_MAX_STEPS]; // the last whole run's
} Benchmark;

typedef struct Stress {
    NvArena permanent;
    NvArena scratch;
    NvArena anim_memory;
    NvWindow window;
    NvGpu gpu;
    NvRenderer renderer;
    NvImgui imgui;
    NvVfx vfx;
    char download_text[64];

    NvScene* scene; // about 1 MB: pushed from the arena
    NvNodeId camera;
    NvOrbitCamera orbit;
    NvResolution resolution;
    NvRect viewport, panel; // framebuffer pixels
    NvSceneOutput scene_output;

    f64 last_time;
    u64 frames;

    FrameTimes times;
    FrameTimes window_sum; // a second's frames, summed
    f64 window_start, window_worst_frame;
    u32 window_frames;
    FrameTimes shown_average; // the last whole second's
    f64 shown_worst_frame;

    Benchmark benchmark;
    Workloads workloads;
} Stress;

global Stress stress_state;
global u8 permanent_memory[NV_MEGABYTES(48)];
global u8 scratch_memory[NV_MEGABYTES(32)]; // holds a glb while it loads
global u8 anim_memory[NV_MEGABYTES(48)];

#define ORBIT_RADIANS_PER_PIXEL 0.008f
#define PANEL_WIDTH 340.0f // CSS pixels, times the UI scale

// The nodes in use (a removed node's slot has an even generation).
internal u32 stress_live_nodes(const NvScene* scene)
{
    u32 count = 0;
    for (u32 i = 1; i <= scene->node_count; ++i)
        count += scene->nodes[i].gen & 1;
    return count;
}

//
// The workloads
//

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

internal void workloads_build(Stress* stress)
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
internal void workloads_reset(Stress* stress)
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
internal void workloads_update(Stress* stress, f32 dt)
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
internal void workloads_after_update(Stress* stress)
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

internal u32 workloads_step_count(void)
{
    return (u32)NV_ARRAY_COUNT(stress_steps);
}

internal const char* workloads_step_name(u32 step)
{
    return stress_steps[step].name;
}

internal void workloads_apply_step(Stress* stress, u32 step)
{
    stress->workloads.settings = stress_steps[step].settings;
}

//
// The shell
//

// A step starts from here: no workload, no effects.
internal void stress_reset(Stress* stress)
{
    nv_vfx_clear(&stress->vfx);
    workloads_reset(stress);
}

//
// Frame times
//

internal f64 now_ms(void)
{
    return nv_time_seconds() * 1000.0;
}

internal f64 stress_load(const FrameTimes* t)
{
    f64 cpu = t->anim + t->scene + t->draw + t->ui;
    f64 gpu = t->gpu + t->gpu_shadow + t->gpu_upscale + t->gpu_bloom + t->gpu_particles;
    f64 busy = cpu > gpu ? cpu : gpu;
    return t->frame > 0.0 ? busy / t->frame * 100.0 : 0.0;
}

internal void add_times(FrameTimes* sum, const FrameTimes* t)
{
    sum->frame += t->frame;
    sum->anim += t->anim;
    sum->scene += t->scene;
    sum->draw += t->draw;
    sum->ui += t->ui;
    sum->gpu += t->gpu;
    sum->gpu_shadow += t->gpu_shadow;
    sum->gpu_upscale += t->gpu_upscale;
    sum->gpu_bloom += t->gpu_bloom;
    sum->gpu_particles += t->gpu_particles;
}

internal FrameTimes divide_times(const FrameTimes* sum, f64 n)
{
    return (FrameTimes){sum->frame / n, sum->anim / n, sum->scene / n, sum->draw / n, sum->ui / n, sum->gpu / n,
                        sum->gpu_shadow / n, sum->gpu_upscale / n, sum->gpu_bloom / n, sum->gpu_particles / n};
}

// Averages the frame times over windows of about a second, for the stats.
internal void accumulate_times(Stress* stress, f64 now)
{
    add_times(&stress->window_sum, &stress->times);
    if (stress->times.frame > stress->window_worst_frame)
        stress->window_worst_frame = stress->times.frame;
    ++stress->window_frames;
    if (now - stress->window_start < 1.0)
        return;
    stress->shown_average = divide_times(&stress->window_sum, (f64)stress->window_frames);
    stress->shown_worst_frame = stress->window_worst_frame;
    stress->window_sum = (FrameTimes){0};
    stress->window_worst_frame = 0.0;
    stress->window_frames = 0;
    stress->window_start = now;
}

// "585 x 497, scale 1/2": what the scene renders at.
internal void resolution_text(Stress* stress, char* out, umm size)
{
    snprintf(out, size, "%u x %u, scale 1/%u", stress->scene_output.width, stress->scene_output.height, stress->resolution.divisor);
}

// "2048 32-bit float High, 30 m", or "off".
internal void shadow_settings_text(const NvShadowSettings* s, char* out, umm size)
{
    if (!s->size)
        snprintf(out, size, "off");
    else
        snprintf(out, size, "%u %s %s, %.0f m", s->size, s->format == NV_SHADOW_FORMAT_DEPTH16 ? "16-bit" : "32-bit float",
                 s->filter == NV_SHADOW_FILTER_HIGH ? "High" : "Low", (f64)s->distance);
}

//
// Benchmark
//

// Step `step`: the workloads from their start, then the step's settings.
internal void start_step(Stress* stress, u32 step, f64 now)
{
    Benchmark* benchmark = &stress->benchmark;
    stress_reset(stress);
    workloads_apply_step(stress, step);
    benchmark->step = step;
    benchmark->step_start = now;
    benchmark->results[step] = (BenchmarkResult){0};
}

// The last whole run as a text table (the log gets it at the end of each run).
internal void benchmark_table_text(Stress* stress, char* text, umm capacity)
{
    Benchmark* benchmark = &stress->benchmark;
    char agent[256];
    nv_window_system_text(agent, sizeof(agent));
    // Where the browser says nothing of the GPU, the backend knows its name (Direct3D 12).
    if (stress->gpu.adapter_name[0]) {
        umm used_by_agent = strlen(agent);
        snprintf(agent + used_by_agent, sizeof(agent) - used_by_agent, ", GPU %s (%s)", stress->gpu.adapter_name,
                 stress->gpu.backend_name);
    }
    char shadows[64];
    shadow_settings_text(&stress->renderer.shadows, shadows, sizeof(shadows));
    char resolution[64];
    resolution_text(stress, resolution, sizeof(resolution));
    umm used = (umm)snprintf(text, capacity,
                             "nv stress benchmark, run %u\ncommit: %s (%s build)\nbrowser: %s\ncanvas: %ux%u, GPU timestamps: %s\n"
                             "shadows: %s\nanti-aliasing: %s\nresolution: %s\n\n"
                             "step            frames  avg ms  worst ms  load %%  anim  scene  draw    ui    gpu  shadow  upscale  bloom  particles  alive\n",
                             benchmark->runs, NV_GIT_COMMIT, NV_BUILD_NAME, agent, stress->gpu.width, stress->gpu.height,
                             stress->gpu.has_timestamps ? "yes" : "no", shadows, stress->renderer.scene_samples > 1 ? "MSAA 4x" : "off",
                             resolution);
    for (u32 i = 0; i < workloads_step_count() && used < capacity; ++i) {
        const BenchmarkResult* r = &benchmark->finished[i];
        const FrameTimes* a = &r->average;
        used += (umm)snprintf(text + used, capacity - used,
                              "%-15s %6u  %6.2f  %8.2f  %6.0f  %4.2f  %5.2f  %4.2f  %4.2f  %5.2f  %6.2f  %7.2f  %5.2f  %9.2f  %5.0f\n",
                              workloads_step_name(i), r->frames, a->frame, r->worst_frame, stress_load(a), a->anim, a->scene,
                              a->draw, a->ui, a->gpu, a->gpu_shadow, a->gpu_upscale, a->gpu_bloom, a->gpu_particles, r->particles);
    }
}

// Measures the step that is on, and goes to the next when its time is up; after the last, the run starts again.
internal void benchmark_after_frame(Stress* stress)
{
    Benchmark* benchmark = &stress->benchmark;
    f64 now = nv_time_seconds();
    f64 elapsed = now - benchmark->step_start;
    if (elapsed < BENCHMARK_WARMUP)
        return;
    BenchmarkResult* result = &benchmark->results[benchmark->step];
    add_times(&result->average, &stress->times); // a sum until the step ends
    result->particles += (f64)nv_vfx_stats(&stress->vfx).alive;
    if (stress->times.frame > result->worst_frame)
        result->worst_frame = stress->times.frame;
    ++result->frames;
    if (elapsed < BENCHMARK_WARMUP + BENCHMARK_MEASURE)
        return;
    f64 n = (f64)result->frames;
    result->average = divide_times(&result->average, n);
    result->particles /= n;
    if (benchmark->step + 1 < workloads_step_count()) {
        start_step(stress, benchmark->step + 1, now);
        return;
    }
    memcpy(benchmark->finished, benchmark->results, sizeof(benchmark->finished));
    ++benchmark->runs;
    char table[BENCHMARK_TABLE_SIZE];
    benchmark_table_text(stress, table, sizeof(table));
    nv_log(NV_LOG_INFO, "stress", "%s", table);
    start_step(stress, 0, now);
}

//
// Panel
//

internal void stat_row(const char* label, const char* format, f64 value)
{
    igTableNextColumn();
    igTextUnformatted(label, NULL);
    igTableNextColumn();
    igText(format, value);
}

internal void stats_section(Stress* stress)
{
    const FrameTimes* a = &stress->shown_average;
    const NvRenderStats* r = &stress->renderer.stats;
    NvVfxStats effects = nv_vfx_stats(&stress->vfx);
    igSeparatorText("Frame (1 s average, ms)");
    // Label and value pairs: two per row where the panel is wide, one where it is not.
    int columns = igGetContentRegionAvail().x > 520.0f * stress->imgui.ui_scale ? 4 : 2;
    if (!igBeginTable("stats", columns, ImGuiTableFlags_SizingStretchProp, (ImVec2_c){0, 0}, 0.0f))
        return;
    stat_row("Frame", "%.2f", a->frame);
    stat_row("Load %", "%.0f", stress_load(a));
    stat_row("FPS", "%.0f", a->frame > 0.0 ? 1000.0 / a->frame : 0.0);
    stat_row("Worst frame", "%.2f", stress->shown_worst_frame);
    if (stress->gpu.has_timestamps) {
        stat_row("GPU scene pass", "%.2f", a->gpu);
        stat_row("GPU shadow pass", "%.2f", a->gpu_shadow);
        stat_row("GPU upscale pass", "%.2f", a->gpu_upscale);
        stat_row("GPU bloom passes", "%.2f", a->gpu_bloom);
        stat_row("GPU particle passes", "%.2f", a->gpu_particles);
    } else {
        igTableNextColumn();
        igTextUnformatted("GPU passes", NULL);
        igTableNextColumn();
        igTextUnformatted("no timestamps", NULL);
    }
    stat_row("CPU anim", "%.2f", a->anim);
    stat_row("CPU scene", "%.2f", a->scene);
    stat_row("CPU draw", "%.2f", a->draw);
    stat_row("CPU ui", "%.2f", a->ui);
    stat_row("Nodes", "%.0f", (f64)stress_live_nodes(stress->scene));
    stat_row("Draws", "%.0f", (f64)r->draws);
    stat_row("Triangles", "%.0f", (f64)r->triangles);
    stat_row("Skinned draws", "%.0f", (f64)r->skinned_draws);
    stat_row("Skin matrices", "%.0f", (f64)r->skin_matrices);
    stat_row("Debug lines", "%.0f", (f64)r->debug_lines);
    stat_row("Pipeline changes", "%.0f", (f64)r->pipeline_changes);
    stat_row("Material changes", "%.0f", (f64)r->material_changes);
    stat_row("Mesh changes", "%.0f", (f64)r->mesh_changes);
    stat_row("Shadow draws", "%.0f", (f64)r->shadow_draws);
    stat_row("Particles alive", "%.0f", (f64)effects.alive);
    stat_row("Particles visible", "%.0f", (f64)effects.visible);
    stat_row("Particles dropped", "%.0f", (f64)effects.dropped);
    stat_row("Segments", "%.0f", (f64)effects.segments);
    stat_row("Decals", "%.0f", (f64)effects.decals);
    igEndTable();
}

// The step that is on, then the results: the last whole run's, or this run's so far before the first has ended.
internal void benchmark_section(Stress* stress)
{
    Benchmark* benchmark = &stress->benchmark;
    u32 steps = workloads_step_count();
    igSeparatorText("Benchmark");
    f64 elapsed = nv_time_seconds() - benchmark->step_start;
    igText("Run %u, step %u/%u: %s (%s)", benchmark->runs + 1, benchmark->step + 1, steps, workloads_step_name(benchmark->step),
           elapsed < BENCHMARK_WARMUP ? "warming up" : "measuring");
    if (stress->workloads.character_failed)
        igTextDisabled("The character did not load: the crowd steps have no characters.");
    const BenchmarkResult* results = benchmark->runs ? benchmark->finished : benchmark->results;
    u32 count = benchmark->runs ? steps : benchmark->step;
    if (!count)
        return;
    igTextDisabled(benchmark->runs ? "The last whole run:" : "This run so far:");
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (!igBeginTable("results", 8, flags, (ImVec2_c){0, 0}, 0.0f))
        return;
    igTableSetupColumn("Step", 0, 0.0f, 0);
    igTableSetupColumn("Avg ms", 0, 0.0f, 0);
    igTableSetupColumn("Load %", 0, 0.0f, 0);
    igTableSetupColumn("Worst", 0, 0.0f, 0);
    igTableSetupColumn("CPU a/s/d", 0, 0.0f, 0);
    igTableSetupColumn("GPU sc/sh/up", 0, 0.0f, 0);
    igTableSetupColumn("GPU bl/pt", 0, 0.0f, 0);
    igTableSetupColumn("Alive", 0, 0.0f, 0);
    igTableHeadersRow();
    for (u32 i = 0; i < count; ++i) {
        const BenchmarkResult* r = &results[i];
        igTableNextRow(0, 0.0f);
        igTableNextColumn();
        igTextUnformatted(workloads_step_name(i), NULL);
        igTableNextColumn();
        igText("%.2f", r->average.frame);
        igTableNextColumn();
        igText("%.0f", stress_load(&r->average));
        igTableNextColumn();
        igText("%.2f", r->worst_frame);
        igTableNextColumn();
        igText("%.2f/%.2f/%.2f", r->average.anim, r->average.scene, r->average.draw);
        igTableNextColumn();
        if (stress->gpu.has_timestamps)
            igText("%.2f/%.2f/%.2f", r->average.gpu, r->average.gpu_shadow, r->average.gpu_upscale);
        else
            igTextDisabled("-");
        igTableNextColumn();
        if (stress->gpu.has_timestamps)
            igText("%.2f/%.2f", r->average.gpu_bloom, r->average.gpu_particles);
        else
            igTextDisabled("-");
        igTableNextColumn();
        igText("%.0f", r->particles);
    }
    igEndTable();
}

internal void build_ui(Stress* stress)
{
    char text[160];
    snprintf(text, sizeof(text), "%s build %s%s%s", NV_BUILD_NAME, NV_GIT_COMMIT, stress->download_text[0] ? " \xC2\xB7 " : "",
             stress->download_text);
    nv_imgui_draw_build_label(&stress->imgui, stress->viewport, text, 0.0f, "Commit: " NV_GIT_SUBJECT);
    if (nv_imgui_begin_panel(&stress->imgui, "Stress", stress->panel)) {
        benchmark_section(stress);
        stats_section(stress);
    }
    igEnd();
}

//
// Frame
//

// The panel on the right where the canvas is wide, at the bottom (40%) where it is tall; the viewport is the rest.
internal void layout(Stress* stress)
{
    f32 ratio = nv_window_pixel_ratio(&stress->window);
    f32 width_css = (f32)stress->gpu.width / ratio;
    f32 height_css = (f32)stress->gpu.height / ratio;
    if (width_css >= height_css) {
        f32 panel = PANEL_WIDTH * stress->imgui.ui_scale;
        if (panel > width_css * 0.5f)
            panel = width_css * 0.5f;
        stress->viewport = nv_window_framebuffer_rect_from_css(0.0f, 0.0f, width_css - panel, height_css, ratio);
        stress->panel = nv_window_framebuffer_rect_from_css(width_css - panel, 0.0f, width_css, height_css, ratio);
    } else {
        f32 top = height_css * 0.6f;
        stress->viewport = nv_window_framebuffer_rect_from_css(0.0f, 0.0f, width_css, top, ratio);
        stress->panel = nv_window_framebuffer_rect_from_css(0.0f, top, width_css, height_css, ratio);
    }
    stress->scene_output = nv_renderer_scene_output(&stress->resolution, stress->viewport);
}

// The camera needs no input, but a drag still orbits and pans it and the wheel and pinches dolly it, to look closer.
internal void apply_view_input(Stress* stress)
{
    const NvViewInput* in = &stress->imgui.view;
    nv_orbit_camera_turn(&stress->orbit, -in->orbit_x * ORBIT_RADIANS_PER_PIXEL, in->orbit_y * ORBIT_RADIANS_PER_PIXEL, in->dolly);
    if (in->pan_x != 0.0f || in->pan_y != 0.0f) {
        // The image's height in CSS pixels: the scene's height times the screen pixels each of its pixels takes.
        const NvSceneOutput* output = &stress->scene_output;
        f32 height = (f32)output->height * output->pixel_height / nv_window_pixel_ratio(&stress->window);
        NvVec3 move = nv_orbit_camera_pan(&stress->orbit, nv_scene_get(stress->scene, stress->camera), height, in->pan_x, in->pan_y);
        stress->orbit.target = nv_vec3_add(stress->orbit.target, move);
    }
}

internal void frame(void* userdata)
{
    Stress* stress = userdata;
    nv_log_pump();
    f64 now = nv_time_seconds();
    f32 real_dt = (f32)(now - stress->last_time);
    stress->last_time = now;

    NvGpuFrame gpu_frame = nv_gpu_begin_frame(&stress->gpu);
    if (!gpu_frame.ok)
        return;
    FrameTimes* times = &stress->times;
    times->frame = (f64)real_dt * 1000.0;

    f64 t = now_ms();
    layout(stress);
    stress->imgui.view_rect = stress->viewport;
    nv_imgui_new_frame(&stress->imgui, real_dt);
    build_ui(stress);
    times->ui = now_ms() - t;

    // The workloads, the animation and the effects advance by a fixed step, so each frame of a step is the same on every run;
    // the frame time is measured as it is.
    NvScene* scene = stress->scene;
    t = now_ms();
    workloads_update(stress, FIXED_DT);
    nv_vfx_update(&stress->vfx, FIXED_DT);
    times->scene = now_ms() - t;
    t = now_ms();
    nv_anim_update_scene(scene, FIXED_DT);
    times->anim = now_ms() - t;
    t = now_ms();
    nv_scene_update(scene);
    times->scene += now_ms() - t;
    apply_view_input(stress);
    nv_orbit_camera_place(&stress->orbit, nv_scene_get(scene, stress->camera)); // after nv_scene_update: it sets the world too
    workloads_after_update(stress);

    t = now_ms();
    NvRenderer* renderer = &stress->renderer;
    nv_renderer_draw(renderer, scene, nv_anim_skins(), stress->scene_output, gpu_frame.view);
    times->draw = now_ms() - t;
    times->gpu = renderer->gpu_ms;
    times->gpu_shadow = renderer->shadows.size ? renderer->gpu_shadow_ms : 0.0;
    times->gpu_upscale = renderer->gpu_upscale_ms;
    times->gpu_bloom = renderer->post.bloom ? renderer->gpu_bloom_ms : 0.0;
    times->gpu_particles = renderer->gpu_particles_ms;
    t = now_ms();
    nv_imgui_render(&stress->imgui, gpu_frame.view);
    times->ui += now_ms() - t;
    nv_gpu_end_frame(&stress->gpu);
    nv_renderer_end_frame(renderer);

    ++stress->frames;
    accumulate_times(stress, now);
    benchmark_after_frame(stress);
}

//
// Debug exports (docs/specs/samples.md): they read, they never set
//

#if NV_DEBUG
// 0 frames drawn, 1 nodes, 2 draws, 3 particles alive, 4 warnings and errors in the log.
NV_DEBUG_EXPORT int stress_debug_stat(int which)
{
    Stress* stress = &stress_state;
    switch (which) {
    case 0: return (int)stress->frames;
    case 1: return (int)stress_live_nodes(stress->scene);
    case 2: return (int)stress->renderer.stats.draws;
    case 3: return (int)nv_vfx_stats(&stress->vfx).alive;
    case 4: return (int)(nv_log_ring.level_counts[NV_LOG_WARNING] + nv_log_ring.level_counts[NV_LOG_ERROR]);
    }
    return -1;
}

// 0 cubes, 1 links, 2 characters shown.
NV_DEBUG_EXPORT int stress_debug_built(int which)
{
    const Workloads* workloads = &stress_state.workloads;
    switch (which) {
    case 0: return (int)workloads->cubes_built;
    case 1: return (int)workloads->links_built;
    case 2: return (int)workloads->characters_shown;
    }
    return -1;
}

// 0 runs finished, 1 the step that is on, 2 steps in a run, 3 frames the last whole run measured in all.
NV_DEBUG_EXPORT int stress_debug_benchmark(int which)
{
    const Benchmark* benchmark = &stress_state.benchmark;
    switch (which) {
    case 0: return (int)benchmark->runs;
    case 1: return (int)benchmark->step;
    case 2: return (int)workloads_step_count();
    case 3: {
        u32 frames = 0;
        for (u32 i = 0; i < workloads_step_count(); ++i)
            frames += benchmark->finished[i].frames;
        return (int)frames;
    }
    }
    return -1;
}
#endif

int main(void)
{
    Stress* stress = &stress_state;
    nv_arena_init(&stress->permanent, permanent_memory, sizeof(permanent_memory));
    nv_arena_init(&stress->scratch, scratch_memory, sizeof(scratch_memory));
    nv_arena_init(&stress->anim_memory, anim_memory, sizeof(anim_memory));

    nv_window_create(&stress->window, "nv stress sample");
    if (!nv_gpu_create(&stress->gpu, &stress->window)) {
        nv_log(NV_LOG_ERROR, "stress", "fatal: failed to initialize the GPU");
        return 1;
    }
    nv_renderer_init(&stress->renderer, &stress->gpu, &stress->permanent);
    nv_imgui_init(&stress->imgui, &stress->gpu, &stress->window, &stress->permanent);
    nv_imgui_load_ui_font(&stress->imgui, &stress->permanent);
    nv_window_download_text(stress->download_text, sizeof(stress->download_text));
    if (!stress->download_text[0]) // nothing was downloaded (a native build, whose files are on the disk): the label names the backend
        snprintf(stress->download_text, sizeof(stress->download_text), "%s", stress->gpu.backend_name);
    nv_anim_init(&stress->anim_memory);
    nv_vfx_init(&stress->vfx, &stress->gpu, (NvVfxCapacity){0}, &stress->permanent);
    stress->renderer.vfx = &stress->vfx;

    // The sandbox's settings: lighter on touch screens, where the GPU is the limit. Fixed: the benchmark measures with them.
    b32 touch = stress->imgui.ui_scale > 1.0f;
    stress->renderer.shadows = (NvShadowSettings){
        .size = touch ? 1024 : 2048,
        .format = NV_SHADOW_FORMAT_DEPTH32F,
        .filter = touch ? NV_SHADOW_FILTER_LOW : NV_SHADOW_FILTER_HIGH,
        .distance = 30.0f,
    };
    stress->renderer.msaa = 4;
    stress->renderer.post = (NvPostSettings){.tone = NV_TONE_PBR_NEUTRAL, .exposure = 1.0f, .bloom = 1, .bloom_intensity = 0.04f};
    stress->resolution = (NvResolution){.mode = NV_RESOLUTION_SCALE, .divisor = touch ? 2 : 1};

    stress->scene = NV_PUSH_STRUCT(&stress->permanent, NvScene);
    workloads_build(stress);

    stress->window_start = stress->last_time = nv_time_seconds();
    start_step(stress, 0, stress->last_time);
    nv_window_run(&stress->window, frame, stress);
    return 0;
}
