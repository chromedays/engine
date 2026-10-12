// Debug lines (engine/renderer.c): positions and colors straight through.
struct DebugFrame {
    float4x4 view_proj;
};
[[vk::binding(0, 0)]] ConstantBuffer<DebugFrame> frame;

struct VsOut {
    float4 clip : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

[shader("vertex")]
VsOut vs_main([[vk::location(0)]] float3 position : POSITION, [[vk::location(1)]] float4 color : COLOR0)
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
