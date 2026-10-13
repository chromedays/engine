// The effects' particle passes (engine/vfx.c): emit, simulate and the two small passes between them. Particles live on
// the GPU only: a free list (a stack), two alive lists that swap roles every frame, and a visible list per blend mode.
import vfx_common;

[[vk::binding(1, 0)]] RWStructuredBuffer<Particle> particles : register(u1, space0);
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> free_list : register(u2, space0);
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> alive_cur : register(u3, space0);
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> alive_next : register(u4, space0);
[[vk::binding(5, 0)]] RWStructuredBuffer<uint> visible : register(u5, space0);
[[vk::binding(6, 0)]] RWStructuredBuffer<Atomic<int>> counters : register(u6, space0);
[[vk::binding(7, 0)]] StructuredBuffer<Emitter> emitters : register(t7, space0);
[[vk::binding(8, 0)]] StructuredBuffer<Burst> bursts : register(t8, space0);
[[vk::binding(9, 0)]] StructuredBuffer<uint2> jobs : register(t9, space0);
[[vk::binding(10, 0)]] RWStructuredBuffer<uint> sim_args : register(u10, space0);
[[vk::binding(11, 0)]] RWStructuredBuffer<uint> draw_args : register(u11, space0);

// Workgroup memory: the counts a group needs and the places it was given.
groupshared Atomic<uint> wg[8];

float next(inout uint state)
{
    state = pcg(state);
    return unit(state);
}

// The free list starts as every index, with the top of the stack at the end.
[shader("compute")]
[numthreads(64, 1, 1)]
void init_free(uint3 gid : SV_DispatchThreadID)
{
    uint capacity = frame.counts.x;
    if (gid.x < capacity) {
        free_list[gid.x] = gid.x;
    }
    if (gid.x == 0u) {
        counters[C_FREE].store(int(capacity));
        for (uint i = 1u; i < 16u; i++) {
            counters[i].store(0);
        }
    }
}

// One workgroup spawns up to 64 particles of one burst. The group counts how many threads spawn in
// workgroup memory, then one thread takes that many entries off the free list and out of the
// alive list in two global atomics. A group that finds the list short refuses the ones that do not
// fit and counts them.
[shader("compute")]
[numthreads(64, 1, 1)]
void emit(uint3 wid : SV_GroupID, uint li : SV_GroupIndex)
{
    uint2 job = jobs[wid.x];
    Burst burst = bursts[job.x];
    uint i = job.y + li;
    bool valid = i < burst.count;
    if (li == 0u) {
        wg[0].store(0u);
        wg[2].store(0u);
    }
    GroupMemoryBarrierWithGroupSync();
    uint rank = 0u;
    if (valid) {
        rank = wg[0].add(1u);
    }
    GroupMemoryBarrierWithGroupSync();
    if (li == 0u) {
        int wanted = int(wg[0].load());
        if (wanted > 0) {
            int old = counters[C_FREE].sub(wanted);
            int granted = clamp(old, 0, wanted);
            if (granted < wanted) {
                counters[C_FREE].add(wanted - granted);
                counters[C_DROPPED].add(wanted - granted);
            }
            wg[1].store(uint(old));
            wg[2].store(uint(granted));
            wg[3].store(uint(counters[C_ALIVE].add(granted)));
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (valid && rank < wg[2].load()) {
        uint slot = free_list[wg[1].load() - 1u - rank];
        Emitter e = emitters[burst.emitter];
        uint s = pcg(burst.seed ^ (i * 2654435769u + 1u));
        float r_life = next(s);
        float r_speed = next(s);
        float r_cos = next(s);
        float r_phi = next(s);
        float r_along = next(s);
        float r_rot = next(s);

        // A direction inside the cone around the burst's: uniform in the cosine, so uniform on the cap.
        float3 axis = burst.dir;
        if (dot(axis, axis) < 0.0001) {
            axis = float3(0.0, 1.0, 0.0);
        }
        axis = normalize(axis);
        float ct = lerp(cos(e.cone), 1.0, r_cos);
        float st = sqrt(max(1.0 - ct * ct, 0.0));
        float phi = 6.2831853 * r_phi;
        float3 helper = float3(1.0, 0.0, 0.0);
        if (abs(axis.x) > 0.9) {
            helper = float3(0.0, 1.0, 0.0);
        }
        float3 tangent = normalize(cross(axis, helper));
        float3 bitangent = cross(axis, tangent);
        float3 dir = tangent * (st * cos(phi)) + bitangent * (st * sin(phi)) + axis * ct;
        float3 vel = dir * (lerp(e.speed.x, e.speed.y, r_speed) * burst.scale);
        float life = lerp(e.life.x, e.life.y, r_life);
        float along = (float(i) + r_along) / float(max(burst.count, 1u));

        Particle p;
        p.pos = lerp(burst.pos, burst.end, along);
        p.age = 0.0;
        p.vel_xy = pack_half2(vel.xy);
        p.vel_z_life = pack_half2(float2(vel.z, life));
        p.emitter_seed = (s & 0xFFFFF000u) | burst.emitter;
        p.scale_rot = pack_half2(float2(burst.scale, 6.2831853 * r_rot));
        particles[slot] = p;
        alive_cur[wg[3].load() + rank] = slot;
    }
}

// Tells simulate how many workgroups to run: one per 64 living particles, new ones included.
[shader("compute")]
[numthreads(1, 1, 1)]
void prepare_a()
{
    uint n = uint(counters[C_ALIVE].load());
    sim_args[0] = (n + 63u) / 64u;
    sim_args[1] = 1u;
    sim_args[2] = 1u;
    counters[C_ALIVE_NEXT].store(0);
    counters[C_VIS_ADD].store(0);
    counters[C_VIS_ALPHA].store(0);
}

float hash3(int3 p)
{
    uint h = pcg((uint(p.x) * 73856093u) ^ (uint(p.y) * 19349663u) ^ (uint(p.z) * 83492791u));
    return unit(h) * 2.0 - 1.0;
}

float value_noise(float3 x)
{
    int3 i = int3(floor(x));
    float3 f = frac(x);
    float3 u = f * f * (3.0 - 2.0 * f);
    float a = lerp(lerp(hash3(i), hash3(i + int3(1, 0, 0)), u.x), lerp(hash3(i + int3(0, 1, 0)), hash3(i + int3(1, 1, 0)), u.x), u.y);
    float b = lerp(lerp(hash3(i + int3(0, 0, 1)), hash3(i + int3(1, 0, 1)), u.x), lerp(hash3(i + int3(0, 1, 1)), hash3(i + int3(1, 1, 1)), u.x), u.y);
    return lerp(a, b, u.z);
}

float3 potential(float3 p)
{
    return float3(value_noise(p), value_noise(p + float3(31.4, 17.1, 5.9)), value_noise(p + float3(-12.7, 41.3, 23.8)));
}

// The curl of a noise vector field: it has no sources or sinks, so particles swirl instead of
// bunching up.
float3 curl(float3 p)
{
    float e = 0.1;
    float3 dx = potential(p + float3(e, 0.0, 0.0)) - potential(p - float3(e, 0.0, 0.0));
    float3 dy = potential(p + float3(0.0, e, 0.0)) - potential(p - float3(0.0, e, 0.0));
    float3 dz = potential(p + float3(0.0, 0.0, e)) - potential(p - float3(0.0, 0.0, e));
    return float3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x) / (2.0 * e);
}

// Ages and moves every living particle of the current list. Survivors go to the next list, and to the
// visible list of their blend mode when they are inside the frustum; the dead go back on the free list.
// Each of those four lists is appended to a workgroup at a time, like emit.
[shader("compute")]
[numthreads(64, 1, 1)]
void simulate(uint3 gid : SV_DispatchThreadID, uint li : SV_GroupIndex)
{
    float dt = frame.times.x;
    float time = frame.times.y;
    if (li < 8u) {
        wg[li].store(0u);
    }
    GroupMemoryBarrierWithGroupSync();

    uint count = uint(counters[C_ALIVE].load());
    bool valid = gid.x < count;
    uint idx = 0u;
    uint kind = 0u; // 1 dead, 2 alive, 3 alive and visible (additive), 4 alive and visible (alpha)
    uint rank = 0u;
    uint rank_alive = 0u;
    uint rank_visible = 0u;
    if (valid) {
        idx = alive_cur[gid.x];
        Particle p = particles[idx];
        Emitter e = emitters[p.emitter_seed & 0xFFFu];
        float2 vxy = unpack_half2(p.vel_xy);
        float2 vzl = unpack_half2(p.vel_z_life);
        float3 vel = float3(vxy.x, vxy.y, vzl.x);
        float life = vzl.y;
        float age = p.age + dt;
        if (age >= life) {
            kind = 1u;
            rank = wg[0].add(1u);
        } else {
            vel.y -= e.gravity * dt;
            vel += frame.wind.xyz * dt;
            if (e.turbulence != 0.0) {
                vel += curl(p.pos * e.turbulence_scale + float3(0.0, time * 0.5, 0.0)) * (e.turbulence * dt);
            }
            vel *= exp(-e.drag * dt);
            float3 pos = p.pos + vel * dt;
            if (pos.y < e.ground) {
                pos.y = e.ground;
                if (vel.y < 0.0) {
                    vel.y = -vel.y * e.restitution;
                    vel.x *= 1.0 - e.friction;
                    vel.z *= 1.0 - e.friction;
                    if (vel.y < 0.3) {
                        vel.y = 0.0; // nearly still: it rests on the ground
                    }
                }
            }
            p.pos = pos;
            p.age = age;
            p.vel_xy = pack_half2(vel.xy);
            p.vel_z_life = pack_half2(float2(vel.z, life));
            particles[idx] = p;
            kind = 2u;
            rank_alive = wg[1].add(1u);

            // A sphere around the particle against the frustum's six planes.
            float scale = unpack_half2(p.scale_rot).x;
            float size = lerp(e.size_start, e.size_end, age / life) * scale;
            float radius = size * (1.0 + e.stretch * length(vel)) * 1.5;
            bool inside = true;
            for (uint k = 0u; k < 6u; k++) {
                if (dot(frame.planes[k].xyz, pos) + frame.planes[k].w < -radius) {
                    inside = false;
                }
            }
            if (inside) {
                if (e.blend == 0u) {
                    kind = 3u;
                    rank_visible = wg[2].add(1u);
                } else {
                    kind = 4u;
                    rank_visible = wg[3].add(1u);
                }
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (li == 0u) {
        // wg[4..8): where this group's dead, survivors, additive and alpha particles start.
        uint dead_n = wg[0].load();
        if (dead_n > 0u) {
            wg[4].store(uint(counters[C_FREE].add(int(dead_n))));
        }
        uint alive_n = wg[1].load();
        if (alive_n > 0u) {
            wg[5].store(uint(counters[C_ALIVE_NEXT].add(int(alive_n))));
        }
        uint add_n = wg[2].load();
        if (add_n > 0u) {
            wg[6].store(uint(counters[C_VIS_ADD].add(int(add_n))));
        }
        uint alpha_n = wg[3].load();
        if (alpha_n > 0u) {
            wg[7].store(uint(counters[C_VIS_ALPHA].add(int(alpha_n))));
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (valid) {
        if (kind == 1u) {
            free_list[wg[4].load() + rank] = idx;
        } else {
            alive_next[wg[5].load() + rank_alive] = idx;
            if (kind == 3u) {
                visible[wg[6].load() + rank_visible] = idx;
            } else if (kind == 4u) {
                // The alpha list fills from the other end of the visible buffer.
                visible[frame.counts.x - 1u - (wg[7].load() + rank_visible)] = idx;
            }
        }
    }
}

// Writes the two indirect draws (additive, then alpha: four words each), makes the survivors the next
// frame's current list and reports the counts to the CPU.
[shader("compute")]
[numthreads(1, 1, 1)]
void prepare_b()
{
    uint additive = uint(counters[C_VIS_ADD].load());
    uint alpha = uint(counters[C_VIS_ALPHA].load());
    draw_args[0] = additive * 6u;
    draw_args[1] = 1u;
    draw_args[2] = 0u;
    draw_args[3] = 0u;
    draw_args[4] = alpha * 6u;
    draw_args[5] = 1u;
    draw_args[6] = 0u;
    draw_args[7] = 0u;
    int survivors = counters[C_ALIVE_NEXT].load();
    counters[C_ALIVE].store(survivors);
    counters[C_ALIVE_NEXT].store(0);
    counters[C_STAT_ALIVE].store(survivors);
    counters[C_STAT_VISIBLE].store(int(additive + alpha));
    counters[C_STAT_DROPPED].store(counters[C_DROPPED].load());
}
