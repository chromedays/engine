// The last pass of the scene (engine/renderer.c): shows the scene's color on the canvas with the nearest filter, adds the
// bloom, applies the exposure and the tone mapping. The canvas's sRGB view encodes at the end.
struct Params {
    float2 origin;
    float2 block;
    float2 size;
    float exposure;
    float tone;
    float4 bloom;     // x: intensity (0 = off), yz: the valid size of bloom mip 0 in texels
    float4 bloom_tex; // xy: the size of that mip's texture
};
[[vk::binding(0, 0)]] ConstantBuffer<Params> p : register(b0, space0);
[[vk::binding(1, 0)]] Texture2D<float4> scene : register(t1, space0);
[[vk::binding(2, 0)]] Texture2D<float4> bloom_tex : register(t2, space0);
[[vk::binding(3, 0)]] SamplerState bloom_sampler : register(s3, space0);

[shader("vertex")]
float4 vs_main(uint i : SV_VertexID) : SV_Position
{
    float2 uv = float2(float((i << 1u) & 2u), float(i & 2u));
    return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

// Khronos PBR Neutral tone mapper (github.com/KhronosGroup/ToneMapping, Apache-2.0): colors whose
// brightest channel is below 0.76 change by at most 0.04, brighter ones are compressed toward 1
// by their peak so the hue stays, and the very bright lose saturation toward white.
float3 tone_pbr_neutral(float3 input)
{
    float start_compression = 0.8 - 0.04;
    float desaturation = 0.15;
    float3 color = input;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= float3(offset);
    float peak = max(color.r, max(color.g, color.b));
    if (peak < start_compression) {
        return color;
    }
    float d = 1.0 - start_compression;
    float new_peak = 1.0 - d * d / (peak + d - start_compression);
    color *= new_peak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return lerp(color, new_peak * float3(1.0), g);
}

// Krzysztof Narkowicz's fit of the ACES filmic curve.
float3 tone_aces(float3 x)
{
    return clamp((x * (2.51 * x + float3(0.03))) / (x * (2.43 * x + float3(0.59)) + float3(0.14)), float3(0.0), float3(1.0));
}

float3 tone_map(float3 color)
{
    float3 c = max(color * p.exposure, float3(0.0));
    if (p.tone > 1.5) {
        return tone_aces(c);
    }
    if (p.tone > 0.5) {
        return clamp(tone_pbr_neutral(c), float3(0.0), float3(1.0));
    }
    return clamp(c, float3(0.0), float3(1.0));
}

[shader("fragment")]
float4 fs_main(float4 pos : SV_Position) : SV_Target
{
    // Nearest: the scene pixel under this screen pixel, counted from the image's corner.
    int2 t = int2(floor((pos.xy - p.origin) / p.block));
    int2 c = clamp(t, int2(0), int2(p.size) - int2(1));
    float3 color = scene.Load(int3(c, 0)).rgb;
    // Bloom mip 0 covers two scene pixels per texel; bilinear, kept inside what the chain wrote.
    if (p.bloom.x > 0.0) {
        float2 q = clamp((float2(c) + float2(0.5)) * 0.5, float2(0.5), p.bloom.yz - float2(0.5));
        color += bloom_tex.SampleLevel(bloom_sampler, q / p.bloom_tex.xy, 0.0).rgb * p.bloom.x;
    }
    return float4(tone_map(color), 1.0);
}
