// The Textures tab's depth preview (engine/imgui.c).
import preview_common;

[[vk::binding(2, 0)]] DepthTexture2D d;

[shader("vertex")]
VsOut vs_main(VsIn in)
{
    return preview_vertex(in);
}

[shader("fragment")]
float4 fs_main(VsOut in) : SV_Target
{
    uint width, height;
    d.GetDimensions(width, height);
    uint2 texel = depth_texel(in.uv, uint2(width, height));
    return depth_color(d.Load(int3(int2(texel), 0)), in);
}
