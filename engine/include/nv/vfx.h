#pragma once

// Visual effects (docs/specs/vfx.md): particles simulated by compute shaders. An effect is a group of
// emitters; firing one queues a burst per emitter, and each frame three compute passes spawn the
// bursts' particles from a free list (emit), age and move every living particle and cull it against
// the camera's frustum (simulate), and fill the indirect draw arguments (prepare). The scene pass
// then draws the visible particles with one indirect draw per blend mode. Particle state lives on
// the GPU only.
//
// Usage: nv_vfx_init, nv_vfx_add_effect for each effect at start, then every frame nv_vfx_update with
// the game clock's dt and nv_vfx_burst or nv_vfx_emit where something happens. Set NvRenderer.vfx and
// the renderer records the passes in nv_renderer_draw.

#include "nv/base.h"
#include "nv/gpu.h"
#include "nv/math.h"

#define NV_VFX_MAX_EFFECTS             1024
#define NV_VFX_MAX_EMITTERS            4096
#define NV_VFX_MAX_EMITTERS_PER_EFFECT 8
#define NV_VFX_MAX_BURSTS              8192             // bursts spawned in one frame
#define NV_VFX_MAX_DELAYED             4096             // bursts waiting for their delay
#define NV_VFX_MAX_PARTICLES           (65535u * 64u)   // a dispatch has at most 65535 workgroups of 64
#define NV_VFX_DEFAULT_PARTICLES       (2u * 1024u * 1024u)

typedef enum NvVfxBlend {
    NV_VFX_BLEND_ADD,   // light: order does not matter
    NV_VFX_BLEND_ALPHA, // smoke: unsorted, so overlapping particles can flicker (a known issue)
} NvVfxBlend;

typedef enum NvVfxShape {
    NV_VFX_SHAPE_DISC,   // a soft disc
    NV_VFX_SHAPE_RING,   // a ring, for shockwaves
    NV_VFX_SHAPE_STREAK, // a core with a tail, for sparks (stretch it along the velocity)
    NV_VFX_SHAPE_PUFF,   // a noisy blob, for smoke
} NvVfxShape;

// One stream of particles. Zero is a valid emitter that spawns nothing.
typedef struct NvVfxEmitterDesc {
    u32 count;                  // particles per burst
    f32 delay;                  // seconds after the effect fires
    f32 life_min, life_max;     // seconds
    f32 speed_min, speed_max;   // m/s
    f32 cone;                   // radians around the direction; pi is every direction
    f32 gravity;                // m/s^2, down
    f32 drag;                   // 1/s: the velocity decays by exp(-drag t)
    f32 turbulence;             // m/s^2 of curl noise
    f32 turbulence_scale;       // feature size of the noise, in 1/m
    b32 use_ground;             // collide with the plane y = ground
    f32 ground;
    f32 restitution;            // share of the speed into the ground that bounces back
    f32 friction;               // share of the speed along the ground lost at each bounce
    f32 size_start, size_end;   // half extent in meters, over the lifetime
    f32 spin;                   // largest rotation speed, rad/s
    f32 stretch;                // 0 faces the camera; else lengthens along the velocity per m/s
    f32 colors[3][4];           // start, middle, end: linear RGBA, may exceed 1 (bloom)
    NvVfxShape shape;
    NvVfxBlend blend;
} NvVfxEmitterDesc;

typedef struct NvVfxEffectDesc {
    char name[32];
    u32 emitter_count;
    NvVfxEmitterDesc emitters[NV_VFX_MAX_EMITTERS_PER_EFFECT];
} NvVfxEffectDesc;

typedef struct NvVfxEffectId { u32 index; } NvVfxEffectId; // 0 = none

// Chosen by the app per device; zero fields take the defaults.
typedef struct NvVfxCapacity {
    u32 particles; // alive at once, all effects combined (clamped to what the device can bind)
} NvVfxCapacity;

// What the GPU reported a frame or two ago.
typedef struct NvVfxStats {
    u32 alive;
    u32 visible;       // inside the camera's frustum, drawn
    u64 dropped;       // spawns refused because every slot was taken, ever
    u32 bursts_lost;   // bursts refused because a frame had too many, ever
} NvVfxStats;

// What the renderer gives the passes each frame.
typedef struct NvVfxFrame {
    NvMat4 view_proj;       // the scene pass's, reverse Z included
    NvVec3 camera_right, camera_up;
    u32 samples;            // of the scene pass
} NvVfxFrame;

typedef struct NvVfx {
    NvGpu* gpu;
    u32 capacity;           // particles
    f32 time;               // effect time in seconds: the sum of the dt given to nv_vfx_update
    f32 dt;
    NvVec3 wind;            // m/s^2 added to every particle's acceleration

    // Effects: slot 0 is none.
    struct NvVfxEffect {
        char name[32];
        u32 first_emitter, emitter_count;
    } effects[NV_VFX_MAX_EFFECTS];
    u32 effect_count;
    NvVfxEmitterDesc* emitter_descs; // [NV_VFX_MAX_EMITTERS]: count and delay are read on the CPU
    struct NvVfxGpuEmitter* gpu_emitters; // [NV_VFX_MAX_EMITTERS] as the shaders read them
    u32 emitter_count;
    b32 emitters_dirty;

    // This frame's bursts, and the ones waiting for their delay.
    struct NvVfxBurst* bursts;      // [NV_VFX_MAX_BURSTS]
    u32 burst_count;
    struct NvVfxDelayed* delayed;   // [NV_VFX_MAX_DELAYED]
    u32 delayed_count;
    u32* jobs;                      // [65535 * 2]: the burst and the first particle each workgroup spawns
    u32 random;                     // seeds the bursts
    b32 clear_requested;

    // GPU.
    WGPUBuffer frame_buffer, particles, free_list, alive[2], visible, counters, emitter_buffer, burst_buffer, job_buffer;
    WGPUBuffer sim_args, draw_args, stats_buffer;
    WGPUShaderModule compute_module, render_module;
    WGPUBindGroupLayout layouts[5];                // emit, simulate, prepare a, prepare b, init
    WGPUComputePipeline emit_pipeline, simulate_pipeline, prepare_a_pipeline, prepare_b_pipeline, init_pipeline;
    WGPUBindGroup emit_groups[2], simulate_groups[2]; // by which alive list is the current one
    WGPUBindGroup prepare_a_group, prepare_b_group, init_group;
    WGPUBindGroupLayout render_layout;
    WGPUBindGroup render_group;
    WGPURenderPipeline add_pipeline, alpha_pipeline;
    u32 pipeline_samples;                          // of the render pipelines that exist
    u32 parity;                                    // which alive list the next frame starts from

    NvVfxStats stats;
    b32 stats_mapping;
    b32 stats_copied;
} NvVfx;

// `capacity.particles` zero is NV_VFX_DEFAULT_PARTICLES. Allocates the CPU tables from `arena` and
// the buffers and pipelines on the GPU.
void nv_vfx_init(NvVfx* vfx, NvGpu* gpu, NvVfxCapacity capacity, NvArena* arena);

// Registers an effect; the id is valid for the rest of the run. An emitter past 8, or past the
// table's room, is dropped (with an assert in Debug).
NvVfxEffectId nv_vfx_add_effect(NvVfx* vfx, const NvVfxEffectDesc* desc);

// Advances the effect clock and queues the bursts whose delay is over. Pass the game clock's dt, so
// that a pause (0) holds the particles and slow motion slows them.
void nv_vfx_update(NvVfx* vfx, f32 dt);

// Fires every emitter of the effect at `position`, the particles leaving around `direction`
// (zero means up); `scale` multiplies their sizes and speeds.
void nv_vfx_burst(NvVfx* vfx, NvVfxEffectId effect, NvVec3 position, NvVec3 direction, f32 scale);

// Spawns `count` particles per emitter spread evenly along the stretch from `from` to `to`: a
// missile's smoke between where it was and where it is.
void nv_vfx_emit(NvVfx* vfx, NvVfxEffectId effect, NvVec3 from, NvVec3 to, u32 count);

// Removes every particle (Play and Stop) and forgets the bursts not yet spawned.
void nv_vfx_clear(NvVfx* vfx);

// The latest numbers the GPU reported.
NvVfxStats nv_vfx_stats(const NvVfx* vfx);

// Called by the renderer. Records the emit, simulate and prepare passes (`timestamps`, when not
// NULL, times them) after uploading what the CPU queued, and remakes the draw pipelines when the
// scene pass's sample count changed.
void nv_vfx_compute(NvVfx* vfx, WGPUCommandEncoder encoder, const NvVfxFrame* frame, const WGPUPassTimestampWrites* timestamps);

// Draws the visible particles into the scene pass: alpha blended, then additive.
void nv_vfx_draw(NvVfx* vfx, WGPURenderPassEncoder pass);

// Starts reading the GPU's counters back; call after the frame's commands are submitted.
void nv_vfx_end_frame(NvVfx* vfx);
