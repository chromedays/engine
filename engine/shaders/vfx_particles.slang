// The particles' draw (engine/vfx.c): vertex pulling, six vertices per visible particle, additive and alpha blended.
import vfx_shapes;

[[vk::binding(1, 0)]] StructuredBuffer<Particle> particles;
[[vk::binding(2, 0)]] StructuredBuffer<uint> visible;
[[vk::binding(3, 0)]] StructuredBuffer<Emitter> emitters;

struct VsOut {
    float4 clip : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    nointerpolation uint shape : TEXCOORD2;
    nointerpolation uint seed : TEXCOORD3;
};

// Vertex pulling: six vertices per visible particle, found by the vertex index. The additive list
// starts at the front of the visible buffer and the alpha list at the back.
VsOut shade(uint vi, bool alpha)
{
    uint k = vi / 6u;
    float2 corner = CORNERS[vi % 6u];
    uint slot = visible[k];
    if (alpha) {
        slot = visible[frame.counts.x - 1u - k];
    }
    Particle p = particles[slot];
    Emitter e = emitters[p.emitter_seed & 0xFFFu];
    float2 vxy = unpack_half2(p.vel_xy);
    float2 vzl = unpack_half2(p.vel_z_life);
    float3 vel = float3(vxy.x, vxy.y, vzl.x);
    float t = clamp(p.age / vzl.y, 0.0, 1.0);
    float2 scale_rot = unpack_half2(p.scale_rot);
    float size = lerp(e.size_start, e.size_end, t) * scale_rot.x;
    uint seed = p.emitter_seed >> 12u;
    float spin = (unit(pcg(seed)) * 2.0 - 1.0) * e.spin;
    float angle = scale_rot.y + spin * p.age;
    float3 right = frame.camera_right.xyz;
    float3 up = frame.camera_up.xyz;

    float3 offset = (right * corner.x + up * corner.y) * size;
    if (e.stretch > 0.0) {
        // Lengthened along the velocity as the camera sees it.
        float2 screen = float2(dot(vel, right), dot(vel, up));
        float speed = length(screen);
        if (speed > 0.001) {
            float2 d = screen / speed;
            float3 axis = right * d.x + up * d.y;
            float3 across = right * (-d.y) + up * d.x;
            offset = axis * (corner.x * size * (1.0 + e.stretch * speed)) + across * (corner.y * size);
        }
    } else {
        float c = cos(angle);
        float s = sin(angle);
        float2 r = float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c);
        offset = (right * r.x + up * r.y) * size;
    }

    float4 color = lerp(e.color0, e.color1, clamp(t * 2.0, 0.0, 1.0));
    if (t > 0.5) {
        color = lerp(e.color1, e.color2, (t - 0.5) * 2.0);
    }
    VsOut out;
    out.clip = mul(frame.view_proj, float4(p.pos + offset, 1.0));
    out.uv = corner;
    out.color = color;
    out.shape = e.shape;
    out.seed = seed;
    return out;
}

[shader("vertex")]
VsOut vs_add(uint vi : SV_VertexID)
{
    return shade(vi, false);
}

[shader("vertex")]
VsOut vs_alpha(uint vi : SV_VertexID)
{
    return shade(vi, true);
}

[shader("fragment")]
float4 fs_add(VsOut in) : SV_Target
{
    float a = shape_alpha(in.shape, in.uv, in.seed) * in.color.a;
    return float4(in.color.rgb * a, 0.0);
}

[shader("fragment")]
float4 fs_alpha(VsOut in) : SV_Target
{
    return float4(in.color.rgb, shape_alpha(in.shape, in.uv, in.seed) * in.color.a);
}
