// Trails, beams and decals (engine/vfx.c): instances read from ring buffers, six vertices each. A slot that is not
// born yet or has expired is moved outside the clip volume, so nothing is drawn for it.
import vfx_shapes;

struct Segment {
    float3 a;
    float birth;
    float3 b;
    float life;
    float4 color0;
    float4 color1;
    float width0;
    float width1;
    float flicker;
    float scroll;
};

struct Decal {
    float3 pos;
    float angle;
    float size;
    float birth;
    float life;
    uint shape;
    float4 color;
};

[[vk::binding(1, 0)]] StructuredBuffer<Segment> segments;
[[vk::binding(2, 0)]] StructuredBuffer<Decal> decals;

static const float4 OUTSIDE = float4(2.0, 2.0, 2.0, 1.0);

struct SegmentOut {
    float4 clip : SV_Position;
    float2 uv : TEXCOORD0;    // x along the segment 0 to 1, y across -1 to 1
    float3 color : TEXCOORD1; // already faded and flickered
    float2 bands : TEXCOORD2; // x: length in meters, y: scroll phase (0 = no bands)
};

[shader("vertex")]
SegmentOut vs_segment(uint vi : SV_VertexID, uint ii : SV_InstanceID)
{
    SegmentOut out;
    out.clip = OUTSIDE;
    out.uv = float2(0.0, 0.0);
    out.color = float3(0.0, 0.0, 0.0);
    out.bands = float2(0.0, 0.0);
    Segment s = segments[ii];
    float age = frame.times.y - s.birth;
    if (s.life <= 0.0 || age < 0.0 || age >= s.life) {
        return out;
    }
    float t = age / s.life;
    float2 corner = CORNERS[vi];
    float3 d = s.b - s.a;
    // The strip turns around its axis to face the camera.
    float3 toward = cross(frame.camera_right.xyz, frame.camera_up.xyz);
    float3 side = cross(d, toward);
    float side_length = length(side);
    if (side_length > 1.0e-6) {
        side = side / side_length;
    } else {
        side = frame.camera_right.xyz;
    }
    float width = lerp(s.width0, s.width1, t);
    float along = corner.x * 0.5 + 0.5;
    float3 pos = lerp(s.a, s.b, along) + side * (corner.y * width * 0.5);
    float4 color = lerp(s.color0, s.color1, t);
    float flick = 1.0;
    if (s.flicker > 0.0) {
        flick = 1.0 - s.flicker * unit(pcg(uint(frame.times.y * 40.0) + ii * 7919u));
    }
    out.clip = mul(frame.view_proj, float4(pos, 1.0));
    out.uv = float2(along, corner.y);
    out.color = color.rgb * (color.a * flick);
    out.bands = float2(length(d), frame.times.y * s.scroll);
    if (s.scroll == 0.0) {
        out.bands.y = 0.0;
    }
    return out;
}

[shader("fragment")]
float4 fs_segment(SegmentOut in) : SV_Target
{
    float across = 1.0 - in.uv.y * in.uv.y;
    float k = across * across;
    if (in.bands.y != 0.0) {
        k = k * (0.65 + 0.35 * sin(6.2831853 * (in.uv.x * in.bands.x * 2.0 - in.bands.y)));
    }
    return float4(in.color * k, 0.0);
}

struct DecalOut {
    float4 clip : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    nointerpolation uint shape : TEXCOORD2;
    nointerpolation uint seed : TEXCOORD3;
};

[shader("vertex")]
DecalOut vs_decal(uint vi : SV_VertexID, uint ii : SV_InstanceID)
{
    DecalOut out;
    out.clip = OUTSIDE;
    out.uv = float2(0.0, 0.0);
    out.color = float4(0.0, 0.0, 0.0, 0.0);
    out.shape = 0u;
    out.seed = 0u;
    Decal d = decals[ii];
    float age = frame.times.y - d.birth;
    if (d.life <= 0.0 || age < 0.0 || age >= d.life) {
        return out;
    }
    float2 corner = CORNERS[vi];
    float c = cos(d.angle);
    float s = sin(d.angle);
    float2 r = float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c) * (d.size * 0.5);
    float3 pos = float3(d.pos.x + r.x, d.pos.y, d.pos.z + r.y);
    // Lifted off the ground a little, more with distance, where the depth buffer is coarser.
    // (A depth bias is not used: its size depends on the format and slope.)
    float w = mul(frame.view_proj, float4(pos, 1.0)).w;
    pos.y = pos.y + 0.002 + 0.0015 * max(w, 0.0);
    float t = age / d.life;
    float fade_share = float(d.shape >> 8u) / 255.0;
    float fade = 1.0;
    if (fade_share > 0.0) {
        fade = clamp((1.0 - t) / fade_share, 0.0, 1.0);
    }
    out.clip = mul(frame.view_proj, float4(pos, 1.0));
    out.uv = corner;
    out.color = float4(d.color.rgb, d.color.a * fade);
    out.shape = d.shape & 255u;
    out.seed = pcg(ii);
    return out;
}

[shader("fragment")]
float4 fs_decal(DecalOut in) : SV_Target
{
    return float4(in.color.rgb, shape_alpha(in.shape, in.uv, in.seed) * in.color.a);
}
