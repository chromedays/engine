#pragma once

// The stress sample (docs/specs/samples.md, docs/specs/stress.md): a standalone executable that loads the engine with many objects
// and measures the frame time as they grow. Its scene and logic are fixed in C and it needs no input: it runs its benchmark by
// itself, again and again, and shows the stats and the results. It depends on the engine only, like every sample. main.c is the
// shell (the frame, the camera, the panel, the benchmark), workloads.c the scene and what loads it.

#include <engine/anim.h>
#include <engine/camera.h>
#include <engine/gltf.h>
#include <engine/gpu.h>
#include <engine/imgui.h>
#include <engine/log.h>
#include <engine/renderer.h>
#include <engine/scene.h>
#include <engine/vfx.h>
#include <engine/window.h>

#define FIXED_DT (1.0f / 60.0f) // what the workloads, the animation and the effects advance by each frame

//
// The workloads (workloads.c)
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

// What a benchmark step sets (workloads.c). A count of 0 is off.
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
// The shell (main.c)
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

// main.c
f64 stress_load(const FrameTimes* times); // percent of the frame the busier of the CPU and the GPU took
u32 stress_live_nodes(const NvScene* scene);
void stress_reset(Stress* stress);

// workloads.c
void workloads_build(Stress* stress);
void workloads_reset(Stress* stress);
void workloads_update(Stress* stress, f32 dt);
void workloads_after_update(Stress* stress);
u32 workloads_step_count(void);
const char* workloads_step_name(u32 step);
void workloads_apply_step(Stress* stress, u32 step);
