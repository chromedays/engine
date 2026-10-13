// What the scene pass and the shadow pass share (engine/renderer.c): the frame's uniforms, the objects (one per mesh node, found
// by the instance index) and the skin's joint matrices.
struct Frame {
    float4x4 view_proj;
    float4x4 light_view_proj; // world to the shadow map's clip space
    float4 light_dir;
    float4 light_color;
    float4 ambient;
    float4 camera_pos;
    float4 shadow;      // x: one texel in uv, y: 0 off / 1 low / 2 high, z: normal offset (m)
    float4 shadow_fade; // x, y: distances from the camera where shadows start and finish fading
};

struct Object {
    float4x4 model;
    uint joint_offset; // first matrix in the skin buffer, or 0xFFFFFFFF
    // NvObjectData has 80 bytes: WGSL rounds a struct up to its alignment and HLSL does not, and the D3D12 view must give the same
    // stride the shader was compiled with. Three scalars, not a uint3: a vec3 would start at 80 in WGSL and make it 96.
    uint padding0;
    uint padding1;
    uint padding2;
};

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame : register(b0, space0);
[[vk::binding(1, 0)]] StructuredBuffer<Object> objects : register(t1, space0);
[[vk::binding(2, 0)]] StructuredBuffer<float4x4> skin : register(t2, space0);

float4x4 skinned_model(Object object, uint4 joints, float4 weights)
{
    if (object.joint_offset == 0xFFFFFFFFu) {
        return object.model;
    }
    uint o = object.joint_offset;
    float4x4 s = skin[o + joints.x] * weights.x + skin[o + joints.y] * weights.y +
                 skin[o + joints.z] * weights.z + skin[o + joints.w] * weights.w;
    return mul(object.model, s);
}

// The object an instance draws: nv_gpu_draw_indexed passes the object's number as the draw's first instance. WebGPU's
// instance_index counts from it, but D3D12's SV_InstanceID does not (it leaves out StartInstanceLocation), so there the backend
// puts the first instance in a root constant (engine/gpu_d3d12.c) and it is added here. The build defines NV_D3D12 for D3D12.
#if defined(NV_D3D12)
struct FirstInstance {
    uint value;
};
ConstantBuffer<FirstInstance> first_instance : register(b0, space8);

uint object_index(uint instance_id)
{
    return instance_id + first_instance.value;
}
#else
uint object_index(uint instance_id)
{
    return instance_id;
}
#endif
