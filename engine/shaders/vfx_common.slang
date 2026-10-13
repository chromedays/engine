// Declarations shared by the effects' compute passes and draws (engine/vfx.c): the frame's uniforms, the particle,
// emitter and burst layouts, the counter slots and the hash the particles are random from.

// The frame's uniforms, shared by the compute passes and the draw.
struct Frame {
    float4x4 view_proj;
    float4 planes[6];   // the camera's frustum, normalized, pointing inside
    float4 camera_right;
    float4 camera_up;
    float4 times;       // x: dt, y: time
    float4 wind;
    uint4 counts;       // x: capacity
};
[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame : register(b0, space0);

// 32 bytes: where it is, how old, what it moves with and how long it lives (half floats), its
// emitter and seed, and the scale it was fired with and its starting rotation (half floats).
struct Particle {
    float3 pos;
    float age;
    uint vel_xy;
    uint vel_z_life;
    uint emitter_seed; // seed in the top 20 bits, emitter in the low 12
    uint scale_rot;
};

struct Emitter {
    float2 life;
    float2 speed;
    float cone;
    float gravity;
    float drag;
    float turbulence;
    float turbulence_scale;
    float size_start;
    float size_end;
    float ground;
    float restitution;
    float friction;
    float stretch;
    float spin;
    uint shape;
    uint blend;
    uint pad0;
    uint pad1;
    float4 color0;
    float4 color1;
    float4 color2;
};

struct Burst {
    float3 pos;
    float scale;
    float3 dir;
    uint count;
    float3 end;
    uint emitter;
    uint seed;
    uint pad0;
    uint pad1;
    uint pad2;
};

// Indices into the counters buffer.
static const uint C_FREE = 0u;       // entries on the free list
static const uint C_ALIVE = 1u;      // entries in the current alive list
static const uint C_ALIVE_NEXT = 2u; // survivors written by simulate
static const uint C_VIS_ADD = 3u;    // visible additive particles
static const uint C_VIS_ALPHA = 4u;  // visible alpha particles
static const uint C_DROPPED = 5u;    // spawns refused, ever
static const uint C_STAT_ALIVE = 8u; // what prepare reports to the CPU
static const uint C_STAT_VISIBLE = 9u;
static const uint C_STAT_DROPPED = 10u;

uint pcg(uint v)
{
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

float unit(uint h)
{
    return float(h >> 8u) * (1.0 / 16777216.0);
}

// Two half floats in one word, the first in the low half (what WGSL's pack2x16float does).
uint pack_half2(float2 v)
{
    return f32tof16(v.x) | (f32tof16(v.y) << 16u);
}

float2 unpack_half2(uint u)
{
    return float2(f16tof32(u & 0xFFFFu), f16tof32(u >> 16u));
}
