#pragma once

// The parts of the effects system that need no GPU, so that tests can run them under Node
// (tests/vfx_test.c): how a frame's bursts split into workgroup jobs, and where the next slot of a
// ring buffer goes. Private to the engine.

#include "nv/base.h"

// One burst as the emit shader reads it: an emitter fired at a point.
typedef struct NvVfxBurst {
    f32 pos[3], scale;
    f32 dir[3];
    u32 count;
    f32 end[3];
    u32 emitter;
    u32 seed, pad[3];
} NvVfxBurst; // 64 bytes

// A workgroup of 64 particles is one job: the burst it spawns for and the first particle (a
// multiple of 64) it spawns. Writes the jobs to `jobs` (two u32 each) and returns their number.
// A burst that does not fit under `max_jobs` is cut to what fits (its `count` is lowered) and a
// burst left with no job is skipped and counted in `*lost`. `*bursts_used` is the number of bursts
// the jobs refer to (the first so many of the array, to upload).
u32 nv_vfx_split_bursts(NvVfxBurst* bursts, u32 burst_count, u32 max_jobs, u32* jobs, u32* bursts_used, u32* lost);

// Takes the next slot of a ring of `capacity`: returns its index, moves `*head` on (wrapping) and
// counts it in `*filled` (at most `capacity`). The oldest slot is overwritten once the ring is full.
u32 nv_vfx_ring_take(u32* head, u32* filled, u32 capacity);
