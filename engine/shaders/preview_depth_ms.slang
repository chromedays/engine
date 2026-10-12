// The same for a multisampled depth texture (the scene's with MSAA on): it shows sample 0.
import preview_common;

[[vk::binding(2, 0)]] DepthTexture2DMS d;

[shader("vertex")]
VsOut vs_main(VsIn in)
{
    return preview_vertex(in);
}

[shader("fragment")]
float4 fs_main(VsOut in) : SV_Target
{
    uint width, height, samples;
    d.GetDimensions(width, height, samples);
    uint2 texel = depth_texel(in.uv, uint2(width, height));
    return depth_color(d.Load(int2(texel), 0), in);
}
