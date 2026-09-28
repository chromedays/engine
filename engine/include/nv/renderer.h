#pragma once

#include "nv/gpu.h"
#include "nv/scene.h"

#define NV_MAX_MESHES        256
#define NV_MAX_MATERIALS     256
#define NV_MAX_TEXTURES      256
#define NV_MAX_SKIN_MATRICES 16384 // joint matrices across all skinned draws in one frame
#define NV_MAX_DEBUG_LINES   16384

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
    NvVec3 bounds_min; // local bounding box; the bind pose for skinned meshes
    NvVec3 bounds_max;
} NvRenderMesh;

typedef struct NvRay {
    NvVec3 origin;
    NvVec3 direction; // unit length
} NvRay;

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

// What the last nv_renderer_draw did.
typedef struct NvRenderStats {
    u32 draws;          // mesh draw calls, one per mesh node
    u32 skinned_draws;
    u32 triangles;
    u32 pipeline_changes;
    u32 material_changes; // material bind group changes
    u32 mesh_changes;     // vertex and index buffer changes
    u32 skin_matrices;
    u32 debug_lines;
} NvRenderStats;

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
    // Where each animator's matrices start in this frame's skin buffer, so the meshes of one
    // character share one copy. Valid while skin_frame matches frame_index.
    u32 skin_offsets[NV_MAX_ANIMATORS];
    u32 skin_frame[NV_MAX_ANIMATORS];
    u32 frame_index;
    NvDebugVertex* debug_vertices; // [NV_MAX_DEBUG_LINES * 2]
    u32 debug_vertex_count;

    f32 ambient[3];
    f32 clear_color[4];

    NvRenderStats stats;

    // GPU time of the scene pass, where the device has timestamps. Results arrive a few frames
    // late, through a buffer that is read back while no other readback is in flight.
    WGPUQuerySet timestamp_queries;
    WGPUBuffer timestamp_resolve;
    WGPUBuffer timestamp_readback;
    b32 timestamp_copied;   // this frame's timestamps are being copied to the readback buffer
    b32 timestamp_mapping;  // the readback buffer is being mapped
    f64 gpu_ms;             // latest scene pass time; 0 without timestamps
} NvRenderer;

void nv_renderer_init(NvRenderer* renderer, NvGpu* gpu, NvArena* arena);

// Call after the frame's commands are submitted: starts reading back the scene pass time.
void nv_renderer_end_frame(NvRenderer* renderer);

NvMeshId nv_renderer_add_mesh(NvRenderer* renderer, const NvMeshData* data);

// `rgba` is width * height * 4 bytes. Mipmaps are generated with `scratch`, which is left as it was.
NvTextureId nv_renderer_add_texture(NvRenderer* renderer, u32 width, u32 height, const u8* rgba,
                                    b32 srgb, NvArena* scratch);

NvMaterialId nv_renderer_add_material(NvRenderer* renderer, const NvMaterialDesc* desc);

// Changes a material's base color (linear RGBA); the default material (id 0) included.
void nv_renderer_set_material_color(NvRenderer* renderer, NvMaterialId id, const f32 base_color[4]);

// The world ray through (x, y) of `viewport` (framebuffer pixels of the target, as passed to
// nv_renderer_draw), seen by the scene's active camera.
NvRay nv_renderer_view_ray(NvScene* scene, NvRect viewport, f32 x, f32 y);

// The nearest mesh node whose bounding box (in the node's own space) the ray hits, or a zeroed id.
// `distance`, when not NULL, receives how far along the ray the hit is.
NvNodeId nv_renderer_pick(NvRenderer* renderer, NvScene* scene, NvRay ray, f32* distance);

// Queues a line for this frame, drawn on top of the scene.
void nv_renderer_debug_line(NvRenderer* renderer, NvVec3 a, NvVec3 b, NvVec3 color);

// Records the scene pass. The whole target is cleared and the scene is drawn inside `viewport`,
// whose size also sets the camera's aspect ratio; a zeroed viewport means the whole target.
// A skinned node is posed by `skins[node->animator.index]`; with no animator, or `skins` NULL,
// it is drawn in its bind pose.
void nv_renderer_draw(NvRenderer* renderer, NvScene* scene, const NvSkin* skins, NvRect viewport,
                      WGPUCommandEncoder encoder, WGPUTextureView target);
