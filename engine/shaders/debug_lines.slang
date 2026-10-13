// Debug lines (engine/renderer.c): positions and colors straight through.
struct DebugFrame {
    float4x4 view_proj;
};
[[vk::binding(0, 0)]] ConstantBuffer<DebugFrame> frame : register(b0, space0);

struct VsOut {
    float4 clip : SV_Position;
    float4 color : TEXCOORD0;
};

[shader("vertex")]
VsOut vs_main(float3 position : TEXCOORD0, float4 color : TEXCOORD1)
{
    VsOut out;
    out.clip = mul(frame.view_proj, float4(position, 1.0));
    out.color = color;
    return out;
}

[shader("fragment")]
float4 fs_main(VsOut in) : SV_Target
{
    return in.color;
}
