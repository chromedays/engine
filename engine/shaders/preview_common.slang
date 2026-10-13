// Shared by the three texture previews of engine/imgui.c (the Textures tab): ImGui's vertex shader again, and the parameters
// the fragment shaders read.
struct Uniforms {
    float4x4 mvp;
};

struct Params {
    float4 range; // min, max, near, far
    uint4 mode;   // NvImguiPreviewMode, linearize, NvImguiDepth
};

[[vk::binding(0, 0)]] ConstantBuffer<Uniforms> u : register(b0, space0);
[[vk::binding(3, 0)]] ConstantBuffer<Params> p : register(b3, space0);

// NOTE: slangc's WGSL takes a vertex attribute's location from its TEXCOORD<n> semantic (see mesh.slang).
struct VsIn {
    float2 pos : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 col : TEXCOORD2;
};

struct VsOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 col : TEXCOORD1;
};

// NOTE: slangc compiles only the entry points of the file it is given, not those of what it imports, so each preview has its own
// vs_main that calls this.
VsOut preview_vertex(VsIn in)
{
    VsOut out;
    out.pos = mul(u.mvp, float4(in.pos, 0.0, 1.0));
    out.uv = in.uv;
    out.col = float4(pow(in.col.rgb, float3(2.2)), in.col.a);
    return out;
}

float4 gray(float v)
{
    return float4(float3(v), 1.0);
}

// Depth cannot be filtered, so it is read texel by texel. Reverse Z turns back into distance t:
// the standard depth s = 1 - d is far (t - near) / ((far - near) t) for a perspective camera, and
// (t - near) / (far - near) for an orthographic one (nv_mat4_perspective, nv_mat4_orthographic).
float4 depth_color(float z, VsOut in)
{
    float near = p.range.z;
    float far = p.range.w;
    float v = z;
    if (p.mode.z == 1u) {
        v = far * near / (far - (1.0 - z) * (far - near));
    } else if (p.mode.z == 2u) {
        v = near + (1.0 - z) * (far - near);
    }
    float g = clamp((v - p.range.x) / max(p.range.y - p.range.x, 1e-6), 0.0, 1.0);
    return float4(float3(pow(g, 2.2)), 1.0) * in.col;
}

uint2 depth_texel(float2 uv, uint2 size)
{
    return min(uint2(clamp(uv, float2(0.0), float2(1.0)) * float2(size)), size - uint2(1u));
}
