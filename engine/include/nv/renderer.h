#pragma once

#include "nv/gpu.h"
#include "nv/scene.h"

#define NV_MAX_MESHES        256
#define NV_MAX_MATERIALS     256
#define NV_MAX_TEXTURES      256
#define NV_MAX_SKIN_MATRICES 16384 // joint matrices across all skinned draws in one frame
#define NV_MAX_DEBUG_LINES   8192

typedef struct NvTextureId { u32 index; } NvTextureId; // 0 = plain white

typedef struct NvVertex {
    f32 position[3];
    f32 normal[3];
    f32 uv[2];
} NvVertex;

typedef struct NvSkinnedVertex {
    f32 position[3];
    f32 normal[3];
    f32 uv[2];
    u16 joints[4];
    f32 weights[4];
} NvSkinnedVertex;

// Mesh data to upload. Exactly one of `vertices` / `skinned_vertices` is set.
typedef struct NvMeshData {
    const NvVertex* vertices;
    const NvSkinnedVertex* skinned_vertices;
    u32 vertex_count;
    const u32* indices;
    u32 index_count;
} NvMeshData;

typedef struct NvMaterialDesc {
    f32 base_color[4];   // linear RGBA, multiplied with the texture
    NvTextureId base_color_texture;
    b32 double_sided;
} NvMaterialDesc;

typedef struct NvRenderMesh {
    WGPUBuffer vertices;
    WGPUBuffer indices;
    u32 index_count;
    b32 skinned;
} NvRenderMesh;

typedef struct NvRenderTexture {
    WGPUTexture texture;
    WGPUTextureView view;
} NvRenderTexture;

typedef struct NvRenderMaterial {
    NvMaterialDesc desc;
    WGPUBuffer uniform;
    WGPUBindGroup bind_group;
} NvRenderMaterial;

typedef struct NvDebugVertex {
    f32 position[3];
    f32 color[4];
} NvDebugVertex;

// Skinning matrices (joint model matrix * inverse bind matrix) for a skinned node this frame.
typedef struct NvSkin {
    const NvMat4* matrices;
    u32 count;
} NvSkin;

// Draws an NvScene: meshes with a base color material, lit by the first directional light, seen
// through the scene's active camera. Tables are indexed by the ids in nv/scene.h; slot 0 of each
// is the default (no mesh, white material, white texture).
typedef struct NvRenderer {
    NvGpu* gpu;

    NvRenderMesh meshes[NV_MAX_MESHES];
    u32 mesh_count;
    NvRenderMaterial materials[NV_MAX_MATERIALS];
    u32 material_count;
    NvRenderTexture textures[NV_MAX_TEXTURES];
    u32 texture_count;

    WGPUBindGroupLayout frame_layout;
    WGPUBindGroupLayout material_layout;
    WGPURenderPipeline pipelines[2][2]; // [skinned][double_sided]
    WGPURenderPipeline debug_pipeline;
    WGPUSampler sampler;

    WGPUBuffer frame_buffer;
    WGPUBuffer object_buffer;
    WGPUBuffer skin_buffer;
    WGPUBuffer debug_buffer;
    WGPUBindGroup frame_group;

    WGPUTexture depth_texture;
    WGPUTextureView depth_view;
    u32 depth_width;
    u32 depth_height;

    // Per-frame CPU staging, from the permanent arena.
    struct NvObjectData* objects; // [NV_MAX_NODES]
    u32* object_nodes;            // [NV_MAX_NODES] node index per object
    NvMat4* skin_matrices;        // [NV_MAX_SKIN_MATRICES]
    u32 skin_matrix_count;
    NvDebugVertex* debug_vertices; // [NV_MAX_DEBUG_LINES * 2]
    u32 debug_vertex_count;

    f32 ambient[3];
    f32 clear_color[4];
} NvRenderer;

void nv_renderer_init(NvRenderer* renderer, NvGpu* gpu, NvArena* arena);

NvMeshId nv_renderer_add_mesh(NvRenderer* renderer, const NvMeshData* data);

// `rgba` is width * height * 4 bytes. Mipmaps are generated with `scratch`, which is left as it was.
NvTextureId nv_renderer_add_texture(NvRenderer* renderer, u32 width, u32 height, const u8* rgba,
                                    b32 srgb, NvArena* scratch);

NvMaterialId nv_renderer_add_material(NvRenderer* renderer, const NvMaterialDesc* desc);

// Queues a line for this frame, drawn on top of the scene.
void nv_renderer_debug_line(NvRenderer* renderer, NvVec3 a, NvVec3 b, NvVec3 color);

// Records the scene pass. The whole target is cleared and the scene is drawn inside `viewport`,
// whose size also sets the camera's aspect ratio; a zeroed viewport means the whole target.
// A skinned node is posed by `skins[node->animator.index]`; with no animator, or `skins` NULL,
// it is drawn in its bind pose.
void nv_renderer_draw(NvRenderer* renderer, NvScene* scene, const NvSkin* skins, NvRect viewport,
                      WGPUCommandEncoder encoder, WGPUTextureView target);
