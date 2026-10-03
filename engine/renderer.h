#pragma once

#include "engine/gpu.h"
#include "engine/mesh.h"
#include "engine/vfx.h"
#include "engine/scene.h"

#define NV_MAX_MESHES        256
#define NV_MAX_MATERIALS     256
#define NV_MAX_TEXTURES      256
#define NV_MAX_SKIN_MATRICES 16384 // joint matrices across all skinned draws in one frame
#define NV_MAX_DEBUG_LINES   16384
#define NV_MAX_JOINT_BOUNDS  4096 // per-joint boxes across all skinned meshes

typedef struct NvTextureId { u32 index; } NvTextureId; // 0 = plain white

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
    NvBox bounds; // local; the bind pose for skinned meshes
    // Skinned meshes: one bind-pose box per joint, around every vertex that joint moves. Carried
    // by the joints' skinning matrices, their union bounds the posed mesh (see
    // nv_renderer_mesh_bounds). They live in NvRenderer.joint_bounds.
    u32 joint_bounds_offset;
    u32 joint_bounds_count;
} NvRenderMesh;

typedef struct NvRay {
    NvVec3 origin;
    NvVec3 direction; // unit length
} NvRay;

#define NV_TEXTURE_NAME_MAX 64

// How a frame's scene reaches the canvas (docs/specs/resolution.md): rendered at `width` x `height`
// pixels of its own, then shown with the nearest-pixel filter in `image`, a rectangle of the
// canvas (framebuffer pixels), `pixel_width` x `pixel_height` screen pixels per scene pixel (equal,
// whole numbers unless a fixed size is fitted to the viewport; less than 1 when it is shrunk, and
// different when it is stretched). The image may cut the last, partial scene pixels off.
typedef struct NvSceneOutput {
    u32 width, height;
    NvRect image;
    f32 pixel_width, pixel_height;
} NvSceneOutput;

// How the scene's resolution follows the viewport (docs/specs/resolution.md): at a whole fraction of the viewport's
// pixels filling it, or at a fixed size shown in it, with black bars or stretched.
typedef enum NvResolutionMode {
    NV_RESOLUTION_SCALE,
    NV_RESOLUTION_FIXED,
} NvResolutionMode;

// How a fixed size is fitted to the viewport.
typedef enum NvFixedFit {
    NV_FIT_WHOLE,    // the largest whole multiple that fits, centered, black bars around
    NV_FIT_VIEWPORT, // the largest scale that fits keeping the aspect ratio (pixels of uneven width)
    NV_FIT_STRETCH,  // stretched to fill the viewport, aspect ratio and all
} NvFixedFit;

typedef struct NvResolution {
    NvResolutionMode mode;
    NvFixedFit fixed_fit;
    u32 divisor;                   // SCALE: 1 to 4
    u32 fixed_width, fixed_height; // FIXED: NV_RESOLUTION_MIN..NV_RESOLUTION_MAX each
} NvResolution;

#define NV_RESOLUTION_MIN 16
#define NV_RESOLUTION_MAX 4096

// The scene's size and where its image goes in `viewport` (framebuffer pixels). A viewport with no area gives a 1 x 1
// scene.
NvSceneOutput nv_renderer_scene_output(const NvResolution* resolution, NvRect viewport);

typedef struct NvRenderTexture {
    WGPUTexture texture;
    WGPUTextureView view;
    char name[NV_TEXTURE_NAME_MAX]; // also the WebGPU label, so browser tools show the same name
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

typedef enum NvShadowFormat {
    NV_SHADOW_FORMAT_DEPTH32F, // depth32float
    NV_SHADOW_FORMAT_DEPTH16,  // depth16unorm: half the memory and bandwidth
} NvShadowFormat;

typedef enum NvShadowFilter {
    NV_SHADOW_FILTER_LOW,  // one hardware 2x2 PCF lookup
    NV_SHADOW_FILTER_HIGH, // 3x3 of them
} NvShadowFilter;

// How the first directional light casts shadows (docs/specs/shadows.md). One shadow map covers the
// camera's view up to `distance`. Zeroed: no shadows.
typedef struct NvShadowSettings {
    u32 size; // shadow map width and height in texels; 0 = no shadows
    NvShadowFormat format;
    NvShadowFilter filter;
    f32 distance; // meters from the camera; shadows fade out over the last tenth
    b32 show_box; // draw the light's box as debug lines
} NvShadowSettings;

#define NV_BLOOM_LEVELS 6

typedef enum NvToneMap {
    NV_TONE_CLAMP,       // cut at 1: what the renderer showed before HDR
    NV_TONE_PBR_NEUTRAL, // Khronos PBR Neutral: nearly unchanged below 0.76, bright colors compressed, hue kept
    NV_TONE_ACES,        // Narkowicz's fit of the ACES filmic curve
    NV_TONE_COUNT,
} NvToneMap;

// Set by the app each frame, like the shadow settings.
typedef struct NvPostSettings {
    NvToneMap tone;
    f32 exposure;        // multiplies the scene color before tone mapping
    b32 bloom;
    f32 bloom_intensity; // how much of the bloom is added back
} NvPostSettings;

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
    u32 shadow_draws; // draws in the shadow pass
} NvRenderStats;

// Draws an NvScene: meshes with a base color material, lit by the first directional light, seen
// through the scene's active camera. Tables are indexed by the ids in engine/scene.h; slot 0 of each
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
    // Set by the app: make the depth target samplable too (the texture viewer shows it). Off, it is
    // a render attachment only, which some GPUs keep compressed better (docs/specs/textures.md).
    b32 depth_sampled;
    b32 depth_texture_sampled; // what the existing depth target was made with
    u32 depth_samples;         // its sample count (scene_samples when it was made)

    // The scene's own targets (docs/specs/resolution.md): the scene pass renders at the resolution
    // nv_renderer_draw is given, into the top-left part of targets allocated at target_width x
    // target_height (the resolution rounded up to 64 and kept while it is large enough, so a window
    // resize does not remake them every frame). `scene_color` is what the upscale pass samples.
    u32 target_width, target_height;
    u32 scene_width, scene_height;    // the resolution of the last frame
    WGPUTexture scene_color;
    WGPUTextureView scene_color_view; // NV_SCENE_FORMAT, linear: rendered to and sampled through it
    WGPURenderPipeline upscale_pipeline;
    WGPUBuffer upscale_buffer;
    WGPUBindGroup upscale_group;      // for the current scene_color_view

    // Anti-aliasing (docs/specs/msaa.md). The app sets `msaa`: 1 is off, 4 is 4x MSAA (the only
    // counts WebGPU guarantees). nv_renderer_draw remakes the scene pipelines and the targets when it
    // changed: the scene pass then draws into `msaa_color` and the canvas view it is given is the
    // resolve target.
    u32 msaa;
    u32 scene_samples; // what the scene pipelines and the depth target have
    WGPUTexture msaa_color;
    WGPUTextureView msaa_color_view;
    u32 msaa_width, msaa_height;
    WGPUTextureFormat msaa_format;

    // Tone mapping and bloom, applied by the upscale pass.
    NvPostSettings post;

    // Particles (docs/specs/vfx.md), set by the app: the renderer records their compute passes before the
    // shadow pass and draws them at the end of the scene pass. NULL draws none.
    NvVfx* vfx;

    // Bloom (docs/specs/vfx.md): a chain of NV_BLOOM_LEVELS mips, each half the last, starting at half the
    // scene targets' size. The scene color is downsampled into it (13 taps, a Karis average on the first
    // step), then each level is added back into the one above with a tent filter; the upscale pass
    // adds mip 0 to the scene. Made while `post.bloom` is on.
    WGPUTexture bloom_texture;
    WGPUTextureView bloom_views[NV_BLOOM_LEVELS]; // one mip level each
    u32 bloom_width, bloom_height;                // of mip 0
    WGPURenderPipeline bloom_down_first_pipeline, bloom_down_pipeline, bloom_up_pipeline;
    WGPUBindGroupLayout bloom_layout;
    WGPUBindGroup bloom_groups[NV_BLOOM_LEVELS + 1]; // by source: the scene color, then each mip
    WGPUBuffer bloom_buffer;                         // one uniform block per pass, read with dynamic offsets
    WGPUSampler bloom_sampler;                       // linear, clamped
    WGPUTexture bloom_dummy;                         // 1x1 black, bound while there is no chain
    WGPUTextureView bloom_dummy_view;
    b32 bloom_recorded;                              // this frame's bloom passes wrote timestamps
    b32 particles_recorded;                          // ... and the particle passes

    // Shadows. The app sets `shadows`; nv_renderer_draw remakes the map and the pipelines when the
    // size or the format changed. While shadows are off, a 1x1 map stays bound.
    NvShadowSettings shadows;
    u32 shadow_size;               // of the map that exists
    NvShadowFormat shadow_format;  // of the map and shadow_pipelines
    WGPUTexture shadow_texture;
    WGPUTextureView shadow_view;
    WGPUSampler shadow_sampler;    // comparison, linear: hardware 2x2 PCF
    WGPUBindGroupLayout shadow_frame_layout; // frame_layout without the map, for the shadow pass
    WGPUBindGroup shadow_frame_group;
    WGPURenderPipeline shadow_pipelines[2][2]; // [skinned][double_sided], depth only
    NvMat4 light_view_proj;        // the last frame's, for the light box

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
    NvBox* joint_bounds;           // [NV_MAX_JOINT_BOUNDS]
    u32 joint_bounds_used;
    u32 debug_vertex_count;

    f32 ambient[3];
    f32 clear_color[4];

    NvRenderStats stats;

    // GPU time of the scene and shadow passes, where the device has timestamps. Results arrive a few frames
    // late, through a buffer that is read back while no other readback is in flight.
    WGPUQuerySet timestamp_queries;
    WGPUBuffer timestamp_resolve;
    WGPUBuffer timestamp_readback;
    b32 timestamp_copied;   // this frame's timestamps are being copied to the readback buffer
    b32 timestamp_mapping;  // the readback buffer is being mapped
    f64 gpu_ms;             // latest scene pass time; 0 without timestamps
    f64 gpu_shadow_ms;      // latest shadow pass time; 0 without timestamps or shadows
    f64 gpu_upscale_ms;     // latest upscale pass time; 0 without timestamps
    f64 gpu_bloom_ms;       // latest time of all the bloom passes; 0 without timestamps or bloom
    f64 gpu_particles_ms;   // latest time of the particle compute passes; 0 without timestamps or particles
} NvRenderer;

void nv_renderer_init(NvRenderer* renderer, NvGpu* gpu, NvArena* arena);

// Call after the frame's commands are submitted: starts reading back the scene pass time.
void nv_renderer_end_frame(NvRenderer* renderer);

NvMeshId nv_renderer_add_mesh(NvRenderer* renderer, const NvMeshData* data);

// `rgba` is width * height * 4 bytes. Mipmaps are generated with `scratch`, which is left as it was.
// `name` (NULL = "texture N") is shown by the texture viewer and given to WebGPU as the label.
NvTextureId nv_renderer_add_texture(NvRenderer* renderer, const char* name, u32 width, u32 height, const u8* rgba,
                                    b32 srgb, NvArena* scratch);

NvMaterialId nv_renderer_add_material(NvRenderer* renderer, const NvMaterialDesc* desc);

// Changes a material's base color (linear RGBA); the default material (id 0) included.
void nv_renderer_set_material_color(NvRenderer* renderer, NvMaterialId id, const f32 base_color[4]);

// The scene's active camera as nv_renderer_draw sees it at the scene's resolution: world to view,
// and view to clip space (depth 0 to 1).
void nv_renderer_camera_matrices(NvScene* scene, NvSceneOutput output, NvMat4* view, NvMat4* projection);

// The world ray through (x, y) of the canvas (framebuffer pixels), seen by the scene's active
// camera as drawn with `output`.
NvRay nv_renderer_view_ray(NvScene* scene, NvSceneOutput output, f32 x, f32 y);

// A ray through a tap at (tap_x, tap_y) CSS pixels, `pixel_ratio` canvas pixels each. None (zeroed) when the tap is outside
// the image (on a black bar around a fixed size).
typedef struct NvTapRay {
    b32 ok;
    NvRay ray;
} NvTapRay;
NvTapRay nv_renderer_tap_ray(NvScene* scene, NvSceneOutput output, f32 tap_x, f32 tap_y, f32 pixel_ratio);

// The view-projection matrix the scene pass draws with: the camera node's projection, reverse Z (docs/specs/vfx.md), and its
// inverse world matrix.
NvMat4 nv_renderer_camera_view_proj(NvNode* camera_node, f32 aspect);

// A mesh's box in its node's space. A skinned mesh posed by `skin` gets the box of its current
// pose (the union of its joint boxes moved by their skinning matrices); without a skin, the bind
// pose box.
NvBox nv_renderer_mesh_bounds(NvRenderer* renderer, NvMeshId mesh, const NvSkin* skin);

// The nearest mesh node whose box (nv_renderer_mesh_bounds, in the node's own space) the ray hits,
// or a zeroed id. Skinned nodes are posed by `skins` as in nv_renderer_draw.
NvNodeId nv_renderer_pick(NvRenderer* renderer, NvScene* scene, const NvSkin* skins, NvRay ray);

// Queues a line for this frame, drawn on top of the scene.
void nv_renderer_debug_line(NvRenderer* renderer, NvVec3 a, NvVec3 b, NvVec3 color);

// Records the scene pass and the upscale pass. The scene is drawn at `output.width` x
// `output.height` pixels (which set the camera's aspect ratio) into the renderer's own targets,
// then shown in `output.image` of `target`, the canvas; the rest of the canvas is cleared to black.
// A zeroed output means the whole target at its own resolution.
// A skinned node is posed by `skins[node->animator.index]`; with no animator, or `skins` NULL,
// it is drawn in its bind pose.
void nv_renderer_draw(NvRenderer* renderer, NvScene* scene, const NvSkin* skins, NvSceneOutput output,
                      WGPUCommandEncoder encoder, WGPUTextureView target);
