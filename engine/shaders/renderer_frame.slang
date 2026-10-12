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
};

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<Object> objects;
[[vk::binding(2, 0)]] StructuredBuffer<float4x4> skin;

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
