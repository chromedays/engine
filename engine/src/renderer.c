#include "nv/renderer.h"

#include <string.h>

#define NO_SKIN 0xFFFFFFFFu

// Layouts match the WGSL structs below.
typedef struct FrameUniforms {
    NvMat4 view_proj;
    f32 light_dir[4];
    f32 light_color[4];
    f32 ambient[4];
} FrameUniforms;

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
    "struct Frame {\n"
    "    view_proj: mat4x4f,\n"
    "    light_dir: vec4f,\n"
    "    light_color: vec4f,\n"
    "    ambient: vec4f,\n"
    "};\n"
    "struct Object {\n"
    "    model: mat4x4f,\n"
    "    joint_offset: u32,\n"
    "};\n"
    "struct Material {\n"
    "    base_color: vec4f,\n"
    "};\n"
    "@group(0) @binding(0) var<uniform> frame: Frame;\n"
    "@group(0) @binding(1) var<storage, read> objects: array<Object>;\n"
    "@group(0) @binding(2) var<storage, read> skin: array<mat4x4f>;\n"
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
    "};\n"
    "\n"
    "fn finish(model: mat4x4f, position: vec3f, normal: vec3f, uv: vec2f) -> VsOut {\n"
    "    var out: VsOut;\n"
    "    out.clip = frame.view_proj * model * vec4f(position, 1.0);\n"
    "    out.normal = (model * vec4f(normal, 0.0)).xyz;\n"
    "    out.uv = uv;\n"
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
    "    let object = objects[i];\n"
    "    var model = object.model;\n"
    "    if (object.joint_offset != 0xFFFFFFFFu) {\n"
    "        let o = object.joint_offset;\n"
    "        let s = skin[o + in.joints.x] * in.weights.x + skin[o + in.joints.y] * in.weights.y +\n"
    "                skin[o + in.joints.z] * in.weights.z + skin[o + in.joints.w] * in.weights.w;\n"
    "        model = model * s;\n"
    "    }\n"
    "    return finish(model, in.position, in.normal, in.uv);\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_main(in: VsOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {\n"
    "    var n = normalize(in.normal);\n"
    "    if (!front) { n = -n; }\n"
    "    let base = material.base_color * textureSample(base_color_texture, material_sampler, in.uv);\n"
    "    let diffuse = max(dot(n, -frame.light_dir.xyz), 0.0);\n"
    "    let light = frame.ambient.rgb + frame.light_color.rgb * diffuse;\n"
    "    return vec4f(base.rgb * light, base.a);\n"
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

    WGPUBindGroupLayoutEntry frame_entries[3] = {
        {.binding = 0, .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
         .buffer = {.type = WGPUBufferBindingType_Uniform, .minBindingSize = sizeof(FrameUniforms)}},
        {.binding = 1, .visibility = WGPUShaderStage_Vertex,
         .buffer = {.type = WGPUBufferBindingType_ReadOnlyStorage}},
        {.binding = 2, .visibility = WGPUShaderStage_Vertex,
         .buffer = {.type = WGPUBufferBindingType_ReadOnlyStorage}},
    };
    WGPUBindGroupLayoutDescriptor frame_desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    frame_desc.entryCount = NV_ARRAY_COUNT(frame_entries);
    frame_desc.entries = frame_entries;
    renderer->frame_layout = wgpuDeviceCreateBindGroupLayout(device, &frame_desc);

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
    color_target.format = gpu->surface_format;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = (WGPUStringView){"fs_main", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth24Plus;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Less;

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
    debug_target.format = gpu->surface_format;
    debug_target.blend = &blend;
    WGPUFragmentState debug_fragment = WGPU_FRAGMENT_STATE_INIT;
    debug_fragment.module = debug_module;
    debug_fragment.entryPoint = (WGPUStringView){"fs_main", WGPU_STRLEN};
    debug_fragment.targetCount = 1;
    debug_fragment.targets = &debug_target;

    WGPUDepthStencilState debug_depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    debug_depth.format = WGPUTextureFormat_Depth24Plus;
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
    debug_desc.fragment = &debug_fragment;
    renderer->debug_pipeline = wgpuDeviceCreateRenderPipeline(device, &debug_desc);
    wgpuShaderModuleRelease(debug_module);
    wgpuPipelineLayoutRelease(debug_layout);
}

void nv_renderer_init(NvRenderer* renderer, NvGpu* gpu, NvArena* arena)
{
    *renderer = (NvRenderer){0};
    renderer->gpu = gpu;
    renderer->objects = NV_PUSH_ARRAY(arena, NV_MAX_NODES, NvObjectData);
    renderer->object_nodes = NV_PUSH_ARRAY(arena, NV_MAX_NODES, u32);
    renderer->skin_matrices = NV_PUSH_ARRAY(arena, NV_MAX_SKIN_MATRICES, NvMat4);
    renderer->debug_vertices = NV_PUSH_ARRAY(arena, NV_MAX_DEBUG_LINES * 2, NvDebugVertex);
    renderer->ambient[0] = renderer->ambient[1] = renderer->ambient[2] = 0.15f;
    renderer->clear_color[0] = 0.02f;
    renderer->clear_color[1] = 0.02f;
    renderer->clear_color[2] = 0.035f;
    renderer->clear_color[3] = 1.0f;

    create_layouts(renderer);
    create_pipelines(renderer);

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
    renderer->debug_buffer = create_buffer(gpu, WGPUBufferUsage_Vertex, NULL, NV_MAX_DEBUG_LINES * 2 * sizeof(NvDebugVertex));

    WGPUBindGroupEntry frame_entries[3] = {
        {.binding = 0, .buffer = renderer->frame_buffer, .size = sizeof(FrameUniforms)},
        {.binding = 1, .buffer = renderer->object_buffer, .size = NV_MAX_NODES * sizeof(NvObjectData)},
        {.binding = 2, .buffer = renderer->skin_buffer, .size = NV_MAX_SKIN_MATRICES * sizeof(NvMat4)},
    };
    WGPUBindGroupDescriptor frame_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    frame_desc.layout = renderer->frame_layout;
    frame_desc.entryCount = NV_ARRAY_COUNT(frame_entries);
    frame_desc.entries = frame_entries;
    renderer->frame_group = wgpuDeviceCreateBindGroup(gpu->device, &frame_desc);

    // Slot 0 of each table is the default: no mesh, a 1x1 white texture, a white material.
    renderer->mesh_count = 1;
    local_persist const u8 white[4] = {255, 255, 255, 255};
    renderer->texture_count = 0;
    nv_renderer_add_texture(renderer, 1, 1, white, 0, NULL);
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
    return (NvMeshId){renderer->mesh_count++};
}

NvTextureId nv_renderer_add_texture(NvRenderer* renderer, u32 width, u32 height, const u8* rgba,
                                    b32 srgb, NvArena* scratch)
{
    NV_ASSERT(renderer->texture_count < NV_MAX_TEXTURES);
    u32 levels = 1;
    if (scratch) {
        for (u32 size = width > height ? width : height; size > 1; size >>= 1)
            ++levels;
    }

    NvRenderTexture* slot = &renderer->textures[renderer->texture_count];
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
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

// Recreates the depth buffer whenever the canvas size changes.
internal void update_depth_buffer(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    if (renderer->depth_texture && renderer->depth_width == gpu->width && renderer->depth_height == gpu->height)
        return;
    if (renderer->depth_texture) {
        wgpuTextureViewRelease(renderer->depth_view);
        wgpuTextureRelease(renderer->depth_texture);
    }
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    desc.size = (WGPUExtent3D){gpu->width, gpu->height, 1};
    desc.format = WGPUTextureFormat_Depth24Plus;
    renderer->depth_texture = wgpuDeviceCreateTexture(gpu->device, &desc);
    renderer->depth_view = wgpuTextureCreateView(renderer->depth_texture, NULL);
    renderer->depth_width = gpu->width;
    renderer->depth_height = gpu->height;
}

internal NvMat4 camera_view_proj(NvNode* camera_node, f32 aspect)
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
    return nv_mat4_mul(proj, nv_mat4_inverse(camera_node->world));
}

void nv_renderer_draw(NvRenderer* renderer, NvScene* scene, const NvSkin* skins, NvRect viewport,
                      WGPUCommandEncoder encoder, WGPUTextureView target)
{
    NvGpu* gpu = renderer->gpu;
    update_depth_buffer(renderer);

    if (!viewport.width || !viewport.height)
        viewport = (NvRect){0, 0, gpu->width, gpu->height};
    NV_ASSERT(viewport.x + viewport.width <= gpu->width && viewport.y + viewport.height <= gpu->height);

    FrameUniforms uniforms = {0};
    uniforms.view_proj = camera_view_proj(nv_scene_get(scene, scene->active_camera),
                                          (f32)viewport.width / (f32)viewport.height);
    memcpy(uniforms.ambient, renderer->ambient, sizeof(renderer->ambient));

    u32 object_count = 0;
    renderer->skin_matrix_count = 0;
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
        }

        if (!node->mesh.index)
            continue;
        NV_ASSERT(node->mesh.index < renderer->mesh_count && node->material.index < renderer->material_count);
        NvObjectData* object = &renderer->objects[object_count];
        object->model = node->world;
        object->joint_offset = NO_SKIN;
        const NvSkin* skin = (skins && node->animator.index) ? &skins[node->animator.index] : NULL;
        if (renderer->meshes[node->mesh.index].skinned && skin && skin->count) {
            NV_ASSERT(renderer->skin_matrix_count + skin->count <= NV_MAX_SKIN_MATRICES);
            object->joint_offset = renderer->skin_matrix_count;
            memcpy(renderer->skin_matrices + renderer->skin_matrix_count, skin->matrices, skin->count * sizeof(NvMat4));
            renderer->skin_matrix_count += skin->count;
        }
        renderer->object_nodes[object_count] = index;
        ++object_count;
    }

    WGPUQueue queue = gpu->queue;
    wgpuQueueWriteBuffer(queue, renderer->frame_buffer, 0, &uniforms, sizeof(uniforms));
    if (object_count)
        wgpuQueueWriteBuffer(queue, renderer->object_buffer, 0, renderer->objects, object_count * sizeof(NvObjectData));
    if (renderer->skin_matrix_count)
        wgpuQueueWriteBuffer(queue, renderer->skin_buffer, 0, renderer->skin_matrices, renderer->skin_matrix_count * sizeof(NvMat4));
    if (renderer->debug_vertex_count)
        wgpuQueueWriteBuffer(queue, renderer->debug_buffer, 0, renderer->debug_vertices, renderer->debug_vertex_count * sizeof(NvDebugVertex));

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = target;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){renderer->clear_color[0], renderer->clear_color[1], renderer->clear_color[2], renderer->clear_color[3]};
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = renderer->depth_view;
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Discard;
    depth.depthClearValue = 1.0f;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.label = (WGPUStringView){"scene", WGPU_STRLEN};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;
    pass_desc.depthStencilAttachment = &depth;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetViewport(pass, (f32)viewport.x, (f32)viewport.y, (f32)viewport.width,
                                     (f32)viewport.height, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, viewport.x, viewport.y, viewport.width, viewport.height);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, renderer->frame_group, 0, NULL);

    WGPURenderPipeline bound_pipeline = NULL;
    u32 bound_mesh = 0;
    u32 bound_material = 0xFFFFFFFFu;
    for (u32 i = 0; i < object_count; ++i) {
        NvNode* node = &scene->nodes[renderer->object_nodes[i]];
        NvRenderMesh* mesh = &renderer->meshes[node->mesh.index];
        NvRenderMaterial* material = &renderer->materials[node->material.index];

        WGPURenderPipeline pipeline = renderer->pipelines[mesh->skinned ? 1 : 0][material->desc.double_sided ? 1 : 0];
        if (pipeline != bound_pipeline) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            bound_pipeline = pipeline;
        }
        if (node->material.index != bound_material) {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, material->bind_group, 0, NULL);
            bound_material = node->material.index;
        }
        if (node->mesh.index != bound_mesh) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh->vertices, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(pass, mesh->indices, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            bound_mesh = node->mesh.index;
        }
        // The first-instance offset makes instance_index equal the object's slot in `objects`.
        wgpuRenderPassEncoderDrawIndexed(pass, mesh->index_count, 1, 0, 0, i);
    }

    if (renderer->debug_vertex_count) {
        wgpuRenderPassEncoderSetPipeline(pass, renderer->debug_pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, renderer->debug_buffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(pass, renderer->debug_vertex_count, 1, 0, 0);
        renderer->debug_vertex_count = 0;
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}
