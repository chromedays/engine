// The scene pass's meshes (engine/renderer.c): every mesh node is one instance, and the vertex shader finds its object by the
// instance index. Lit by the first directional light with its shadow map, plus the ambient light.
import renderer_frame;

struct Material {
    float4 base_color;
};
[[vk::binding(3, 0)]] DepthTexture2D shadow_map;
[[vk::binding(4, 0)]] SamplerComparisonState shadow_sampler;
[[vk::binding(0, 1)]] ConstantBuffer<Material> material;
[[vk::binding(1, 1)]] SamplerState material_sampler;
[[vk::binding(2, 1)]] Texture2D<float4> base_color_texture;

// NOTE: slangc's WGSL ignores [[vk::location]] on vertex inputs and numbers them in order; the number of a TEXCOORD<n> semantic
// is what it uses as the location (the pipelines in renderer.c name the locations), so every input and varying says it that way.
struct StaticIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

struct SkinnedIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    uint4 joints : TEXCOORD3;
    float4 weights : TEXCOORD4;
};

struct VsOut {
    float4 clip : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float3 world : TEXCOORD2;
};

VsOut finish(float4x4 model, float3 position, float3 normal, float2 uv)
{
    VsOut out;
    float4 world = mul(model, float4(position, 1.0));
    out.clip = mul(frame.view_proj, world);
    out.normal = mul(model, float4(normal, 0.0)).xyz;
    out.uv = uv;
    out.world = world.xyz;
    return out;
}

[shader("vertex")]
VsOut vs_static(StaticIn in, uint i : SV_InstanceID)
{
    return finish(objects[i].model, in.position, in.normal, in.uv);
}

[shader("vertex")]
VsOut vs_skinned(SkinnedIn in, uint i : SV_InstanceID)
{
    return finish(skinned_model(objects[i], in.joints, in.weights), in.position, in.normal, in.uv);
}

// How much of the directional light reaches `world`: 1 lit, 0 in shadow. A comparison sampler
// with linear filtering compares the four nearest texels and blends the results (a 2x2
// percentage-closer filter per lookup); High averages 3x3 such lookups a texel apart.
float shadow_lit(float3 world, float3 n)
{
    float4 p = mul(frame.light_view_proj, float4(world + n * frame.shadow.z, 1.0));
    float2 uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    float lit = shadow_map.SampleCmpLevelZero(shadow_sampler, uv, p.z);
    if (frame.shadow.y > 1.5) {
        float sum = 0.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                float2 offset = float2(float(x), float(y)) * frame.shadow.x;
                sum += shadow_map.SampleCmpLevelZero(shadow_sampler, uv + offset, p.z);
            }
        }
        lit = sum / 9.0;
    }
    bool inside = all(uv >= float2(0.0)) && all(uv <= float2(1.0)) && p.z >= 0.0 && p.z <= 1.0;
    lit = (inside && frame.shadow.y > 0.5) ? lit : 1.0;
    float span = max(frame.shadow_fade.y - frame.shadow_fade.x, 0.001);
    float fade = clamp((distance(world, frame.camera_pos.xyz) - frame.shadow_fade.x) / span, 0.0, 1.0);
    return lerp(lit, 1.0, fade);
}

[shader("fragment")]
float4 fs_main(VsOut in, bool front : SV_IsFrontFace) : SV_Target
{
    float3 n = normalize(in.normal);
    if (!front) {
        n = -n;
    }
    float4 base = material.base_color * base_color_texture.Sample(material_sampler, in.uv);
    float diffuse = max(dot(n, -frame.light_dir.xyz), 0.0) * shadow_lit(in.world, n);
    float3 light = frame.ambient.rgb + frame.light_color.rgb * diffuse;
    return float4(base.rgb * light, base.a);
}
