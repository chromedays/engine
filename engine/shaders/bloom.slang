// Bloom after Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare"
// (SIGGRAPH 2014): a 13-tap downsample (the first step weights its groups of four taps by
// 1 / (1 + luma), Karis's average, so a single very bright pixel does not flicker) and a 3x3 tent
// upsample added into the level above. Every tap is clamped to what the level before it wrote, so the
// padding of the scene targets does not bleed in.
struct Pass {
    float2 src_tex;    // the source texture's size in texels
    float2 src_region; // the part of it that holds the image
};
[[vk::binding(0, 0)]] ConstantBuffer<Pass> p;
[[vk::binding(1, 0)]] Texture2D<float4> src;
[[vk::binding(2, 0)]] SamplerState samp;

[shader("vertex")]
float4 vs_main(uint i : SV_VertexID) : SV_Position
{
    float2 uv = float2(float((i << 1u) & 2u), float(i & 2u));
    return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

float3 tap(float2 q)
{
    float2 c = clamp(q, float2(0.5), p.src_region - float2(0.5));
    return src.SampleLevel(samp, c / p.src_tex, 0.0).rgb;
}

float karis(float3 c)
{
    return 1.0 / (1.0 + dot(c, float3(0.2126, 0.7152, 0.0722)));
}

// Two destination pixels are one source pixel apart on each axis, so the destination pixel's center
// is the corner shared by four source texels, and a bilinear tap there averages them.
[shader("fragment")]
float4 fs_down_first(float4 pos : SV_Position) : SV_Target
{
    float2 o = pos.xy * 2.0;
    float3 a = tap(o + float2(-2.0, -2.0)); float3 b = tap(o + float2(0.0, -2.0)); float3 c = tap(o + float2(2.0, -2.0));
    float3 d = tap(o + float2(-2.0, 0.0));  float3 e = tap(o);                     float3 f = tap(o + float2(2.0, 0.0));
    float3 g = tap(o + float2(-2.0, 2.0));  float3 h = tap(o + float2(0.0, 2.0));  float3 i = tap(o + float2(2.0, 2.0));
    float3 j = tap(o + float2(-1.0, -1.0)); float3 k = tap(o + float2(1.0, -1.0));
    float3 l = tap(o + float2(-1.0, 1.0));  float3 m = tap(o + float2(1.0, 1.0));
    float3 g0 = (a + b + d + e) * (0.125 / 4.0);
    float3 g1 = (b + c + e + f) * (0.125 / 4.0);
    float3 g2 = (d + e + g + h) * (0.125 / 4.0);
    float3 g3 = (e + f + h + i) * (0.125 / 4.0);
    float3 g4 = (j + k + l + m) * (0.5 / 4.0);
    g0 *= karis(g0); g1 *= karis(g1); g2 *= karis(g2); g3 *= karis(g3); g4 *= karis(g4);
    return float4(max(g0 + g1 + g2 + g3 + g4, float3(0.0001)), 1.0);
}

[shader("fragment")]
float4 fs_down(float4 pos : SV_Position) : SV_Target
{
    float2 o = pos.xy * 2.0;
    float3 a = tap(o + float2(-2.0, -2.0)); float3 b = tap(o + float2(0.0, -2.0)); float3 c = tap(o + float2(2.0, -2.0));
    float3 d = tap(o + float2(-2.0, 0.0));  float3 e = tap(o);                     float3 f = tap(o + float2(2.0, 0.0));
    float3 g = tap(o + float2(-2.0, 2.0));  float3 h = tap(o + float2(0.0, 2.0));  float3 i = tap(o + float2(2.0, 2.0));
    float3 j = tap(o + float2(-1.0, -1.0)); float3 k = tap(o + float2(1.0, -1.0));
    float3 l = tap(o + float2(-1.0, 1.0));  float3 m = tap(o + float2(1.0, 1.0));
    float3 down = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    return float4(down, 1.0);
}

// The destination is twice the source's size; the tent reaches one source texel around.
[shader("fragment")]
float4 fs_up(float4 pos : SV_Position) : SV_Target
{
    float2 o = pos.xy * 0.5;
    float3 a = tap(o + float2(-1.0, 1.0));  float3 b = tap(o + float2(0.0, 1.0));  float3 c = tap(o + float2(1.0, 1.0));
    float3 d = tap(o + float2(-1.0, 0.0));  float3 e = tap(o);                     float3 f = tap(o + float2(1.0, 0.0));
    float3 g = tap(o + float2(-1.0, -1.0)); float3 h = tap(o + float2(0.0, -1.0)); float3 i = tap(o + float2(1.0, -1.0));
    return float4((e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) * (1.0 / 16.0), 0.0);
}
