// Dear ImGui's draw lists (engine/imgui.c). Vertex colors are authored in sRGB, but the canvas view is
// sRGB-encoding, so they are linearized here to avoid applying the curve twice.
struct Uniforms {
    float4x4 mvp;
};
[[vk::binding(0, 0)]] ConstantBuffer<Uniforms> u : register(b0, space0);
[[vk::binding(1, 0)]] SamplerState s : register(s1, space0);
[[vk::binding(2, 0)]] Texture2D<float4> t : register(t2, space0);

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

[shader("vertex")]
VsOut vs_main(VsIn in)
{
    VsOut out;
    out.pos = mul(u.mvp, float4(in.pos, 0.0, 1.0));
    out.uv = in.uv;
    out.col = float4(pow(in.col.rgb, float3(2.2)), in.col.a);
    return out;
}

[shader("fragment")]
float4 fs_main(VsOut in) : SV_Target
{
    return in.col * t.Sample(s, in.uv);
}
