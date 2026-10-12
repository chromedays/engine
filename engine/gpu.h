#pragma once

// The engine's GPU API (docs/specs/d3d12_port.md, "GPU API"): the part of WebGPU's model that nv uses, and nothing from any
// backend's headers. The renderer, the effects and the UI draw through it; gpu_webgpu.c implements it on the web. Everything the
// backend keeps lives in the backend's file; an object is a handle, an index and a generation, and a handle that was released
// or never made asserts when used.

#include "engine/base.h"
#include "engine/window.h"

typedef struct NvGpuBuffer   { u32 index, generation; } NvGpuBuffer;
typedef struct NvGpuTexture  { u32 index, generation; } NvGpuTexture;
typedef struct NvGpuView     { u32 index, generation; } NvGpuView;     // a texture's mips, or its depth aspect
typedef struct NvGpuSampler  { u32 index, generation; } NvGpuSampler;
typedef struct NvGpuLayout   { u32 index, generation; } NvGpuLayout;   // a bind group's layout
typedef struct NvGpuGroup    { u32 index, generation; } NvGpuGroup;    // what a bind group binds; it never changes once made
typedef struct NvGpuPipeline { u32 index, generation; } NvGpuPipeline; // a render or a compute pipeline

// The texture formats nv uses. Zero is "none". The backend turns them into its API's own formats.
typedef enum NvGpuFormat {
    NV_GPU_FORMAT_NONE,
    NV_GPU_FORMAT_BGRA8,
    NV_GPU_FORMAT_BGRA8_SRGB,
    NV_GPU_FORMAT_RGBA8,
    NV_GPU_FORMAT_RGBA8_SRGB,
    NV_GPU_FORMAT_RGBA16F,
    NV_GPU_FORMAT_DEPTH32F,
    NV_GPU_FORMAT_DEPTH16,
} NvGpuFormat;

// The scene is rendered in linear HDR (docs/specs/vfx.md): colors above 1 are kept until the upscale
// pass multiplies by the exposure, tone maps and writes the canvas. The scene pass has a depth target
// of NV_SCENE_DEPTH_FORMAT, reverse Z.
#define NV_SCENE_FORMAT NV_GPU_FORMAT_RGBA16F
#define NV_SCENE_DEPTH_FORMAT NV_GPU_FORMAT_DEPTH32F

// Slots for pass times (nv_gpu_resolve_timestamps), and for buffers read back (nv_gpu_copy_for_readback).
#define NV_GPU_MAX_TIMESTAMPS 12
#define NV_GPU_MAX_READBACKS 8

// A pipeline has at most this many bind group layouts and vertex attributes, and a bind group at most this many entries.
#define NV_GPU_MAX_LAYOUTS 4
#define NV_GPU_MAX_ATTRIBUTES 8
#define NV_GPU_MAX_ENTRIES 12

// A zeroed NvGpu is the valid "not created" state. The backend's own state is in its file; there is one device per process.
typedef struct NvGpu {
    NvWindow* window;
    NvGpuFormat surface_format; // format of the per-frame view; render pipelines target this
    NvGpuFormat config_format;  // format the canvas itself is configured with
    u32 width;
    u32 height;
    b32 has_timestamps; // the device can time passes
    u64 max_buffer_bytes;          // 0 when the device does not say
    u64 max_storage_binding_bytes; // 0 when the device does not say
    const char* backend_name;      // "WebGPU"
} NvGpu;

// A texture format's name for logs ("BGRA8Unorm").
const char* nv_gpu_format_name(NvGpuFormat format);

// Bytes per texel of the formats nv uses; 0 for none.
static inline u32 nv_gpu_format_bytes(NvGpuFormat format)
{
    switch (format) {
    case NV_GPU_FORMAT_BGRA8:
    case NV_GPU_FORMAT_BGRA8_SRGB:
    case NV_GPU_FORMAT_RGBA8:
    case NV_GPU_FORMAT_RGBA8_SRGB:
    case NV_GPU_FORMAT_DEPTH32F: return 4;
    case NV_GPU_FORMAT_RGBA16F:  return 8;
    case NV_GPU_FORMAT_DEPTH16:  return 2;
    default:                     return 0;
    }
}

// Bytes a texture takes: every mip level's texels (each level half the last, at least 1x1).
static inline u64 nv_gpu_texture_bytes(u32 width, u32 height, u32 mip_count, NvGpuFormat format)
{
    u64 total = 0;
    for (u32 level = 0; level < mip_count; ++level) {
        total += (u64)width * height * nv_gpu_format_bytes(format);
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return total;
}

// NOTE: Fails when there is no usable device (on the web: no WebGPU adapter). Logs (info) the CPU, GPU, display and swapchain
// it found; the swapchain again whenever the canvas is resized.
b32 nv_gpu_create(NvGpu* gpu, NvWindow* window);

// A shader as the build made it (nv_add_shader): the module's code, and the entry points are found by name. The fields are the
// backend's: the web backend reads WGSL text.
typedef struct NvGpuShaderCode {
    const char* wgsl;
} NvGpuShaderCode;

//
// Frames
//

// NOTE: Acquires this frame's canvas view and opens the frame's command list. Fails when the frame should be skipped.
typedef struct NvGpuFrame {
    b32 ok;
    NvGpuView view;
} NvGpuFrame;
NvGpuFrame nv_gpu_begin_frame(NvGpu* gpu);

// Submits the frame's commands, starts reading back what the frame asked to (timestamps, buffers), and gives the view up.
void nv_gpu_end_frame(NvGpu* gpu);

//
// Buffers
//

typedef enum NvGpuBufferUsage {
    NV_GPU_BUFFER_VERTEX   = 1 << 0,
    NV_GPU_BUFFER_INDEX    = 1 << 1,
    NV_GPU_BUFFER_UNIFORM  = 1 << 2,
    NV_GPU_BUFFER_STORAGE  = 1 << 3,
    NV_GPU_BUFFER_INDIRECT = 1 << 4,
    NV_GPU_BUFFER_COPY_SRC = 1 << 5, // read back by nv_gpu_copy_for_readback
} NvGpuBufferUsage;

typedef struct NvGpuBufferDesc {
    const char* label;
    u32 usage; // NvGpuBufferUsage bits; the contents can always be written
    u64 size;  // bytes; rounded up to a multiple of 4
    const void* data; // NULL, or `size` bytes written as the contents
} NvGpuBufferDesc;

NvGpuBuffer nv_gpu_create_buffer(NvGpu* gpu, const NvGpuBufferDesc* desc);
void nv_gpu_release_buffer(NvGpu* gpu, NvGpuBuffer buffer);

// Writes `size` bytes (a multiple of 4) at `offset`. Takes effect before every command of the next submit, as a queue write
// does in WebGPU, at any time: outside a frame, and between the passes of one. So a buffer written twice in a frame holds the
// second write for all of the frame's commands.
void nv_gpu_write_buffer(NvGpu* gpu, NvGpuBuffer buffer, u64 offset, const void* bytes, umm size);

//
// Textures
//

typedef enum NvGpuTextureUsage {
    NV_GPU_TEXTURE_SAMPLED       = 1 << 0,
    NV_GPU_TEXTURE_RENDER_TARGET = 1 << 1, // color or depth
    NV_GPU_TEXTURE_COPY_DST      = 1 << 2, // written by nv_gpu_write_texture
} NvGpuTextureUsage;

typedef struct NvGpuTextureDesc {
    const char* label;
    u32 width, height;
    u32 mip_count; // 0 is 1
    u32 samples;   // 0 is 1; 1 or 4
    NvGpuFormat format;
    u32 usage; // NvGpuTextureUsage bits
} NvGpuTextureDesc;

NvGpuTexture nv_gpu_create_texture(NvGpu* gpu, const NvGpuTextureDesc* desc);
void nv_gpu_release_texture(NvGpu* gpu, NvGpuTexture texture);

// What a texture was made with (label empty; mip_count and samples are never 0).
NvGpuTextureDesc nv_gpu_texture_desc(const NvGpu* gpu, NvGpuTexture texture);

// Writes the `width` x `height` texels at (x, y) of one mip; `bytes_per_row` apart in `texels`. Takes effect like
// nv_gpu_write_buffer.
void nv_gpu_write_texture(NvGpu* gpu, NvGpuTexture texture, u32 mip, u32 x, u32 y, u32 width, u32 height, const void* texels,
                          u32 bytes_per_row);

// A view of a texture. Zero is every mip, as the texture's format.
typedef struct NvGpuViewDesc {
    u32 base_mip;
    u32 mip_count;   // 0 is every mip from base_mip
    b32 depth_only;  // the depth aspect of a depth texture
} NvGpuViewDesc;

NvGpuView nv_gpu_create_view(NvGpu* gpu, NvGpuTexture texture, const NvGpuViewDesc* desc);
void nv_gpu_release_view(NvGpu* gpu, NvGpuView view);

typedef struct NvGpuSamplerDesc {
    b32 repeat;     // repeat the texture past its edge; off clamps to the edge
    b32 mip_linear; // blend the mip levels; off takes the nearest
    b32 compare;    // compare the texture's depth with a reference (less or equal), as shadows do
} NvGpuSamplerDesc; // magnification and minification are always linear

NvGpuSampler nv_gpu_create_sampler(NvGpu* gpu, const NvGpuSamplerDesc* desc);
void nv_gpu_release_sampler(NvGpu* gpu, NvGpuSampler sampler);

//
// Bind groups and pipelines
//

typedef enum NvGpuStage {
    NV_GPU_STAGE_VERTEX   = 1 << 0,
    NV_GPU_STAGE_FRAGMENT = 1 << 1,
    NV_GPU_STAGE_COMPUTE  = 1 << 2,
} NvGpuStage;

typedef enum NvGpuBindingKind {
    NV_GPU_BINDING_UNIFORM,
    NV_GPU_BINDING_STORAGE_READ,
    NV_GPU_BINDING_STORAGE_READ_WRITE, // atomics included
    NV_GPU_BINDING_TEXTURE_FLOAT,
    NV_GPU_BINDING_TEXTURE_DEPTH,
    NV_GPU_BINDING_TEXTURE_DEPTH_MS, // a multisampled depth texture
    NV_GPU_BINDING_SAMPLER,          // filtering
    NV_GPU_BINDING_SAMPLER_COMPARISON,
} NvGpuBindingKind;

typedef struct NvGpuLayoutEntry {
    u32 binding;
    u32 stages; // NvGpuStage bits
    NvGpuBindingKind kind;
    u32 min_size;         // UNIFORM: the least bytes the bound range holds; 0 asks nothing
    b32 dynamic_offset;   // UNIFORM: the offset is given with the group (nv_gpu_set_group)
    u32 stride;           // STORAGE_*: bytes of one element, for backends that need it; the web backend ignores it
} NvGpuLayoutEntry;

NvGpuLayout nv_gpu_create_layout(NvGpu* gpu, const NvGpuLayoutEntry* entries, u32 count);
void nv_gpu_release_layout(NvGpu* gpu, NvGpuLayout layout);

// What one binding gets: a buffer's first `size` bytes, a texture view or a sampler, by the layout's kind.
typedef struct NvGpuGroupEntry {
    u32 binding;
    NvGpuBuffer buffer;
    u64 size;
    NvGpuView view;
    NvGpuSampler sampler;
} NvGpuGroupEntry;

NvGpuGroup nv_gpu_create_group(NvGpu* gpu, NvGpuLayout layout, const NvGpuGroupEntry* entries, u32 count);
void nv_gpu_release_group(NvGpu* gpu, NvGpuGroup group);

typedef enum NvGpuVertexFormat {
    NV_GPU_VERTEX_FLOAT2,
    NV_GPU_VERTEX_FLOAT3,
    NV_GPU_VERTEX_FLOAT4,
    NV_GPU_VERTEX_UINT16X4,
    NV_GPU_VERTEX_UNORM8X4,
} NvGpuVertexFormat;

typedef struct NvGpuVertexAttribute {
    u32 location; // the TEXCOORD<n> number of the shader's input
    NvGpuVertexFormat format;
    u32 offset;
} NvGpuVertexAttribute;

typedef enum NvGpuCull { NV_GPU_CULL_NONE, NV_GPU_CULL_BACK } NvGpuCull; // front faces are counter-clockwise
typedef enum NvGpuTopology { NV_GPU_TOPOLOGY_TRIANGLES, NV_GPU_TOPOLOGY_LINES } NvGpuTopology;
typedef enum NvGpuCompare { NV_GPU_COMPARE_ALWAYS, NV_GPU_COMPARE_LESS, NV_GPU_COMPARE_GREATER } NvGpuCompare;

typedef enum NvGpuBlend {
    NV_GPU_BLEND_NONE,
    NV_GPU_BLEND_ALPHA,           // source alpha over; the alpha channel blends the same way
    NV_GPU_BLEND_ADD,             // color and alpha added
    NV_GPU_BLEND_ADD_KEEP_ALPHA,  // color added, the target's alpha kept (bloom's upsample)
} NvGpuBlend;

// One vertex buffer, in slot 0, stepped per vertex. No color target (shadows) is color_format NONE; no depth is depth_format NONE.
typedef struct NvGpuRenderPipelineDesc {
    const char* label;
    const NvGpuShaderCode* shader;
    const char* vertex_entry;
    const char* fragment_entry; // NULL: no fragment stage
    NvGpuLayout layouts[NV_GPU_MAX_LAYOUTS];
    u32 layout_count;
    u32 vertex_stride; // 0: the vertices come from the shader (a fullscreen triangle)
    NvGpuVertexAttribute attributes[NV_GPU_MAX_ATTRIBUTES];
    u32 attribute_count;
    NvGpuTopology topology;
    NvGpuCull cull;
    NvGpuFormat color_format;
    NvGpuBlend blend;
    NvGpuFormat depth_format;
    b32 depth_write;
    NvGpuCompare depth_compare;
    s32 depth_bias;         // in the depth format's smallest steps
    f32 depth_bias_slope;
    u32 samples; // 0 is 1
} NvGpuRenderPipelineDesc;

typedef struct NvGpuComputePipelineDesc {
    const char* label;
    const NvGpuShaderCode* shader;
    const char* entry;
    NvGpuLayout layouts[NV_GPU_MAX_LAYOUTS];
    u32 layout_count;
} NvGpuComputePipelineDesc;

NvGpuPipeline nv_gpu_create_render_pipeline(NvGpu* gpu, const NvGpuRenderPipelineDesc* desc);
NvGpuPipeline nv_gpu_create_compute_pipeline(NvGpu* gpu, const NvGpuComputePipelineDesc* desc);
void nv_gpu_release_pipeline(NvGpu* gpu, NvGpuPipeline pipeline);

// Whether two handles name the same pipeline (to skip setting the one that is set).
static inline b32 nv_gpu_same_pipeline(NvGpuPipeline a, NvGpuPipeline b)
{
    return a.index == b.index && a.generation == b.generation;
}

//
// Passes
//

typedef enum NvGpuLoad { NV_GPU_LOAD_CLEAR, NV_GPU_LOAD_LOAD } NvGpuLoad;
typedef enum NvGpuStore { NV_GPU_STORE_STORE, NV_GPU_STORE_DISCARD } NvGpuStore;

// A render pass has at most one color attachment (none for the shadow pass) and a depth attachment (none for most). `resolve`
// is the single-sample view an MSAA color is resolved into. Times: `timestamps` writes the begin of the pass to slot
// `timestamp_begin` and the end to `timestamp_end`, of NV_GPU_MAX_TIMESTAMPS; a span over several passes is the first's begin
// and the last's end. Without a device that times passes it does nothing.
typedef struct NvGpuRenderPassDesc {
    const char* label;
    NvGpuView color;
    NvGpuView resolve;
    NvGpuLoad color_load;
    NvGpuStore color_store;
    f32 clear_color[4];
    NvGpuView depth;
    NvGpuLoad depth_load;
    NvGpuStore depth_store;
    f32 clear_depth;
    b32 timestamps;
    u32 timestamp_begin, timestamp_end;
} NvGpuRenderPassDesc;

typedef struct NvGpuComputePassDesc {
    const char* label;
    b32 timestamps;
    u32 timestamp_begin, timestamp_end;
} NvGpuComputePassDesc;

// One pass is open at a time, in the frame (outside a pass, only writes and readback requests).
void nv_gpu_begin_render_pass(NvGpu* gpu, const NvGpuRenderPassDesc* desc);
void nv_gpu_begin_compute_pass(NvGpu* gpu, const NvGpuComputePassDesc* desc);
void nv_gpu_end_pass(NvGpu* gpu);

// Calls inside a pass.
void nv_gpu_set_pipeline(NvGpu* gpu, NvGpuPipeline pipeline);
// Group `index` of the pipeline's layouts; `dynamic_offsets` has one offset (bytes) for each dynamic entry, in binding order.
void nv_gpu_set_group(NvGpu* gpu, u32 index, NvGpuGroup group, const u32* dynamic_offsets, u32 dynamic_count);
void nv_gpu_set_vertex_buffer(NvGpu* gpu, NvGpuBuffer buffer); // slot 0, the whole buffer
void nv_gpu_set_index_buffer(NvGpu* gpu, NvGpuBuffer buffer, b32 is_32_bit);
void nv_gpu_set_viewport(NvGpu* gpu, f32 x, f32 y, f32 width, f32 height); // depth 0 to 1
void nv_gpu_set_scissor(NvGpu* gpu, u32 x, u32 y, u32 width, u32 height);
void nv_gpu_draw(NvGpu* gpu, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance);
// One instance; the shader reads `first_instance` as the object number (instance_index).
void nv_gpu_draw_indexed(NvGpu* gpu, u32 index_count, u32 first_index, s32 base_vertex, u32 first_instance);
// The arguments (vertex count, instance count, first vertex, first instance; 16 bytes) are read from `buffer` at `offset`.
void nv_gpu_draw_indirect(NvGpu* gpu, NvGpuBuffer buffer, u64 offset);
void nv_gpu_dispatch(NvGpu* gpu, u32 x, u32 y, u32 z);
void nv_gpu_dispatch_indirect(NvGpu* gpu, NvGpuBuffer buffer, u64 offset); // three u32 workgroup counts

//
// Reading back
//

// Pass times, in nanoseconds, of the slots a frame's passes wrote. They arrive a few frames late: nv_gpu_resolve_timestamps
// asks for the first `count` slots of this frame, and returns false, asking for nothing, while an earlier request is still
// being read. nv_gpu_poll_timestamps returns the earlier request's result once (ok), when it is ready.
typedef struct NvGpuTimestamps {
    b32 ok;
    u64 nanoseconds[NV_GPU_MAX_TIMESTAMPS];
} NvGpuTimestamps;
b32 nv_gpu_resolve_timestamps(NvGpu* gpu, u32 count);
NvGpuTimestamps nv_gpu_poll_timestamps(NvGpu* gpu);

// Copies `size` bytes of `source` at `offset` for the CPU to read, after the frame's commands so far (outside a frame, at
// once). Returns false, copying nothing, while the slot's earlier request has not been polled. nv_gpu_poll_readback returns
// the copy once (ok), when it is ready; `bytes` stays valid until the slot's next request.
typedef struct NvGpuReadback {
    b32 ok;
    const void* bytes;
    umm size;
} NvGpuReadback;
b32 nv_gpu_copy_for_readback(NvGpu* gpu, u32 slot, NvGpuBuffer source, u64 offset, umm size);
NvGpuReadback nv_gpu_poll_readback(NvGpu* gpu, u32 slot);

#if !defined(NDEBUG)
// Makes the device report a validation error, for tests of the error path (the sandbox's debug error export).
void nv_gpu_debug_raise_error(NvGpu* gpu);
#endif
