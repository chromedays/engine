#include "engine/vfx_cpu.h"

u32 nv_vfx_split_bursts(NvVfxBurst* bursts, u32 burst_count, u32 max_jobs, u32* jobs, u32* bursts_used, u32* lost)
{
    u32 job_count = 0;
    *bursts_used = 0;
    for (u32 b = 0; b < burst_count; ++b) {
        u32 groups = (bursts[b].count + 63u) / 64u;
        if (!groups)
            continue; // an empty burst has nothing to spawn
        if (job_count + groups > max_jobs) {
            groups = max_jobs - job_count;
            bursts[b].count = groups * 64u;
            if (!groups) {
                ++*lost;
                continue;
            }
        }
        for (u32 g = 0; g < groups; ++g) {
            jobs[job_count * 2] = b;
            jobs[job_count * 2 + 1] = g * 64u;
            ++job_count;
        }
        *bursts_used = b + 1;
    }
    return job_count;
}

u32 nv_vfx_ring_take(u32* head, u32* filled, u32 capacity)
{
    u32 slot = *head;
    *head = slot + 1 == capacity ? 0 : slot + 1;
    if (*filled < capacity)
        ++*filled;
    return slot;
}
