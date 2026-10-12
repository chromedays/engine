// Depth only, seen from the light (engine/renderer.c): the same meshes and skinning as the scene, no fragment stage.
import renderer_frame;

struct StaticIn {
    [[vk::location(0)]] float3 position : POSITION;
};

struct SkinnedIn {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(3)]] uint4 joints : BLENDINDICES;
    [[vk::location(4)]] float4 weights : BLENDWEIGHT;
};

[shader("vertex")]
float4 vs_static(StaticIn in, uint i : SV_InstanceID) : SV_Position
{
    return mul(mul(frame.light_view_proj, objects[i].model), float4(in.position, 1.0));
}

[shader("vertex")]
float4 vs_skinned(SkinnedIn in, uint i : SV_InstanceID) : SV_Position
{
    return mul(mul(frame.light_view_proj, skinned_model(objects[i], in.joints, in.weights)), float4(in.position, 1.0));
}
