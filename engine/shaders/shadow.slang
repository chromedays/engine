// Depth only, seen from the light (engine/renderer.c): the same meshes and skinning as the scene, no fragment stage.
import renderer_frame;

struct StaticIn {
    float3 position : TEXCOORD0;
};

struct SkinnedIn {
    float3 position : TEXCOORD0;
    uint4 joints : TEXCOORD3;
    float4 weights : TEXCOORD4;
};

[shader("vertex")]
float4 vs_static(StaticIn in, uint i : SV_InstanceID) : SV_Position
{
    return mul(mul(frame.light_view_proj, objects[object_index(i)].model), float4(in.position, 1.0));
}

[shader("vertex")]
float4 vs_skinned(SkinnedIn in, uint i : SV_InstanceID) : SV_Position
{
    return mul(mul(frame.light_view_proj, skinned_model(objects[object_index(i)], in.joints, in.weights)), float4(in.position, 1.0));
}
