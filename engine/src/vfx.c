#include "nv/vfx.h"
#include "nv/log.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// The shaders' structs, as they are laid out in memory.
typedef struct NvVfxGpuEmitter {
    f32 life[2], speed[2];
    f32 cone, gravity, drag, turbulence;
    f32 turbulence_scale, size_start, size_end, ground;
    f32 restitution, friction, stretch, spin;
    u32 shape, blend, pad[2];
    f32 colors[3][4];
} NvVfxGpuEmitter; // 128 bytes

typedef struct NvVfxBurst {
    f32 pos[3], scale;
    f32 dir[3];
    u32 count;
    f32 end[3];
    u32 emitter;
    u32 seed, pad[3];
} NvVfxBurst; // 64 bytes

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

#define PARTICLE_BYTES 32
#define EMITTER_BYTES 128
#define COUNTERS_COUNT 16
#define MAX_JOBS 65535u
#define STAGE_SLOTS 4096u // segments or decals the CPU stages before one upload

_Static_assert(sizeof(NvVfxGpuEmitter) == EMITTER_BYTES, "the shader reads 128-byte emitters");
_Static_assert(sizeof(NvVfxBurst) == 64, "the shader reads 64-byte bursts");
_Static_assert(sizeof(NvVfxGpuSegment) == 80, "the shader reads 80-byte segments");
_Static_assert(sizeof(NvVfxGpuDecal) == 48, "the shader reads 48-byte decals");
_Static_assert(sizeof(VfxFrameUniforms) == 240, "the shader reads a 240-byte frame");

// WGSL: the frame's declarations, shared by both modules, then the compute passes and the draw.
global const char* vfx_common_wgsl =
    "// The frame's uniforms, shared by the compute passes and the draw.\n"
    "struct Frame {\n"
    "    view_proj: mat4x4f,\n"
    "    planes: array<vec4f, 6>, // the camera's frustum, normalized, pointing inside\n"
    "    camera_right: vec4f,\n"
    "    camera_up: vec4f,\n"
    "    times: vec4f,            // x: dt, y: time\n"
    "    wind: vec4f,\n"
    "    counts: vec4u,           // x: capacity\n"
    "};\n"
    "\n"
    "// 32 bytes: where it is, how old, what it moves with and how long it lives (half floats), its\n"
    "// emitter and seed, and the scale it was fired with and its starting rotation (half floats).\n"
    "struct Particle {\n"
    "    pos: vec3f,\n"
    "    age: f32,\n"
    "    vel_xy: u32,\n"
    "    vel_z_life: u32,\n"
    "    emitter_seed: u32, // seed in the top 20 bits, emitter in the low 12\n"
    "    scale_rot: u32,\n"
    "};\n"
    "\n"
    "struct Emitter {\n"
    "    life: vec2f,\n"
    "    speed: vec2f,\n"
    "    cone: f32,\n"
    "    gravity: f32,\n"
    "    drag: f32,\n"
    "    turbulence: f32,\n"
    "    turbulence_scale: f32,\n"
    "    size_start: f32,\n"
    "    size_end: f32,\n"
    "    ground: f32,\n"
    "    restitution: f32,\n"
    "    friction: f32,\n"
    "    stretch: f32,\n"
    "    spin: f32,\n"
    "    shape: u32,\n"
    "    blend: u32,\n"
    "    pad0: u32,\n"
    "    pad1: u32,\n"
    "    color0: vec4f,\n"
    "    color1: vec4f,\n"
    "    color2: vec4f,\n"
    "};\n"
    "\n"
    "struct Burst {\n"
    "    pos: vec3f,\n"
    "    scale: f32,\n"
    "    dir: vec3f,\n"
    "    count: u32,\n"
    "    end: vec3f,\n"
    "    emitter: u32,\n"
    "    seed: u32,\n"
    "    pad0: u32,\n"
    "    pad1: u32,\n"
    "    pad2: u32,\n"
    "};\n"
    "\n"
    "// Indices into the counters buffer.\n"
    "const C_FREE = 0u;       // entries on the free list\n"
    "const C_ALIVE = 1u;      // entries in the current alive list\n"
    "const C_ALIVE_NEXT = 2u; // survivors written by simulate\n"
    "const C_VIS_ADD = 3u;    // visible additive particles\n"
    "const C_VIS_ALPHA = 4u;  // visible alpha particles\n"
    "const C_DROPPED = 5u;    // spawns refused, ever\n"
    "const C_STAT_ALIVE = 8u; // what prepare reports to the CPU\n"
    "const C_STAT_VISIBLE = 9u;\n"
    "const C_STAT_DROPPED = 10u;\n"
    "\n"
    "fn pcg(v: u32) -> u32 {\n"
    "    let s = v * 747796405u + 2891336453u;\n"
    "    let w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;\n"
    "    return (w >> 22u) ^ w;\n"
    "}\n"
    "\n"
    "fn unit(h: u32) -> f32 {\n"
    "    return f32(h >> 8u) * (1.0 / 16777216.0);\n"
    "}\n"
    "\n"
    "@group(0) @binding(0) var<uniform> frame: Frame;\n";

global const char* vfx_compute_wgsl =
    "@group(0) @binding(1) var<storage, read_write> particles: array<Particle>;\n"
    "@group(0) @binding(2) var<storage, read_write> free_list: array<u32>;\n"
    "@group(0) @binding(3) var<storage, read_write> alive_cur: array<u32>;\n"
    "@group(0) @binding(4) var<storage, read_write> alive_next: array<u32>;\n"
    "@group(0) @binding(5) var<storage, read_write> visible: array<u32>;\n"
    "@group(0) @binding(6) var<storage, read_write> counters: array<atomic<i32>>;\n"
    "@group(0) @binding(7) var<storage, read> emitters: array<Emitter>;\n"
    "@group(0) @binding(8) var<storage, read> bursts: array<Burst>;\n"
    "@group(0) @binding(9) var<storage, read> jobs: array<vec2u>;\n"
    "@group(0) @binding(10) var<storage, read_write> sim_args: array<u32>;\n"
    "@group(0) @binding(11) var<storage, read_write> draw_args: array<u32>;\n"
    "\n"
    "// Workgroup memory: the counts a group needs and the places it was given.\n"
    "var<workgroup> wg: array<atomic<u32>, 8>;\n"
    "\n"
    "fn next(state: ptr<function, u32>) -> f32 {\n"
    "    *state = pcg(*state);\n"
    "    return unit(*state);\n"
    "}\n"
    "\n"
    "// The free list starts as every index, with the top of the stack at the end.\n"
    "@compute @workgroup_size(64)\n"
    "fn init_free(@builtin(global_invocation_id) gid: vec3u) {\n"
    "    let capacity = frame.counts.x;\n"
    "    if (gid.x < capacity) {\n"
    "        free_list[gid.x] = gid.x;\n"
    "    }\n"
    "    if (gid.x == 0u) {\n"
    "        atomicStore(&counters[C_FREE], i32(capacity));\n"
    "        for (var i = 1u; i < 16u; i++) {\n"
    "            atomicStore(&counters[i], 0);\n"
    "        }\n"
    "    }\n"
    "}\n"
    "\n"
    "// One workgroup spawns up to 64 particles of one burst. The group counts how many threads spawn in\n"
    "// workgroup memory, then one thread takes that many entries off the free list and out of the\n"
    "// alive list in two global atomics. A group that finds the list short refuses the ones that do not\n"
    "// fit and counts them.\n"
    "@compute @workgroup_size(64)\n"
    "fn emit(@builtin(workgroup_id) wid: vec3u, @builtin(local_invocation_index) li: u32) {\n"
    "    let job = jobs[wid.x];\n"
    "    let burst = bursts[job.x];\n"
    "    let i = job.y + li;\n"
    "    let valid = i < burst.count;\n"
    "    if (li == 0u) {\n"
    "        atomicStore(&wg[0], 0u);\n"
    "        atomicStore(&wg[2], 0u);\n"
    "    }\n"
    "    workgroupBarrier();\n"
    "    var rank = 0u;\n"
    "    if (valid) {\n"
    "        rank = atomicAdd(&wg[0], 1u);\n"
    "    }\n"
    "    workgroupBarrier();\n"
    "    if (li == 0u) {\n"
    "        let wanted = i32(atomicLoad(&wg[0]));\n"
    "        if (wanted > 0) {\n"
    "            let old = atomicSub(&counters[C_FREE], wanted);\n"
    "            let granted = clamp(old, 0, wanted);\n"
    "            if (granted < wanted) {\n"
    "                atomicAdd(&counters[C_FREE], wanted - granted);\n"
    "                atomicAdd(&counters[C_DROPPED], wanted - granted);\n"
    "            }\n"
    "            atomicStore(&wg[1], u32(old));\n"
    "            atomicStore(&wg[2], u32(granted));\n"
    "            atomicStore(&wg[3], u32(atomicAdd(&counters[C_ALIVE], granted)));\n"
    "        }\n"
    "    }\n"
    "    workgroupBarrier();\n"
    "    if (valid && rank < atomicLoad(&wg[2])) {\n"
    "        let slot = free_list[atomicLoad(&wg[1]) - 1u - rank];\n"
    "        let e = emitters[burst.emitter];\n"
    "        var s = pcg(burst.seed ^ (i * 2654435769u + 1u));\n"
    "        let r_life = next(&s);\n"
    "        let r_speed = next(&s);\n"
    "        let r_cos = next(&s);\n"
    "        let r_phi = next(&s);\n"
    "        let r_along = next(&s);\n"
    "        let r_rot = next(&s);\n"
    "\n"
    "        // A direction inside the cone around the burst's: uniform in the cosine, so uniform on the cap.\n"
    "        var axis = burst.dir;\n"
    "        if (dot(axis, axis) < 0.0001) {\n"
    "            axis = vec3f(0.0, 1.0, 0.0);\n"
    "        }\n"
    "        axis = normalize(axis);\n"
    "        let ct = mix(cos(e.cone), 1.0, r_cos);\n"
    "        let st = sqrt(max(1.0 - ct * ct, 0.0));\n"
    "        let phi = 6.2831853 * r_phi;\n"
    "        var helper = vec3f(1.0, 0.0, 0.0);\n"
    "        if (abs(axis.x) > 0.9) {\n"
    "            helper = vec3f(0.0, 1.0, 0.0);\n"
    "        }\n"
    "        let tangent = normalize(cross(axis, helper));\n"
    "        let bitangent = cross(axis, tangent);\n"
    "        let dir = tangent * (st * cos(phi)) + bitangent * (st * sin(phi)) + axis * ct;\n"
    "        let vel = dir * (mix(e.speed.x, e.speed.y, r_speed) * burst.scale);\n"
    "        let life = mix(e.life.x, e.life.y, r_life);\n"
    "        let along = (f32(i) + r_along) / f32(max(burst.count, 1u));\n"
    "\n"
    "        var p: Particle;\n"
    "        p.pos = mix(burst.pos, burst.end, along);\n"
    "        p.age = 0.0;\n"
    "        p.vel_xy = pack2x16float(vel.xy);\n"
    "        p.vel_z_life = pack2x16float(vec2f(vel.z, life));\n"
    "        p.emitter_seed = (s & 0xFFFFF000u) | burst.emitter;\n"
    "        p.scale_rot = pack2x16float(vec2f(burst.scale, 6.2831853 * r_rot));\n"
    "        particles[slot] = p;\n"
    "        alive_cur[atomicLoad(&wg[3]) + rank] = slot;\n"
    "    }\n"
    "}\n"
    "\n"
    "// Tells simulate how many workgroups to run: one per 64 living particles, new ones included.\n"
    "@compute @workgroup_size(1)\n"
    "fn prepare_a() {\n"
    "    let n = u32(atomicLoad(&counters[C_ALIVE]));\n"
    "    sim_args[0] = (n + 63u) / 64u;\n"
    "    sim_args[1] = 1u;\n"
    "    sim_args[2] = 1u;\n"
    "    atomicStore(&counters[C_ALIVE_NEXT], 0);\n"
    "    atomicStore(&counters[C_VIS_ADD], 0);\n"
    "    atomicStore(&counters[C_VIS_ALPHA], 0);\n"
    "}\n"
    "\n"
    "fn hash3(p: vec3i) -> f32 {\n"
    "    let h = pcg((u32(p.x) * 73856093u) ^ (u32(p.y) * 19349663u) ^ (u32(p.z) * 83492791u));\n"
    "    return unit(h) * 2.0 - 1.0;\n"
    "}\n"
    "\n"
    "fn value_noise(x: vec3f) -> f32 {\n"
    "    let i = vec3i(floor(x));\n"
    "    let f = fract(x);\n"
    "    let u = f * f * (3.0 - 2.0 * f);\n"
    "    let a = mix(mix(hash3(i), hash3(i + vec3i(1, 0, 0)), u.x), mix(hash3(i + vec3i(0, 1, 0)), hash3(i + vec3i(1, 1, 0)), u.x), u.y);\n"
    "    let b = mix(mix(hash3(i + vec3i(0, 0, 1)), hash3(i + vec3i(1, 0, 1)), u.x), mix(hash3(i + vec3i(0, 1, 1)), hash3(i + vec3i(1, 1, 1)), u.x), u.y);\n"
    "    return mix(a, b, u.z);\n"
    "}\n"
    "\n"
    "fn potential(p: vec3f) -> vec3f {\n"
    "    return vec3f(value_noise(p), value_noise(p + vec3f(31.4, 17.1, 5.9)), value_noise(p + vec3f(-12.7, 41.3, 23.8)));\n"
    "}\n"
    "\n"
    "// The curl of a noise vector field: it has no sources or sinks, so particles swirl instead of\n"
    "// bunching up.\n"
    "fn curl(p: vec3f) -> vec3f {\n"
    "    let e = 0.1;\n"
    "    let dx = potential(p + vec3f(e, 0.0, 0.0)) - potential(p - vec3f(e, 0.0, 0.0));\n"
    "    let dy = potential(p + vec3f(0.0, e, 0.0)) - potential(p - vec3f(0.0, e, 0.0));\n"
    "    let dz = potential(p + vec3f(0.0, 0.0, e)) - potential(p - vec3f(0.0, 0.0, e));\n"
    "    return vec3f(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x) / (2.0 * e);\n"
    "}\n"
    "\n"
    "// Ages and moves every living particle of the current list. Survivors go to the next list, and to the\n"
    "// visible list of their blend mode when they are inside the frustum; the dead go back on the free list.\n"
    "// Each of those four lists is appended to a workgroup at a time, like emit.\n"
    "@compute @workgroup_size(64)\n"
    "fn simulate(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_index) li: u32) {\n"
    "    let dt = frame.times.x;\n"
    "    let time = frame.times.y;\n"
    "    if (li < 8u) {\n"
    "        atomicStore(&wg[li], 0u);\n"
    "    }\n"
    "    workgroupBarrier();\n"
    "\n"
    "    let count = u32(atomicLoad(&counters[C_ALIVE]));\n"
    "    let valid = gid.x < count;\n"
    "    var idx = 0u;\n"
    "    var kind = 0u; // 1 dead, 2 alive, 3 alive and visible (additive), 4 alive and visible (alpha)\n"
    "    var rank = 0u;\n"
    "    var rank_alive = 0u;\n"
    "    var rank_visible = 0u;\n"
    "    if (valid) {\n"
    "        idx = alive_cur[gid.x];\n"
    "        var p = particles[idx];\n"
    "        let e = emitters[p.emitter_seed & 0xFFFu];\n"
    "        let vxy = unpack2x16float(p.vel_xy);\n"
    "        let vzl = unpack2x16float(p.vel_z_life);\n"
    "        var vel = vec3f(vxy.x, vxy.y, vzl.x);\n"
    "        let life = vzl.y;\n"
    "        let age = p.age + dt;\n"
    "        if (age >= life) {\n"
    "            kind = 1u;\n"
    "            rank = atomicAdd(&wg[0], 1u);\n"
    "        } else {\n"
    "            vel.y -= e.gravity * dt;\n"
    "            vel += frame.wind.xyz * dt;\n"
    "            if (e.turbulence != 0.0) {\n"
    "                vel += curl(p.pos * e.turbulence_scale + vec3f(0.0, time * 0.5, 0.0)) * (e.turbulence * dt);\n"
    "            }\n"
    "            vel *= exp(-e.drag * dt);\n"
    "            var pos = p.pos + vel * dt;\n"
    "            if (pos.y < e.ground) {\n"
    "                pos.y = e.ground;\n"
    "                if (vel.y < 0.0) {\n"
    "                    vel.y = -vel.y * e.restitution;\n"
    "                    vel.x *= 1.0 - e.friction;\n"
    "                    vel.z *= 1.0 - e.friction;\n"
    "                    if (vel.y < 0.3) {\n"
    "                        vel.y = 0.0; // nearly still: it rests on the ground\n"
    "                    }\n"
    "                }\n"
    "            }\n"
    "            p.pos = pos;\n"
    "            p.age = age;\n"
    "            p.vel_xy = pack2x16float(vel.xy);\n"
    "            p.vel_z_life = pack2x16float(vec2f(vel.z, life));\n"
    "            particles[idx] = p;\n"
    "            kind = 2u;\n"
    "            rank_alive = atomicAdd(&wg[1], 1u);\n"
    "\n"
    "            // A sphere around the particle against the frustum's six planes.\n"
    "            let scale = unpack2x16float(p.scale_rot).x;\n"
    "            let size = mix(e.size_start, e.size_end, age / life) * scale;\n"
    "            let radius = size * (1.0 + e.stretch * length(vel)) * 1.5;\n"
    "            var inside = true;\n"
    "            for (var k = 0u; k < 6u; k++) {\n"
    "                if (dot(frame.planes[k].xyz, pos) + frame.planes[k].w < -radius) {\n"
    "                    inside = false;\n"
    "                }\n"
    "            }\n"
    "            if (inside) {\n"
    "                if (e.blend == 0u) {\n"
    "                    kind = 3u;\n"
    "                    rank_visible = atomicAdd(&wg[2], 1u);\n"
    "                } else {\n"
    "                    kind = 4u;\n"
    "                    rank_visible = atomicAdd(&wg[3], 1u);\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    workgroupBarrier();\n"
    "    if (li == 0u) {\n"
    "        // wg[4..8): where this group's dead, survivors, additive and alpha particles start.\n"
    "        let dead_n = atomicLoad(&wg[0]);\n"
    "        if (dead_n > 0u) {\n"
    "            atomicStore(&wg[4], u32(atomicAdd(&counters[C_FREE], i32(dead_n))));\n"
    "        }\n"
    "        let alive_n = atomicLoad(&wg[1]);\n"
    "        if (alive_n > 0u) {\n"
    "            atomicStore(&wg[5], u32(atomicAdd(&counters[C_ALIVE_NEXT], i32(alive_n))));\n"
    "        }\n"
    "        let add_n = atomicLoad(&wg[2]);\n"
    "        if (add_n > 0u) {\n"
    "            atomicStore(&wg[6], u32(atomicAdd(&counters[C_VIS_ADD], i32(add_n))));\n"
    "        }\n"
    "        let alpha_n = atomicLoad(&wg[3]);\n"
    "        if (alpha_n > 0u) {\n"
    "            atomicStore(&wg[7], u32(atomicAdd(&counters[C_VIS_ALPHA], i32(alpha_n))));\n"
    "        }\n"
    "    }\n"
    "    workgroupBarrier();\n"
    "    if (valid) {\n"
    "        if (kind == 1u) {\n"
    "            free_list[atomicLoad(&wg[4]) + rank] = idx;\n"
    "        } else {\n"
    "            alive_next[atomicLoad(&wg[5]) + rank_alive] = idx;\n"
    "            if (kind == 3u) {\n"
    "                visible[atomicLoad(&wg[6]) + rank_visible] = idx;\n"
    "            } else if (kind == 4u) {\n"
    "                // The alpha list fills from the other end of the visible buffer.\n"
    "                visible[frame.counts.x - 1u - (atomicLoad(&wg[7]) + rank_visible)] = idx;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}\n"
    "\n"
    "// Writes the two indirect draws (additive, then alpha: four words each), makes the survivors the next\n"
    "// frame's current list and reports the counts to the CPU.\n"
    "@compute @workgroup_size(1)\n"
    "fn prepare_b() {\n"
    "    let additive = u32(atomicLoad(&counters[C_VIS_ADD]));\n"
    "    let alpha = u32(atomicLoad(&counters[C_VIS_ALPHA]));\n"
    "    draw_args[0] = additive * 6u;\n"
    "    draw_args[1] = 1u;\n"
    "    draw_args[2] = 0u;\n"
    "    draw_args[3] = 0u;\n"
    "    draw_args[4] = alpha * 6u;\n"
    "    draw_args[5] = 1u;\n"
    "    draw_args[6] = 0u;\n"
    "    draw_args[7] = 0u;\n"
    "    let survivors = atomicLoad(&counters[C_ALIVE_NEXT]);\n"
    "    atomicStore(&counters[C_ALIVE], survivors);\n"
    "    atomicStore(&counters[C_ALIVE_NEXT], 0);\n"
    "    atomicStore(&counters[C_STAT_ALIVE], survivors);\n"
    "    atomicStore(&counters[C_STAT_VISIBLE], i32(additive + alpha));\n"
    "    atomicStore(&counters[C_STAT_DROPPED], atomicLoad(&counters[C_DROPPED]));\n"
    "}\n";

// Shared by the particle and mark draws: the quad's corners and the shapes drawn in the fragment shader.
global const char* vfx_shapes_wgsl =
    "const CORNERS = array<vec2f, 6>(\n"
    "    vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),\n"
    "    vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));\n"
    "\n"
    "fn hash2(p: vec2i) -> f32 {\n"
    "    return unit(pcg((u32(p.x) * 73856093u) ^ (u32(p.y) * 19349663u)));\n"
    "}\n"
    "\n"
    "fn value_noise2(x: vec2f) -> f32 {\n"
    "    let i = vec2i(floor(x));\n"
    "    let f = fract(x);\n"
    "    let u = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(hash2(i), hash2(i + vec2i(1, 0)), u.x), mix(hash2(i + vec2i(0, 1)), hash2(i + vec2i(1, 1)), u.x), u.y);\n"
    "}\n"
    "\n"
    "// How much of the quad's pixel is lit: 1 at the center of a disc, 0 outside.\n"
    "fn shape_alpha(shape: u32, uv: vec2f, seed: u32) -> f32 {\n"
    "    let r = length(uv);\n"
    "    if (shape == 1u) {\n"
    "        return smoothstep(0.55, 0.8, r) * (1.0 - smoothstep(0.8, 1.0, r));\n"
    "    }\n"
    "    if (shape == 2u) {\n"
    "        let ax = clamp(1.0 - abs(uv.x), 0.0, 1.0);\n"
    "        let ay = clamp(1.0 - abs(uv.y), 0.0, 1.0);\n"
    "        return ax * ax * ay * ay * ay;\n"
    "    }\n"
    "    if (shape == 3u) {\n"
    "        let offset = vec2f(f32(seed & 255u), f32((seed >> 8u) & 255u));\n"
    "        let n = value_noise2(uv * 2.5 + offset) * 0.6 + value_noise2(uv * 5.0 + offset * 1.7) * 0.4;\n"
    "        return smoothstep(0.0, 0.45, 1.0 - r + 0.55 * (n - 0.5));\n"
    "    }\n"
    "    let a = clamp(1.0 - r, 0.0, 1.0);\n"
    "    return a * a * (3.0 - 2.0 * a);\n"
    "}\n";

global const char* vfx_render_wgsl =
    "@group(0) @binding(1) var<storage, read> particles: array<Particle>;\n"
    "@group(0) @binding(2) var<storage, read> visible: array<u32>;\n"
    "@group(0) @binding(3) var<storage, read> emitters: array<Emitter>;\n"
    "\n"
    "struct VsOut {\n"
    "    @builtin(position) clip: vec4f,\n"
    "    @location(0) uv: vec2f,\n"
    "    @location(1) color: vec4f,\n"
    "    @location(2) @interpolate(flat) shape: u32,\n"
    "    @location(3) @interpolate(flat) seed: u32,\n"
    "};\n"
    "\n"
    "// Vertex pulling: six vertices per visible particle, found by the vertex index. The additive list\n"
    "// starts at the front of the visible buffer and the alpha list at the back.\n"
    "fn shade(vi: u32, alpha: bool) -> VsOut {\n"
    "    let k = vi / 6u;\n"
    "    let corner = CORNERS[vi % 6u];\n"
    "    var slot = visible[k];\n"
    "    if (alpha) {\n"
    "        slot = visible[frame.counts.x - 1u - k];\n"
    "    }\n"
    "    let p = particles[slot];\n"
    "    let e = emitters[p.emitter_seed & 0xFFFu];\n"
    "    let vxy = unpack2x16float(p.vel_xy);\n"
    "    let vzl = unpack2x16float(p.vel_z_life);\n"
    "    let vel = vec3f(vxy.x, vxy.y, vzl.x);\n"
    "    let t = clamp(p.age / vzl.y, 0.0, 1.0);\n"
    "    let scale_rot = unpack2x16float(p.scale_rot);\n"
    "    let size = mix(e.size_start, e.size_end, t) * scale_rot.x;\n"
    "    let seed = p.emitter_seed >> 12u;\n"
    "    let spin = (unit(pcg(seed)) * 2.0 - 1.0) * e.spin;\n"
    "    let angle = scale_rot.y + spin * p.age;\n"
    "    let right = frame.camera_right.xyz;\n"
    "    let up = frame.camera_up.xyz;\n"
    "\n"
    "    var offset = (right * corner.x + up * corner.y) * size;\n"
    "    if (e.stretch > 0.0) {\n"
    "        // Lengthened along the velocity as the camera sees it.\n"
    "        let screen = vec2f(dot(vel, right), dot(vel, up));\n"
    "        let speed = length(screen);\n"
    "        if (speed > 0.001) {\n"
    "            let d = screen / speed;\n"
    "            let axis = right * d.x + up * d.y;\n"
    "            let across = right * (-d.y) + up * d.x;\n"
    "            offset = axis * (corner.x * size * (1.0 + e.stretch * speed)) + across * (corner.y * size);\n"
    "        }\n"
    "    } else {\n"
    "        let c = cos(angle);\n"
    "        let s = sin(angle);\n"
    "        let r = vec2f(corner.x * c - corner.y * s, corner.x * s + corner.y * c);\n"
    "        offset = (right * r.x + up * r.y) * size;\n"
    "    }\n"
    "\n"
    "    var color = mix(e.color0, e.color1, clamp(t * 2.0, 0.0, 1.0));\n"
    "    if (t > 0.5) {\n"
    "        color = mix(e.color1, e.color2, (t - 0.5) * 2.0);\n"
    "    }\n"
    "    var out: VsOut;\n"
    "    out.clip = frame.view_proj * vec4f(p.pos + offset, 1.0);\n"
    "    out.uv = corner;\n"
    "    out.color = color;\n"
    "    out.shape = e.shape;\n"
    "    out.seed = seed;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "@vertex\n"
    "fn vs_add(@builtin(vertex_index) vi: u32) -> VsOut {\n"
    "    return shade(vi, false);\n"
    "}\n"
    "\n"
    "@vertex\n"
    "fn vs_alpha(@builtin(vertex_index) vi: u32) -> VsOut {\n"
    "    return shade(vi, true);\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_add(in: VsOut) -> @location(0) vec4f {\n"
    "    let a = shape_alpha(in.shape, in.uv, in.seed) * in.color.a;\n"
    "    return vec4f(in.color.rgb * a, 0.0);\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_alpha(in: VsOut) -> @location(0) vec4f {\n"
    "    return vec4f(in.color.rgb, shape_alpha(in.shape, in.uv, in.seed) * in.color.a);\n"
    "}\n";

// Trails, beams and decals: instances read from ring buffers, six vertices each. A slot that is not
// born yet or has expired is moved outside the clip volume, so nothing is drawn for it.
global const char* vfx_marks_wgsl =
    "struct Segment {\n"
    "    a: vec3f,\n"
    "    birth: f32,\n"
    "    b: vec3f,\n"
    "    life: f32,\n"
    "    color0: vec4f,\n"
    "    color1: vec4f,\n"
    "    width0: f32,\n"
    "    width1: f32,\n"
    "    flicker: f32,\n"
    "    scroll: f32,\n"
    "};\n"
    "\n"
    "struct Decal {\n"
    "    pos: vec3f,\n"
    "    angle: f32,\n"
    "    size: f32,\n"
    "    birth: f32,\n"
    "    life: f32,\n"
    "    shape: u32,\n"
    "    color: vec4f,\n"
    "};\n"
    "\n"
    "@group(0) @binding(1) var<storage, read> segments: array<Segment>;\n"
    "@group(0) @binding(2) var<storage, read> decals: array<Decal>;\n"
    "\n"
    "const OUTSIDE = vec4f(2.0, 2.0, 2.0, 1.0);\n"
    "\n"
    "struct SegmentOut {\n"
    "    @builtin(position) clip: vec4f,\n"
    "    @location(0) uv: vec2f,      // x along the segment 0 to 1, y across -1 to 1\n"
    "    @location(1) color: vec3f,   // already faded and flickered\n"
    "    @location(2) bands: vec2f,   // x: length in meters, y: scroll phase (0 = no bands)\n"
    "};\n"
    "\n"
    "@vertex\n"
    "fn vs_segment(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> SegmentOut {\n"
    "    var out: SegmentOut;\n"
    "    out.clip = OUTSIDE;\n"
    "    let s = segments[ii];\n"
    "    let age = frame.times.y - s.birth;\n"
    "    if (s.life <= 0.0 || age < 0.0 || age >= s.life) {\n"
    "        return out;\n"
    "    }\n"
    "    let t = age / s.life;\n"
    "    let corner = CORNERS[vi];\n"
    "    let d = s.b - s.a;\n"
    "    // The strip turns around its axis to face the camera.\n"
    "    let toward = cross(frame.camera_right.xyz, frame.camera_up.xyz);\n"
    "    var side = cross(d, toward);\n"
    "    let side_length = length(side);\n"
    "    if (side_length > 1.0e-6) {\n"
    "        side = side / side_length;\n"
    "    } else {\n"
    "        side = frame.camera_right.xyz;\n"
    "    }\n"
    "    let width = mix(s.width0, s.width1, t);\n"
    "    let along = corner.x * 0.5 + 0.5;\n"
    "    let pos = mix(s.a, s.b, along) + side * (corner.y * width * 0.5);\n"
    "    let color = mix(s.color0, s.color1, t);\n"
    "    var flick = 1.0;\n"
    "    if (s.flicker > 0.0) {\n"
    "        flick = 1.0 - s.flicker * unit(pcg(u32(frame.times.y * 40.0) + ii * 7919u));\n"
    "    }\n"
    "    out.clip = frame.view_proj * vec4f(pos, 1.0);\n"
    "    out.uv = vec2f(along, corner.y);\n"
    "    out.color = color.rgb * (color.a * flick);\n"
    "    out.bands = vec2f(length(d), frame.times.y * s.scroll);\n"
    "    if (s.scroll == 0.0) {\n"
    "        out.bands.y = 0.0;\n"
    "    }\n"
    "    return out;\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_segment(in: SegmentOut) -> @location(0) vec4f {\n"
    "    let across = 1.0 - in.uv.y * in.uv.y;\n"
    "    var k = across * across;\n"
    "    if (in.bands.y != 0.0) {\n"
    "        k = k * (0.65 + 0.35 * sin(6.2831853 * (in.uv.x * in.bands.x * 2.0 - in.bands.y)));\n"
    "    }\n"
    "    return vec4f(in.color * k, 0.0);\n"
    "}\n"
    "\n"
    "struct DecalOut {\n"
    "    @builtin(position) clip: vec4f,\n"
    "    @location(0) uv: vec2f,\n"
    "    @location(1) color: vec4f,\n"
    "    @location(2) @interpolate(flat) shape: u32,\n"
    "    @location(3) @interpolate(flat) seed: u32,\n"
    "};\n"
    "\n"
    "@vertex\n"
    "fn vs_decal(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> DecalOut {\n"
    "    var out: DecalOut;\n"
    "    out.clip = OUTSIDE;\n"
    "    let d = decals[ii];\n"
    "    let age = frame.times.y - d.birth;\n"
    "    if (d.life <= 0.0 || age < 0.0 || age >= d.life) {\n"
    "        return out;\n"
    "    }\n"
    "    let corner = CORNERS[vi];\n"
    "    let c = cos(d.angle);\n"
    "    let s = sin(d.angle);\n"
    "    let r = vec2f(corner.x * c - corner.y * s, corner.x * s + corner.y * c) * (d.size * 0.5);\n"
    "    var pos = vec3f(d.pos.x + r.x, d.pos.y, d.pos.z + r.y);\n"
    "    // Lifted off the ground a little, more with distance, where the depth buffer is coarser.\n"
    "    // (A depth bias is not used: its size depends on the format and slope.)\n"
    "    let w = (frame.view_proj * vec4f(pos, 1.0)).w;\n"
    "    pos.y = pos.y + 0.002 + 0.0015 * max(w, 0.0);\n"
    "    let t = age / d.life;\n"
    "    let fade_share = f32(d.shape >> 8u) / 255.0;\n"
    "    var fade = 1.0;\n"
    "    if (fade_share > 0.0) {\n"
    "        fade = clamp((1.0 - t) / fade_share, 0.0, 1.0);\n"
    "    }\n"
    "    out.clip = frame.view_proj * vec4f(pos, 1.0);\n"
    "    out.uv = corner;\n"
    "    out.color = vec4f(d.color.rgb, d.color.a * fade);\n"
    "    out.shape = d.shape & 255u;\n"
    "    out.seed = pcg(ii);\n"
    "    return out;\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_decal(in: DecalOut) -> @location(0) vec4f {\n"
    "    return vec4f(in.color.rgb, shape_alpha(in.shape, in.uv, in.seed) * in.color.a);\n"
    "}\n";

internal WGPUBuffer create_buffer(NvGpu* gpu, const char* label, WGPUBufferUsage usage, u64 size)
{
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){label, WGPU_STRLEN};
    desc.usage = usage | WGPUBufferUsage_CopyDst;
    desc.size = (size + 3) & ~(u64)3;
    return wgpuDeviceCreateBuffer(gpu->device, &desc);
}

internal WGPUShaderModule create_module(WGPUDevice device, const char* label, const char* shared, const char* body)
{
    // The common declarations first, then what the draws share (shapes, or nothing), then the module's own.
    local_persist char text[32768];
    int length = snprintf(text, sizeof(text), "%s%s%s", vfx_common_wgsl, shared, body);
    NV_ASSERT(length > 0 && (umm)length < sizeof(text));
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){text, WGPU_STRLEN};
    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){label, WGPU_STRLEN};
    desc.nextInChain = &wgsl.chain;
    return wgpuDeviceCreateShaderModule(device, &desc);
}

typedef struct BindingKind {
    u32 binding;
    WGPUBufferBindingType type;
} BindingKind;

internal WGPUBindGroupLayout create_layout(WGPUDevice device, WGPUShaderStage visibility, const BindingKind* kinds, u32 count)
{
    WGPUBindGroupLayoutEntry entries[12] = {0};
    NV_ASSERT(count <= NV_ARRAY_COUNT(entries));
    for (u32 i = 0; i < count; ++i) {
        entries[i].binding = kinds[i].binding;
        entries[i].visibility = visibility;
        entries[i].buffer.type = kinds[i].type;
    }
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = count;
    desc.entries = entries;
    return wgpuDeviceCreateBindGroupLayout(device, &desc);
}

internal WGPUComputePipeline create_compute_pipeline(WGPUDevice device, WGPUShaderModule module, const char* entry, WGPUBindGroupLayout layout)
{
    WGPUPipelineLayoutDescriptor layout_desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layout_desc.bindGroupLayoutCount = 1;
    layout_desc.bindGroupLayouts = &layout;
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &layout_desc);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){entry, WGPU_STRLEN};
    desc.layout = pipeline_layout;
    desc.compute.module = module;
    desc.compute.entryPoint = (WGPUStringView){entry, WGPU_STRLEN};
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}

// A bind group of whole buffers: `buffers[i]` is bound at `kinds[i].binding`.
internal WGPUBindGroup create_group(WGPUDevice device, WGPUBindGroupLayout layout, const BindingKind* kinds, const WGPUBuffer* buffers,
                                    const u64* sizes, u32 count)
{
    WGPUBindGroupEntry entries[12] = {0};
    for (u32 i = 0; i < count; ++i) {
        entries[i].binding = kinds[i].binding;
        entries[i].buffer = buffers[i];
        entries[i].size = sizes[i];
    }
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = count;
    desc.entries = entries;
    return wgpuDeviceCreateBindGroup(device, &desc);
}

internal void create_render_pipelines(NvVfx* vfx, u32 samples)
{
    WGPUDevice device = vfx->gpu->device;
    if (vfx->add_pipeline)
        wgpuRenderPipelineRelease(vfx->add_pipeline);
    if (vfx->alpha_pipeline)
        wgpuRenderPipelineRelease(vfx->alpha_pipeline);
    if (vfx->segment_pipeline)
        wgpuRenderPipelineRelease(vfx->segment_pipeline);
    if (vfx->decal_pipeline)
        wgpuRenderPipelineRelease(vfx->decal_pipeline);
    WGPUPipelineLayoutDescriptor layout_desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layout_desc.bindGroupLayoutCount = 1;
    layout_desc.bindGroupLayouts = &vfx->render_layout;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &layout_desc);

    for (u32 alpha = 0; alpha < 2; ++alpha) {
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        if (alpha) {
            blend.color = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
            blend.alpha = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
        } else {
            blend.color = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
            blend.alpha = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
        }
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = NV_SCENE_FORMAT;
        target.blend = &blend;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = vfx->render_module;
        fragment.entryPoint = (WGPUStringView){alpha ? "fs_alpha" : "fs_add", WGPU_STRLEN};
        fragment.targetCount = 1;
        fragment.targets = &target;
        // Tested against the scene's depth (reverse Z, so Greater) but never written to.
        WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
        depth.format = NV_SCENE_DEPTH_FORMAT;
        depth.depthWriteEnabled = WGPUOptionalBool_False;
        depth.depthCompare = WGPUCompareFunction_Greater;
        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.label = (WGPUStringView){alpha ? "particles (alpha)" : "particles (additive)", WGPU_STRLEN};
        desc.layout = layout;
        desc.vertex.module = vfx->render_module;
        desc.vertex.entryPoint = (WGPUStringView){alpha ? "vs_alpha" : "vs_add", WGPU_STRLEN};
        desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        desc.primitive.cullMode = WGPUCullMode_None;
        desc.depthStencil = &depth;
        desc.multisample.count = samples;
        desc.fragment = &fragment;
        WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
        if (alpha)
            vfx->alpha_pipeline = pipeline;
        else
            vfx->add_pipeline = pipeline;
    }
    wgpuPipelineLayoutRelease(layout);

    // Segments add light; decals blend. Both are tested against the scene's depth and never write it.
    WGPUPipelineLayoutDescriptor marks_layout_desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    marks_layout_desc.bindGroupLayoutCount = 1;
    marks_layout_desc.bindGroupLayouts = &vfx->marks_layout;
    WGPUPipelineLayout marks_layout = wgpuDeviceCreatePipelineLayout(device, &marks_layout_desc);
    for (u32 decal = 0; decal < 2; ++decal) {
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        if (decal) {
            blend.color = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
            blend.alpha = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
        } else {
            blend.color = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
            blend.alpha = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
        }
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = NV_SCENE_FORMAT;
        target.blend = &blend;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = vfx->marks_module;
        fragment.entryPoint = (WGPUStringView){decal ? "fs_decal" : "fs_segment", WGPU_STRLEN};
        fragment.targetCount = 1;
        fragment.targets = &target;
        WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
        depth.format = NV_SCENE_DEPTH_FORMAT;
        depth.depthWriteEnabled = WGPUOptionalBool_False;
        depth.depthCompare = WGPUCompareFunction_Greater;
        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.label = (WGPUStringView){decal ? "decals" : "trails and beams", WGPU_STRLEN};
        desc.layout = marks_layout;
        desc.vertex.module = vfx->marks_module;
        desc.vertex.entryPoint = (WGPUStringView){decal ? "vs_decal" : "vs_segment", WGPU_STRLEN};
        desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        desc.primitive.cullMode = WGPUCullMode_None;
        desc.depthStencil = &depth;
        desc.multisample.count = samples;
        desc.fragment = &fragment;
        WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
        if (decal)
            vfx->decal_pipeline = pipeline;
        else
            vfx->segment_pipeline = pipeline;
    }
    wgpuPipelineLayoutRelease(marks_layout);
    vfx->pipeline_samples = samples;
}

// A ring of segments or decals: the CPU stages new slots and uploads them in runs, and when the ring
// is full the oldest are overwritten. `death` keeps when each slot expires, to count the live ones.
internal void ring_init(NvVfx* vfx, struct VfxRing* ring, const char* label, u32 capacity, u32 slot_bytes, NvArena* arena)
{
    ring->capacity = capacity;
    ring->staged = NV_PUSH_ARRAY(arena, (umm)STAGE_SLOTS * slot_bytes, u8);
    ring->death = NV_PUSH_ARRAY(arena, capacity, f32);
    ring->buffer = create_buffer(vfx->gpu, label, WGPUBufferUsage_Storage, (u64)capacity * slot_bytes);
}

internal void ring_flush(NvVfx* vfx, struct VfxRing* ring, u32 slot_bytes)
{
    if (!ring->pending)
        return;
    wgpuQueueWriteBuffer(vfx->gpu->queue, ring->buffer, (u64)ring->pending_start * slot_bytes, ring->staged, (umm)ring->pending * slot_bytes);
    ring->pending = 0;
}

// The next slot to fill, staged (zeroed). The run being staged stays contiguous in the ring: it is
// uploaded when full and when the ring wrapped, before the slot after the wrap is staged.
internal void* ring_add(NvVfx* vfx, struct VfxRing* ring, u32 slot_bytes, f32 death)
{
    if (!ring->capacity)
        return NULL;
    if (ring->pending && (ring->pending == STAGE_SLOTS || ring->head == 0))
        ring_flush(vfx, ring, slot_bytes);
    if (!ring->pending)
        ring->pending_start = ring->head;
    u8* slot = (u8*)ring->staged + (umm)ring->pending * slot_bytes;
    memset(slot, 0, slot_bytes);
    ++ring->pending;
    ring->death[ring->head] = death;
    ring->head = ring->head + 1 == ring->capacity ? 0 : ring->head + 1;
    if (ring->filled < ring->capacity)
        ++ring->filled;
    return slot;
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
    WGPUDevice device = gpu->device;

    // The state buffer has to fit one binding.
    u32 particles = capacity.particles ? capacity.particles : NV_VFX_DEFAULT_PARTICLES;
    WGPULimits limits = WGPU_LIMITS_INIT;
    u64 room = (u64)NV_VFX_MAX_PARTICLES;
    if (wgpuDeviceGetLimits(device, &limits) == WGPUStatus_Success) {
        u64 binding = limits.maxStorageBufferBindingSize / PARTICLE_BYTES;
        u64 buffer = limits.maxBufferSize / PARTICLE_BYTES;
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
        u64 binding = limits.maxStorageBufferBindingSize ? limits.maxStorageBufferBindingSize : (128ull << 20);
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
    vfx->frame_buffer = create_buffer(gpu, "vfx frame", WGPUBufferUsage_Uniform, sizeof(VfxFrameUniforms));
    vfx->particles = create_buffer(gpu, "particles", WGPUBufferUsage_Storage, capacity_u64 * PARTICLE_BYTES);
    vfx->free_list = create_buffer(gpu, "particle free list", WGPUBufferUsage_Storage, capacity_u64 * 4);
    vfx->alive[0] = create_buffer(gpu, "particle alive list A", WGPUBufferUsage_Storage, capacity_u64 * 4);
    vfx->alive[1] = create_buffer(gpu, "particle alive list B", WGPUBufferUsage_Storage, capacity_u64 * 4);
    vfx->visible = create_buffer(gpu, "particle visible list", WGPUBufferUsage_Storage, capacity_u64 * 4);
    vfx->counters = create_buffer(gpu, "particle counters", WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc, COUNTERS_COUNT * 4);
    vfx->emitter_buffer = create_buffer(gpu, "particle emitters", WGPUBufferUsage_Storage, (u64)NV_VFX_MAX_EMITTERS * EMITTER_BYTES);
    vfx->burst_buffer = create_buffer(gpu, "particle bursts", WGPUBufferUsage_Storage, (u64)NV_VFX_MAX_BURSTS * sizeof(NvVfxBurst));
    vfx->job_buffer = create_buffer(gpu, "particle spawn jobs", WGPUBufferUsage_Storage, (u64)MAX_JOBS * 8);
    vfx->sim_args = create_buffer(gpu, "particle simulate arguments", WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect, 16);
    vfx->draw_args = create_buffer(gpu, "particle draw arguments", WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect, 32);
    WGPUBufferDescriptor readback = WGPU_BUFFER_DESCRIPTOR_INIT;
    readback.label = (WGPUStringView){"particle stats", WGPU_STRLEN};
    readback.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    readback.size = 16;
    vfx->stats_buffer = wgpuDeviceCreateBuffer(device, &readback);

    ring_init(vfx, &vfx->segments, "trail and beam segments", segments, sizeof(NvVfxGpuSegment), arena);
    ring_init(vfx, &vfx->decals, "decals", decals, sizeof(NvVfxGpuDecal), arena);
    vfx->compute_module = create_module(device, "particle passes", "", vfx_compute_wgsl);
    vfx->render_module = create_module(device, "particle draw", vfx_shapes_wgsl, vfx_render_wgsl);
    vfx->marks_module = create_module(device, "trails, beams and decals", vfx_shapes_wgsl, vfx_marks_wgsl);

    const WGPUBufferBindingType U = WGPUBufferBindingType_Uniform, RW = WGPUBufferBindingType_Storage,
                                RO = WGPUBufferBindingType_ReadOnlyStorage;
    // Each pass binds only what it uses, so the indirect arguments are never bound where they are also read as arguments.
    const BindingKind emit_kinds[] = {{0, U}, {1, RW}, {2, RW}, {3, RW}, {6, RW}, {7, RO}, {8, RO}, {9, RO}};
    const BindingKind simulate_kinds[] = {{0, U}, {1, RW}, {2, RW}, {3, RW}, {4, RW}, {5, RW}, {6, RW}, {7, RO}};
    const BindingKind prepare_a_kinds[] = {{6, RW}, {10, RW}};
    const BindingKind prepare_b_kinds[] = {{6, RW}, {11, RW}};
    const BindingKind init_kinds[] = {{0, U}, {2, RW}, {6, RW}};
    const BindingKind render_kinds[] = {{0, U}, {1, RO}, {2, RO}, {3, RO}};
    const BindingKind marks_kinds[] = {{0, U}, {1, RO}, {2, RO}};
    vfx->marks_layout = create_layout(device, WGPUShaderStage_Vertex, marks_kinds, NV_ARRAY_COUNT(marks_kinds));
    vfx->layouts[0] = create_layout(device, WGPUShaderStage_Compute, emit_kinds, NV_ARRAY_COUNT(emit_kinds));
    vfx->layouts[1] = create_layout(device, WGPUShaderStage_Compute, simulate_kinds, NV_ARRAY_COUNT(simulate_kinds));
    vfx->layouts[2] = create_layout(device, WGPUShaderStage_Compute, prepare_a_kinds, NV_ARRAY_COUNT(prepare_a_kinds));
    vfx->layouts[3] = create_layout(device, WGPUShaderStage_Compute, prepare_b_kinds, NV_ARRAY_COUNT(prepare_b_kinds));
    vfx->layouts[4] = create_layout(device, WGPUShaderStage_Compute, init_kinds, NV_ARRAY_COUNT(init_kinds));
    vfx->render_layout = create_layout(device, WGPUShaderStage_Vertex, render_kinds, NV_ARRAY_COUNT(render_kinds));
    vfx->emit_pipeline = create_compute_pipeline(device, vfx->compute_module, "emit", vfx->layouts[0]);
    vfx->simulate_pipeline = create_compute_pipeline(device, vfx->compute_module, "simulate", vfx->layouts[1]);
    vfx->prepare_a_pipeline = create_compute_pipeline(device, vfx->compute_module, "prepare_a", vfx->layouts[2]);
    vfx->prepare_b_pipeline = create_compute_pipeline(device, vfx->compute_module, "prepare_b", vfx->layouts[3]);
    vfx->init_pipeline = create_compute_pipeline(device, vfx->compute_module, "init_free", vfx->layouts[4]);

    const u64 list = capacity_u64 * 4, state = capacity_u64 * PARTICLE_BYTES, emitters = (u64)NV_VFX_MAX_EMITTERS * EMITTER_BYTES;
    for (u32 parity = 0; parity < 2; ++parity) {
        WGPUBuffer cur = vfx->alive[parity], next = vfx->alive[parity ^ 1];
        const WGPUBuffer emit_buffers[] = {vfx->frame_buffer, vfx->particles, vfx->free_list, cur, vfx->counters, vfx->emitter_buffer,
                                           vfx->burst_buffer, vfx->job_buffer};
        const u64 emit_sizes[] = {sizeof(VfxFrameUniforms), state, list, list, COUNTERS_COUNT * 4, emitters,
                                  (u64)NV_VFX_MAX_BURSTS * sizeof(NvVfxBurst), (u64)MAX_JOBS * 8};
        vfx->emit_groups[parity] = create_group(device, vfx->layouts[0], emit_kinds, emit_buffers, emit_sizes, NV_ARRAY_COUNT(emit_kinds));
        const WGPUBuffer simulate_buffers[] = {vfx->frame_buffer, vfx->particles, vfx->free_list, cur, next, vfx->visible, vfx->counters,
                                               vfx->emitter_buffer};
        const u64 simulate_sizes[] = {sizeof(VfxFrameUniforms), state, list, list, list, list, COUNTERS_COUNT * 4, emitters};
        vfx->simulate_groups[parity] = create_group(device, vfx->layouts[1], simulate_kinds, simulate_buffers, simulate_sizes,
                                                    NV_ARRAY_COUNT(simulate_kinds));
    }
    {
        const WGPUBuffer buffers[] = {vfx->counters, vfx->sim_args};
        const u64 sizes[] = {COUNTERS_COUNT * 4, 16};
        vfx->prepare_a_group = create_group(device, vfx->layouts[2], prepare_a_kinds, buffers, sizes, 2);
    }
    {
        const WGPUBuffer buffers[] = {vfx->counters, vfx->draw_args};
        const u64 sizes[] = {COUNTERS_COUNT * 4, 32};
        vfx->prepare_b_group = create_group(device, vfx->layouts[3], prepare_b_kinds, buffers, sizes, 2);
    }
    {
        const WGPUBuffer buffers[] = {vfx->frame_buffer, vfx->free_list, vfx->counters};
        const u64 sizes[] = {sizeof(VfxFrameUniforms), list, COUNTERS_COUNT * 4};
        vfx->init_group = create_group(device, vfx->layouts[4], init_kinds, buffers, sizes, 3);
    }
    {
        const WGPUBuffer buffers[] = {vfx->frame_buffer, vfx->particles, vfx->visible, vfx->emitter_buffer};
        const u64 sizes[] = {sizeof(VfxFrameUniforms), state, list, emitters};
        vfx->render_group = create_group(device, vfx->render_layout, render_kinds, buffers, sizes, 4);
    }

    {
        const WGPUBuffer buffers[] = {vfx->frame_buffer, vfx->segments.buffer, vfx->decals.buffer};
        const u64 sizes[] = {sizeof(VfxFrameUniforms), (u64)segments * sizeof(NvVfxGpuSegment), (u64)decals * sizeof(NvVfxGpuDecal)};
        vfx->marks_group = create_group(device, vfx->marks_layout, marks_kinds, buffers, sizes, 3);
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
    NvVfxGpuSegment* segment = ring_add(vfx, &vfx->segments, sizeof(NvVfxGpuSegment), vfx->time + life);
    if (!segment)
        return;
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
    NvVfxGpuDecal* decal = ring_add(vfx, &vfx->decals, sizeof(NvVfxGpuDecal), vfx->time + style->life);
    if (!decal)
        return;
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

NvVfxStats nv_vfx_stats(const NvVfx* vfx)
{
    return vfx->stats;
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

void nv_vfx_compute(NvVfx* vfx, WGPUCommandEncoder encoder, const NvVfxFrame* frame, const WGPUPassTimestampWrites* timestamps)
{
    WGPUQueue queue = vfx->gpu->queue;
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
    wgpuQueueWriteBuffer(queue, vfx->frame_buffer, 0, &uniforms, sizeof(uniforms));
    if (vfx->emitters_dirty) {
        wgpuQueueWriteBuffer(queue, vfx->emitter_buffer, 0, vfx->gpu_emitters, (umm)vfx->emitter_count * EMITTER_BYTES);
        vfx->emitters_dirty = 0;
    }

    // One job per workgroup of 64 particles: which burst, and the first particle of the burst it spawns.
    u32 job_count = 0;
    u32 bursts_used = 0;
    for (u32 b = 0; b < vfx->burst_count; ++b) {
        u32 groups = (vfx->bursts[b].count + 63u) / 64u;
        if (job_count + groups > MAX_JOBS) {
            groups = MAX_JOBS - job_count;
            vfx->bursts[b].count = groups * 64u;
            if (!groups) {
                ++vfx->stats.bursts_lost;
                continue;
            }
        }
        for (u32 g = 0; g < groups; ++g) {
            vfx->jobs[job_count * 2] = b;
            vfx->jobs[job_count * 2 + 1] = g * 64u;
            ++job_count;
        }
        bursts_used = b + 1;
    }
    if (job_count) {
        wgpuQueueWriteBuffer(queue, vfx->burst_buffer, 0, vfx->bursts, (umm)bursts_used * sizeof(NvVfxBurst));
        wgpuQueueWriteBuffer(queue, vfx->job_buffer, 0, vfx->jobs, (umm)job_count * 8);
    }

    WGPUComputePassDescriptor pass_desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    pass_desc.label = (WGPUStringView){"particles", WGPU_STRLEN};
    pass_desc.timestampWrites = timestamps;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &pass_desc);
    if (vfx->clear_requested) {
        wgpuComputePassEncoderSetPipeline(pass, vfx->init_pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, vfx->init_group, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, (vfx->capacity + 63u) / 64u, 1, 1);
        vfx->clear_requested = 0;
    }
    if (job_count) {
        wgpuComputePassEncoderSetPipeline(pass, vfx->emit_pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, vfx->emit_groups[vfx->parity], 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, job_count, 1, 1);
    }
    wgpuComputePassEncoderSetPipeline(pass, vfx->prepare_a_pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, vfx->prepare_a_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderSetPipeline(pass, vfx->simulate_pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, vfx->simulate_groups[vfx->parity], 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, vfx->sim_args, 0);
    wgpuComputePassEncoderSetPipeline(pass, vfx->prepare_b_pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, vfx->prepare_b_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    vfx->burst_count = 0;
    vfx->parity ^= 1u;

    // The counters prepare reported, for the CPU, unless the last copy is still being read.
    vfx->stats_copied = 0;
    if (!vfx->stats_mapping) {
        wgpuCommandEncoderCopyBufferToBuffer(encoder, vfx->counters, 8 * 4, vfx->stats_buffer, 0, 16);
        vfx->stats_copied = 1;
    }
}

void nv_vfx_draw_decals(NvVfx* vfx, WGPURenderPassEncoder pass)
{
    if (!vfx->decals.filled)
        return;
    wgpuRenderPassEncoderSetPipeline(pass, vfx->decal_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, vfx->marks_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 6, vfx->decals.filled, 0, 0);
}

void nv_vfx_draw(NvVfx* vfx, WGPURenderPassEncoder pass)
{
    if (vfx->segments.filled) {
        wgpuRenderPassEncoderSetPipeline(pass, vfx->segment_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, vfx->marks_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 6, vfx->segments.filled, 0, 0);
    }
    wgpuRenderPassEncoderSetBindGroup(pass, 0, vfx->render_group, 0, NULL);
    // Alpha first (words 4 to 7 of the arguments), then additive (0 to 3).
    wgpuRenderPassEncoderSetPipeline(pass, vfx->alpha_pipeline);
    wgpuRenderPassEncoderDrawIndirect(pass, vfx->draw_args, 16);
    wgpuRenderPassEncoderSetPipeline(pass, vfx->add_pipeline);
    wgpuRenderPassEncoderDrawIndirect(pass, vfx->draw_args, 0);
}

internal void on_stats_mapped(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2)
{
    (void)message, (void)userdata2;
    NvVfx* vfx = userdata1;
    if (status == WGPUMapAsyncStatus_Success) {
        const u32* values = wgpuBufferGetConstMappedRange(vfx->stats_buffer, 0, 16);
        if (values) {
            vfx->stats.alive = values[0];
            vfx->stats.visible = values[1];
            vfx->stats.dropped = values[2];
        }
        wgpuBufferUnmap(vfx->stats_buffer);
    }
    vfx->stats_mapping = 0;
}

void nv_vfx_end_frame(NvVfx* vfx)
{
    // Counting the live segments walks a ring of up to hundreds of thousands: now and then is enough.
    if ((vfx->marks_frame++ & 7u) == 0) {
        vfx->stats.segments = ring_live(&vfx->segments, vfx->time);
        vfx->stats.decals = ring_live(&vfx->decals, vfx->time);
    }
    if (!vfx->stats_copied)
        return;
    vfx->stats_copied = 0;
    vfx->stats_mapping = 1;
    WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = on_stats_mapped;
    callback.userdata1 = vfx;
    wgpuBufferMapAsync(vfx->stats_buffer, WGPUMapMode_Read, 0, 16, callback);
}
