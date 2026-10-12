#include "engine/renderer.h"
#include "shaders/bloom_shader.h"
#include "shaders/debug_lines_shader.h"
#include "shaders/mesh_shader.h"
#include "shaders/shadow_shader.h"
#include "shaders/upscale_shader.h"
#include "engine/log.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NV_TIMESTAMP_COUNT 12 // scene (0, 1), shadow (2, 3), upscale (4, 5), bloom (6 to 9), particles (10, 11)

internal void create_upscale_pipeline(NvRenderer* renderer);
internal void create_bloom_pipelines(NvRenderer* renderer);

#define NO_SKIN 0xFFFFFFFFu

// Layouts match the structs of engine/shaders/renderer_frame.slang and mesh.slang (tests/shader_layout_test.mjs).
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

typedef struct NvObjectData {
    NvMat4 model;
    u32 joint_offset; // first matrix in the skin buffer, or NO_SKIN
    u32 pad[3];
} NvObjectData;

typedef struct MaterialUniforms {
    f32 base_color[4];
} MaterialUniforms;

_Static_assert(sizeof(FrameUniforms) == 224, "the shaders read a 224-byte frame");
_Static_assert(sizeof(NvObjectData) == 80, "the shaders read 80-byte objects");
_Static_assert(sizeof(MaterialUniforms) == 16, "the shader reads a 16-byte material");

// The shaders are engine/shaders/{mesh,shadow,debug_lines,upscale,bloom}.slang: the build makes mesh_wgsl and the others from them.

internal NvGpuBuffer create_buffer(NvGpu* gpu, const char* label, u32 usage, const void* data, umm size)
{
    return nv_gpu_create_buffer(gpu, &(NvGpuBufferDesc){.label = label, .usage = usage, .size = size, .data = data});
}

internal void create_layouts(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;

    NvGpuLayoutEntry frame_entries[5] = {
        {.binding = 0, .stages = NV_GPU_STAGE_VERTEX | NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_UNIFORM, .min_size = sizeof(FrameUniforms)},
        {.binding = 1, .stages = NV_GPU_STAGE_VERTEX, .kind = NV_GPU_BINDING_STORAGE_READ, .stride = sizeof(NvObjectData)},
        {.binding = 2, .stages = NV_GPU_STAGE_VERTEX, .kind = NV_GPU_BINDING_STORAGE_READ, .stride = sizeof(NvMat4)},
        {.binding = 3, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_TEXTURE_DEPTH},
        {.binding = 4, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_SAMPLER_COMPARISON},
    };
    renderer->frame_layout = nv_gpu_create_layout(gpu, frame_entries, NV_ARRAY_COUNT(frame_entries));
    // IMPORTANT: The shadow pass writes the map, so its bind group must not also bind it.
    renderer->shadow_frame_layout = nv_gpu_create_layout(gpu, frame_entries, 3);

    NvGpuLayoutEntry material_entries[3] = {
        {.binding = 0, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_UNIFORM, .min_size = sizeof(MaterialUniforms)},
        {.binding = 1, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_SAMPLER},
        {.binding = 2, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_TEXTURE_FLOAT},
    };
    renderer->material_layout = nv_gpu_create_layout(gpu, material_entries, NV_ARRAY_COUNT(material_entries));
}

internal void release_scene_pipelines(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    for (u32 skinned = 0; skinned < 2; ++skinned) {
        for (u32 double_sided = 0; double_sided < 2; ++double_sided) {
            if (renderer->pipelines[skinned][double_sided].index)
                nv_gpu_release_pipeline(gpu, renderer->pipelines[skinned][double_sided]);
            renderer->pipelines[skinned][double_sided] = (NvGpuPipeline){0};
        }
    }
    if (renderer->debug_pipeline.index)
        nv_gpu_release_pipeline(gpu, renderer->debug_pipeline);
    renderer->debug_pipeline = (NvGpuPipeline){0};
}

global const NvGpuVertexAttribute static_attributes[3] = {
    {.location = 0, .format = NV_GPU_VERTEX_FLOAT3, .offset = offsetof(NvVertex, position)},
    {.location = 1, .format = NV_GPU_VERTEX_FLOAT3, .offset = offsetof(NvVertex, normal)},
    {.location = 2, .format = NV_GPU_VERTEX_FLOAT2, .offset = offsetof(NvVertex, uv)},
};
global const NvGpuVertexAttribute skinned_attributes[5] = {
    {.location = 0, .format = NV_GPU_VERTEX_FLOAT3, .offset = offsetof(NvSkinnedVertex, position)},
    {.location = 1, .format = NV_GPU_VERTEX_FLOAT3, .offset = offsetof(NvSkinnedVertex, normal)},
    {.location = 2, .format = NV_GPU_VERTEX_FLOAT2, .offset = offsetof(NvSkinnedVertex, uv)},
    {.location = 3, .format = NV_GPU_VERTEX_UINT16X4, .offset = offsetof(NvSkinnedVertex, joints)},
    {.location = 4, .format = NV_GPU_VERTEX_FLOAT4, .offset = offsetof(NvSkinnedVertex, weights)},
};

// The pipelines the scene pass uses, for `scene_samples` samples per pixel.
internal void create_pipelines(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;

    for (u32 skinned = 0; skinned < 2; ++skinned) {
        for (u32 double_sided = 0; double_sided < 2; ++double_sided) {
            NvGpuRenderPipelineDesc desc = {
                .label = skinned ? "skinned mesh" : "static mesh",
                .shader = &mesh_shader,
                .vertex_entry = skinned ? "vs_skinned" : "vs_static",
                .fragment_entry = "fs_main",
                .layouts = {renderer->frame_layout, renderer->material_layout},
                .layout_count = 2,
                .vertex_stride = skinned ? sizeof(NvSkinnedVertex) : sizeof(NvVertex),
                .attribute_count = skinned ? NV_ARRAY_COUNT(skinned_attributes) : NV_ARRAY_COUNT(static_attributes),
                .topology = NV_GPU_TOPOLOGY_TRIANGLES,
                .cull = double_sided ? NV_GPU_CULL_NONE : NV_GPU_CULL_BACK,
                .color_format = NV_SCENE_FORMAT,
                .depth_format = NV_SCENE_DEPTH_FORMAT,
                .depth_write = 1,
                .depth_compare = NV_GPU_COMPARE_GREATER, // reverse Z: near is 1, far is 0
                .samples = renderer->scene_samples,
            };
            memcpy(desc.attributes, skinned ? skinned_attributes : static_attributes,
                   desc.attribute_count * sizeof(NvGpuVertexAttribute));
            renderer->pipelines[skinned][double_sided] = nv_gpu_create_render_pipeline(gpu, &desc);
        }
    }

    // Debug lines: drawn over everything, so the depth test always passes.
    NvGpuRenderPipelineDesc debug_desc = {
        .label = "debug lines",
        .shader = &debug_lines_shader,
        .vertex_entry = "vs_main",
        .fragment_entry = "fs_main",
        .layouts = {renderer->frame_layout},
        .layout_count = 1,
        .vertex_stride = sizeof(NvDebugVertex),
        .attributes = {
            {.location = 0, .format = NV_GPU_VERTEX_FLOAT3, .offset = offsetof(NvDebugVertex, position)},
            {.location = 1, .format = NV_GPU_VERTEX_FLOAT4, .offset = offsetof(NvDebugVertex, color)},
        },
        .attribute_count = 2,
        .topology = NV_GPU_TOPOLOGY_LINES,
        .color_format = NV_SCENE_FORMAT,
        .blend = NV_GPU_BLEND_ALPHA,
        .depth_format = NV_SCENE_DEPTH_FORMAT,
        .depth_write = 0,
        .depth_compare = NV_GPU_COMPARE_ALWAYS,
        .samples = renderer->scene_samples,
    };
    renderer->debug_pipeline = nv_gpu_create_render_pipeline(gpu, &debug_desc);
}

internal NvGpuFormat shadow_texture_format(NvShadowFormat format)
{
    return format == NV_SHADOW_FORMAT_DEPTH16 ? NV_GPU_FORMAT_DEPTH16 : NV_GPU_FORMAT_DEPTH32F;
}

internal void create_shadow_pipelines(NvRenderer* renderer, NvShadowFormat format)
{
    NvGpu* gpu = renderer->gpu;
    for (u32 skinned = 0; skinned < 2; ++skinned) {
        for (u32 double_sided = 0; double_sided < 2; ++double_sided) {
            if (renderer->shadow_pipelines[skinned][double_sided].index)
                nv_gpu_release_pipeline(gpu, renderer->shadow_pipelines[skinned][double_sided]);
        }
    }
    // Only the position, and for a skinned vertex the joints and weights, are read.
    local_persist const NvGpuVertexAttribute shadow_static_attributes[1] = {
        {.location = 0, .format = NV_GPU_VERTEX_FLOAT3, .offset = offsetof(NvVertex, position)},
    };
    local_persist const NvGpuVertexAttribute shadow_skinned_attributes[3] = {
        {.location = 0, .format = NV_GPU_VERTEX_FLOAT3, .offset = offsetof(NvSkinnedVertex, position)},
        {.location = 3, .format = NV_GPU_VERTEX_UINT16X4, .offset = offsetof(NvSkinnedVertex, joints)},
        {.location = 4, .format = NV_GPU_VERTEX_FLOAT4, .offset = offsetof(NvSkinnedVertex, weights)},
    };
    for (u32 skinned = 0; skinned < 2; ++skinned) {
        for (u32 double_sided = 0; double_sided < 2; ++double_sided) {
            NvGpuRenderPipelineDesc desc = {
                .label = skinned ? "skinned shadow" : "static shadow",
                .shader = &shadow_shader,
                .vertex_entry = skinned ? "vs_skinned" : "vs_static",
                .layouts = {renderer->shadow_frame_layout},
                .layout_count = 1,
                .vertex_stride = skinned ? sizeof(NvSkinnedVertex) : sizeof(NvVertex),
                .attribute_count = skinned ? NV_ARRAY_COUNT(shadow_skinned_attributes) : NV_ARRAY_COUNT(shadow_static_attributes),
                .topology = NV_GPU_TOPOLOGY_TRIANGLES,
                .cull = double_sided ? NV_GPU_CULL_NONE : NV_GPU_CULL_BACK,
                .depth_format = shadow_texture_format(format),
                .depth_write = 1,
                .depth_compare = NV_GPU_COMPARE_LESS,
                .depth_bias = shadow_bias_constant[format],
                .depth_bias_slope = SHADOW_BIAS_SLOPE,
            };
            memcpy(desc.attributes, skinned ? shadow_skinned_attributes : shadow_static_attributes,
                   desc.attribute_count * sizeof(NvGpuVertexAttribute));
            renderer->shadow_pipelines[skinned][double_sided] = nv_gpu_create_render_pipeline(gpu, &desc);
        }
    }
}

// Makes the shadow map (and, for a new format, its pipelines) match the settings, and the frame
// bind group that samples it. Off keeps a 1x1 map bound, since the bind group needs one.
internal void update_shadow_map(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    u32 size = renderer->shadows.size ? renderer->shadows.size : 1;
    NvShadowFormat format = renderer->shadows.format;
    if (renderer->shadow_texture.index && renderer->shadow_size == size && renderer->shadow_format == format)
        return;
    if (!renderer->shadow_texture.index || renderer->shadow_format != format)
        create_shadow_pipelines(renderer, format);
    if (renderer->shadow_texture.index) {
        nv_gpu_release_view(gpu, renderer->shadow_view);
        nv_gpu_release_texture(gpu, renderer->shadow_texture);
        nv_gpu_release_group(gpu, renderer->frame_group);
    }
    renderer->shadow_texture = nv_gpu_create_texture(gpu, &(NvGpuTextureDesc){
        .label = "shadow map",
        .width = size,
        .height = size,
        .format = shadow_texture_format(format),
        .usage = NV_GPU_TEXTURE_RENDER_TARGET | NV_GPU_TEXTURE_SAMPLED,
    });
    renderer->shadow_view = nv_gpu_create_view(gpu, renderer->shadow_texture, NULL);
    renderer->shadow_size = size;
    renderer->shadow_format = format;
    if (renderer->shadows.size)
        nv_log(NV_LOG_INFO, "nv", "shadow map: %ux%u %s", size, size, nv_gpu_format_name(shadow_texture_format(format)));
    else
        nv_log(NV_LOG_INFO, "nv", "shadow map: off (a 1x1 %s placeholder stays bound)", nv_gpu_format_name(shadow_texture_format(format)));

    NvGpuGroupEntry frame_entries[5] = {
        {.binding = 0, .buffer = renderer->frame_buffer, .size = sizeof(FrameUniforms)},
        {.binding = 1, .buffer = renderer->object_buffer, .size = NV_MAX_NODES * sizeof(NvObjectData)},
        {.binding = 2, .buffer = renderer->skin_buffer, .size = NV_MAX_SKIN_MATRICES * sizeof(NvMat4)},
        {.binding = 3, .view = renderer->shadow_view},
        {.binding = 4, .sampler = renderer->shadow_sampler},
    };
    renderer->frame_group = nv_gpu_create_group(gpu, renderer->frame_layout, frame_entries, NV_ARRAY_COUNT(frame_entries));
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

    renderer->sampler = nv_gpu_create_sampler(gpu, &(NvGpuSamplerDesc){.repeat = 1, .mip_linear = 1});

    renderer->frame_buffer = create_buffer(gpu, NULL, NV_GPU_BUFFER_UNIFORM, NULL, sizeof(FrameUniforms));
    renderer->object_buffer = create_buffer(gpu, NULL, NV_GPU_BUFFER_STORAGE, NULL, NV_MAX_NODES * sizeof(NvObjectData));
    renderer->skin_buffer = create_buffer(gpu, NULL, NV_GPU_BUFFER_STORAGE, NULL, NV_MAX_SKIN_MATRICES * sizeof(NvMat4));
    renderer->debug_buffer = create_buffer(gpu, NULL, NV_GPU_BUFFER_VERTEX, NULL, NV_MAX_DEBUG_LINES * 2 * sizeof(NvDebugVertex));

    NvGpuGroupEntry shadow_frame_entries[3] = {
        {.binding = 0, .buffer = renderer->frame_buffer, .size = sizeof(FrameUniforms)},
        {.binding = 1, .buffer = renderer->object_buffer, .size = NV_MAX_NODES * sizeof(NvObjectData)},
        {.binding = 2, .buffer = renderer->skin_buffer, .size = NV_MAX_SKIN_MATRICES * sizeof(NvMat4)},
    };
    renderer->shadow_frame_group = nv_gpu_create_group(gpu, renderer->shadow_frame_layout, shadow_frame_entries,
                                                       NV_ARRAY_COUNT(shadow_frame_entries));

    // 1 where the point is not behind the map's depth.
    renderer->shadow_sampler = nv_gpu_create_sampler(gpu, &(NvGpuSamplerDesc){.compare = 1});
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
    mesh->vertices = create_buffer(renderer->gpu, NULL, NV_GPU_BUFFER_VERTEX, vertices, data->vertex_count * vertex_size);
    mesh->indices = create_buffer(renderer->gpu, NULL, NV_GPU_BUFFER_INDEX, data->indices, data->index_count * sizeof(u32));
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

void nv_renderer_replace_mesh(NvRenderer* renderer, NvMeshId id, const NvMeshData* data)
{
    NV_ASSERT(id.index > 0 && id.index < renderer->mesh_count);
    NV_ASSERT(data->vertices && !data->skinned_vertices);
    NvRenderMesh* mesh = &renderer->meshes[id.index];
    NV_ASSERT(!mesh->skinned);
    // Commands already submitted keep the old buffers alive until they finish; releasing only drops our reference.
    nv_gpu_release_buffer(renderer->gpu, mesh->vertices);
    nv_gpu_release_buffer(renderer->gpu, mesh->indices);
    mesh->vertices = create_buffer(renderer->gpu, NULL, NV_GPU_BUFFER_VERTEX, data->vertices, data->vertex_count * sizeof(NvVertex));
    mesh->indices = create_buffer(renderer->gpu, NULL, NV_GPU_BUFFER_INDEX, data->indices, data->index_count * sizeof(u32));
    mesh->index_count = data->index_count;
    mesh->bounds = nv_box_empty();
    for (u32 v = 0; v < data->vertex_count; ++v) {
        const f32* p = data->vertices[v].position;
        mesh->bounds = nv_box_add_point(mesh->bounds, nv_vec3(p[0], p[1], p[2]));
    }
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
    slot->texture = nv_gpu_create_texture(renderer->gpu, &(NvGpuTextureDesc){
        .label = slot->name,
        .width = width,
        .height = height,
        .mip_count = levels,
        .format = srgb ? NV_GPU_FORMAT_RGBA8_SRGB : NV_GPU_FORMAT_RGBA8,
        .usage = NV_GPU_TEXTURE_SAMPLED | NV_GPU_TEXTURE_COPY_DST,
    });
    slot->view = nv_gpu_create_view(renderer->gpu, slot->texture, NULL);

    // Each mip level is a 2x2 box filter of the previous one.
    umm scratch_mark = scratch ? scratch->used : 0;
    const u8* level_pixels = rgba;
    u32 w = width;
    u32 h = height;
    for (u32 level = 0; level < levels; ++level) {
        nv_gpu_write_texture(renderer->gpu, slot->texture, level, 0, 0, w, h, level_pixels, w * 4);

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
    material->uniform = create_buffer(renderer->gpu, NULL, NV_GPU_BUFFER_UNIFORM, &uniforms, sizeof(uniforms));

    NvGpuGroupEntry entries[3] = {
        {.binding = 0, .buffer = material->uniform, .size = sizeof(MaterialUniforms)},
        {.binding = 1, .sampler = renderer->sampler},
        {.binding = 2, .view = renderer->textures[desc->base_color_texture.index].view},
    };
    material->bind_group = nv_gpu_create_group(renderer->gpu, renderer->material_layout, entries, NV_ARRAY_COUNT(entries));
    return (NvMaterialId){renderer->material_count++};
}

void nv_renderer_set_material_color(NvRenderer* renderer, NvMaterialId id, const f32 base_color[4])
{
    NV_ASSERT(id.index < renderer->material_count);
    NvRenderMaterial* material = &renderer->materials[id.index];
    memcpy(material->desc.base_color, base_color, sizeof(material->desc.base_color));
    MaterialUniforms uniforms = {0};
    memcpy(uniforms.base_color, base_color, sizeof(uniforms.base_color));
    nv_gpu_write_buffer(renderer->gpu, material->uniform, 0, &uniforms, sizeof(uniforms));
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

internal void release_texture(NvGpu* gpu, NvGpuTexture* texture, NvGpuView* view)
{
    if (view->index)
        nv_gpu_release_view(gpu, *view);
    if (texture->index)
        nv_gpu_release_texture(gpu, *texture);
    *view = (NvGpuView){0};
    *texture = (NvGpuTexture){0};
}

// A color target in the scene's HDR format (NV_SCENE_FORMAT, linear): what the scene pass draws, what it
// resolves into and what the upscale pass samples. The canvas still gets the sRGB view format at the
// end, in the upscale pass, so the tone mapped value is encoded by the canvas's own view
// (docs/specs/vfx.md; before HDR the scene targets were made like the canvas, see msaa.md).
internal void create_color_target(NvGpu* gpu, const char* label, u32 width, u32 height, u32 samples, u32 usage,
                                  NvGpuTexture* texture, NvGpuView* view)
{
    *texture = nv_gpu_create_texture(gpu, &(NvGpuTextureDesc){
        .label = label,
        .width = width,
        .height = height,
        .samples = samples,
        .format = NV_SCENE_FORMAT,
        .usage = usage,
    });
    *view = nv_gpu_create_view(gpu, *texture, NULL);
}

internal f64 target_megabytes(u32 width, u32 height, u32 samples)
{
    return (f64)width * (f64)height * (f64)nv_gpu_format_bytes(NV_SCENE_FORMAT) * (f64)samples / (1024.0 * 1024.0);
}

internal void create_upscale_pipeline(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    NvGpuLayoutEntry entries[4] = {
        {.binding = 0, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_UNIFORM},
        {.binding = 1, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_TEXTURE_FLOAT},
        {.binding = 2, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_TEXTURE_FLOAT},
        {.binding = 3, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_SAMPLER},
    };
    renderer->upscale_layout = nv_gpu_create_layout(gpu, entries, NV_ARRAY_COUNT(entries));
    renderer->upscale_pipeline = nv_gpu_create_render_pipeline(gpu, &(NvGpuRenderPipelineDesc){
        .label = "upscale",
        .shader = &upscale_shader,
        .vertex_entry = "vs_main",
        .fragment_entry = "fs_main",
        .layouts = {renderer->upscale_layout},
        .layout_count = 1,
        .topology = NV_GPU_TOPOLOGY_TRIANGLES,
        .color_format = gpu->surface_format,
    });
    renderer->upscale_buffer = create_buffer(gpu, NULL, NV_GPU_BUFFER_UNIFORM, NULL, 64);
}

// The upscale pass's bind group: the scene color, and bloom mip 0 (a 1x1 black texture while there
// is no chain, with the intensity 0 so it is not added).
internal void rebuild_upscale_group(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    if (renderer->upscale_group.index)
        nv_gpu_release_group(gpu, renderer->upscale_group);
    NvGpuGroupEntry entries[4] = {
        {.binding = 0, .buffer = renderer->upscale_buffer, .size = 64},
        {.binding = 1, .view = renderer->scene_color_view},
        {.binding = 2, .view = renderer->bloom_texture.index ? renderer->bloom_views[0] : renderer->bloom_dummy_view},
        {.binding = 3, .sampler = renderer->bloom_sampler},
    };
    renderer->upscale_group = nv_gpu_create_group(gpu, renderer->upscale_layout, entries, NV_ARRAY_COUNT(entries));
}

// Bloom is engine/shaders/bloom.slang.


#define BLOOM_PASSES (2 * NV_BLOOM_LEVELS - 1) // six downsamples and five upsamples
#define BLOOM_UNIFORM_STRIDE 256               // the uniform dynamic offset alignment WebGPU guarantees

internal void create_bloom_pipelines(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    NvGpuLayoutEntry entries[3] = {
        {.binding = 0, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_UNIFORM, .min_size = 16, .dynamic_offset = 1},
        {.binding = 1, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_TEXTURE_FLOAT},
        {.binding = 2, .stages = NV_GPU_STAGE_FRAGMENT, .kind = NV_GPU_BINDING_SAMPLER},
    };
    renderer->bloom_layout = nv_gpu_create_layout(gpu, entries, NV_ARRAY_COUNT(entries));

    const char* fragments[3] = {"fs_down_first", "fs_down", "fs_up"};
    NvGpuPipeline* pipelines[3] = {&renderer->bloom_down_first_pipeline, &renderer->bloom_down_pipeline, &renderer->bloom_up_pipeline};
    const char* labels[3] = {"bloom downsample (first)", "bloom downsample", "bloom upsample"};
    for (u32 i = 0; i < 3; ++i) {
        *pipelines[i] = nv_gpu_create_render_pipeline(gpu, &(NvGpuRenderPipelineDesc){
            .label = labels[i],
            .shader = &bloom_shader,
            .vertex_entry = "vs_main",
            .fragment_entry = fragments[i],
            .layouts = {renderer->bloom_layout},
            .layout_count = 1,
            .topology = NV_GPU_TOPOLOGY_TRIANGLES,
            .color_format = NV_SCENE_FORMAT,
            .blend = i == 2 ? NV_GPU_BLEND_ADD_KEEP_ALPHA : NV_GPU_BLEND_NONE, // the upsample is added into the level it writes
        });
    }

    renderer->bloom_sampler = nv_gpu_create_sampler(gpu, &(NvGpuSamplerDesc){0});
    renderer->bloom_buffer = create_buffer(gpu, NULL, NV_GPU_BUFFER_UNIFORM, NULL, BLOOM_PASSES * BLOOM_UNIFORM_STRIDE);

    renderer->bloom_dummy = nv_gpu_create_texture(gpu, &(NvGpuTextureDesc){
        .label = "bloom placeholder",
        .width = 1,
        .height = 1,
        .format = NV_SCENE_FORMAT,
        .usage = NV_GPU_TEXTURE_SAMPLED,
    });
    renderer->bloom_dummy_view = nv_gpu_create_view(gpu, renderer->bloom_dummy, NULL);
}

internal void release_bloom_chain(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    for (u32 i = 0; i <= NV_BLOOM_LEVELS; ++i) {
        if (renderer->bloom_groups[i].index)
            nv_gpu_release_group(gpu, renderer->bloom_groups[i]);
        renderer->bloom_groups[i] = (NvGpuGroup){0};
    }
    for (u32 i = 0; i < NV_BLOOM_LEVELS; ++i) {
        if (renderer->bloom_views[i].index)
            nv_gpu_release_view(gpu, renderer->bloom_views[i]);
        renderer->bloom_views[i] = (NvGpuView){0};
    }
    if (renderer->bloom_texture.index)
        nv_gpu_release_texture(gpu, renderer->bloom_texture);
    renderer->bloom_texture = (NvGpuTexture){0};
    renderer->bloom_width = renderer->bloom_height = 0;
}

// The pass bind groups by source: the scene color, then each bloom mip. Made again when either changed.
internal void rebuild_bloom_groups(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    if (!renderer->bloom_texture.index)
        return;
    for (u32 i = 0; i <= NV_BLOOM_LEVELS; ++i) {
        if (renderer->bloom_groups[i].index)
            nv_gpu_release_group(gpu, renderer->bloom_groups[i]);
        NvGpuGroupEntry entries[3] = {
            {.binding = 0, .buffer = renderer->bloom_buffer, .size = 16},
            {.binding = 1, .view = i == 0 ? renderer->scene_color_view : renderer->bloom_views[i - 1]},
            {.binding = 2, .sampler = renderer->bloom_sampler},
        };
        renderer->bloom_groups[i] = nv_gpu_create_group(gpu, renderer->bloom_layout, entries, NV_ARRAY_COUNT(entries));
    }
}

// Keeps the bloom chain while bloom is on, at half the size of the scene targets.
internal void update_bloom(NvRenderer* renderer)
{
    NvGpu* gpu = renderer->gpu;
    b32 want = renderer->post.bloom && renderer->post.bloom_intensity > 0.0f;
    if (!want) {
        if (renderer->bloom_texture.index) {
            release_bloom_chain(renderer);
            rebuild_upscale_group(renderer);
            nv_log(NV_LOG_INFO, "nv", "bloom: off");
        }
        return;
    }
    u32 width = renderer->target_width / 2, height = renderer->target_height / 2;
    if (renderer->bloom_texture.index && renderer->bloom_width == width && renderer->bloom_height == height)
        return;
    release_bloom_chain(renderer);
    renderer->bloom_texture = nv_gpu_create_texture(gpu, &(NvGpuTextureDesc){
        .label = "bloom",
        .width = width,
        .height = height,
        .mip_count = NV_BLOOM_LEVELS,
        .format = NV_SCENE_FORMAT,
        .usage = NV_GPU_TEXTURE_RENDER_TARGET | NV_GPU_TEXTURE_SAMPLED,
    });
    for (u32 i = 0; i < NV_BLOOM_LEVELS; ++i)
        renderer->bloom_views[i] = nv_gpu_create_view(gpu, renderer->bloom_texture, &(NvGpuViewDesc){.base_mip = i, .mip_count = 1});
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
internal void record_bloom(NvRenderer* renderer, u32 scene_width, u32 scene_height, b32 timed)
{
    NvGpu* gpu = renderer->gpu;
    u32 region[NV_BLOOM_LEVELS][2];
    for (u32 i = 0; i < NV_BLOOM_LEVELS; ++i)
        bloom_region(scene_width, scene_height, i, region[i]);

    // Pass k writes `dst` from `src` (src 0 is the scene color, src n + 1 is mip n).
    typedef struct BloomPass {
        u32 dst, src;
        NvGpuPipeline pipeline;
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
    nv_gpu_write_buffer(gpu, renderer->bloom_buffer, 0, uniforms, sizeof(uniforms));

    for (u32 k = 0; k < count; ++k) {
        // The first pass starts the timing (slots 6 and 7) and the last ends it (8 and 9); a pass's
        // timestamps must both be given, so the span is from 6 to 9.
        nv_gpu_begin_render_pass(gpu, &(NvGpuRenderPassDesc){
            .label = passes[k].add ? "bloom up" : "bloom down",
            .color = renderer->bloom_views[passes[k].dst],
            .color_load = passes[k].add ? NV_GPU_LOAD_LOAD : NV_GPU_LOAD_CLEAR,
            .color_store = NV_GPU_STORE_STORE,
            .timestamps = timed && (k == 0 || k == count - 1),
            .timestamp_begin = k == 0 ? 6 : 8,
            .timestamp_end = k == 0 ? 7 : 9,
        });
        u32 dst_w = region[passes[k].dst][0], dst_h = region[passes[k].dst][1];
        nv_gpu_set_viewport(gpu, 0.0f, 0.0f, (f32)dst_w, (f32)dst_h);
        nv_gpu_set_scissor(gpu, 0, 0, dst_w, dst_h);
        nv_gpu_set_pipeline(gpu, passes[k].pipeline);
        u32 offset = k * BLOOM_UNIFORM_STRIDE;
        nv_gpu_set_group(gpu, 0, renderer->bloom_groups[passes[k].src], &offset, 1);
        nv_gpu_draw(gpu, 3, 1, 0, 0);
        nv_gpu_end_pass(gpu);
    }
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
    b32 resized = !renderer->scene_color.index || !large_enough || too_large || renderer->msaa_format != NV_SCENE_FORMAT;
    if (resized) {
        renderer->target_width = need_width;
        renderer->target_height = need_height;
        renderer->msaa_format = NV_SCENE_FORMAT;
        release_texture(gpu, &renderer->scene_color, &renderer->scene_color_view);
        create_color_target(gpu, "scene color", need_width, need_height, 1,
                            NV_GPU_TEXTURE_RENDER_TARGET | NV_GPU_TEXTURE_SAMPLED, &renderer->scene_color,
                            &renderer->scene_color_view);
        rebuild_upscale_group(renderer);
        rebuild_bloom_groups(renderer);
        nv_log(NV_LOG_INFO, "nv", "scene color target: %ux%u %s, linear HDR (%.1f MB) for a scene of %ux%u, tone mapped and shown with the nearest filter",
               need_width, need_height, nv_gpu_format_name(NV_SCENE_FORMAT), target_megabytes(need_width, need_height, 1), width, height);
    }

    // The multisampled color target.
    if (samples == 1) {
        release_texture(gpu, &renderer->msaa_color, &renderer->msaa_color_view);
    } else if (!renderer->msaa_color.index || resized) {
        release_texture(gpu, &renderer->msaa_color, &renderer->msaa_color_view);
        create_color_target(gpu, "msaa color target", renderer->target_width, renderer->target_height, samples,
                            NV_GPU_TEXTURE_RENDER_TARGET, &renderer->msaa_color, &renderer->msaa_color_view);
        renderer->msaa_width = renderer->target_width;
        renderer->msaa_height = renderer->target_height;
        nv_log(NV_LOG_INFO, "nv", "msaa color target: %ux%u %s x%u samples (%.1f MB), resolved into the scene color target",
               renderer->target_width, renderer->target_height, nv_gpu_format_name(NV_SCENE_FORMAT), samples,
               target_megabytes(renderer->target_width, renderer->target_height, samples));
    }

    // The depth target.
    if (renderer->depth_texture.index && !resized && renderer->depth_texture_sampled == renderer->depth_sampled &&
        renderer->depth_samples == samples)
        return;
    if (renderer->depth_texture.index) {
        nv_gpu_release_view(gpu, renderer->depth_view);
        nv_gpu_release_texture(gpu, renderer->depth_texture);
    }
    renderer->depth_texture = nv_gpu_create_texture(gpu, &(NvGpuTextureDesc){
        .label = "depth target",
        .width = renderer->target_width,
        .height = renderer->target_height,
        .samples = samples,
        .format = NV_SCENE_DEPTH_FORMAT,
        .usage = NV_GPU_TEXTURE_RENDER_TARGET | (renderer->depth_sampled ? NV_GPU_TEXTURE_SAMPLED : 0),
    });
    renderer->depth_view = nv_gpu_create_view(gpu, renderer->depth_texture, NULL);
    renderer->depth_width = renderer->target_width;
    renderer->depth_height = renderer->target_height;
    renderer->depth_texture_sampled = renderer->depth_sampled;
    renderer->depth_samples = samples;
    nv_log(NV_LOG_INFO, "nv", "depth target: %ux%u %s x%u samples (%.1f MB), reverse Z (cleared to 0, compare Greater)%s",
           renderer->target_width, renderer->target_height, nv_gpu_format_name(NV_SCENE_DEPTH_FORMAT), samples,
           target_megabytes(renderer->target_width, renderer->target_height, samples),
           renderer->depth_sampled ? ", samplable (texture viewer)" : "");
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

NvNodeId nv_renderer_pick(NvRenderer* renderer, NvScene* scene, const NvSkin* skins, NvRay ray)
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

// Reads the pass times that arrived from an earlier frame.
internal void read_timestamps(NvRenderer* renderer)
{
    NvGpuTimestamps ticks = nv_gpu_poll_timestamps(renderer->gpu);
    if (!ticks.ok)
        return;
    const u64* t = ticks.nanoseconds;
    // Timestamps are nanoseconds; a pass the browser could not time reads as 0 or reversed.
    if (t[1] > t[0])
        renderer->gpu_ms = (f64)(t[1] - t[0]) / 1.0e6;
    renderer->gpu_shadow_ms = t[3] > t[2] ? (f64)(t[3] - t[2]) / 1.0e6 : 0.0;
    renderer->gpu_upscale_ms = t[5] > t[4] ? (f64)(t[5] - t[4]) / 1.0e6 : 0.0;
    renderer->gpu_particles_ms = renderer->vfx && t[11] > t[10] ? (f64)(t[11] - t[10]) / 1.0e6 : 0.0;
    renderer->gpu_bloom_ms = renderer->bloom_texture.index && t[9] > t[6] ? (f64)(t[9] - t[6]) / 1.0e6 : 0.0;
}

void nv_renderer_draw(NvRenderer* renderer, NvScene* scene, const NvSkin* skins, NvSceneOutput output, NvGpuView target)
{
    NvGpu* gpu = renderer->gpu;
    read_timestamps(renderer);
    b32 timed = gpu->has_timestamps; // the passes write their times; they are read back when the last request has been taken
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
    uniforms.view_proj = nv_renderer_camera_view_proj(nv_scene_get(scene, scene->active_camera),
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

    nv_gpu_write_buffer(gpu, renderer->frame_buffer, 0, &uniforms, sizeof(uniforms));
    if (object_count)
        nv_gpu_write_buffer(gpu, renderer->object_buffer, 0, renderer->objects, object_count * sizeof(NvObjectData));
    if (renderer->skin_matrix_count)
        nv_gpu_write_buffer(gpu, renderer->skin_buffer, 0, renderer->skin_matrices, renderer->skin_matrix_count * sizeof(NvMat4));
    if (renderer->debug_vertex_count)
        nv_gpu_write_buffer(gpu, renderer->debug_buffer, 0, renderer->debug_vertices, renderer->debug_vertex_count * sizeof(NvDebugVertex));

    // Particles: spawn, simulate, cull. The scene pass draws what they leave in the visible list.
    if (renderer->vfx) {
        NvVec3 camera_right = nv_vec3(camera->world.e[0], camera->world.e[1], camera->world.e[2]);
        NvVec3 camera_up = nv_vec3(camera->world.e[4], camera->world.e[5], camera->world.e[6]);
        NvVfxFrame vfx_frame = {.view_proj = uniforms.view_proj,
                                .camera_right = nv_vec3_normalize(camera_right),
                                .camera_up = nv_vec3_normalize(camera_up),
                                .samples = renderer->scene_samples,
                                .timestamps = timed,
                                .timestamp_begin = 10,
                                .timestamp_end = 11};
        nv_vfx_compute(renderer->vfx, &vfx_frame);
    }

    // Shadow pass: every mesh's depth from the light, into the map the scene pass then samples.
    // The GPU orders the two passes, so the scene pass sees what this one wrote.
    u32 shadow_draws = 0;
    if (shadows) {
        nv_gpu_begin_render_pass(gpu, &(NvGpuRenderPassDesc){
            .label = "shadow",
            .depth = renderer->shadow_view,
            .depth_load = NV_GPU_LOAD_CLEAR,
            .depth_store = NV_GPU_STORE_STORE,
            .clear_depth = 1.0f,
            .timestamps = timed,
            .timestamp_begin = 2,
            .timestamp_end = 3,
        });
        nv_gpu_set_group(gpu, 0, renderer->shadow_frame_group, NULL, 0);
        NvGpuPipeline bound = {0};
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
            NvGpuPipeline pipeline = renderer->shadow_pipelines[mesh->skinned ? 1 : 0][material->desc.double_sided ? 1 : 0];
            if (!nv_gpu_same_pipeline(pipeline, bound)) {
                nv_gpu_set_pipeline(gpu, pipeline);
                bound = pipeline;
            }
            if (node->mesh.index != bound_mesh) {
                nv_gpu_set_vertex_buffer(gpu, mesh->vertices);
                nv_gpu_set_index_buffer(gpu, mesh->indices, 1);
                bound_mesh = node->mesh.index;
            }
            nv_gpu_draw_indexed(gpu, mesh->index_count, 0, 0, i);
            ++shadow_draws;
        }
        nv_gpu_end_pass(gpu);
    }

    NvGpuRenderPassDesc scene_pass = {
        .label = "scene",
        .color_load = NV_GPU_LOAD_CLEAR,
        .clear_color = {renderer->clear_color[0], renderer->clear_color[1], renderer->clear_color[2], renderer->clear_color[3]},
        .depth = renderer->depth_view,
        .depth_load = NV_GPU_LOAD_CLEAR,
        // Nothing reads the depth after the pass, unless the texture viewer shows it.
        .depth_store = renderer->depth_texture_sampled ? NV_GPU_STORE_STORE : NV_GPU_STORE_DISCARD,
        .clear_depth = 0.0f, // reverse Z: far is 0
        .timestamps = timed,
        .timestamp_begin = 0,
        .timestamp_end = 1,
    };
    if (renderer->scene_samples > 1) {
        // Draws into the multisampled target, which is resolved into the scene color target and then
        // not needed.
        scene_pass.color = renderer->msaa_color_view;
        scene_pass.resolve = renderer->scene_color_view;
        scene_pass.color_store = NV_GPU_STORE_DISCARD;
    } else {
        scene_pass.color = renderer->scene_color_view;
        scene_pass.color_store = NV_GPU_STORE_STORE;
    }
    nv_gpu_begin_render_pass(gpu, &scene_pass);
    nv_gpu_set_viewport(gpu, (f32)viewport.x, (f32)viewport.y, (f32)viewport.width, (f32)viewport.height);
    nv_gpu_set_scissor(gpu, viewport.x, viewport.y, viewport.width, viewport.height);
    nv_gpu_set_group(gpu, 0, renderer->frame_group, NULL, 0);

    NvGpuPipeline bound_pipeline = {0};
    u32 bound_mesh = 0;
    u32 bound_material = 0xFFFFFFFFu;
    NvRenderStats* stats = &renderer->stats;
    *stats = (NvRenderStats){.draws = object_count, .skin_matrices = renderer->skin_matrix_count,
                             .debug_lines = renderer->debug_vertex_count / 2, .shadow_draws = shadow_draws};
    for (u32 i = 0; i < object_count; ++i) {
        NvNode* node = &scene->nodes[renderer->object_nodes[i]];
        NvRenderMesh* mesh = &renderer->meshes[node->mesh.index];
        NvRenderMaterial* material = &renderer->materials[node->material.index];

        NvGpuPipeline pipeline = renderer->pipelines[mesh->skinned ? 1 : 0][material->desc.double_sided ? 1 : 0];
        if (!nv_gpu_same_pipeline(pipeline, bound_pipeline)) {
            nv_gpu_set_pipeline(gpu, pipeline);
            bound_pipeline = pipeline;
            ++stats->pipeline_changes;
        }
        if (node->material.index != bound_material) {
            nv_gpu_set_group(gpu, 1, material->bind_group, NULL, 0);
            bound_material = node->material.index;
            ++stats->material_changes;
        }
        if (node->mesh.index != bound_mesh) {
            ++stats->mesh_changes;
            nv_gpu_set_vertex_buffer(gpu, mesh->vertices);
            nv_gpu_set_index_buffer(gpu, mesh->indices, 1);
            bound_mesh = node->mesh.index;
        }
        // The first-instance offset makes instance_index equal the object's slot in `objects`.
        nv_gpu_draw_indexed(gpu, mesh->index_count, 0, 0, i);
        stats->triangles += mesh->index_count / 3;
        stats->skinned_draws += mesh->skinned;
    }

    if (renderer->vfx) {
        nv_vfx_draw_decals(renderer->vfx);
        nv_gpu_set_group(gpu, 0, renderer->frame_group, NULL, 0); // the decals bound their own
    }
    if (renderer->debug_vertex_count) {
        nv_gpu_set_pipeline(gpu, renderer->debug_pipeline);
        nv_gpu_set_vertex_buffer(gpu, renderer->debug_buffer);
        nv_gpu_draw(gpu, renderer->debug_vertex_count, 1, 0, 0);
        renderer->debug_vertex_count = 0;
    }
    if (renderer->vfx)
        nv_vfx_draw(renderer->vfx);

    nv_gpu_end_pass(gpu);

    if (renderer->bloom_texture.index)
        record_bloom(renderer, output.width, output.height, timed);

    // The upscale pass: the scene color target, shown with the nearest filter in the image's
    // rectangle of the canvas. The canvas is cleared to black first, which is the bars around a
    // fixed size; the docks drawn later cover the rest.
    NvPostSettings* post = &renderer->post;
    f32 exposure = post->exposure > 0.0f ? post->exposure : 1.0f;
    f32 tone = (f32)(post->tone < NV_TONE_COUNT ? post->tone : NV_TONE_CLAMP);
    u32 bloom_size[2] = {1, 1};
    if (renderer->bloom_texture.index)
        bloom_region(output.width, output.height, 0, bloom_size);
    f32 bloom_intensity = renderer->bloom_texture.index ? post->bloom_intensity : 0.0f;
    f32 params[16] = {(f32)output.image.x, (f32)output.image.y, output.pixel_width, output.pixel_height, (f32)output.width, (f32)output.height,
                      exposure, tone,
                      bloom_intensity, (f32)bloom_size[0], (f32)bloom_size[1], 0.0f,
                      (f32)(renderer->bloom_texture.index ? renderer->bloom_width : 1), (f32)(renderer->bloom_texture.index ? renderer->bloom_height : 1), 0.0f, 0.0f};
    nv_gpu_write_buffer(gpu, renderer->upscale_buffer, 0, params, sizeof(params));
    nv_gpu_begin_render_pass(gpu, &(NvGpuRenderPassDesc){
        .label = "upscale",
        .color = target,
        .color_load = NV_GPU_LOAD_CLEAR,
        .color_store = NV_GPU_STORE_STORE,
        .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .timestamps = timed,
        .timestamp_begin = 4,
        .timestamp_end = 5,
    });
    if (output.image.width && output.image.height) {
        nv_gpu_set_viewport(gpu, (f32)output.image.x, (f32)output.image.y, (f32)output.image.width, (f32)output.image.height);
        nv_gpu_set_scissor(gpu, output.image.x, output.image.y, output.image.width, output.image.height);
        nv_gpu_set_pipeline(gpu, renderer->upscale_pipeline);
        nv_gpu_set_group(gpu, 0, renderer->upscale_group, NULL, 0);
        nv_gpu_draw(gpu, 3, 1, 0, 0);
    }
    nv_gpu_end_pass(gpu);

    // The frame's end submits the commands and reads the times back; none are asked for while the last ones are still unread.
    if (timed)
        nv_gpu_resolve_timestamps(gpu, NV_TIMESTAMP_COUNT);
}

void nv_renderer_end_frame(NvRenderer* renderer)
{
    if (renderer->vfx)
        nv_vfx_end_frame(renderer->vfx);
}
