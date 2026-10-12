// The Textures tab's color preview (engine/imgui.c): a texture through a channel selector, linearized when it is sRGB.
import preview_common;

[[vk::binding(1, 0)]] SamplerState s;
[[vk::binding(2, 0)]] Texture2D<float4> t;

[shader("vertex")]
VsOut vs_main(VsIn in)
{
    return preview_vertex(in);
}

[shader("fragment")]
float4 fs_main(VsOut in) : SV_Target
{
    float4 raw = t.Sample(s, in.uv);
    float4 c = raw;
    if (p.mode.y != 0u) {
        c = float4(pow(raw.rgb, float3(2.2)), raw.a);
    }
    float4 out = c;
    switch (p.mode.x) {
    case 1u: out = float4(c.rgb, 1.0); break;
    case 2u: out = gray(c.r); break;
    case 3u: out = gray(c.g); break;
    case 4u: out = gray(c.b); break;
    case 5u: out = gray(pow(raw.a, 2.2)); break;
    default: break;
    }
    return out * in.col;
}
