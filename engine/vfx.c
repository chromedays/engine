#include "engine/vfx.h"
#include "engine/log.h"
#include "engine/vfx_cpu.h"
#include "shaders/vfx_compute_shader.h"
#include "shaders/vfx_marks_shader.h"
#include "shaders/vfx_particles_shader.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#if !defined(NDEBUG)
#include <stdlib.h> // the debug dump
#endif

// The shaders' structs, as they are laid out in memory.
typedef struct NvVfxGpuEmitter {
    f32 life[2], speed[2];
    f32 cone, gravity, drag, turbulence;
    f32 turbulence_scale, size_start, size_end, ground;
    f32 restitution, friction, stretch, spin;
    u32 shape, blend, pad[2];
    f32 colors[3][4];
} NvVfxGpuEmitter; // 128 bytes

typedef struct NvVfxDelayed {
    NvVfxBurst burst;
    f32 release; // effect time at which it is queued
} NvVfxDelayed;

typedef struct VfxFrameUniforms {
    NvMat4 view_proj;
    f32 planes[6][4];
    f32 camera_right[4];
    f32 camera_up[4];
    f32 times[4];
    f32 wind[4];
    u32 counts[4];
} VfxFrameUniforms; // 240 bytes

// What the segment and decal shaders read (docs/specs/vfx.md): written by the CPU into rings.
typedef struct NvVfxGpuSegment {
    f32 a[3];
    f32 birth;
    f32 b[3];
    f32 life;
    f32 color0[4], color1[4];
    f32 width0, width1, flicker, scroll;
} NvVfxGpuSegment; // 80 bytes

typedef struct NvVfxGpuDecal {
    f32 pos[3];
    f32 angle;
    f32 size, birth, life;
    u32 shape; // the shape in the low 8 bits, the fade share times 255 above
    f32 color[4];
} NvVfxGpuDecal; // 48 bytes

#if defined(NDEBUG)
#define DEBUG_COPY_SRC 0
#else
#define DEBUG_COPY_SRC NV_GPU_BUFFER_COPY_SRC // the debug dump reads these back
#endif
#define PARTICLE_BYTES 32
#define EMITTER_BYTES 128
#define COUNTERS_COUNT 16
#define MAX_JOBS 65535u
#define STAGE_SLOTS 4096u // segments or decals the CPU stages before one upload
#define STATS_SLOT 0       // the readback slot of the counters; the debug dump uses the ones after it

_Static_assert(sizeof(NvVfxGpuEmitter) == EMITTER_BYTES, "the shader reads 128-byte emitters");
_Static_assert(sizeof(NvVfxBurst) == 64, "the shader reads 64-byte bursts");
_Static_assert(sizeof(NvVfxGpuSegment) == 80, "the shader reads 80-byte segments");
_Static_assert(sizeof(NvVfxGpuDecal) == 48, "the shader reads 48-byte decals");
_Static_assert(sizeof(VfxFrameUniforms) == 240, "the shader reads a 240-byte frame");

// The shaders are engine/shaders/vfx_*.slang: the build makes vfx_compute_wgsl, vfx_particles_wgsl and vfx_marks_wgsl from them.

internal NvGpuBuffer create_buffer(NvGpu* gpu, const char* label, u32 usage, u64 size)
{
    return nv_gpu_create_buffer(gpu, &(NvGpuBufferDesc){.label = label, .usage = usage, .size = size});
}

typedef struct BindingKind {
    u32 binding;
    NvGpuBindingKind kind;
    u32 stride; // a storage binding's element, in bytes (the shader's StructuredBuffer<T>; the web backend does not need it)
} BindingKind;

internal NvGpuLayout create_layout(NvGpu* gpu, u32 stages, const BindingKind* kinds, u32 count)
{
    NvGpuLayoutEntry entries[NV_GPU_MAX_ENTRIES] = {0};
    NV_ASSERT(count <= NV_ARRAY_COUNT(entries));
    for (u32 i = 0; i < count; ++i) {
        entries[i].binding = kinds[i].binding;
        entries[i].stages = stages;
        entries[i].kind = kinds[i].kind;
        entries[i].stride = kinds[i].stride;
    }
    return nv_gpu_create_layout(gpu, entries, count);
}

internal NvGpuPipeline create_compute_pipeline(NvGpu* gpu, const char* entry, NvGpuLayout layout)
{
    return nv_gpu_create_compute_pipeline(gpu, &(NvGpuComputePipelineDesc){
        .label = entry,
        .shader = &vfx_compute_shader,
        .entry = entry,
        .layouts = {layout},
        .layout_count = 1,
    });
}

// A bind group of whole buffers: `buffers[i]` is bound at `kinds[i].binding`.
internal NvGpuGroup create_group(NvGpu* gpu, NvGpuLayout layout, const BindingKind* kinds, const NvGpuBuffer* buffers,
                                 const u64* sizes, u32 count)
{
    NvGpuGroupEntry entries[NV_GPU_MAX_ENTRIES] = {0};
    for (u32 i = 0; i < count; ++i) {
        entries[i].binding = kinds[i].binding;
        entries[i].buffer = buffers[i];
        entries[i].size = sizes[i];
    }
    return nv_gpu_create_group(gpu, layout, entries, count);
}

internal void create_render_pipelines(NvVfx* vfx, u32 samples)
{
    NvGpu* gpu = vfx->gpu;
    if (vfx->add_pipeline.index)
        nv_gpu_release_pipeline(gpu, vfx->add_pipeline);
    if (vfx->alpha_pipeline.index)
        nv_gpu_release_pipeline(gpu, vfx->alpha_pipeline);
    if (vfx->segment_pipeline.index)
        nv_gpu_release_pipeline(gpu, vfx->segment_pipeline);
    if (vfx->decal_pipeline.index)
        nv_gpu_release_pipeline(gpu, vfx->decal_pipeline);

    // All of them are tested against the scene's depth (reverse Z, so Greater) but never write to it.
    for (u32 alpha = 0; alpha < 2; ++alpha) {
        NvGpuPipeline pipeline = nv_gpu_create_render_pipeline(gpu, &(NvGpuRenderPipelineDesc){
            .label = alpha ? "particles (alpha)" : "particles (additive)",
            .shader = &vfx_particles_shader,
            .vertex_entry = alpha ? "vs_alpha" : "vs_add",
            .fragment_entry = alpha ? "fs_alpha" : "fs_add",
            .layouts = {vfx->render_layout},
            .layout_count = 1,
            .topology = NV_GPU_TOPOLOGY_TRIANGLES,
            .cull = NV_GPU_CULL_NONE,
            .color_format = NV_SCENE_FORMAT,
            .blend = alpha ? NV_GPU_BLEND_ALPHA : NV_GPU_BLEND_ADD,
            .depth_format = NV_SCENE_DEPTH_FORMAT,
            .depth_write = 0,
            .depth_compare = NV_GPU_COMPARE_GREATER,
            .samples = samples,
        });
        if (alpha)
            vfx->alpha_pipeline = pipeline;
        else
            vfx->add_pipeline = pipeline;
    }

    // Segments add light; decals blend.
    for (u32 decal = 0; decal < 2; ++decal) {
        NvGpuPipeline pipeline = nv_gpu_create_render_pipeline(gpu, &(NvGpuRenderPipelineDesc){
            .label = decal ? "decals" : "trails and beams",
            .shader = &vfx_marks_shader,
            .vertex_entry = decal ? "vs_decal" : "vs_segment",
            .fragment_entry = decal ? "fs_decal" : "fs_segment",
            .layouts = {vfx->marks_layout},
            .layout_count = 1,
            .topology = NV_GPU_TOPOLOGY_TRIANGLES,
            .cull = NV_GPU_CULL_NONE,
            .color_format = NV_SCENE_FORMAT,
            .blend = decal ? NV_GPU_BLEND_ALPHA : NV_GPU_BLEND_ADD,
            .depth_format = NV_SCENE_DEPTH_FORMAT,
            .depth_write = 0,
            .depth_compare = NV_GPU_COMPARE_GREATER,
            .samples = samples,
        });
        if (decal)
            vfx->decal_pipeline = pipeline;
        else
            vfx->segment_pipeline = pipeline;
    }
    vfx->pipeline_samples = samples;
}

// A ring of segments or decals: the CPU stages new slots and uploads them in runs, and when the ring
// is full the oldest are overwritten. `death` keeps when each slot expires, to count the live ones.
internal void ring_init(NvVfx* vfx, struct VfxRing* ring, const char* label, u32 capacity, u32 slot_bytes, NvArena* arena)
{
    ring->capacity = capacity;
    ring->staged = NV_PUSH_ARRAY(arena, (umm)STAGE_SLOTS * slot_bytes, u8);
    ring->death = NV_PUSH_ARRAY(arena, capacity, f32);
    ring->buffer = create_buffer(vfx->gpu, label, NV_GPU_BUFFER_STORAGE, (u64)capacity * slot_bytes);
}

internal void ring_flush(NvVfx* vfx, struct VfxRing* ring, u32 slot_bytes)
{
    if (!ring->pending)
        return;
    nv_gpu_write_buffer(vfx->gpu, ring->buffer, (u64)ring->pending_start * slot_bytes, ring->staged, (umm)ring->pending * slot_bytes);
    ring->pending = 0;
}

typedef struct RingSlot {
    b32 ok;
    void* at;
} RingSlot;

// The next slot to fill, staged (zeroed). The run being staged stays contiguous in the ring: it is
// uploaded when full and when the ring wrapped, before the slot after the wrap is staged. Fails
// before nv_vfx_init made the ring.
internal RingSlot ring_add(NvVfx* vfx, struct VfxRing* ring, u32 slot_bytes, f32 death)
{
    if (!ring->capacity)
        return (RingSlot){0};
    if (ring->pending && (ring->pending == STAGE_SLOTS || ring->head == 0))
        ring_flush(vfx, ring, slot_bytes);
    u32 index = nv_vfx_ring_take(&ring->head, &ring->filled, ring->capacity);
    if (!ring->pending)
        ring->pending_start = index;
    u8* slot = (u8*)ring->staged + (umm)ring->pending * slot_bytes;
    memset(slot, 0, slot_bytes);
    ++ring->pending;
    ring->death[index] = death;
    return (RingSlot){.ok = 1, .at = slot};
}

internal void ring_clear(struct VfxRing* ring)
{
    ring->head = ring->filled = ring->pending = 0;
    if (ring->death)
        memset(ring->death, 0, (umm)ring->capacity * sizeof(f32));
}

internal u32 ring_live(const struct VfxRing* ring, f32 time)
{
    u32 live = 0;
    for (u32 i = 0; i < ring->filled; ++i)
        live += ring->death[i] > time;
    return live;
}

void nv_vfx_init(NvVfx* vfx, NvGpu* gpu, NvVfxCapacity capacity, NvArena* arena)
{
    *vfx = (NvVfx){0};
    vfx->gpu = gpu;

    // The state buffer has to fit one binding.
    u32 particles = capacity.particles ? capacity.particles : NV_VFX_DEFAULT_PARTICLES;
    u64 room = (u64)NV_VFX_MAX_PARTICLES;
    if (gpu->max_storage_binding_bytes && gpu->max_buffer_bytes) {
        u64 binding = gpu->max_storage_binding_bytes / PARTICLE_BYTES;
        u64 buffer = gpu->max_buffer_bytes / PARTICLE_BYTES;
        room = binding < room ? binding : room;
        room = buffer < room ? buffer : room;
    }
    if (particles > room) {
        nv_log(NV_LOG_WARNING, "nv", "particles: %u asked for, %llu fit the device's buffer limits", particles, (unsigned long long)room);
        particles = (u32)room;
    }
    particles = (particles + 63u) & ~63u;
    vfx->capacity = particles;
    u32 segments = capacity.segments ? capacity.segments : NV_VFX_DEFAULT_SEGMENTS;
    u32 decals = capacity.decals ? capacity.decals : NV_VFX_DEFAULT_DECALS;
    {
        u64 binding = gpu->max_storage_binding_bytes ? gpu->max_storage_binding_bytes : (128ull << 20);
        if ((u64)segments * sizeof(NvVfxGpuSegment) > binding)
            segments = (u32)(binding / sizeof(NvVfxGpuSegment));
        if ((u64)decals * sizeof(NvVfxGpuDecal) > binding)
            decals = (u32)(binding / sizeof(NvVfxGpuDecal));
    }

    vfx->emitter_descs = NV_PUSH_ARRAY(arena, NV_VFX_MAX_EMITTERS, NvVfxEmitterDesc);
    vfx->gpu_emitters = NV_PUSH_ARRAY(arena, NV_VFX_MAX_EMITTERS, NvVfxGpuEmitter);
    vfx->bursts = NV_PUSH_ARRAY(arena, NV_VFX_MAX_BURSTS, NvVfxBurst);
    vfx->delayed = NV_PUSH_ARRAY(arena, NV_VFX_MAX_DELAYED, NvVfxDelayed);
    vfx->jobs = NV_PUSH_ARRAY(arena, MAX_JOBS * 2, u32);
    vfx->random = 0x2545F491u;

    u64 capacity_u64 = particles;
    vfx->frame_buffer = create_buffer(gpu, "vfx frame", NV_GPU_BUFFER_UNIFORM, sizeof(VfxFrameUniforms));
    vfx->particles = create_buffer(gpu, "particles", NV_GPU_BUFFER_STORAGE | DEBUG_COPY_SRC, capacity_u64 * PARTICLE_BYTES);
    vfx->free_list = create_buffer(gpu, "particle free list", NV_GPU_BUFFER_STORAGE | DEBUG_COPY_SRC, capacity_u64 * 4);
    vfx->alive[0] = create_buffer(gpu, "particle alive list A", NV_GPU_BUFFER_STORAGE | DEBUG_COPY_SRC, capacity_u64 * 4);
    vfx->alive[1] = create_buffer(gpu, "particle alive list B", NV_GPU_BUFFER_STORAGE | DEBUG_COPY_SRC, capacity_u64 * 4);
    vfx->visible = create_buffer(gpu, "particle visible list", NV_GPU_BUFFER_STORAGE, capacity_u64 * 4);
    vfx->counters = create_buffer(gpu, "particle counters", NV_GPU_BUFFER_STORAGE | NV_GPU_BUFFER_COPY_SRC, COUNTERS_COUNT * 4);
    vfx->emitter_buffer = create_buffer(gpu, "particle emitters", NV_GPU_BUFFER_STORAGE, (u64)NV_VFX_MAX_EMITTERS * EMITTER_BYTES);
    vfx->burst_buffer = create_buffer(gpu, "particle bursts", NV_GPU_BUFFER_STORAGE, (u64)NV_VFX_MAX_BURSTS * sizeof(NvVfxBurst));
    vfx->job_buffer = create_buffer(gpu, "particle spawn jobs", NV_GPU_BUFFER_STORAGE, (u64)MAX_JOBS * 8);
    vfx->sim_args = create_buffer(gpu, "particle simulate arguments", NV_GPU_BUFFER_STORAGE | NV_GPU_BUFFER_INDIRECT | DEBUG_COPY_SRC, 16);
    vfx->draw_args = create_buffer(gpu, "particle draw arguments", NV_GPU_BUFFER_STORAGE | NV_GPU_BUFFER_INDIRECT | DEBUG_COPY_SRC, 32);

    ring_init(vfx, &vfx->segments, "trail and beam segments", segments, sizeof(NvVfxGpuSegment), arena);
    ring_init(vfx, &vfx->decals, "decals", decals, sizeof(NvVfxGpuDecal), arena);

    const NvGpuBindingKind U = NV_GPU_BINDING_UNIFORM, RW = NV_GPU_BINDING_STORAGE_READ_WRITE, RO = NV_GPU_BINDING_STORAGE_READ;
    // Each pass binds only what it uses, so the indirect arguments are never bound where they are also read as arguments.
    const BindingKind emit_kinds[] = {{0, U, 0}, {1, RW, PARTICLE_BYTES}, {2, RW, 4}, {3, RW, 4}, {6, RW, 4}, {7, RO, EMITTER_BYTES}, {8, RO, sizeof(NvVfxBurst)}, {9, RO, 8}};
    const BindingKind simulate_kinds[] = {{0, U, 0}, {1, RW, PARTICLE_BYTES}, {2, RW, 4}, {3, RW, 4}, {4, RW, 4}, {5, RW, 4}, {6, RW, 4}, {7, RO, EMITTER_BYTES}};
    const BindingKind prepare_a_kinds[] = {{6, RW, 4}, {10, RW, 4}};
    const BindingKind prepare_b_kinds[] = {{6, RW, 4}, {11, RW, 4}};
    const BindingKind init_kinds[] = {{0, U, 0}, {2, RW, 4}, {6, RW, 4}};
    const BindingKind render_kinds[] = {{0, U, 0}, {1, RO, PARTICLE_BYTES}, {2, RO, 4}, {3, RO, EMITTER_BYTES}};
    const BindingKind marks_kinds[] = {{0, U, 0}, {1, RO, sizeof(NvVfxGpuSegment)}, {2, RO, sizeof(NvVfxGpuDecal)}};
    vfx->marks_layout = create_layout(gpu, NV_GPU_STAGE_VERTEX, marks_kinds, NV_ARRAY_COUNT(marks_kinds));
    vfx->layouts[0] = create_layout(gpu, NV_GPU_STAGE_COMPUTE, emit_kinds, NV_ARRAY_COUNT(emit_kinds));
    vfx->layouts[1] = create_layout(gpu, NV_GPU_STAGE_COMPUTE, simulate_kinds, NV_ARRAY_COUNT(simulate_kinds));
    vfx->layouts[2] = create_layout(gpu, NV_GPU_STAGE_COMPUTE, prepare_a_kinds, NV_ARRAY_COUNT(prepare_a_kinds));
    vfx->layouts[3] = create_layout(gpu, NV_GPU_STAGE_COMPUTE, prepare_b_kinds, NV_ARRAY_COUNT(prepare_b_kinds));
    vfx->layouts[4] = create_layout(gpu, NV_GPU_STAGE_COMPUTE, init_kinds, NV_ARRAY_COUNT(init_kinds));
    vfx->render_layout = create_layout(gpu, NV_GPU_STAGE_VERTEX, render_kinds, NV_ARRAY_COUNT(render_kinds));
    vfx->emit_pipeline = create_compute_pipeline(gpu, "emit", vfx->layouts[0]);
    vfx->simulate_pipeline = create_compute_pipeline(gpu, "simulate", vfx->layouts[1]);
    vfx->prepare_a_pipeline = create_compute_pipeline(gpu, "prepare_a", vfx->layouts[2]);
    vfx->prepare_b_pipeline = create_compute_pipeline(gpu, "prepare_b", vfx->layouts[3]);
    vfx->init_pipeline = create_compute_pipeline(gpu, "init_free", vfx->layouts[4]);

    const u64 list = capacity_u64 * 4, state = capacity_u64 * PARTICLE_BYTES, emitters = (u64)NV_VFX_MAX_EMITTERS * EMITTER_BYTES;
    for (u32 parity = 0; parity < 2; ++parity) {
        NvGpuBuffer cur = vfx->alive[parity], next = vfx->alive[parity ^ 1];
        const NvGpuBuffer emit_buffers[] = {vfx->frame_buffer, vfx->particles, vfx->free_list, cur, vfx->counters, vfx->emitter_buffer,
                                            vfx->burst_buffer, vfx->job_buffer};
        const u64 emit_sizes[] = {sizeof(VfxFrameUniforms), state, list, list, COUNTERS_COUNT * 4, emitters,
                                  (u64)NV_VFX_MAX_BURSTS * sizeof(NvVfxBurst), (u64)MAX_JOBS * 8};
        vfx->emit_groups[parity] = create_group(gpu, vfx->layouts[0], emit_kinds, emit_buffers, emit_sizes, NV_ARRAY_COUNT(emit_kinds));
        const NvGpuBuffer simulate_buffers[] = {vfx->frame_buffer, vfx->particles, vfx->free_list, cur, next, vfx->visible, vfx->counters,
                                                vfx->emitter_buffer};
        const u64 simulate_sizes[] = {sizeof(VfxFrameUniforms), state, list, list, list, list, COUNTERS_COUNT * 4, emitters};
        vfx->simulate_groups[parity] = create_group(gpu, vfx->layouts[1], simulate_kinds, simulate_buffers, simulate_sizes,
                                                    NV_ARRAY_COUNT(simulate_kinds));
    }
    {
        const NvGpuBuffer buffers[] = {vfx->counters, vfx->sim_args};
        const u64 sizes[] = {COUNTERS_COUNT * 4, 16};
        vfx->prepare_a_group = create_group(gpu, vfx->layouts[2], prepare_a_kinds, buffers, sizes, 2);
    }
    {
        const NvGpuBuffer buffers[] = {vfx->counters, vfx->draw_args};
        const u64 sizes[] = {COUNTERS_COUNT * 4, 32};
        vfx->prepare_b_group = create_group(gpu, vfx->layouts[3], prepare_b_kinds, buffers, sizes, 2);
    }
    {
        const NvGpuBuffer buffers[] = {vfx->frame_buffer, vfx->free_list, vfx->counters};
        const u64 sizes[] = {sizeof(VfxFrameUniforms), list, COUNTERS_COUNT * 4};
        vfx->init_group = create_group(gpu, vfx->layouts[4], init_kinds, buffers, sizes, 3);
    }
    {
        const NvGpuBuffer buffers[] = {vfx->frame_buffer, vfx->particles, vfx->visible, vfx->emitter_buffer};
        const u64 sizes[] = {sizeof(VfxFrameUniforms), state, list, emitters};
        vfx->render_group = create_group(gpu, vfx->render_layout, render_kinds, buffers, sizes, 4);
    }

    {
        const NvGpuBuffer buffers[] = {vfx->frame_buffer, vfx->segments.buffer, vfx->decals.buffer};
        const u64 sizes[] = {sizeof(VfxFrameUniforms), (u64)segments * sizeof(NvVfxGpuSegment), (u64)decals * sizeof(NvVfxGpuDecal)};
        vfx->marks_group = create_group(gpu, vfx->marks_layout, marks_kinds, buffers, sizes, 3);
    }

    vfx->clear_requested = 1; // fills the free list on the first frame
    vfx->effect_count = 1;    // slot 0 is none
    f64 megabytes = (f64)capacity_u64 * (PARTICLE_BYTES + 4 * 4) / (1024.0 * 1024.0);
    nv_log(NV_LOG_INFO, "nv", "particles: %u alive at most, %.1f MB of GPU buffers (state %u bytes, lists 16 bytes each), simulated by compute", particles, megabytes,
           PARTICLE_BYTES);
    nv_log(NV_LOG_INFO, "nv", "trails, beams and decals: %u segments, %u decals kept, %.1f MB", segments, decals,
           ((f64)segments * sizeof(NvVfxGpuSegment) + (f64)decals * sizeof(NvVfxGpuDecal)) / (1024.0 * 1024.0));
}

NvVfxEffectId nv_vfx_add_effect(NvVfx* vfx, const NvVfxEffectDesc* desc)
{
    NV_ASSERT(vfx->effect_count < NV_VFX_MAX_EFFECTS);
    NV_ASSERT(desc->emitter_count <= NV_VFX_MAX_EMITTERS_PER_EFFECT);
    if (vfx->effect_count >= NV_VFX_MAX_EFFECTS)
        return (NvVfxEffectId){0};
    u32 count = desc->emitter_count < NV_VFX_MAX_EMITTERS_PER_EFFECT ? desc->emitter_count : NV_VFX_MAX_EMITTERS_PER_EFFECT;
    NV_ASSERT(vfx->emitter_count + count <= NV_VFX_MAX_EMITTERS);
    if (vfx->emitter_count + count > NV_VFX_MAX_EMITTERS)
        return (NvVfxEffectId){0};

    struct NvVfxEffect* effect = &vfx->effects[vfx->effect_count];
    snprintf(effect->name, sizeof(effect->name), "%s", desc->name);
    effect->first_emitter = vfx->emitter_count;
    effect->emitter_count = count;
    for (u32 i = 0; i < count; ++i) {
        const NvVfxEmitterDesc* in = &desc->emitters[i];
        u32 slot = vfx->emitter_count++;
        vfx->emitter_descs[slot] = *in;
        NvVfxGpuEmitter* out = &vfx->gpu_emitters[slot];
        *out = (NvVfxGpuEmitter){0};
        out->life[0] = in->life_min;
        out->life[1] = in->life_max > in->life_min ? in->life_max : in->life_min;
        out->speed[0] = in->speed_min;
        out->speed[1] = in->speed_max > in->speed_min ? in->speed_max : in->speed_min;
        out->cone = in->cone;
        out->gravity = in->gravity;
        out->drag = in->drag;
        out->turbulence = in->turbulence;
        out->turbulence_scale = in->turbulence_scale;
        out->size_start = in->size_start;
        out->size_end = in->size_end;
        out->ground = in->use_ground ? in->ground : -1.0e30f;
        out->restitution = in->restitution;
        out->friction = in->friction;
        out->stretch = in->stretch;
        out->spin = in->spin;
        out->shape = (u32)in->shape;
        out->blend = (u32)in->blend;
        memcpy(out->colors, in->colors, sizeof(out->colors));
    }
    vfx->emitters_dirty = 1;
    return (NvVfxEffectId){vfx->effect_count++};
}

internal u32 next_random(NvVfx* vfx)
{
    u32 x = vfx->random;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    vfx->random = x;
    return x;
}

internal void queue_burst(NvVfx* vfx, const NvVfxBurst* burst, f32 delay)
{
    if (delay > 0.0f) {
        if (vfx->delayed_count >= NV_VFX_MAX_DELAYED) {
            ++vfx->stats.bursts_lost;
            return;
        }
        vfx->delayed[vfx->delayed_count++] = (NvVfxDelayed){*burst, vfx->time + delay};
        return;
    }
    if (vfx->burst_count >= NV_VFX_MAX_BURSTS) {
        ++vfx->stats.bursts_lost;
        return;
    }
    vfx->bursts[vfx->burst_count++] = *burst;
}

internal void fire(NvVfx* vfx, NvVfxEffectId id, NvVec3 from, NvVec3 to, NvVec3 direction, f32 scale, u32 count_override)
{
    if (!id.index || id.index >= vfx->effect_count)
        return;
    const struct NvVfxEffect* effect = &vfx->effects[id.index];
    for (u32 i = 0; i < effect->emitter_count; ++i) {
        u32 slot = effect->first_emitter + i;
        const NvVfxEmitterDesc* desc = &vfx->emitter_descs[slot];
        u32 count = count_override ? count_override : desc->count;
        if (!count)
            continue;
        NvVfxBurst burst = {
            .pos = {from.x, from.y, from.z},
            .scale = scale > 0.0f ? scale : 1.0f,
            .dir = {direction.x, direction.y, direction.z},
            .count = count,
            .end = {to.x, to.y, to.z},
            .emitter = slot,
            .seed = next_random(vfx),
        };
        queue_burst(vfx, &burst, desc->delay);
    }
}

void nv_vfx_burst(NvVfx* vfx, NvVfxEffectId effect, NvVec3 position, NvVec3 direction, f32 scale)
{
    fire(vfx, effect, position, position, direction, scale, 0);
}

void nv_vfx_emit(NvVfx* vfx, NvVfxEffectId effect, NvVec3 from, NvVec3 to, u32 count)
{
    fire(vfx, effect, from, to, nv_vec3(0.0f, 1.0f, 0.0f), 1.0f, count ? count : 1);
}

internal void add_segment(NvVfx* vfx, const NvVfxLineStyle* style, NvVec3 from, NvVec3 to, f32 life)
{
    if (!(life > 0.0f))
        return;
    RingSlot slot = ring_add(vfx, &vfx->segments, sizeof(NvVfxGpuSegment), vfx->time + life);
    if (!slot.ok)
        return;
    NvVfxGpuSegment* segment = slot.at;
    segment->a[0] = from.x, segment->a[1] = from.y, segment->a[2] = from.z;
    segment->b[0] = to.x, segment->b[1] = to.y, segment->b[2] = to.z;
    segment->birth = vfx->time;
    segment->life = life;
    memcpy(segment->color0, style->colors[0], sizeof(segment->color0));
    memcpy(segment->color1, style->colors[1], sizeof(segment->color1));
    segment->width0 = style->width_start;
    segment->width1 = style->width_end;
    segment->flicker = style->flicker;
    segment->scroll = style->scroll;
}

void nv_vfx_trail(NvVfx* vfx, const NvVfxLineStyle* style, NvVec3 from, NvVec3 to)
{
    add_segment(vfx, style, from, to, style->life);
}

void nv_vfx_beam(NvVfx* vfx, const NvVfxLineStyle* style, NvVec3 from, NvVec3 to, f32 seconds)
{
    add_segment(vfx, style, from, to, seconds);
}

void nv_vfx_decal(NvVfx* vfx, const NvVfxDecalStyle* style, NvVec3 position, f32 angle, f32 size)
{
    if (!(style->life > 0.0f))
        return;
    RingSlot slot = ring_add(vfx, &vfx->decals, sizeof(NvVfxGpuDecal), vfx->time + style->life);
    if (!slot.ok)
        return;
    NvVfxGpuDecal* decal = slot.at;
    decal->pos[0] = position.x, decal->pos[1] = position.y, decal->pos[2] = position.z;
    decal->angle = angle;
    decal->size = size;
    decal->birth = vfx->time;
    decal->life = style->life;
    f32 fade = style->fade < 0.0f ? 0.0f : style->fade > 1.0f ? 1.0f : style->fade;
    decal->shape = (u32)style->shape | ((u32)(fade * 255.0f) << 8);
    memcpy(decal->color, style->color, sizeof(decal->color));
}

void nv_vfx_update(NvVfx* vfx, f32 dt)
{
    // A tab that was hidden comes back with a long dt: do not age every particle at once.
    if (dt > 0.1f)
        dt = 0.1f;
    if (dt < 0.0f)
        dt = 0.0f;
    vfx->dt = dt;
    vfx->time += dt;
    if (vfx->time > 1.0e5f) {
        // Keeps the float's precision. The delayed bursts are relative to the clock, so they are released
        // (below), and the segments and decals, whose times are of the old clock, are dropped.
        vfx->time = 0.0f;
        ring_clear(&vfx->segments);
        ring_clear(&vfx->decals);
    }
    for (u32 i = 0; i < vfx->delayed_count;) {
        if (vfx->delayed[i].release <= vfx->time || vfx->time == 0.0f) {
            NvVfxBurst burst = vfx->delayed[i].burst;
            vfx->delayed[i] = vfx->delayed[--vfx->delayed_count];
            queue_burst(vfx, &burst, 0.0f);
        } else {
            ++i;
        }
    }
}

void nv_vfx_clear(NvVfx* vfx)
{
    vfx->burst_count = 0;
    vfx->delayed_count = 0;
    vfx->clear_requested = 1;
    ring_clear(&vfx->segments);
    ring_clear(&vfx->decals);
}

// The six planes of the frustum of `m` (column major, clip = m * point), each normalized and
// pointing inside: a point is inside when dot(plane.xyz, p) + plane.w >= 0 for all of them.
internal void frustum_planes(NvMat4 m, f32 planes[6][4])
{
    f32 row[4][4];
    for (u32 r = 0; r < 4; ++r)
        for (u32 c = 0; c < 4; ++c)
            row[r][c] = m.e[c * 4 + r];
    f32 combos[6][4];
    for (u32 c = 0; c < 4; ++c) {
        combos[0][c] = row[3][c] + row[0][c]; // left
        combos[1][c] = row[3][c] - row[0][c]; // right
        combos[2][c] = row[3][c] + row[1][c]; // bottom
        combos[3][c] = row[3][c] - row[1][c]; // top
        combos[4][c] = row[2][c];             // depth 0
        combos[5][c] = row[3][c] - row[2][c]; // depth 1
    }
    for (u32 i = 0; i < 6; ++i) {
        f32 length = sqrtf(combos[i][0] * combos[i][0] + combos[i][1] * combos[i][1] + combos[i][2] * combos[i][2]);
        f32 inverse = length > 1.0e-6f ? 1.0f / length : 1.0f;
        for (u32 c = 0; c < 4; ++c)
            planes[i][c] = combos[i][c] * inverse;
    }
}

// Takes the counters the GPU reported, if they have arrived.
internal void take_stats(NvVfx* vfx)
{
    NvGpuReadback readback = nv_gpu_poll_readback(vfx->gpu, STATS_SLOT);
    if (!readback.ok)
        return;
    const u32* values = readback.bytes;
    vfx->stats.alive = values[0];
    vfx->stats.visible = values[1];
    vfx->stats.dropped = values[2];
}

NvVfxStats nv_vfx_stats(NvVfx* vfx)
{
    take_stats(vfx);
    return vfx->stats;
}

void nv_vfx_compute(NvVfx* vfx, const NvVfxFrame* frame)
{
    NvGpu* gpu = vfx->gpu;
    if (vfx->pipeline_samples != frame->samples)
        create_render_pipelines(vfx, frame->samples);

    ring_flush(vfx, &vfx->segments, sizeof(NvVfxGpuSegment));
    ring_flush(vfx, &vfx->decals, sizeof(NvVfxGpuDecal));

    VfxFrameUniforms uniforms = {0};
    uniforms.view_proj = frame->view_proj;
    frustum_planes(frame->view_proj, uniforms.planes);
    uniforms.camera_right[0] = frame->camera_right.x;
    uniforms.camera_right[1] = frame->camera_right.y;
    uniforms.camera_right[2] = frame->camera_right.z;
    uniforms.camera_up[0] = frame->camera_up.x;
    uniforms.camera_up[1] = frame->camera_up.y;
    uniforms.camera_up[2] = frame->camera_up.z;
    uniforms.times[0] = vfx->dt;
    uniforms.times[1] = vfx->time;
    uniforms.wind[0] = vfx->wind.x;
    uniforms.wind[1] = vfx->wind.y;
    uniforms.wind[2] = vfx->wind.z;
    uniforms.counts[0] = vfx->capacity;
    nv_gpu_write_buffer(gpu, vfx->frame_buffer, 0, &uniforms, sizeof(uniforms));
    if (vfx->emitters_dirty) {
        nv_gpu_write_buffer(gpu, vfx->emitter_buffer, 0, vfx->gpu_emitters, (umm)vfx->emitter_count * EMITTER_BYTES);
        vfx->emitters_dirty = 0;
    }

    // One job per workgroup of 64 particles: which burst, and the first particle of the burst it spawns.
    u32 bursts_used = 0;
    u32 job_count = nv_vfx_split_bursts(vfx->bursts, vfx->burst_count, MAX_JOBS, vfx->jobs, &bursts_used, &vfx->stats.bursts_lost);
    if (job_count) {
        nv_gpu_write_buffer(gpu, vfx->burst_buffer, 0, vfx->bursts, (umm)bursts_used * sizeof(NvVfxBurst));
        nv_gpu_write_buffer(gpu, vfx->job_buffer, 0, vfx->jobs, (umm)job_count * 8);
    }

    nv_gpu_begin_compute_pass(gpu, &(NvGpuComputePassDesc){
        .label = "particles",
        .timestamps = frame->timestamps,
        .timestamp_begin = frame->timestamp_begin,
        .timestamp_end = frame->timestamp_end,
    });
    if (vfx->clear_requested) {
        nv_gpu_set_pipeline(gpu, vfx->init_pipeline);
        nv_gpu_set_group(gpu, 0, vfx->init_group, NULL, 0);
        nv_gpu_dispatch(gpu, (vfx->capacity + 63u) / 64u, 1, 1);
        vfx->clear_requested = 0;
    }
    if (job_count) {
        nv_gpu_set_pipeline(gpu, vfx->emit_pipeline);
        nv_gpu_set_group(gpu, 0, vfx->emit_groups[vfx->parity], NULL, 0);
        nv_gpu_dispatch(gpu, job_count, 1, 1);
    }
    nv_gpu_set_pipeline(gpu, vfx->prepare_a_pipeline);
    nv_gpu_set_group(gpu, 0, vfx->prepare_a_group, NULL, 0);
    nv_gpu_dispatch(gpu, 1, 1, 1);
    nv_gpu_set_pipeline(gpu, vfx->simulate_pipeline);
    nv_gpu_set_group(gpu, 0, vfx->simulate_groups[vfx->parity], NULL, 0);
    nv_gpu_dispatch_indirect(gpu, vfx->sim_args, 0);
    nv_gpu_set_pipeline(gpu, vfx->prepare_b_pipeline);
    nv_gpu_set_group(gpu, 0, vfx->prepare_b_group, NULL, 0);
    nv_gpu_dispatch(gpu, 1, 1, 1);
    nv_gpu_end_pass(gpu);

    vfx->burst_count = 0;
    vfx->parity ^= 1u;

    // The counters prepare reported, for the CPU, unless the last copy has not been taken yet.
    take_stats(vfx);
    nv_gpu_copy_for_readback(gpu, STATS_SLOT, vfx->counters, 8 * 4, 16);
}

void nv_vfx_draw_decals(NvVfx* vfx)
{
    if (!vfx->decals.filled)
        return;
    nv_gpu_set_pipeline(vfx->gpu, vfx->decal_pipeline);
    nv_gpu_set_group(vfx->gpu, 0, vfx->marks_group, NULL, 0);
    nv_gpu_draw(vfx->gpu, 6, vfx->decals.filled, 0, 0);
}

void nv_vfx_draw(NvVfx* vfx)
{
    NvGpu* gpu = vfx->gpu;
    if (vfx->segments.filled) {
        nv_gpu_set_pipeline(gpu, vfx->segment_pipeline);
        nv_gpu_set_group(gpu, 0, vfx->marks_group, NULL, 0);
        nv_gpu_draw(gpu, 6, vfx->segments.filled, 0, 0);
    }
    nv_gpu_set_group(gpu, 0, vfx->render_group, NULL, 0);
    // Alpha first (words 4 to 7 of the arguments), then additive (0 to 3).
    nv_gpu_set_pipeline(gpu, vfx->alpha_pipeline);
    nv_gpu_draw_indirect(gpu, vfx->draw_args, 16);
    nv_gpu_set_pipeline(gpu, vfx->add_pipeline);
    nv_gpu_draw_indirect(gpu, vfx->draw_args, 0);
}

void nv_vfx_end_frame(NvVfx* vfx)
{
    // Counting the live segments walks a ring of up to hundreds of thousands: now and then is enough.
    if ((vfx->marks_frame++ & 7u) == 0) {
        vfx->stats.segments = ring_live(&vfx->segments, vfx->time);
        vfx->stats.decals = ring_live(&vfx->decals, vfx->time);
    }
}

#if !defined(NDEBUG)
// Debug dump for the regression tests (engine/vfx.h). Everything it needs is copied into readback slots between frames
// (nv_gpu_copy_for_readback), so it sees the state after the last submitted frame. The readbacks arrive on the browser's event
// loop and are taken when the state is asked for; the frame never waits.
typedef struct NvVfxDumpParticle {
    u32 emitter, seed;
    f32 v[10]; // position, age, velocity, life, scale, rotation
} NvVfxDumpParticle;

typedef struct NvVfxDumpSource {
    u32 slot; // the readback slot
    const u8* mapped;
} NvVfxDumpSource;

struct NvVfxDump {
    NvVfx* vfx;
    b32 full;
    NvVfxDumpState state;
    u32 pending;
    NvVfxDumpSource counters, sim_args, draw_args, free_list, alive, particles;
    u32 counter_values[COUNTERS_COUNT];
    u32 sim[4], draw[8];
    u32 violations;
    u32 particle_count;
    NvVfxDumpParticle* particles_sorted;
};

internal f32 half_to_float(u32 h)
{
    u32 sign = (h >> 15) & 1u, exponent = (h >> 10) & 31u, fraction = h & 1023u;
    f32 value;
    if (exponent == 0)
        value = ldexpf((f32)fraction, -24);
    else if (exponent == 31)
        value = fraction ? NAN : INFINITY;
    else
        value = ldexpf((f32)(fraction | 1024u), (int)exponent - 25);
    return sign ? -value : value;
}

internal int compare_dump_particles(const void* a, const void* b)
{
    const NvVfxDumpParticle* x = a;
    const NvVfxDumpParticle* y = b;
    if (x->emitter != y->emitter) return x->emitter < y->emitter ? -1 : 1;
    if (x->seed != y->seed) return x->seed < y->seed ? -1 : 1;
    for (u32 i = 0; i < 10; ++i)
        if (x->v[i] != y->v[i]) return x->v[i] < y->v[i] ? -1 : 1;
    return 0;
}

internal void dump_process(struct NvVfxDump* dump)
{
    NvVfx* vfx = dump->vfx;
    memcpy(dump->counter_values, dump->counters.mapped, sizeof(dump->counter_values));
    memcpy(dump->sim, dump->sim_args.mapped, 16);
    memcpy(dump->draw, dump->draw_args.mapped, 32);
    const u32* c = dump->counter_values;
    enum { C_FREE_ = 0, C_ALIVE_ = 1, C_VIS_ADD_ = 3, C_VIS_ALPHA_ = 4, C_DROPPED_ = 5, C_STAT_ALIVE_ = 8, C_STAT_VISIBLE_ = 9, C_STAT_DROPPED_ = 10 };
    u32 violations = 0;
    u32 free_count = c[C_FREE_], alive_count = c[C_ALIVE_];
    u32 visible = c[C_VIS_ADD_] + c[C_VIS_ALPHA_];
    if (dump->full && free_count + alive_count != vfx->capacity)
        violations |= 1u;
    if (visible > alive_count)
        violations |= 4u;
    // The draws take their vertex counts from the counters and draw one instance each.
    if (dump->draw[0] != c[C_VIS_ADD_] * 6u || dump->draw[1] != 1u || dump->draw[2] != 0u || dump->draw[3] != 0u ||
        dump->draw[4] != c[C_VIS_ALPHA_] * 6u || dump->draw[5] != 1u || dump->draw[6] != 0u || dump->draw[7] != 0u)
        violations |= 16u;
    if (c[C_STAT_ALIVE_] != alive_count || c[C_STAT_VISIBLE_] != visible || c[C_STAT_DROPPED_] != c[C_DROPPED_] || dump->sim[1] != 1u ||
        dump->sim[2] != 1u)
        violations |= 32u;

    if (dump->full && free_count <= vfx->capacity && alive_count <= vfx->capacity) {
        const u32* free_list = (const u32*)dump->free_list.mapped;
        const u32* alive_list = (const u32*)dump->alive.mapped;
        const u32* state = (const u32*)dump->particles.mapped;
        u8* seen = calloc(vfx->capacity, 1);
        for (u32 i = 0; i < free_count; ++i) {
            if (free_list[i] >= vfx->capacity || seen[free_list[i]]++)
                violations |= 2u;
        }
        dump->particles_sorted = malloc((umm)(alive_count ? alive_count : 1) * sizeof(NvVfxDumpParticle));
        for (u32 i = 0; i < alive_count; ++i) {
            u32 slot = alive_list[i];
            if (slot >= vfx->capacity || seen[slot]++) {
                violations |= 2u;
                continue;
            }
            const u32* word = state + (umm)slot * (PARTICLE_BYTES / 4);
            NvVfxDumpParticle* out = &dump->particles_sorted[dump->particle_count++];
            f32 pos[3], age;
            memcpy(pos, word, 12);
            memcpy(&age, word + 3, 4);
            out->emitter = word[6] & 0xFFFu;
            out->seed = word[6] >> 12;
            out->v[0] = pos[0]; out->v[1] = pos[1]; out->v[2] = pos[2];
            out->v[3] = age;
            out->v[4] = half_to_float(word[4] & 0xFFFFu);
            out->v[5] = half_to_float(word[4] >> 16);
            out->v[6] = half_to_float(word[5] & 0xFFFFu);
            out->v[7] = half_to_float(word[5] >> 16); // life
            out->v[8] = half_to_float(word[7] & 0xFFFFu);
            out->v[9] = half_to_float(word[7] >> 16);
            if (!(age < out->v[7]))
                violations |= 8u;
        }
        for (u32 i = 0; i < vfx->capacity; ++i)
            if (!seen[i]) {
                violations |= 2u;
                break;
            }
        free(seen);
        qsort(dump->particles_sorted, dump->particle_count, sizeof(NvVfxDumpParticle), compare_dump_particles);
    }
    dump->violations = violations;
}

// Asks for the copy of `size` bytes of `from` in readback slot `slot`; false when the slot is still busy.
internal b32 dump_copy(struct NvVfxDump* dump, NvVfxDumpSource* source, u32 slot, NvGpuBuffer from, u64 size)
{
    source->slot = slot;
    source->mapped = NULL;
    return nv_gpu_copy_for_readback(dump->vfx->gpu, slot, from, 0, (umm)size);
}

void nv_vfx_debug_dump_start(NvVfx* vfx, b32 full)
{
    if (vfx->dump && vfx->dump->state == NV_VFX_DUMP_MAPPING)
        return;
    nv_vfx_debug_dump_finish(vfx);
    struct NvVfxDump* dump = calloc(1, sizeof(*dump));
    vfx->dump = dump;
    dump->vfx = vfx;
    dump->full = full;
    dump->state = NV_VFX_DUMP_MAPPING;
    NvVfxDumpSource* sources[6];
    u32 count = 0;
    b32 ok = dump_copy(dump, &dump->counters, STATS_SLOT + 1, vfx->counters, COUNTERS_COUNT * 4);
    sources[count++] = &dump->counters;
    ok = ok && dump_copy(dump, &dump->sim_args, STATS_SLOT + 2, vfx->sim_args, 16);
    sources[count++] = &dump->sim_args;
    ok = ok && dump_copy(dump, &dump->draw_args, STATS_SLOT + 3, vfx->draw_args, 32);
    sources[count++] = &dump->draw_args;
    if (full) {
        ok = ok && dump_copy(dump, &dump->free_list, STATS_SLOT + 4, vfx->free_list, (u64)vfx->capacity * 4);
        sources[count++] = &dump->free_list;
        // The list the last frame's simulate wrote is the one the next frame starts from.
        ok = ok && dump_copy(dump, &dump->alive, STATS_SLOT + 5, vfx->alive[vfx->parity], (u64)vfx->capacity * 4);
        sources[count++] = &dump->alive;
        ok = ok && dump_copy(dump, &dump->particles, STATS_SLOT + 6, vfx->particles, (u64)vfx->capacity * PARTICLE_BYTES);
        sources[count++] = &dump->particles;
    }
    dump->pending = count;
    if (!ok) {
        nv_log(NV_LOG_ERROR, "vfx", "dump: a readback slot was still busy");
        dump->state = NV_VFX_DUMP_IDLE;
    }
}

NvVfxDumpState nv_vfx_debug_dump_state(NvVfx* vfx)
{
    struct NvVfxDump* dump = vfx->dump;
    if (dump && dump->state == NV_VFX_DUMP_MAPPING) {
        NvVfxDumpSource* sources[] = {&dump->counters, &dump->sim_args, &dump->draw_args, &dump->free_list, &dump->alive, &dump->particles};
        for (u32 i = 0; i < NV_ARRAY_COUNT(sources); ++i) {
            NvVfxDumpSource* source = sources[i];
            if (!source->slot || source->mapped)
                continue;
            NvGpuReadback readback = nv_gpu_poll_readback(vfx->gpu, source->slot);
            if (readback.ok) {
                source->mapped = readback.bytes;
                --dump->pending;
            }
        }
        if (dump->pending == 0) {
            dump_process(dump);
            dump->state = NV_VFX_DUMP_READY;
        }
    }
    return dump ? dump->state : NV_VFX_DUMP_IDLE;
}

u32 nv_vfx_debug_dump_counter(const NvVfx* vfx, u32 index)
{
    return vfx->dump && vfx->dump->state == NV_VFX_DUMP_READY && index < COUNTERS_COUNT ? vfx->dump->counter_values[index] : 0;
}

u32 nv_vfx_debug_dump_args(const NvVfx* vfx, u32 which, u32 index)
{
    if (!vfx->dump || vfx->dump->state != NV_VFX_DUMP_READY)
        return 0;
    return which == 0 ? (index < 4 ? vfx->dump->sim[index] : 0) : (index < 8 ? vfx->dump->draw[index] : 0);
}

u32 nv_vfx_debug_dump_violations(const NvVfx* vfx)
{
    return vfx->dump && vfx->dump->state == NV_VFX_DUMP_READY ? vfx->dump->violations : 0;
}

u32 nv_vfx_debug_dump_particle_count(const NvVfx* vfx)
{
    return vfx->dump && vfx->dump->state == NV_VFX_DUMP_READY ? vfx->dump->particle_count : 0;
}

f64 nv_vfx_debug_dump_particle(const NvVfx* vfx, u32 index, u32 field)
{
    if (!vfx->dump || vfx->dump->state != NV_VFX_DUMP_READY || index >= vfx->dump->particle_count || field > 11)
        return 0.0;
    const NvVfxDumpParticle* p = &vfx->dump->particles_sorted[index];
    if (field == 0) return (f64)p->emitter;
    if (field == 1) return (f64)p->seed;
    return (f64)p->v[field - 2];
}

void nv_vfx_debug_dump_finish(NvVfx* vfx)
{
    if (!vfx->dump || vfx->dump->state == NV_VFX_DUMP_MAPPING)
        return;
    free(vfx->dump->particles_sorted);
    free(vfx->dump);
    vfx->dump = NULL;
}
#endif
