#include "nv/renderer.h"
#include "nv/log.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NV_TIMESTAMP_COUNT 12 // scene (0, 1), shadow (2, 3), upscale (4, 5), bloom (6 to 9), particles (10, 11)

internal void create_upscale_pipeline(NvRenderer* renderer);
internal void create_bloom_pipelines(NvRenderer* renderer);

#define NO_SKIN 0xFFFFFFFFu

// Layouts match the WGSL structs below.
typedef struct FrameUniforms {
    NvMat4 view_proj;
    NvMat4 light_view_proj; // world to the shadow map's clip space
    f32 light_dir[4];
    f32 light_color[4];
    f32 ambient[4];
    f32 camera_pos[4];
    f32 shadow[4];      // x: one texel in uv, y: 0 off / 1 low / 2 high, z: normal offset (m)
    f32 shadow_fade[4]; // x, y: distances from the camera where shadows start and finish fading
} FrameUniforms;

// Shadow pass bias per format. The constant counts the format's smallest depth step, which is
// fixed for depth16unorm but depends on each triangle's depth for depth32float, so each format has
// its own; the slope scale is shared.
#define SHADOW_BIAS_SLOPE 2.0f
global const s32 shadow_bias_constant[2] = {
    [NV_SHADOW_FORMAT_DEPTH32F] = 0,
    [NV_SHADOW_FORMAT_DEPTH16] = 8,
};
#define SHADOW_PULLBACK 50.0f // meters the light's box reaches back toward the light, for casters

#define FRAME_WGSL \
    "struct Frame {\n" \
    "    view_proj: mat4x4f,\n" \
    "    light_view_proj: mat4x4f,\n" \
    "    light_dir: vec4f,\n" \
    "    light_color: vec4f,\n" \
    "    ambient: vec4f,\n" \
    "    camera_pos: vec4f,\n" \
    "    shadow: vec4f,\n" \
    "    shadow_fade: vec4f,\n" \
    "};\n" \
    "struct Object {\n" \
    "    model: mat4x4f,\n" \
    "    joint_offset: u32,\n" \
    "};\n" \
    "@group(0) @binding(0) var<uniform> frame: Frame;\n" \
    "@group(0) @binding(1) var<storage, read> objects: array<Object>;\n" \
    "@group(0) @binding(2) var<storage, read> skin: array<mat4x4f>;\n" \
    "fn skinned_model(object: Object, joints: vec4u, weights: vec4f) -> mat4x4f {\n" \
    "    if (object.joint_offset == 0xFFFFFFFFu) { return object.model; }\n" \
    "    let o = object.joint_offset;\n" \
    "    let s = skin[o + joints.x] * weights.x + skin[o + joints.y] * weights.y +\n" \
    "            skin[o + joints.z] * weights.z + skin[o + joints.w] * weights.w;\n" \
    "    return object.model * s;\n" \
    "}\n"

typedef struct NvObjectData {
    NvMat4 model;
    u32 joint_offset; // first matrix in the skin buffer, or NO_SKIN
    u32 pad[3];
} NvObjectData;

typedef struct MaterialUniforms {
    f32 base_color[4];
} MaterialUniforms;

// Every mesh node becomes one instance: the vertex shader finds its object data by instance index.
global const char* mesh_shader =
    FRAME_WGSL
    "struct Material {\n"
    "    base_color: vec4f,\n"
    "};\n"
    "@group(0) @binding(3) var shadow_map: texture_depth_2d;\n"
    "@group(0) @binding(4) var shadow_sampler: sampler_comparison;\n"
    "@group(1) @binding(0) var<uniform> material: Material;\n"
    "@group(1) @binding(1) var material_sampler: sampler;\n"
    "@group(1) @binding(2) var base_color_texture: texture_2d<f32>;\n"
    "\n"
    "struct StaticIn {\n"
    "    @location(0) position: vec3f,\n"
    "    @location(1) normal: vec3f,\n"
    "    @location(2) uv: vec2f,\n"
    "};\n"
    "struct SkinnedIn {\n"
    "    @location(0) position: vec3f,\n"
    "    @location(1) normal: vec3f,\n"
    "    @location(2) uv: vec2f,\n"
    "    @location(3) joints: vec4u,\n"
    "    @location(4) weights: vec4f,\n"
    "};\n"
    "struct VsOut {\n"
    "    @builtin(position) clip: vec4f,\n"
    "    @location(0) normal: vec3f,\n"
    "    @location(1) uv: vec2f,\n"
    "    @location(2) world: vec3f,\n"
    "};\n"
    "\n"
    "fn finish(model: mat4x4f, position: vec3f, normal: vec3f, uv: vec2f) -> VsOut {\n"
    "    var out: VsOut;\n"
    "    let world = model * vec4f(position, 1.0);\n"
    "    out.clip = frame.view_proj * world;\n"
    "    out.normal = (model * vec4f(normal, 0.0)).xyz;\n"
    "    out.uv = uv;\n"
    "    out.world = world.xyz;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "@vertex\n"
    "fn vs_static(in: StaticIn, @builtin(instance_index) i: u32) -> VsOut {\n"
    "    return finish(objects[i].model, in.position, in.normal, in.uv);\n"
    "}\n"
    "\n"
    "@vertex\n"
    "fn vs_skinned(in: SkinnedIn, @builtin(instance_index) i: u32) -> VsOut {\n"
    "    return finish(skinned_model(objects[i], in.joints, in.weights), in.position, in.normal, in.uv);\n"
    "}\n"
    "\n"
    // How much of the directional light reaches `world`: 1 lit, 0 in shadow. A comparison sampler
    // with linear filtering compares the four nearest texels and blends the results (a 2x2
    // percentage-closer filter per lookup); High averages 3x3 such lookups a texel apart.
    "fn shadow_lit(world: vec3f, n: vec3f) -> f32 {\n"
    "    let p = frame.light_view_proj * vec4f(world + n * frame.shadow.z, 1.0);\n"
    "    let uv = vec2f(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);\n"
    "    var lit = textureSampleCompareLevel(shadow_map, shadow_sampler, uv, p.z);\n"
    "    if (frame.shadow.y > 1.5) {\n"
    "        var sum = 0.0;\n"
    "        for (var y = -1; y <= 1; y++) {\n"
    "            for (var x = -1; x <= 1; x++) {\n"
    "                let offset = vec2f(f32(x), f32(y)) * frame.shadow.x;\n"
    "                sum += textureSampleCompareLevel(shadow_map, shadow_sampler, uv + offset, p.z);\n"
    "            }\n"
    "        }\n"
    "        lit = sum / 9.0;\n"
    "    }\n"
    "    let inside = all(uv >= vec2f(0.0)) && all(uv <= vec2f(1.0)) && p.z >= 0.0 && p.z <= 1.0;\n"
    "    lit = select(1.0, lit, inside && frame.shadow.y > 0.5);\n"
    "    let span = max(frame.shadow_fade.y - frame.shadow_fade.x, 0.001);\n"
    "    let fade = clamp((distance(world, frame.camera_pos.xyz) - frame.shadow_fade.x) / span, 0.0, 1.0);\n"
    "    return mix(lit, 1.0, fade);\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_main(in: VsOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {\n"
    "    var n = normalize(in.normal);\n"
    "    if (!front) { n = -n; }\n"
    "    let base = material.base_color * textureSample(base_color_texture, material_sampler, in.uv);\n"
    "    let diffuse = max(dot(n, -frame.light_dir.xyz), 0.0) * shadow_lit(in.world, n);\n"
    "    let light = frame.ambient.rgb + frame.light_color.rgb * diffuse;\n"
    "    return vec4f(base.rgb * light, base.a);\n"
    "}\n";

// Depth only, seen from the light: the same meshes and skinning as the scene, no fragment stage.
global const char* shadow_shader =
    FRAME_WGSL
    "struct StaticIn {\n"
    "    @location(0) position: vec3f,\n"
    "};\n"
    "struct SkinnedIn {\n"
    "    @location(0) position: vec3f,\n"
    "    @location(3) joints: vec4u,\n"
    "    @location(4) weights: vec4f,\n"
    "};\n"
    "@vertex\n"
    "fn vs_static(in: StaticIn, @builtin(instance_index) i: u32) -> @builtin(position) vec4f {\n"
    "    return frame.light_view_proj * objects[i].model * vec4f(in.position, 1.0);\n"
    "}\n"
    "@vertex\n"
    "fn vs_skinned(in: SkinnedIn, @builtin(instance_index) i: u32) -> @builtin(position) vec4f {\n"
    "    return frame.light_view_proj * skinned_model(objects[i], in.joints, in.weights) * vec4f(in.position, 1.0);\n"
    "}\n";

global const char* debug_shader =
    "struct Frame {\n"
    "    view_proj: mat4x4f,\n"
    "};\n"
    "@group(0) @binding(0) var<uniform> frame: Frame;\n"
    "struct VsOut {\n"
    "    @builtin(position) clip: vec4f,\n"
    "    @location(0) color: vec4f,\n"
    "};\n"
    "@vertex\n"
    "fn vs_main(@location(0) position: vec3f, @location(1) color: vec4f) -> VsOut {\n"
    "    var out: VsOut;\n"
    "    out.clip = frame.view_proj * vec4f(position, 1.0);\n"
    "    out.color = color;\n"
    "    return out;\n"
    "}\n"
    "@fragment\n"
    "fn fs_main(in: VsOut) -> @location(0) vec4f {\n"
    "    return in.color;\n"
    "}\n";

internal WGPUBuffer create_buffer(NvGpu* gpu, WGPUBufferUsage usage, const void* data, umm size)
{
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.usage = usage | WGPUBufferUsage_CopyDst;
    desc.size = (size + 3) & ~(umm)3;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu->device, &desc);
    if (data)
        wgpuQueueWriteBuffer(gpu->queue, buffer, 0, data, size);
    return buffer;
}

internal WGPUShaderModule create_shader(WGPUDevice device, const char* code)
{
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &wgsl.chain;
    return wgpuDeviceCreateShaderModule(device, &desc);
}

internal void create_layouts(NvRenderer* renderer)
{
    WGPUDevice device = renderer->gpu->device;

    WGPUBindGroupLayoutEntry frame_entries[5] = {
        {.binding = 0, .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
         .buffer = {.type = WGPUBufferBindingType_Uniform, .minBindingSize = sizeof(FrameUniforms)}},
        {.binding = 1, .visibility = WGPUShaderStage_Vertex,
         .buffer = {.type = WGPUBufferBindingType_ReadOnlyStorage}},
        {.binding = 2, .visibility = WGPUShaderStage_Vertex,
         .buffer = {.type = WGPUBufferBindingType_ReadOnlyStorage}},
        {.binding = 3, .visibility = WGPUShaderStage_Fragment,
         .texture = {.sampleType = WGPUTextureSampleType_Depth, .viewDimension = WGPUTextureViewDimension_2D}},
        {.binding = 4, .visibility = WGPUShaderStage_Fragment,
         .sampler = {.type = WGPUSamplerBindingType_Comparison}},
    };
    WGPUBindGroupLayoutDescriptor frame_desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    frame_desc.entryCount = NV_ARRAY_COUNT(frame_entries);
    frame_desc.entries = frame_entries;
    renderer->frame_layout = wgpuDeviceCreateBindGroupLayout(device, &frame_desc);
    // IMPORTANT: The shadow pass writes the map, so its bind group must not also bind it.
    frame_desc.entryCount = 3;
    renderer->shadow_frame_layout = wgpuDeviceCreateBindGroupLayout(device, &frame_desc);

    WGPUBindGroupLayoutEntry material_entries[3] = {
        {.binding = 0, .visibility = WGPUShaderStage_Fragment,
         .buffer = {.type = WGPUBufferBindingType_Uniform, .minBindingSize = sizeof(MaterialUniforms)}},
        {.binding = 1, .visibility = WGPUShaderStage_Fragment,
         .sampler = {.type = WGPUSamplerBindingType_Filtering}},
        {.binding = 2, .visibility = WGPUShaderStage_Fragment,
         .texture = {.sampleType = WGPUTextureSampleType_Float, .viewDimension = WGPUTextureViewDimension_2D}},
    };
    WGPUBindGroupLayoutDescriptor material_desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    material_desc.entryCount = NV_ARRAY_COUNT(material_entries);
    material_desc.entries = material_entries;
    renderer->material_layout = wgpuDeviceCreateBindGroupLayout(device, &material_desc);
}

internal WGPUPipelineLayout create_pipeline_layout(WGPUDevice device, const WGPUBindGroupLayout* layouts, u32 count)
{
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = count;
    desc.bindGroupLayouts = layouts;
    return wgpuDeviceCreatePipelineLayout(device, &desc);
}

internal void release_scene_pipelines(NvRenderer* renderer)
{
    for (u32 skinned = 0; skinned < 2; ++skinned) {
        for (u32 double_sided = 0; double_sided < 2; ++double_sided) {
            if (renderer->pipelines[skinned][double_sided])
                wgpuRenderPipelineRelease(renderer->pipelines[skinned][double_sided]);
            renderer->pipelines[skinned][double_sided] = NULL;
        }
    }
    if (renderer->debug_pipeline)
        wgpuRenderPipelineRelease(renderer->debug_pipeline);
    renderer->debug_pipeline = NULL;
}

// The pipelines the scene pass uses, for `scene_samples` samples per pixel.
internal void create_pipelines(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    WGPUDevice device = gpu->device;

    WGPUBindGroupLayout mesh_layouts[2] = {renderer->frame_layout, renderer->material_layout};
    WGPUPipelineLayout mesh_layout = create_pipeline_layout(device, mesh_layouts, 2);
    WGPUShaderModule module = create_shader(device, mesh_shader);

    WGPUVertexAttribute static_attributes[3] = {
        {.format = WGPUVertexFormat_Float32x3, .offset = offsetof(NvVertex, position), .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x3, .offset = offsetof(NvVertex, normal), .shaderLocation = 1},
        {.format = WGPUVertexFormat_Float32x2, .offset = offsetof(NvVertex, uv), .shaderLocation = 2},
    };
    WGPUVertexAttribute skinned_attributes[5] = {
        {.format = WGPUVertexFormat_Float32x3, .offset = offsetof(NvSkinnedVertex, position), .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x3, .offset = offsetof(NvSkinnedVertex, normal), .shaderLocation = 1},
        {.format = WGPUVertexFormat_Float32x2, .offset = offsetof(NvSkinnedVertex, uv), .shaderLocation = 2},
        {.format = WGPUVertexFormat_Uint16x4, .offset = offsetof(NvSkinnedVertex, joints), .shaderLocation = 3},
        {.format = WGPUVertexFormat_Float32x4, .offset = offsetof(NvSkinnedVertex, weights), .shaderLocation = 4},
    };

    WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
    color_target.format = NV_SCENE_FORMAT;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = (WGPUStringView){"fs_main", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth32Float;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Greater; // reverse Z: near is 1, far is 0

    for (u32 skinned = 0; skinned < 2; ++skinned) {
        WGPUVertexBufferLayout vertex_layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vertex_layout.stepMode = WGPUVertexStepMode_Vertex;
        vertex_layout.arrayStride = skinned ? sizeof(NvSkinnedVertex) : sizeof(NvVertex);
        vertex_layout.attributeCount = skinned ? NV_ARRAY_COUNT(skinned_attributes) : NV_ARRAY_COUNT(static_attributes);
        vertex_layout.attributes = skinned ? skinned_attributes : static_attributes;

        for (u32 double_sided = 0; double_sided < 2; ++double_sided) {
            WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            desc.label = (WGPUStringView){skinned ? "skinned mesh" : "static mesh", WGPU_STRLEN};
            desc.layout = mesh_layout;
            desc.vertex.module = module;
            desc.vertex.entryPoint = (WGPUStringView){skinned ? "vs_skinned" : "vs_static", WGPU_STRLEN};
            desc.vertex.bufferCount = 1;
            desc.vertex.buffers = &vertex_layout;
            desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            desc.primitive.frontFace = WGPUFrontFace_CCW;
            desc.primitive.cullMode = double_sided ? WGPUCullMode_None : WGPUCullMode_Back;
            desc.depthStencil = &depth;
            desc.multisample.count = renderer->scene_samples;
            desc.fragment = &fragment;
            renderer->pipelines[skinned][double_sided] = wgpuDeviceCreateRenderPipeline(device, &desc);
        }
    }
    wgpuShaderModuleRelease(module);
    wgpuPipelineLayoutRelease(mesh_layout);

    // Debug lines: drawn over everything, so the depth test always passes.
    WGPUPipelineLayout debug_layout = create_pipeline_layout(device, &renderer->frame_layout, 1);
    WGPUShaderModule debug_module = create_shader(device, debug_shader);
    WGPUVertexAttribute debug_attributes[2] = {
        {.format = WGPUVertexFormat_Float32x3, .offset = offsetof(NvDebugVertex, position), .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x4, .offset = offsetof(NvDebugVertex, color), .shaderLocation = 1},
    };
    WGPUVertexBufferLayout debug_vertex_layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    debug_vertex_layout.stepMode = WGPUVertexStepMode_Vertex;
    debug_vertex_layout.arrayStride = sizeof(NvDebugVertex);
    debug_vertex_layout.attributeCount = NV_ARRAY_COUNT(debug_attributes);
    debug_vertex_layout.attributes = debug_attributes;

    WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
    blend.color = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
    blend.alpha = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
    WGPUColorTargetState debug_target = WGPU_COLOR_TARGET_STATE_INIT;
    debug_target.format = NV_SCENE_FORMAT;
    debug_target.blend = &blend;
    WGPUFragmentState debug_fragment = WGPU_FRAGMENT_STATE_INIT;
    debug_fragment.module = debug_module;
    debug_fragment.entryPoint = (WGPUStringView){"fs_main", WGPU_STRLEN};
    debug_fragment.targetCount = 1;
    debug_fragment.targets = &debug_target;

    WGPUDepthStencilState debug_depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    debug_depth.format = WGPUTextureFormat_Depth32Float;
    debug_depth.depthWriteEnabled = WGPUOptionalBool_False;
    debug_depth.depthCompare = WGPUCompareFunction_Always;

    WGPURenderPipelineDescriptor debug_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    debug_desc.label = (WGPUStringView){"debug lines", WGPU_STRLEN};
    debug_desc.layout = debug_layout;
    debug_desc.vertex.module = debug_module;
    debug_desc.vertex.entryPoint = (WGPUStringView){"vs_main", WGPU_STRLEN};
    debug_desc.vertex.bufferCount = 1;
    debug_desc.vertex.buffers = &debug_vertex_layout;
    debug_desc.primitive.topology = WGPUPrimitiveTopology_LineList;
    debug_desc.depthStencil = &debug_depth;
    debug_desc.multisample.count = renderer->scene_samples;
    debug_desc.fragment = &debug_fragment;
    renderer->debug_pipeline = wgpuDeviceCreateRenderPipeline(device, &debug_desc);
    wgpuShaderModuleRelease(debug_module);
    wgpuPipelineLayoutRelease(debug_layout);
}

internal WGPUTextureFormat shadow_texture_format(NvShadowFormat format)
{
    return format == NV_SHADOW_FORMAT_DEPTH16 ? WGPUTextureFormat_Depth16Unorm : WGPUTextureFormat_Depth32Float;
}

internal void create_shadow_pipelines(NvRenderer* renderer, NvShadowFormat format)
{
    WGPUDevice device = renderer->gpu->device;
    for (u32 skinned = 0; skinned < 2; ++skinned) {
        for (u32 double_sided = 0; double_sided < 2; ++double_sided) {
            if (renderer->shadow_pipelines[skinned][double_sided])
                wgpuRenderPipelineRelease(renderer->shadow_pipelines[skinned][double_sided]);
        }
    }
    WGPUPipelineLayout layout = create_pipeline_layout(device, &renderer->shadow_frame_layout, 1);
    WGPUShaderModule module = create_shader(device, shadow_shader);
    WGPUVertexAttribute static_attributes[1] = {
        {.format = WGPUVertexFormat_Float32x3, .offset = offsetof(NvVertex, position), .shaderLocation = 0},
    };
    WGPUVertexAttribute skinned_attributes[3] = {
        {.format = WGPUVertexFormat_Float32x3, .offset = offsetof(NvSkinnedVertex, position), .shaderLocation = 0},
        {.format = WGPUVertexFormat_Uint16x4, .offset = offsetof(NvSkinnedVertex, joints), .shaderLocation = 3},
        {.format = WGPUVertexFormat_Float32x4, .offset = offsetof(NvSkinnedVertex, weights), .shaderLocation = 4},
    };
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = shadow_texture_format(format);
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Less;
    depth.depthBias = shadow_bias_constant[format];
    depth.depthBiasSlopeScale = SHADOW_BIAS_SLOPE;
    for (u32 skinned = 0; skinned < 2; ++skinned) {
        WGPUVertexBufferLayout vertex_layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vertex_layout.stepMode = WGPUVertexStepMode_Vertex;
        vertex_layout.arrayStride = skinned ? sizeof(NvSkinnedVertex) : sizeof(NvVertex);
        vertex_layout.attributeCount = skinned ? NV_ARRAY_COUNT(skinned_attributes) : NV_ARRAY_COUNT(static_attributes);
        vertex_layout.attributes = skinned ? skinned_attributes : static_attributes;
        for (u32 double_sided = 0; double_sided < 2; ++double_sided) {
            WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            desc.label = (WGPUStringView){skinned ? "skinned shadow" : "static shadow", WGPU_STRLEN};
            desc.layout = layout;
            desc.vertex.module = module;
            desc.vertex.entryPoint = (WGPUStringView){skinned ? "vs_skinned" : "vs_static", WGPU_STRLEN};
            desc.vertex.bufferCount = 1;
            desc.vertex.buffers = &vertex_layout;
            desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            desc.primitive.frontFace = WGPUFrontFace_CCW;
            desc.primitive.cullMode = double_sided ? WGPUCullMode_None : WGPUCullMode_Back;
            desc.depthStencil = &depth;
            renderer->shadow_pipelines[skinned][double_sided] = wgpuDeviceCreateRenderPipeline(device, &desc);
        }
    }
    wgpuShaderModuleRelease(module);
    wgpuPipelineLayoutRelease(layout);
}

// Makes the shadow map (and, for a new format, its pipelines) match the settings, and the frame
// bind group that samples it. Off keeps a 1x1 map bound, since the bind group needs one.
internal void update_shadow_map(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    u32 size = renderer->shadows.size ? renderer->shadows.size : 1;
    NvShadowFormat format = renderer->shadows.format;
    if (renderer->shadow_texture && renderer->shadow_size == size && renderer->shadow_format == format)
        return;
    if (!renderer->shadow_texture || renderer->shadow_format != format)
        create_shadow_pipelines(renderer, format);
    if (renderer->shadow_texture) {
        wgpuTextureViewRelease(renderer->shadow_view);
        wgpuTextureRelease(renderer->shadow_texture);
        wgpuBindGroupRelease(renderer->frame_group);
    }
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"shadow map", WGPU_STRLEN};
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    desc.size = (WGPUExtent3D){size, size, 1};
    desc.format = shadow_texture_format(format);
    renderer->shadow_texture = wgpuDeviceCreateTexture(gpu->device, &desc);
    renderer->shadow_view = wgpuTextureCreateView(renderer->shadow_texture, NULL);
    renderer->shadow_size = size;
    renderer->shadow_format = format;
    if (renderer->shadows.size)
        nv_log(NV_LOG_INFO, "nv", "shadow map: %ux%u %s", size, size, nv_gpu_format_name(shadow_texture_format(format)));
    else
        nv_log(NV_LOG_INFO, "nv", "shadow map: off (a 1x1 %s placeholder stays bound)", nv_gpu_format_name(shadow_texture_format(format)));

    WGPUBindGroupEntry frame_entries[5] = {
        {.binding = 0, .buffer = renderer->frame_buffer, .size = sizeof(FrameUniforms)},
        {.binding = 1, .buffer = renderer->object_buffer, .size = NV_MAX_NODES * sizeof(NvObjectData)},
        {.binding = 2, .buffer = renderer->skin_buffer, .size = NV_MAX_SKIN_MATRICES * sizeof(NvMat4)},
        {.binding = 3, .textureView = renderer->shadow_view},
        {.binding = 4, .sampler = renderer->shadow_sampler},
    };
    WGPUBindGroupDescriptor frame_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    frame_desc.layout = renderer->frame_layout;
    frame_desc.entryCount = NV_ARRAY_COUNT(frame_entries);
    frame_desc.entries = frame_entries;
    renderer->frame_group = wgpuDeviceCreateBindGroup(gpu->device, &frame_desc);
}

void nv_renderer_init(NvRenderer* renderer, NvGpu* gpu, NvArena* arena)
{
    *renderer = (NvRenderer){0};
    renderer->gpu = gpu;
    renderer->objects = NV_PUSH_ARRAY(arena, NV_MAX_NODES, NvObjectData);
    renderer->joint_bounds = NV_PUSH_ARRAY(arena, NV_MAX_JOINT_BOUNDS, NvBox);
    renderer->object_nodes = NV_PUSH_ARRAY(arena, NV_MAX_NODES, u32);
    renderer->skin_matrices = NV_PUSH_ARRAY(arena, NV_MAX_SKIN_MATRICES, NvMat4);
    renderer->debug_vertices = NV_PUSH_ARRAY(arena, NV_MAX_DEBUG_LINES * 2, NvDebugVertex);
    renderer->ambient[0] = renderer->ambient[1] = renderer->ambient[2] = 0.15f;
    renderer->clear_color[0] = 0.02f;
    renderer->clear_color[1] = 0.02f;
    renderer->clear_color[2] = 0.035f;
    renderer->clear_color[3] = 1.0f;
    renderer->post = (NvPostSettings){.tone = NV_TONE_CLAMP, .exposure = 1.0f, .bloom = 0, .bloom_intensity = 0.04f};

    create_layouts(renderer);
    renderer->msaa = 1;
    renderer->scene_samples = 1;
    create_pipelines(renderer);
    create_upscale_pipeline(renderer);
    create_bloom_pipelines(renderer);

    WGPUSamplerDescriptor sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler_desc.addressModeU = WGPUAddressMode_Repeat;
    sampler_desc.addressModeV = WGPUAddressMode_Repeat;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    sampler_desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    renderer->sampler = wgpuDeviceCreateSampler(gpu->device, &sampler_desc);

    renderer->frame_buffer = create_buffer(gpu, WGPUBufferUsage_Uniform, NULL, sizeof(FrameUniforms));
    renderer->object_buffer = create_buffer(gpu, WGPUBufferUsage_Storage, NULL, NV_MAX_NODES * sizeof(NvObjectData));
    renderer->skin_buffer = create_buffer(gpu, WGPUBufferUsage_Storage, NULL, NV_MAX_SKIN_MATRICES * sizeof(NvMat4));

    if (gpu->has_timestamps) {
        WGPUQuerySetDescriptor query_desc = {.type = WGPUQueryType_Timestamp, .count = NV_TIMESTAMP_COUNT}; // scene, shadow, upscale
        renderer->timestamp_queries = wgpuDeviceCreateQuerySet(gpu->device, &query_desc);
        renderer->timestamp_resolve = create_buffer(gpu, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc, NULL, NV_TIMESTAMP_COUNT * sizeof(u64));
        WGPUBufferDescriptor readback_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readback_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        readback_desc.size = NV_TIMESTAMP_COUNT * sizeof(u64);
        renderer->timestamp_readback = wgpuDeviceCreateBuffer(gpu->device, &readback_desc);
    }
    renderer->debug_buffer = create_buffer(gpu, WGPUBufferUsage_Vertex, NULL, NV_MAX_DEBUG_LINES * 2 * sizeof(NvDebugVertex));

    WGPUBindGroupEntry shadow_frame_entries[3] = {
        {.binding = 0, .buffer = renderer->frame_buffer, .size = sizeof(FrameUniforms)},
        {.binding = 1, .buffer = renderer->object_buffer, .size = NV_MAX_NODES * sizeof(NvObjectData)},
        {.binding = 2, .buffer = renderer->skin_buffer, .size = NV_MAX_SKIN_MATRICES * sizeof(NvMat4)},
    };
    WGPUBindGroupDescriptor shadow_frame_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    shadow_frame_desc.layout = renderer->shadow_frame_layout;
    shadow_frame_desc.entryCount = NV_ARRAY_COUNT(shadow_frame_entries);
    shadow_frame_desc.entries = shadow_frame_entries;
    renderer->shadow_frame_group = wgpuDeviceCreateBindGroup(gpu->device, &shadow_frame_desc);

    WGPUSamplerDescriptor shadow_sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    shadow_sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    shadow_sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    shadow_sampler_desc.magFilter = WGPUFilterMode_Linear;
    shadow_sampler_desc.minFilter = WGPUFilterMode_Linear;
    shadow_sampler_desc.compare = WGPUCompareFunction_LessEqual; // 1 where the point is not behind the map's depth
    renderer->shadow_sampler = wgpuDeviceCreateSampler(gpu->device, &shadow_sampler_desc);
    update_shadow_map(renderer); // the frame bind group, with a 1x1 map until shadows are on

    // Slot 0 of each table is the default: no mesh, a 1x1 white texture, a white material.
    renderer->mesh_count = 1;
    local_persist const u8 white[4] = {255, 255, 255, 255};
    renderer->texture_count = 0;
    nv_renderer_add_texture(renderer, "white", 1, 1, white, 0, NULL);
    renderer->material_count = 0;
    nv_renderer_add_material(renderer, &(NvMaterialDesc){.base_color = {1, 1, 1, 1}});
}

NvMeshId nv_renderer_add_mesh(NvRenderer* renderer, const NvMeshData* data)
{
    NV_ASSERT(renderer->mesh_count < NV_MAX_MESHES);
    NV_ASSERT((data->vertices != NULL) != (data->skinned_vertices != NULL));
    NvRenderMesh* mesh = &renderer->meshes[renderer->mesh_count];
    mesh->skinned = data->skinned_vertices != NULL;
    umm vertex_size = mesh->skinned ? sizeof(NvSkinnedVertex) : sizeof(NvVertex);
    const void* vertices = mesh->skinned ? (const void*)data->skinned_vertices : (const void*)data->vertices;
    mesh->vertices = create_buffer(renderer->gpu, WGPUBufferUsage_Vertex, vertices, data->vertex_count * vertex_size);
    mesh->indices = create_buffer(renderer->gpu, WGPUBufferUsage_Index, data->indices, data->index_count * sizeof(u32));
    mesh->index_count = data->index_count;

    mesh->bounds = nv_box_empty();
    for (u32 v = 0; v < data->vertex_count; ++v) {
        const f32* p = mesh->skinned ? data->skinned_vertices[v].position : data->vertices[v].position;
        mesh->bounds = nv_box_add_point(mesh->bounds, nv_vec3(p[0], p[1], p[2]));
    }

    // A skinned vertex ends up at a weighted average of where its joints move it, so it stays
    // inside the union of the moved boxes of every joint that has weight on it.
    if (mesh->skinned) {
        u32 joint_count = 0;
        for (u32 v = 0; v < data->vertex_count; ++v) {
            for (u32 k = 0; k < 4; ++k) {
                if (data->skinned_vertices[v].weights[k] > 0.0f && data->skinned_vertices[v].joints[k] + 1u > joint_count)
                    joint_count = data->skinned_vertices[v].joints[k] + 1u;
            }
        }
        NV_ASSERT(renderer->joint_bounds_used + joint_count <= NV_MAX_JOINT_BOUNDS);
        mesh->joint_bounds_offset = renderer->joint_bounds_used;
        mesh->joint_bounds_count = joint_count;
        renderer->joint_bounds_used += joint_count;
        NvBox* boxes = renderer->joint_bounds + mesh->joint_bounds_offset;
        for (u32 j = 0; j < joint_count; ++j)
            boxes[j] = nv_box_empty();
        for (u32 v = 0; v < data->vertex_count; ++v) {
            const NvSkinnedVertex* vertex = &data->skinned_vertices[v];
            NvVec3 p = nv_vec3(vertex->position[0], vertex->position[1], vertex->position[2]);
            for (u32 k = 0; k < 4; ++k) {
                if (vertex->weights[k] > 0.0f)
                    boxes[vertex->joints[k]] = nv_box_add_point(boxes[vertex->joints[k]], p);
            }
        }
    }
    return (NvMeshId){renderer->mesh_count++};
}

NvTextureId nv_renderer_add_texture(NvRenderer* renderer, const char* name, u32 width, u32 height, const u8* rgba,
                                    b32 srgb, NvArena* scratch)
{
    NV_ASSERT(renderer->texture_count < NV_MAX_TEXTURES);
    u32 levels = 1;
    if (scratch) {
        for (u32 size = width > height ? width : height; size > 1; size >>= 1)
            ++levels;
    }

    NvRenderTexture* slot = &renderer->textures[renderer->texture_count];
    if (name && name[0])
        snprintf(slot->name, sizeof(slot->name), "%s", name);
    else
        snprintf(slot->name, sizeof(slot->name), "texture %u", renderer->texture_count);
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){slot->name, WGPU_STRLEN};
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.size = (WGPUExtent3D){width, height, 1};
    desc.format = srgb ? WGPUTextureFormat_RGBA8UnormSrgb : WGPUTextureFormat_RGBA8Unorm;
    desc.mipLevelCount = levels;
    slot->texture = wgpuDeviceCreateTexture(renderer->gpu->device, &desc);
    slot->view = wgpuTextureCreateView(slot->texture, NULL);

    // Each mip level is a 2x2 box filter of the previous one.
    umm scratch_mark = scratch ? scratch->used : 0;
    const u8* level_pixels = rgba;
    u32 w = width;
    u32 h = height;
    for (u32 level = 0; level < levels; ++level) {
        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = slot->texture;
        destination.mipLevel = level;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = w * 4;
        layout.rowsPerImage = h;
        WGPUExtent3D size = {w, h, 1};
        wgpuQueueWriteTexture(renderer->gpu->queue, &destination, level_pixels, (umm)w * h * 4, &layout, &size);

        if (level + 1 == levels)
            break;
        u32 next_w = w > 1 ? w / 2 : 1;
        u32 next_h = h > 1 ? h / 2 : 1;
        u8* next = NV_PUSH_ARRAY(scratch, (umm)next_w * next_h * 4, u8);
        for (u32 y = 0; y < next_h; ++y) {
            for (u32 x = 0; x < next_w; ++x) {
                u32 x0 = x * 2, y0 = y * 2;
                u32 x1 = x0 + 1 < w ? x0 + 1 : x0;
                u32 y1 = y0 + 1 < h ? y0 + 1 : y0;
                for (u32 c = 0; c < 4; ++c) {
                    u32 sum = level_pixels[(y0 * w + x0) * 4 + c] + level_pixels[(y0 * w + x1) * 4 + c] +
                              level_pixels[(y1 * w + x0) * 4 + c] + level_pixels[(y1 * w + x1) * 4 + c];
                    next[(y * next_w + x) * 4 + c] = (u8)((sum + 2) / 4);
                }
            }
        }
        level_pixels = next;
        w = next_w;
        h = next_h;
    }
    if (scratch)
        scratch->used = scratch_mark;

    return (NvTextureId){renderer->texture_count++};
}

NvMaterialId nv_renderer_add_material(NvRenderer* renderer, const NvMaterialDesc* desc)
{
    NV_ASSERT(renderer->material_count < NV_MAX_MATERIALS);
    NV_ASSERT(desc->base_color_texture.index < renderer->texture_count);
    NvRenderMaterial* material = &renderer->materials[renderer->material_count];
    material->desc = *desc;

    MaterialUniforms uniforms = {0};
    memcpy(uniforms.base_color, desc->base_color, sizeof(uniforms.base_color));
    material->uniform = create_buffer(renderer->gpu, WGPUBufferUsage_Uniform, &uniforms, sizeof(uniforms));

    WGPUBindGroupEntry entries[3] = {
        {.binding = 0, .buffer = material->uniform, .size = sizeof(MaterialUniforms)},
        {.binding = 1, .sampler = renderer->sampler},
        {.binding = 2, .textureView = renderer->textures[desc->base_color_texture.index].view},
    };
    WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group_desc.layout = renderer->material_layout;
    group_desc.entryCount = NV_ARRAY_COUNT(entries);
    group_desc.entries = entries;
    material->bind_group = wgpuDeviceCreateBindGroup(renderer->gpu->device, &group_desc);
    return (NvMaterialId){renderer->material_count++};
}

void nv_renderer_set_material_color(NvRenderer* renderer, NvMaterialId id, const f32 base_color[4])
{
    NV_ASSERT(id.index < renderer->material_count);
    NvRenderMaterial* material = &renderer->materials[id.index];
    memcpy(material->desc.base_color, base_color, sizeof(material->desc.base_color));
    MaterialUniforms uniforms = {0};
    memcpy(uniforms.base_color, base_color, sizeof(uniforms.base_color));
    wgpuQueueWriteBuffer(renderer->gpu->queue, material->uniform, 0, &uniforms, sizeof(uniforms));
}

void nv_renderer_debug_line(NvRenderer* renderer, NvVec3 a, NvVec3 b, NvVec3 color)
{
    if (renderer->debug_vertex_count + 2 > NV_MAX_DEBUG_LINES * 2)
        return; // debug output past capacity is dropped rather than asserting
    NvDebugVertex* v = &renderer->debug_vertices[renderer->debug_vertex_count];
    v[0] = (NvDebugVertex){{a.x, a.y, a.z}, {color.x, color.y, color.z, 1.0f}};
    v[1] = (NvDebugVertex){{b.x, b.y, b.z}, {color.x, color.y, color.z, 1.0f}};
    renderer->debug_vertex_count += 2;
}

internal u32 round_up_64(u32 value)
{
    return (value + 63u) & ~63u;
}

internal void release_texture(WGPUTexture* texture, WGPUTextureView* view)
{
    if (*view)
        wgpuTextureViewRelease(*view);
    if (*texture)
        wgpuTextureRelease(*texture);
    *view = NULL;
    *texture = NULL;
}

// A color target in the scene's HDR format (NV_SCENE_FORMAT, linear): what the scene pass draws, what it
// resolves into and what the upscale pass samples. The canvas still gets the sRGB view format at the
// end, in the upscale pass, so the tone mapped value is encoded by the canvas's own view
// (docs/specs/vfx.md; before HDR the scene targets were made like the canvas, see msaa.md).
internal void create_color_target(NvGpu* gpu, const char* label, u32 width, u32 height, u32 samples, WGPUTextureUsage usage,
                                  WGPUTexture* texture, WGPUTextureView* view)
{
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){label, WGPU_STRLEN};
    desc.usage = usage;
    desc.size = (WGPUExtent3D){width, height, 1};
    desc.format = NV_SCENE_FORMAT;
    desc.sampleCount = samples;
    *texture = wgpuDeviceCreateTexture(gpu->device, &desc);
    *view = wgpuTextureCreateView(*texture, NULL);
}

internal f64 target_megabytes(u32 width, u32 height, u32 samples)
{
    return (f64)width * (f64)height * (f64)nv_gpu_format_bytes(NV_SCENE_FORMAT) * (f64)samples / (1024.0 * 1024.0);
}

internal const char* upscale_shader =
    "struct Params {\n"
    "    origin: vec2f,\n"
    "    block: vec2f,\n"
    "    size: vec2f,\n"
    "    exposure: f32,\n"
    "    tone: f32,\n"
    "    bloom: vec4f,\n"     // x: intensity (0 = off), yz: the valid size of bloom mip 0 in texels
    "    bloom_tex: vec4f,\n" // xy: the size of that mip's texture
    "}\n"
    "@group(0) @binding(0) var<uniform> p: Params;\n"
    "@group(0) @binding(1) var scene: texture_2d<f32>;\n"
    "@group(0) @binding(2) var bloom_tex: texture_2d<f32>;\n"
    "@group(0) @binding(3) var bloom_sampler: sampler;\n"
    "@vertex\n"
    "fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
    "    let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n"
    "\n"
    // Khronos PBR Neutral tone mapper (github.com/KhronosGroup/ToneMapping, Apache-2.0): colors whose
    // brightest channel is below 0.76 change by at most 0.04, brighter ones are compressed toward 1
    // by their peak so the hue stays, and the very bright lose saturation toward white.
    "fn tone_pbr_neutral(input: vec3f) -> vec3f {\n"
    "    let start_compression = 0.8 - 0.04;\n"
    "    let desaturation = 0.15;\n"
    "    var color = input;\n"
    "    let x = min(color.r, min(color.g, color.b));\n"
    "    let offset = select(0.04, x - 6.25 * x * x, x < 0.08);\n"
    "    color -= vec3f(offset);\n"
    "    let peak = max(color.r, max(color.g, color.b));\n"
    "    if (peak < start_compression) { return color; }\n"
    "    let d = 1.0 - start_compression;\n"
    "    let new_peak = 1.0 - d * d / (peak + d - start_compression);\n"
    "    color *= new_peak / peak;\n"
    "    let g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);\n"
    "    return mix(color, new_peak * vec3f(1.0), g);\n"
    "}\n"
    "\n"
    // Krzysztof Narkowicz's fit of the ACES filmic curve.
    "fn tone_aces(x: vec3f) -> vec3f {\n"
    "    return clamp((x * (2.51 * x + vec3f(0.03))) / (x * (2.43 * x + vec3f(0.59)) + vec3f(0.14)), vec3f(0.0), vec3f(1.0));\n"
    "}\n"
    "\n"
    "fn tone_map(color: vec3f) -> vec3f {\n"
    "    let c = max(color * p.exposure, vec3f(0.0));\n"
    "    if (p.tone > 1.5) { return tone_aces(c); }\n"
    "    if (p.tone > 0.5) { return clamp(tone_pbr_neutral(c), vec3f(0.0), vec3f(1.0)); }\n"
    "    return clamp(c, vec3f(0.0), vec3f(1.0));\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {\n"
    "    // Nearest: the scene pixel under this screen pixel, counted from the image's corner.\n"
    "    let t = vec2i(floor((pos.xy - p.origin) / p.block));\n"
    "    let c = clamp(t, vec2i(0), vec2i(p.size) - vec2i(1));\n"
    "    var color = textureLoad(scene, c, 0).rgb;\n"
    // Bloom mip 0 covers two scene pixels per texel; bilinear, kept inside what the chain wrote.
    "    if (p.bloom.x > 0.0) {\n"
    "        let q = clamp((vec2f(c) + vec2f(0.5)) * 0.5, vec2f(0.5), p.bloom.yz - vec2f(0.5));\n"
    "        color += textureSampleLevel(bloom_tex, bloom_sampler, q / p.bloom_tex.xy, 0.0).rgb * p.bloom.x;\n"
    "    }\n"
    "    return vec4f(tone_map(color), 1.0);\n"
    "}\n";

internal void create_upscale_pipeline(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    WGPUShaderModule module = create_shader(gpu->device, upscale_shader);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = gpu->surface_format;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = (WGPUStringView){"fs_main", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"upscale", WGPU_STRLEN};
    desc.vertex.module = module;
    desc.vertex.entryPoint = (WGPUStringView){"vs_main", WGPU_STRLEN};
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.fragment = &fragment;
    renderer->upscale_pipeline = wgpuDeviceCreateRenderPipeline(gpu->device, &desc);
    wgpuShaderModuleRelease(module);
    renderer->upscale_buffer = create_buffer(gpu, WGPUBufferUsage_Uniform, NULL, 64);
}

// The upscale pass's bind group: the scene color, and bloom mip 0 (a 1x1 black texture while there
// is no chain, with the intensity 0 so it is not added).
internal void rebuild_upscale_group(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    if (renderer->upscale_group)
        wgpuBindGroupRelease(renderer->upscale_group);
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(renderer->upscale_pipeline, 0);
    WGPUBindGroupEntry entries[4] = {
        {.binding = 0, .buffer = renderer->upscale_buffer, .size = 64},
        {.binding = 1, .textureView = renderer->scene_color_view},
        {.binding = 2, .textureView = renderer->bloom_texture ? renderer->bloom_views[0] : renderer->bloom_dummy_view},
        {.binding = 3, .sampler = renderer->bloom_sampler},
    };
    WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group_desc.layout = layout;
    group_desc.entryCount = 4;
    group_desc.entries = entries;
    renderer->upscale_group = wgpuDeviceCreateBindGroup(gpu->device, &group_desc);
    wgpuBindGroupLayoutRelease(layout);
}

// Bloom after Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare"
// (SIGGRAPH 2014): a 13-tap downsample (the first step weights its groups of four taps by
// 1 / (1 + luma), Karis's average, so a single very bright pixel does not flicker) and a 3x3 tent
// upsample added into the level above. Every tap is clamped to what the level before it wrote, so the
// padding of the scene targets does not bleed in.
internal const char* bloom_shader =
    "struct Pass {\n"
    "    src_tex: vec2f,\n"    // the source texture's size in texels
    "    src_region: vec2f,\n" // the part of it that holds the image
    "}\n"
    "@group(0) @binding(0) var<uniform> p: Pass;\n"
    "@group(0) @binding(1) var src: texture_2d<f32>;\n"
    "@group(0) @binding(2) var samp: sampler;\n"
    "@vertex\n"
    "fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
    "    let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n"
    "fn tap(q: vec2f) -> vec3f {\n"
    "    let c = clamp(q, vec2f(0.5), p.src_region - vec2f(0.5));\n"
    "    return textureSampleLevel(src, samp, c / p.src_tex, 0.0).rgb;\n"
    "}\n"
    "fn karis(c: vec3f) -> f32 {\n"
    "    return 1.0 / (1.0 + dot(c, vec3f(0.2126, 0.7152, 0.0722)));\n"
    "}\n"
    // Two destination pixels are one source pixel apart on each axis, so the destination pixel's center
    // is the corner shared by four source texels, and a bilinear tap there averages them.
    "@fragment\n"
    "fn fs_down_first(@builtin(position) pos: vec4f) -> @location(0) vec4f {\n"
    "    let o = pos.xy * 2.0;\n"
    "    let a = tap(o + vec2f(-2.0, -2.0)); let b = tap(o + vec2f(0.0, -2.0)); let c = tap(o + vec2f(2.0, -2.0));\n"
    "    let d = tap(o + vec2f(-2.0, 0.0));  let e = tap(o);                    let f = tap(o + vec2f(2.0, 0.0));\n"
    "    let g = tap(o + vec2f(-2.0, 2.0));  let h = tap(o + vec2f(0.0, 2.0));  let i = tap(o + vec2f(2.0, 2.0));\n"
    "    let j = tap(o + vec2f(-1.0, -1.0)); let k = tap(o + vec2f(1.0, -1.0));\n"
    "    let l = tap(o + vec2f(-1.0, 1.0));  let m = tap(o + vec2f(1.0, 1.0));\n"
    "    var g0 = (a + b + d + e) * (0.125 / 4.0);\n"
    "    var g1 = (b + c + e + f) * (0.125 / 4.0);\n"
    "    var g2 = (d + e + g + h) * (0.125 / 4.0);\n"
    "    var g3 = (e + f + h + i) * (0.125 / 4.0);\n"
    "    var g4 = (j + k + l + m) * (0.5 / 4.0);\n"
    "    g0 *= karis(g0); g1 *= karis(g1); g2 *= karis(g2); g3 *= karis(g3); g4 *= karis(g4);\n"
    "    return vec4f(max(g0 + g1 + g2 + g3 + g4, vec3f(0.0001)), 1.0);\n"
    "}\n"
    "@fragment\n"
    "fn fs_down(@builtin(position) pos: vec4f) -> @location(0) vec4f {\n"
    "    let o = pos.xy * 2.0;\n"
    "    let a = tap(o + vec2f(-2.0, -2.0)); let b = tap(o + vec2f(0.0, -2.0)); let c = tap(o + vec2f(2.0, -2.0));\n"
    "    let d = tap(o + vec2f(-2.0, 0.0));  let e = tap(o);                    let f = tap(o + vec2f(2.0, 0.0));\n"
    "    let g = tap(o + vec2f(-2.0, 2.0));  let h = tap(o + vec2f(0.0, 2.0));  let i = tap(o + vec2f(2.0, 2.0));\n"
    "    let j = tap(o + vec2f(-1.0, -1.0)); let k = tap(o + vec2f(1.0, -1.0));\n"
    "    let l = tap(o + vec2f(-1.0, 1.0));  let m = tap(o + vec2f(1.0, 1.0));\n"
    "    let down = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;\n"
    "    return vec4f(down, 1.0);\n"
    "}\n"
    // The destination is twice the source's size; the tent reaches one source texel around.
    "@fragment\n"
    "fn fs_up(@builtin(position) pos: vec4f) -> @location(0) vec4f {\n"
    "    let o = pos.xy * 0.5;\n"
    "    let a = tap(o + vec2f(-1.0, 1.0));  let b = tap(o + vec2f(0.0, 1.0));  let c = tap(o + vec2f(1.0, 1.0));\n"
    "    let d = tap(o + vec2f(-1.0, 0.0));  let e = tap(o);                    let f = tap(o + vec2f(1.0, 0.0));\n"
    "    let g = tap(o + vec2f(-1.0, -1.0)); let h = tap(o + vec2f(0.0, -1.0)); let i = tap(o + vec2f(1.0, -1.0));\n"
    "    return vec4f((e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) * (1.0 / 16.0), 0.0);\n"
    "}\n";

#define BLOOM_PASSES (2 * NV_BLOOM_LEVELS - 1) // six downsamples and five upsamples
#define BLOOM_UNIFORM_STRIDE 256               // the uniform dynamic offset alignment WebGPU guarantees

internal void create_bloom_pipelines(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    WGPUDevice device = gpu->device;
    WGPUBindGroupLayoutEntry entries[3] = {
        {.binding = 0, .visibility = WGPUShaderStage_Fragment,
         .buffer = {.type = WGPUBufferBindingType_Uniform, .hasDynamicOffset = 1, .minBindingSize = 16}},
        {.binding = 1, .visibility = WGPUShaderStage_Fragment,
         .texture = {.sampleType = WGPUTextureSampleType_Float, .viewDimension = WGPUTextureViewDimension_2D}},
        {.binding = 2, .visibility = WGPUShaderStage_Fragment, .sampler = {.type = WGPUSamplerBindingType_Filtering}},
    };
    WGPUBindGroupLayoutDescriptor layout_desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout_desc.entryCount = 3;
    layout_desc.entries = entries;
    renderer->bloom_layout = wgpuDeviceCreateBindGroupLayout(device, &layout_desc);
    WGPUPipelineLayout pipeline_layout = create_pipeline_layout(device, &renderer->bloom_layout, 1);
    WGPUShaderModule module = create_shader(device, bloom_shader);

    const char* fragments[3] = {"fs_down_first", "fs_down", "fs_up"};
    WGPURenderPipeline* pipelines[3] = {&renderer->bloom_down_first_pipeline, &renderer->bloom_down_pipeline, &renderer->bloom_up_pipeline};
    const char* labels[3] = {"bloom downsample (first)", "bloom downsample", "bloom upsample"};
    for (u32 i = 0; i < 3; ++i) {
        // The upsample is added into the level it writes.
        WGPUBlendState add = WGPU_BLEND_STATE_INIT;
        add.color = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
        add.alpha = (WGPUBlendComponent){WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One};
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = NV_SCENE_FORMAT;
        target.blend = i == 2 ? &add : NULL;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = module;
        fragment.entryPoint = (WGPUStringView){fragments[i], WGPU_STRLEN};
        fragment.targetCount = 1;
        fragment.targets = &target;
        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.label = (WGPUStringView){labels[i], WGPU_STRLEN};
        desc.layout = pipeline_layout;
        desc.vertex.module = module;
        desc.vertex.entryPoint = (WGPUStringView){"vs_main", WGPU_STRLEN};
        desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        desc.fragment = &fragment;
        *pipelines[i] = wgpuDeviceCreateRenderPipeline(device, &desc);
    }
    wgpuShaderModuleRelease(module);
    wgpuPipelineLayoutRelease(pipeline_layout);

    WGPUSamplerDescriptor sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    renderer->bloom_sampler = wgpuDeviceCreateSampler(device, &sampler_desc);
    renderer->bloom_buffer = create_buffer(gpu, WGPUBufferUsage_Uniform, NULL, BLOOM_PASSES * BLOOM_UNIFORM_STRIDE);

    WGPUTextureDescriptor dummy_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    dummy_desc.label = (WGPUStringView){"bloom placeholder", WGPU_STRLEN};
    dummy_desc.usage = WGPUTextureUsage_TextureBinding;
    dummy_desc.size = (WGPUExtent3D){1, 1, 1};
    dummy_desc.format = NV_SCENE_FORMAT;
    renderer->bloom_dummy = wgpuDeviceCreateTexture(device, &dummy_desc);
    renderer->bloom_dummy_view = wgpuTextureCreateView(renderer->bloom_dummy, NULL);
}

internal void release_bloom_chain(NvRenderer* renderer)
{
    for (u32 i = 0; i <= NV_BLOOM_LEVELS; ++i) {
        if (renderer->bloom_groups[i])
            wgpuBindGroupRelease(renderer->bloom_groups[i]);
        renderer->bloom_groups[i] = NULL;
    }
    for (u32 i = 0; i < NV_BLOOM_LEVELS; ++i) {
        if (renderer->bloom_views[i])
            wgpuTextureViewRelease(renderer->bloom_views[i]);
        renderer->bloom_views[i] = NULL;
    }
    if (renderer->bloom_texture)
        wgpuTextureRelease(renderer->bloom_texture);
    renderer->bloom_texture = NULL;
    renderer->bloom_width = renderer->bloom_height = 0;
}

// The pass bind groups by source: the scene color, then each bloom mip. Made again when either changed.
internal void rebuild_bloom_groups(NvRenderer* renderer)
{
    if (!renderer->bloom_texture)
        return;
    for (u32 i = 0; i <= NV_BLOOM_LEVELS; ++i) {
        if (renderer->bloom_groups[i])
            wgpuBindGroupRelease(renderer->bloom_groups[i]);
        WGPUBindGroupEntry entries[3] = {
            {.binding = 0, .buffer = renderer->bloom_buffer, .size = 16},
            {.binding = 1, .textureView = i == 0 ? renderer->scene_color_view : renderer->bloom_views[i - 1]},
            {.binding = 2, .sampler = renderer->bloom_sampler},
        };
        WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        desc.layout = renderer->bloom_layout;
        desc.entryCount = 3;
        desc.entries = entries;
        renderer->bloom_groups[i] = wgpuDeviceCreateBindGroup(renderer->gpu->device, &desc);
    }
}

// Keeps the bloom chain while bloom is on, at half the size of the scene targets.
internal void update_bloom(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    b32 want = renderer->post.bloom && renderer->post.bloom_intensity > 0.0f;
    if (!want) {
        if (renderer->bloom_texture) {
            release_bloom_chain(renderer);
            rebuild_upscale_group(renderer);
            nv_log(NV_LOG_INFO, "nv", "bloom: off");
        }
        return;
    }
    u32 width = renderer->target_width / 2, height = renderer->target_height / 2;
    if (renderer->bloom_texture && renderer->bloom_width == width && renderer->bloom_height == height)
        return;
    release_bloom_chain(renderer);
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"bloom", WGPU_STRLEN};
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    desc.size = (WGPUExtent3D){width, height, 1};
    desc.format = NV_SCENE_FORMAT;
    desc.mipLevelCount = NV_BLOOM_LEVELS;
    renderer->bloom_texture = wgpuDeviceCreateTexture(gpu->device, &desc);
    for (u32 i = 0; i < NV_BLOOM_LEVELS; ++i) {
        WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        view_desc.baseMipLevel = i;
        view_desc.mipLevelCount = 1;
        renderer->bloom_views[i] = wgpuTextureCreateView(renderer->bloom_texture, &view_desc);
    }
    renderer->bloom_width = width;
    renderer->bloom_height = height;
    rebuild_bloom_groups(renderer);
    rebuild_upscale_group(renderer);
    u64 bytes = nv_gpu_texture_bytes(width, height, NV_BLOOM_LEVELS, NV_SCENE_FORMAT);
    nv_log(NV_LOG_INFO, "nv", "bloom chain: %ux%u %s, %u mips (%.1f MB), downsampled from the scene color and added back up", width, height,
           nv_gpu_format_name(NV_SCENE_FORMAT), NV_BLOOM_LEVELS, (f64)bytes / (1024.0 * 1024.0));
}

// The size of bloom mip `level` for a scene of `width` x `height` pixels: half the last, rounded up.
internal void bloom_region(u32 width, u32 height, u32 level, u32 out[2])
{
    for (u32 i = 0; i <= level; ++i) {
        width = (width + 1) / 2;
        height = (height + 1) / 2;
    }
    out[0] = width ? width : 1;
    out[1] = height ? height : 1;
}

// Records the downsample and upsample passes, between the scene pass and the upscale pass.
internal void record_bloom(NvRenderer* renderer, WGPUCommandEncoder encoder, u32 scene_width, u32 scene_height)
{
    u32 region[NV_BLOOM_LEVELS][2];
    for (u32 i = 0; i < NV_BLOOM_LEVELS; ++i)
        bloom_region(scene_width, scene_height, i, region[i]);

    // Pass k writes `dst` from `src` (src 0 is the scene color, src n + 1 is mip n).
    typedef struct BloomPass {
        u32 dst, src;
        WGPURenderPipeline pipeline;
        b32 add;
    } BloomPass;
    BloomPass passes[BLOOM_PASSES];
    u32 count = 0;
    for (u32 i = 0; i < NV_BLOOM_LEVELS; ++i)
        passes[count++] = (BloomPass){i, i, i == 0 ? renderer->bloom_down_first_pipeline : renderer->bloom_down_pipeline, 0};
    for (s32 i = NV_BLOOM_LEVELS - 2; i >= 0; --i)
        passes[count++] = (BloomPass){(u32)i, (u32)i + 2, renderer->bloom_up_pipeline, 1};
    NV_ASSERT(count == BLOOM_PASSES);

    local_persist f32 uniforms[BLOOM_PASSES * BLOOM_UNIFORM_STRIDE / sizeof(f32)];
    memset(uniforms, 0, sizeof(uniforms));
    for (u32 k = 0; k < count; ++k) {
        f32* u = uniforms + k * BLOOM_UNIFORM_STRIDE / sizeof(f32);
        u32 src = passes[k].src;
        if (src == 0) {
            u[0] = (f32)renderer->target_width;
            u[1] = (f32)renderer->target_height;
            u[2] = (f32)scene_width;
            u[3] = (f32)scene_height;
        } else {
            u32 mip = src - 1;
            u[0] = (f32)(renderer->bloom_width >> mip ? renderer->bloom_width >> mip : 1);
            u[1] = (f32)(renderer->bloom_height >> mip ? renderer->bloom_height >> mip : 1);
            u[2] = (f32)region[mip][0];
            u[3] = (f32)region[mip][1];
        }
    }
    wgpuQueueWriteBuffer(renderer->gpu->queue, renderer->bloom_buffer, 0, uniforms, sizeof(uniforms));

    for (u32 k = 0; k < count; ++k) {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = renderer->bloom_views[passes[k].dst];
        color.loadOp = passes[k].add ? WGPULoadOp_Load : WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        desc.label = (WGPUStringView){passes[k].add ? "bloom up" : "bloom down", WGPU_STRLEN};
        desc.colorAttachmentCount = 1;
        desc.colorAttachments = &color;
        // The first pass starts the timing (queries 6 and 7) and the last ends it (8 and 9); a pass's
        // timestamps must both be given, so the span is from 6 to 9.
        WGPUPassTimestampWrites timestamps = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        if (renderer->timestamp_copied && (k == 0 || k == count - 1)) {
            timestamps.querySet = renderer->timestamp_queries;
            timestamps.beginningOfPassWriteIndex = k == 0 ? 6 : 8;
            timestamps.endOfPassWriteIndex = k == 0 ? 7 : 9;
            desc.timestampWrites = &timestamps;
        }
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
        u32 dst_w = region[passes[k].dst][0], dst_h = region[passes[k].dst][1];
        wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f, (f32)dst_w, (f32)dst_h, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, dst_w, dst_h);
        wgpuRenderPassEncoderSetPipeline(pass, passes[k].pipeline);
        u32 offset = k * BLOOM_UNIFORM_STRIDE;
        wgpuRenderPassEncoderSetBindGroup(pass, 0, renderer->bloom_groups[passes[k].src], 1, &offset);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
    renderer->bloom_recorded = renderer->timestamp_copied;
}

// Keeps the scene's targets: the color target the upscale pass samples, the multisampled color
// target the scene pass resolves from (none while MSAA is off) and the depth target, all allocated
// at the same size, the scene's resolution rounded up to 64. They are kept while they are large
// enough and not more than twice the pixels needed, so a splitter drag does not remake them every
// frame. Also follows `msaa`: remakes the scene pipelines when the sample count changes.
internal void update_scene_targets(NvRenderer* renderer, u32 width, u32 height)
{
    NvGpu* gpu = renderer->gpu;
    u32 samples = renderer->msaa == 4 ? 4 : 1;
    if (samples != renderer->scene_samples) {
        release_scene_pipelines(renderer);
        renderer->scene_samples = samples;
        create_pipelines(renderer);
        nv_log(NV_LOG_INFO, "nv", "anti-aliasing: %s", samples > 1 ? "MSAA 4x" : "off");
    }

    u32 need_width = round_up_64(width), need_height = round_up_64(height);
    b32 large_enough = renderer->target_width >= width && renderer->target_height >= height;
    b32 too_large = (u64)renderer->target_width * renderer->target_height > 2ull * need_width * need_height;
    b32 resized = !renderer->scene_color || !large_enough || too_large || renderer->msaa_format != NV_SCENE_FORMAT;
    if (resized) {
        renderer->target_width = need_width;
        renderer->target_height = need_height;
        renderer->msaa_format = NV_SCENE_FORMAT;
        release_texture(&renderer->scene_color, &renderer->scene_color_view);
        create_color_target(gpu, "scene color", need_width, need_height, 1,
                            WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding, &renderer->scene_color,
                            &renderer->scene_color_view);
        rebuild_upscale_group(renderer);
        rebuild_bloom_groups(renderer);
        nv_log(NV_LOG_INFO, "nv", "scene color target: %ux%u %s, linear HDR (%.1f MB) for a scene of %ux%u, tone mapped and shown with the nearest filter",
               need_width, need_height, nv_gpu_format_name(NV_SCENE_FORMAT), target_megabytes(need_width, need_height, 1), width, height);
    }

    // The multisampled color target.
    if (samples == 1) {
        release_texture(&renderer->msaa_color, &renderer->msaa_color_view);
    } else if (!renderer->msaa_color || resized) {
        release_texture(&renderer->msaa_color, &renderer->msaa_color_view);
        create_color_target(gpu, "msaa color target", renderer->target_width, renderer->target_height, samples,
                            WGPUTextureUsage_RenderAttachment, &renderer->msaa_color, &renderer->msaa_color_view);
        renderer->msaa_width = renderer->target_width;
        renderer->msaa_height = renderer->target_height;
        nv_log(NV_LOG_INFO, "nv", "msaa color target: %ux%u %s x%u samples (%.1f MB), resolved into the scene color target",
               renderer->target_width, renderer->target_height, nv_gpu_format_name(NV_SCENE_FORMAT), samples,
               target_megabytes(renderer->target_width, renderer->target_height, samples));
    }

    // The depth target.
    if (renderer->depth_texture && !resized && renderer->depth_texture_sampled == renderer->depth_sampled &&
        renderer->depth_samples == samples)
        return;
    if (renderer->depth_texture) {
        wgpuTextureViewRelease(renderer->depth_view);
        wgpuTextureRelease(renderer->depth_texture);
    }
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"depth target", WGPU_STRLEN};
    desc.usage = WGPUTextureUsage_RenderAttachment;
    if (renderer->depth_sampled)
        desc.usage |= WGPUTextureUsage_TextureBinding;
    desc.size = (WGPUExtent3D){renderer->target_width, renderer->target_height, 1};
    desc.format = WGPUTextureFormat_Depth32Float;
    desc.sampleCount = samples;
    renderer->depth_texture = wgpuDeviceCreateTexture(gpu->device, &desc);
    renderer->depth_view = wgpuTextureCreateView(renderer->depth_texture, NULL);
    renderer->depth_width = renderer->target_width;
    renderer->depth_height = renderer->target_height;
    renderer->depth_texture_sampled = renderer->depth_sampled;
    renderer->depth_samples = samples;
    nv_log(NV_LOG_INFO, "nv", "depth target: %ux%u %s x%u samples (%.1f MB), reverse Z (cleared to 0, compare Greater)%s",
           renderer->target_width, renderer->target_height, nv_gpu_format_name(WGPUTextureFormat_Depth32Float), samples,
           target_megabytes(renderer->target_width, renderer->target_height, samples),
           renderer->depth_sampled ? ", samplable (texture viewer)" : "");
}

internal NvMat4 camera_projection(NvNode* camera_node, f32 aspect)
{
    NvCamera* camera = &camera_node->camera;
    NvMat4 proj = nv_mat4_identity();
    switch (camera->projection) {
    case NV_PROJECTION_PERSPECTIVE:
        proj = nv_mat4_perspective(camera->fov_y, aspect, camera->near_z, camera->far_z);
        break;
    case NV_PROJECTION_ORTHOGRAPHIC:
        proj = nv_mat4_orthographic(camera->ortho_height, aspect, camera->near_z, camera->far_z);
        break;
    default:
        NV_INVALID_CODE_PATH;
    }
    return proj;
}

// Reverse Z for drawing: clip z becomes w - z, so depth runs 1 (near) to 0 (far). A float depth
// buffer keeps its precision near 0, where the far range is, instead of wasting it near the camera.
internal NvMat4 reverse_depth(NvMat4 m)
{
    for (u32 column = 0; column < 4; ++column)
        m.e[column * 4 + 2] = m.e[column * 4 + 3] - m.e[column * 4 + 2];
    return m;
}

internal NvMat4 camera_view_proj(NvNode* camera_node, f32 aspect)
{
    return nv_mat4_mul(reverse_depth(camera_projection(camera_node, aspect)), nv_mat4_inverse(camera_node->world));
}

void nv_renderer_camera_matrices(NvScene* scene, NvSceneOutput output, NvMat4* view, NvMat4* projection)
{
    NV_ASSERT(output.width && output.height);
    NvNode* camera = nv_scene_get(scene, scene->active_camera);
    *view = nv_mat4_inverse(camera->world);
    *projection = camera_projection(camera, (f32)output.width / (f32)output.height);
}

// A point through a projective matrix, with the perspective divide.
internal NvVec3 project(NvMat4 m, f32 x, f32 y, f32 z)
{
    f32 w = m.e[3] * x + m.e[7] * y + m.e[11] * z + m.e[15];
    NvVec3 p = nv_mat4_transform_point(m, nv_vec3(x, y, z));
    return nv_vec3_scale(p, 1.0f / w);
}

NvRay nv_renderer_view_ray(NvScene* scene, NvSceneOutput output, f32 x, f32 y)
{
    NV_ASSERT(output.width && output.height && output.pixel_width > 0.0f && output.pixel_height > 0.0f);
    NvNode* camera = nv_scene_get(scene, scene->active_camera);
    NvMat4 view_proj = nv_mat4_mul(camera_projection(camera, (f32)output.width / (f32)output.height), nv_mat4_inverse(camera->world));
    NvMat4 to_world = nv_mat4_inverse(view_proj);
    // Canvas pixels to scene pixels: from the image's corner, `pixel_width` x `pixel_height` screen pixels each.
    f32 scene_x = (x - (f32)output.image.x) / output.pixel_width;
    f32 scene_y = (y - (f32)output.image.y) / output.pixel_height;
    f32 ndc_x = scene_x / (f32)output.width * 2.0f - 1.0f;
    f32 ndc_y = 1.0f - scene_y / (f32)output.height * 2.0f;
    NvVec3 near_point = project(to_world, ndc_x, ndc_y, 0.0f); // depth runs 0 (near) to 1 (far)
    NvVec3 far_point = project(to_world, ndc_x, ndc_y, 1.0f);
    return (NvRay){near_point, nv_vec3_normalize(nv_vec3_sub(far_point, near_point))};
}

// Where a ray enters a box (slab test), or a negative number when it misses.
internal f32 ray_box(NvVec3 origin, NvVec3 direction, NvVec3 lo, NvVec3 hi)
{
    f32 o[3] = {origin.x, origin.y, origin.z};
    f32 d[3] = {direction.x, direction.y, direction.z};
    f32 l[3] = {lo.x, lo.y, lo.z};
    f32 h[3] = {hi.x, hi.y, hi.z};
    f32 t_enter = 0.0f;
    f32 t_exit = 1.0e30f;
    for (u32 axis = 0; axis < 3; ++axis) {
        // Flat meshes (the ground) get a little thickness so they can still be hit.
        f32 pad = (h[axis] - l[axis] < 1.0e-3f) ? 1.0e-3f : 0.0f;
        if (fabsf(d[axis]) < 1.0e-12f) {
            if (o[axis] < l[axis] - pad || o[axis] > h[axis] + pad)
                return -1.0f;
            continue;
        }
        f32 t0 = (l[axis] - pad - o[axis]) / d[axis];
        f32 t1 = (h[axis] + pad - o[axis]) / d[axis];
        if (t0 > t1) {
            f32 swap = t0;
            t0 = t1;
            t1 = swap;
        }
        t_enter = t0 > t_enter ? t0 : t_enter;
        t_exit = t1 < t_exit ? t1 : t_exit;
        if (t_enter > t_exit)
            return -1.0f;
    }
    return t_enter;
}

NvBox nv_renderer_mesh_bounds(NvRenderer* renderer, NvMeshId id, const NvSkin* skin)
{
    NV_ASSERT(id.index < renderer->mesh_count);
    NvRenderMesh* mesh = &renderer->meshes[id.index];
    if (!mesh->skinned || !skin || !skin->count)
        return mesh->bounds;
    NvBox posed = nv_box_empty();
    const NvBox* boxes = renderer->joint_bounds + mesh->joint_bounds_offset;
    u32 count = mesh->joint_bounds_count < skin->count ? mesh->joint_bounds_count : skin->count;
    for (u32 j = 0; j < count; ++j)
        posed = nv_box_union(posed, nv_box_transform(boxes[j], skin->matrices[j]));
    return posed;
}

NvNodeId nv_renderer_pick(NvRenderer* renderer, NvScene* scene, const NvSkin* skins, NvRay ray, f32* distance)
{
    NvNodeId best = {0};
    f32 best_t = 1.0e30f;
    for (u32 index = 1; index <= scene->node_count; ++index) {
        NvNode* node = &scene->nodes[index];
        if (!(node->gen & 1) || !node->mesh.index)
            continue;
        // In the node's space the box is axis-aligned; a linear map keeps the ray parameter, so
        // hits in different nodes stay comparable.
        NvMat4 to_local = nv_mat4_inverse(node->world);
        const NvSkin* skin = (skins && node->animator.index) ? &skins[node->animator.index] : NULL;
        NvBox box = nv_renderer_mesh_bounds(renderer, node->mesh, skin);
        if (nv_box_is_empty(box))
            continue;
        f32 t = ray_box(nv_mat4_transform_point(to_local, ray.origin), nv_mat4_transform_dir(to_local, ray.direction),
                        box.min, box.max);
        if (t >= 0.0f && t < best_t) {
            best_t = t;
            best = (NvNodeId){index, node->gen};
        }
    }
    if (distance)
        *distance = best_t;
    return best;
}

// Fits the light's orthographic box around the camera's view up to the shadow distance
// (docs/specs/shadows.md): the view is enclosed in a sphere, whose radius does not change as the
// camera turns, and the sphere's center is snapped to whole shadow-map texels, so shadows neither
// shimmer nor crawl as the camera moves. Fills the uniforms' light matrix and shadow parameters.
internal void fit_shadow(NvRenderer* renderer, NvNode* camera, f32 aspect, NvNode* light, FrameUniforms* uniforms)
{
    const NvShadowSettings* settings = &renderer->shadows;
    NvCamera* lens = &camera->camera;
    f32 near_z = lens->near_z;
    f32 far_z = settings->distance < lens->far_z ? settings->distance : lens->far_z;
    if (far_z <= near_z)
        far_z = near_z + 1.0f;
    NvVec3 eye = nv_mat4_translation(camera->world);
    NvVec3 forward = nv_mat4_forward(camera->world);

    // Half the view's width and height at one meter, for either projection.
    f32 tan_y = lens->projection == NV_PROJECTION_PERSPECTIVE ? tanf(lens->fov_y * 0.5f) : 0.0f;
    f32 tan_x = tan_y * aspect;
    f32 middle = (near_z + far_z) * 0.5f;
    NvVec3 center = nv_vec3_add(eye, nv_vec3_scale(forward, middle));
    f32 far_side = sqrtf(far_z * tan_x * far_z * tan_x + far_z * tan_y * far_z * tan_y);
    f32 near_side = sqrtf(near_z * tan_x * near_z * tan_x + near_z * tan_y * near_z * tan_y);
    f32 radius = sqrtf((far_z - middle) * (far_z - middle) + far_side * far_side);
    f32 near_radius = sqrtf((middle - near_z) * (middle - near_z) + near_side * near_side);
    radius = radius > near_radius ? radius : near_radius;
    if (lens->projection == NV_PROJECTION_ORTHOGRAPHIC) {
        f32 half_h = lens->ortho_height * 0.5f;
        radius = sqrtf(half_h * half_h * (1.0f + aspect * aspect) + (far_z - middle) * (far_z - middle));
    }

    // The light's axes, from its node: it shines along its -Z.
    NvVec3 right = nv_vec3_normalize(nv_vec3(light->world.e[0], light->world.e[1], light->world.e[2]));
    NvVec3 up = nv_vec3_normalize(nv_vec3(light->world.e[4], light->world.e[5], light->world.e[6]));
    NvVec3 back = nv_vec3_normalize(nv_vec3(light->world.e[8], light->world.e[9], light->world.e[10]));

    f32 size = (f32)renderer->shadow_size;
    f32 texel = 2.0f * radius / size;
    f32 cx = floorf(nv_vec3_dot(center, right) / texel) * texel;
    f32 cy = floorf(nv_vec3_dot(center, up) / texel) * texel;
    f32 cz = nv_vec3_dot(center, back);
    center = nv_vec3_add(nv_vec3_add(nv_vec3_scale(right, cx), nv_vec3_scale(up, cy)), nv_vec3_scale(back, cz));

    NvVec3 origin = nv_vec3_add(center, nv_vec3_scale(back, radius + SHADOW_PULLBACK));
    NvMat4 light_world = {{right.x, right.y, right.z, 0, up.x, up.y, up.z, 0, back.x, back.y, back.z, 0,
                           origin.x, origin.y, origin.z, 1}};
    NvMat4 projection = nv_mat4_orthographic(2.0f * radius, 1.0f, 0.0f, 2.0f * radius + SHADOW_PULLBACK);
    renderer->light_view_proj = nv_mat4_mul(projection, nv_mat4_inverse(light_world));
    uniforms->light_view_proj = renderer->light_view_proj;

    uniforms->shadow[0] = 1.0f / size;
    uniforms->shadow[1] = settings->filter == NV_SHADOW_FILTER_HIGH ? 2.0f : 1.0f;
    // The lookup moves off the surface by about one and a half texels, against acne on slopes.
    uniforms->shadow[2] = texel * 1.5f;
    uniforms->shadow_fade[0] = settings->distance * 0.9f;
    uniforms->shadow_fade[1] = settings->distance;

    if (settings->show_box) {
        NvMat4 to_world = nv_mat4_inverse(renderer->light_view_proj);
        NvVec3 corners[8];
        for (u32 c = 0; c < 8; ++c) {
            f32 x = (c & 1) ? 1.0f : -1.0f, y = (c & 2) ? 1.0f : -1.0f, z = (c & 4) ? 1.0f : 0.0f;
            corners[c] = nv_mat4_transform_point(to_world, nv_vec3(x, y, z));
        }
        for (u32 c = 0; c < 8; ++c) {
            for (u32 bit = 1; bit < 8; bit <<= 1) {
                if (!(c & bit))
                    nv_renderer_debug_line(renderer, corners[c], corners[c | bit], nv_vec3(1.0f, 0.9f, 0.3f));
            }
        }
    }
}

void nv_renderer_draw(NvRenderer* renderer, NvScene* scene, const NvSkin* skins, NvSceneOutput output,
                      WGPUCommandEncoder encoder, WGPUTextureView target)
{
    NvGpu* gpu = renderer->gpu;
    if (!output.width || !output.height)
        output = (NvSceneOutput){gpu->width, gpu->height, {0, 0, gpu->width, gpu->height}, 1.0f, 1.0f};
    NV_ASSERT(output.image.x + output.image.width <= gpu->width && output.image.y + output.image.height <= gpu->height);
    update_scene_targets(renderer, output.width, output.height);
    update_bloom(renderer);
    update_shadow_map(renderer);
    renderer->scene_width = output.width;
    renderer->scene_height = output.height;
    // The scene is drawn in the top-left part of its targets.
    NvRect viewport = {0, 0, output.width, output.height};

    FrameUniforms uniforms = {0};
    uniforms.view_proj = camera_view_proj(nv_scene_get(scene, scene->active_camera),
                                          (f32)viewport.width / (f32)viewport.height);
    memcpy(uniforms.ambient, renderer->ambient, sizeof(renderer->ambient));
    NvNode* camera = nv_scene_get(scene, scene->active_camera);
    NvVec3 eye = nv_mat4_translation(camera->world);
    uniforms.camera_pos[0] = eye.x;
    uniforms.camera_pos[1] = eye.y;
    uniforms.camera_pos[2] = eye.z;
    NvNode* sun = NULL;

    u32 object_count = 0;
    renderer->skin_matrix_count = 0;
    ++renderer->frame_index;
    b32 have_light = 0;
    for (u32 index = 1; index <= scene->node_count; ++index) {
        NvNode* node = &scene->nodes[index];
        if (!(node->gen & 1))
            continue;

        // TODO: Only the first directional light is used until lighting gets its own pass.
        if (node->light.type == NV_LIGHT_DIRECTIONAL && !have_light) {
            NvVec3 dir = nv_mat4_forward(node->world);
            NvVec3 color = nv_vec3_scale(node->light.color, node->light.intensity);
            uniforms.light_dir[0] = dir.x;
            uniforms.light_dir[1] = dir.y;
            uniforms.light_dir[2] = dir.z;
            uniforms.light_color[0] = color.x;
            uniforms.light_color[1] = color.y;
            uniforms.light_color[2] = color.z;
            have_light = 1;
            sun = node;
        }

        if (!node->mesh.index)
            continue;
        NV_ASSERT(node->mesh.index < renderer->mesh_count && node->material.index < renderer->material_count);
        NvObjectData* object = &renderer->objects[object_count];
        object->model = node->world;
        object->joint_offset = NO_SKIN;
        const NvSkin* skin = (skins && node->animator.index) ? &skins[node->animator.index] : NULL;
        if (renderer->meshes[node->mesh.index].skinned && skin && skin->count) {
            u32 a = node->animator.index;
            NV_ASSERT(a < NV_MAX_ANIMATORS);
            if (renderer->skin_frame[a] != renderer->frame_index) {
                NV_ASSERT(renderer->skin_matrix_count + skin->count <= NV_MAX_SKIN_MATRICES);
                renderer->skin_frame[a] = renderer->frame_index;
                renderer->skin_offsets[a] = renderer->skin_matrix_count;
                memcpy(renderer->skin_matrices + renderer->skin_matrix_count, skin->matrices, skin->count * sizeof(NvMat4));
                renderer->skin_matrix_count += skin->count;
            }
            object->joint_offset = renderer->skin_offsets[a];
        }
        renderer->object_nodes[object_count] = index;
        ++object_count;
    }

    b32 shadows = renderer->shadows.size && sun && object_count;
    if (shadows)
        fit_shadow(renderer, camera, (f32)viewport.width / (f32)viewport.height, sun, &uniforms);

    WGPUQueue queue = gpu->queue;
    wgpuQueueWriteBuffer(queue, renderer->frame_buffer, 0, &uniforms, sizeof(uniforms));
    if (object_count)
        wgpuQueueWriteBuffer(queue, renderer->object_buffer, 0, renderer->objects, object_count * sizeof(NvObjectData));
    if (renderer->skin_matrix_count)
        wgpuQueueWriteBuffer(queue, renderer->skin_buffer, 0, renderer->skin_matrices, renderer->skin_matrix_count * sizeof(NvMat4));
    if (renderer->debug_vertex_count)
        wgpuQueueWriteBuffer(queue, renderer->debug_buffer, 0, renderer->debug_vertices, renderer->debug_vertex_count * sizeof(NvDebugVertex));

    // Shadow pass: every mesh's depth from the light, into the map the scene pass then samples.
    // WebGPU orders the two passes, so the scene pass sees what this one wrote.
    u32 shadow_draws = 0;
    renderer->timestamp_copied = renderer->timestamp_queries && !renderer->timestamp_mapping;
    if (shadows) {
        WGPURenderPassDepthStencilAttachment shadow_depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        shadow_depth.view = renderer->shadow_view;
        shadow_depth.depthLoadOp = WGPULoadOp_Clear;
        shadow_depth.depthStoreOp = WGPUStoreOp_Store;
        shadow_depth.depthClearValue = 1.0f;
        WGPURenderPassDescriptor shadow_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        shadow_desc.label = (WGPUStringView){"shadow", WGPU_STRLEN};
        shadow_desc.depthStencilAttachment = &shadow_depth;
        WGPUPassTimestampWrites shadow_timestamps = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        if (renderer->timestamp_copied) {
            shadow_timestamps.querySet = renderer->timestamp_queries;
            shadow_timestamps.beginningOfPassWriteIndex = 2;
            shadow_timestamps.endOfPassWriteIndex = 3;
            shadow_desc.timestampWrites = &shadow_timestamps;
        }
        WGPURenderPassEncoder shadow_pass = wgpuCommandEncoderBeginRenderPass(encoder, &shadow_desc);
        wgpuRenderPassEncoderSetBindGroup(shadow_pass, 0, renderer->shadow_frame_group, 0, NULL);
        WGPURenderPipeline bound = NULL;
        u32 bound_mesh = 0;
        for (u32 i = 0; i < object_count; ++i) {
            NvNode* node = &scene->nodes[renderer->object_nodes[i]];
            NvRenderMesh* mesh = &renderer->meshes[node->mesh.index];
            NvRenderMaterial* material = &renderer->materials[node->material.index];
            // Casters outside the light's box cannot shade anything the camera sees. The box goes
            // through the light's matrix; clip space is a unit box there (depth 0 to 1).
            const NvSkin* skin = (skins && node->animator.index) ? &skins[node->animator.index] : NULL;
            NvBox box = nv_box_transform(nv_renderer_mesh_bounds(renderer, node->mesh, skin),
                                         nv_mat4_mul(renderer->light_view_proj, node->world));
            if (box.max.x < -1.0f || box.min.x > 1.0f || box.max.y < -1.0f || box.min.y > 1.0f || box.min.z > 1.0f)
                continue;
            WGPURenderPipeline pipeline = renderer->shadow_pipelines[mesh->skinned ? 1 : 0][material->desc.double_sided ? 1 : 0];
            if (pipeline != bound) {
                wgpuRenderPassEncoderSetPipeline(shadow_pass, pipeline);
                bound = pipeline;
            }
            if (node->mesh.index != bound_mesh) {
                wgpuRenderPassEncoderSetVertexBuffer(shadow_pass, 0, mesh->vertices, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderSetIndexBuffer(shadow_pass, mesh->indices, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
                bound_mesh = node->mesh.index;
            }
            wgpuRenderPassEncoderDrawIndexed(shadow_pass, mesh->index_count, 1, 0, 0, i);
            ++shadow_draws;
        }
        wgpuRenderPassEncoderEnd(shadow_pass);
        wgpuRenderPassEncoderRelease(shadow_pass);
    }

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    if (renderer->scene_samples > 1) {
        // Draws into the multisampled target, which is resolved into the scene color target and then
        // not needed.
        color.view = renderer->msaa_color_view;
        color.resolveTarget = renderer->scene_color_view;
        color.storeOp = WGPUStoreOp_Discard;
    } else {
        color.view = renderer->scene_color_view;
        color.storeOp = WGPUStoreOp_Store;
    }
    color.loadOp = WGPULoadOp_Clear;
    color.clearValue = (WGPUColor){renderer->clear_color[0], renderer->clear_color[1], renderer->clear_color[2], renderer->clear_color[3]};
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = renderer->depth_view;
    depth.depthLoadOp = WGPULoadOp_Clear;
    // Nothing reads the depth after the pass, unless the texture viewer shows it.
    depth.depthStoreOp = renderer->depth_texture_sampled ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
    depth.depthClearValue = 0.0f; // reverse Z: far is 0
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.label = (WGPUStringView){"scene", WGPU_STRLEN};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;
    pass_desc.depthStencilAttachment = &depth;
    WGPUPassTimestampWrites timestamps = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    if (renderer->timestamp_copied) {
        timestamps.querySet = renderer->timestamp_queries;
        timestamps.beginningOfPassWriteIndex = 0;
        timestamps.endOfPassWriteIndex = 1;
        pass_desc.timestampWrites = &timestamps;
    }

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetViewport(pass, (f32)viewport.x, (f32)viewport.y, (f32)viewport.width,
                                     (f32)viewport.height, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, viewport.x, viewport.y, viewport.width, viewport.height);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, renderer->frame_group, 0, NULL);

    WGPURenderPipeline bound_pipeline = NULL;
    u32 bound_mesh = 0;
    u32 bound_material = 0xFFFFFFFFu;
    NvRenderStats* stats = &renderer->stats;
    *stats = (NvRenderStats){.draws = object_count, .skin_matrices = renderer->skin_matrix_count,
                             .debug_lines = renderer->debug_vertex_count / 2, .shadow_draws = shadow_draws};
    for (u32 i = 0; i < object_count; ++i) {
        NvNode* node = &scene->nodes[renderer->object_nodes[i]];
        NvRenderMesh* mesh = &renderer->meshes[node->mesh.index];
        NvRenderMaterial* material = &renderer->materials[node->material.index];

        WGPURenderPipeline pipeline = renderer->pipelines[mesh->skinned ? 1 : 0][material->desc.double_sided ? 1 : 0];
        if (pipeline != bound_pipeline) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            bound_pipeline = pipeline;
            ++stats->pipeline_changes;
        }
        if (node->material.index != bound_material) {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, material->bind_group, 0, NULL);
            bound_material = node->material.index;
            ++stats->material_changes;
        }
        if (node->mesh.index != bound_mesh) {
            ++stats->mesh_changes;
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh->vertices, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(pass, mesh->indices, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            bound_mesh = node->mesh.index;
        }
        // The first-instance offset makes instance_index equal the object's slot in `objects`.
        wgpuRenderPassEncoderDrawIndexed(pass, mesh->index_count, 1, 0, 0, i);
        stats->triangles += mesh->index_count / 3;
        stats->skinned_draws += mesh->skinned;
    }

    if (renderer->debug_vertex_count) {
        wgpuRenderPassEncoderSetPipeline(pass, renderer->debug_pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, renderer->debug_buffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(pass, renderer->debug_vertex_count, 1, 0, 0);
        renderer->debug_vertex_count = 0;
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    if (renderer->bloom_texture)
        record_bloom(renderer, encoder, output.width, output.height);
    else
        renderer->bloom_recorded = 0;

    // The upscale pass: the scene color target, shown with the nearest filter in the image's
    // rectangle of the canvas. The canvas is cleared to black first, which is the bars around a
    // fixed size; the docks drawn later cover the rest.
    NvPostSettings* post = &renderer->post;
    f32 exposure = post->exposure > 0.0f ? post->exposure : 1.0f;
    f32 tone = (f32)(post->tone < NV_TONE_COUNT ? post->tone : NV_TONE_CLAMP);
    u32 bloom_size[2] = {1, 1};
    if (renderer->bloom_texture)
        bloom_region(output.width, output.height, 0, bloom_size);
    f32 bloom_intensity = renderer->bloom_texture ? post->bloom_intensity : 0.0f;
    f32 params[16] = {(f32)output.image.x, (f32)output.image.y, output.pixel_width, output.pixel_height, (f32)output.width, (f32)output.height,
                      exposure, tone,
                      bloom_intensity, (f32)bloom_size[0], (f32)bloom_size[1], 0.0f,
                      (f32)(renderer->bloom_texture ? renderer->bloom_width : 1), (f32)(renderer->bloom_texture ? renderer->bloom_height : 1), 0.0f, 0.0f};
    wgpuQueueWriteBuffer(queue, renderer->upscale_buffer, 0, params, sizeof(params));
    WGPURenderPassColorAttachment upscale_color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    upscale_color.view = target;
    upscale_color.loadOp = WGPULoadOp_Clear;
    upscale_color.storeOp = WGPUStoreOp_Store;
    upscale_color.clearValue = (WGPUColor){0.0, 0.0, 0.0, 1.0};
    WGPURenderPassDescriptor upscale_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    upscale_desc.label = (WGPUStringView){"upscale", WGPU_STRLEN};
    upscale_desc.colorAttachmentCount = 1;
    upscale_desc.colorAttachments = &upscale_color;
    WGPUPassTimestampWrites upscale_timestamps = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    if (renderer->timestamp_copied) {
        upscale_timestamps.querySet = renderer->timestamp_queries;
        upscale_timestamps.beginningOfPassWriteIndex = 4;
        upscale_timestamps.endOfPassWriteIndex = 5;
        upscale_desc.timestampWrites = &upscale_timestamps;
    }
    WGPURenderPassEncoder upscale_pass = wgpuCommandEncoderBeginRenderPass(encoder, &upscale_desc);
    if (output.image.width && output.image.height) {
        wgpuRenderPassEncoderSetViewport(upscale_pass, (f32)output.image.x, (f32)output.image.y, (f32)output.image.width,
                                         (f32)output.image.height, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(upscale_pass, output.image.x, output.image.y, output.image.width, output.image.height);
        wgpuRenderPassEncoderSetPipeline(upscale_pass, renderer->upscale_pipeline);
        wgpuRenderPassEncoderSetBindGroup(upscale_pass, 0, renderer->upscale_group, 0, NULL);
        wgpuRenderPassEncoderDraw(upscale_pass, 3, 1, 0, 0);
    }
    wgpuRenderPassEncoderEnd(upscale_pass);
    wgpuRenderPassEncoderRelease(upscale_pass);

    if (renderer->timestamp_copied) {
        // A frame without a shadow pass resolves its two queries as 0, which reads as no time.
        wgpuCommandEncoderResolveQuerySet(encoder, renderer->timestamp_queries, 0, NV_TIMESTAMP_COUNT, renderer->timestamp_resolve, 0);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, renderer->timestamp_resolve, 0, renderer->timestamp_readback, 0, NV_TIMESTAMP_COUNT * sizeof(u64));
    }
}

internal void on_timestamps_mapped(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2)
{
    (void)message, (void)userdata2;
    NvRenderer* renderer = userdata1;
    if (status == WGPUMapAsyncStatus_Success) {
        const u64* ticks = wgpuBufferGetConstMappedRange(renderer->timestamp_readback, 0, NV_TIMESTAMP_COUNT * sizeof(u64));
        // Timestamps are nanoseconds; a pass the browser could not time reads as 0 or reversed.
        if (ticks && ticks[1] > ticks[0])
            renderer->gpu_ms = (f64)(ticks[1] - ticks[0]) / 1.0e6;
        if (ticks)
            renderer->gpu_shadow_ms = ticks[3] > ticks[2] ? (f64)(ticks[3] - ticks[2]) / 1.0e6 : 0.0;
        if (ticks)
            renderer->gpu_upscale_ms = ticks[5] > ticks[4] ? (f64)(ticks[5] - ticks[4]) / 1.0e6 : 0.0;
        if (ticks)
            renderer->gpu_bloom_ms = renderer->bloom_texture && ticks[9] > ticks[6] ? (f64)(ticks[9] - ticks[6]) / 1.0e6 : 0.0;
        wgpuBufferUnmap(renderer->timestamp_readback);
    }
    renderer->timestamp_mapping = 0;
}

void nv_renderer_end_frame(NvRenderer* renderer)
{
    if (!renderer->timestamp_copied)
        return;
    renderer->timestamp_copied = 0;
    renderer->timestamp_mapping = 1;
    WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = on_timestamps_mapped;
    callback.userdata1 = renderer;
    wgpuBufferMapAsync(renderer->timestamp_readback, WGPUMapMode_Read, 0, NV_TIMESTAMP_COUNT * sizeof(u64), callback);
}
