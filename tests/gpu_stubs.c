// Trap stubs for the GPU backend (engine/gpu.h), so that a test can build engine/renderer.c
// without a GPU: resolution_test runs only its GPU-free code (the scene's resolution, the
// camera's matrices, the rays through the image), and any call past them into the backend traps.

#include <engine/gpu.h>

const char* nv_gpu_format_name(NvGpuFormat format)
{
    (void)format;
    NV_TRAP();
    return "";
}

NvGpuBuffer nv_gpu_create_buffer(NvGpu* gpu, const NvGpuBufferDesc* desc)
{
    (void)gpu;
    (void)desc;
    NV_TRAP();
    return (NvGpuBuffer){0};
}

void nv_gpu_release_buffer(NvGpu* gpu, NvGpuBuffer buffer)
{
    (void)gpu;
    (void)buffer;
    NV_TRAP();
}

void nv_gpu_write_buffer(NvGpu* gpu, NvGpuBuffer buffer, u64 offset, const void* bytes, umm size)
{
    (void)gpu;
    (void)buffer;
    (void)offset;
    (void)bytes;
    (void)size;
    NV_TRAP();
}

NvGpuTexture nv_gpu_create_texture(NvGpu* gpu, const NvGpuTextureDesc* desc)
{
    (void)gpu;
    (void)desc;
    NV_TRAP();
    return (NvGpuTexture){0};
}

void nv_gpu_release_texture(NvGpu* gpu, NvGpuTexture texture)
{
    (void)gpu;
    (void)texture;
    NV_TRAP();
}

void nv_gpu_write_texture(NvGpu* gpu, NvGpuTexture texture, u32 mip, u32 x, u32 y, u32 width, u32 height, const void* texels,
                          u32 bytes_per_row)
{
    (void)gpu;
    (void)texture;
    (void)mip;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)texels;
    (void)bytes_per_row;
    NV_TRAP();
}

NvGpuView nv_gpu_create_view(NvGpu* gpu, NvGpuTexture texture, const NvGpuViewDesc* desc)
{
    (void)gpu;
    (void)texture;
    (void)desc;
    NV_TRAP();
    return (NvGpuView){0};
}

void nv_gpu_release_view(NvGpu* gpu, NvGpuView view)
{
    (void)gpu;
    (void)view;
    NV_TRAP();
}

NvGpuSampler nv_gpu_create_sampler(NvGpu* gpu, const NvGpuSamplerDesc* desc)
{
    (void)gpu;
    (void)desc;
    NV_TRAP();
    return (NvGpuSampler){0};
}

NvGpuLayout nv_gpu_create_layout(NvGpu* gpu, const NvGpuLayoutEntry* entries, u32 count)
{
    (void)gpu;
    (void)entries;
    (void)count;
    NV_TRAP();
    return (NvGpuLayout){0};
}

NvGpuGroup nv_gpu_create_group(NvGpu* gpu, NvGpuLayout layout, const NvGpuGroupEntry* entries, u32 count)
{
    (void)gpu;
    (void)layout;
    (void)entries;
    (void)count;
    NV_TRAP();
    return (NvGpuGroup){0};
}

void nv_gpu_release_group(NvGpu* gpu, NvGpuGroup group)
{
    (void)gpu;
    (void)group;
    NV_TRAP();
}

NvGpuPipeline nv_gpu_create_render_pipeline(NvGpu* gpu, const NvGpuRenderPipelineDesc* desc)
{
    (void)gpu;
    (void)desc;
    NV_TRAP();
    return (NvGpuPipeline){0};
}

NvGpuPipeline nv_gpu_create_compute_pipeline(NvGpu* gpu, const NvGpuComputePipelineDesc* desc)
{
    (void)gpu;
    (void)desc;
    NV_TRAP();
    return (NvGpuPipeline){0};
}

void nv_gpu_release_pipeline(NvGpu* gpu, NvGpuPipeline pipeline)
{
    (void)gpu;
    (void)pipeline;
    NV_TRAP();
}

void nv_gpu_begin_render_pass(NvGpu* gpu, const NvGpuRenderPassDesc* desc)
{
    (void)gpu;
    (void)desc;
    NV_TRAP();
}

void nv_gpu_begin_compute_pass(NvGpu* gpu, const NvGpuComputePassDesc* desc)
{
    (void)gpu;
    (void)desc;
    NV_TRAP();
}

void nv_gpu_end_pass(NvGpu* gpu)
{
    (void)gpu;
    NV_TRAP();
}

void nv_gpu_set_pipeline(NvGpu* gpu, NvGpuPipeline pipeline)
{
    (void)gpu;
    (void)pipeline;
    NV_TRAP();
}

void nv_gpu_set_group(NvGpu* gpu, u32 index, NvGpuGroup group, const u32* dynamic_offsets, u32 dynamic_count)
{
    (void)gpu;
    (void)index;
    (void)group;
    (void)dynamic_offsets;
    (void)dynamic_count;
    NV_TRAP();
}

void nv_gpu_set_vertex_buffer(NvGpu* gpu, NvGpuBuffer buffer)
{
    (void)gpu;
    (void)buffer;
    NV_TRAP();
}

void nv_gpu_set_index_buffer(NvGpu* gpu, NvGpuBuffer buffer, b32 is_32_bit)
{
    (void)gpu;
    (void)buffer;
    (void)is_32_bit;
    NV_TRAP();
}

void nv_gpu_set_viewport(NvGpu* gpu, f32 x, f32 y, f32 width, f32 height)
{
    (void)gpu;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    NV_TRAP();
}

void nv_gpu_set_scissor(NvGpu* gpu, u32 x, u32 y, u32 width, u32 height)
{
    (void)gpu;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    NV_TRAP();
}

void nv_gpu_draw(NvGpu* gpu, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance)
{
    (void)gpu;
    (void)vertex_count;
    (void)instance_count;
    (void)first_vertex;
    (void)first_instance;
    NV_TRAP();
}

void nv_gpu_draw_indexed(NvGpu* gpu, u32 index_count, u32 first_index, s32 base_vertex, u32 first_instance)
{
    (void)gpu;
    (void)index_count;
    (void)first_index;
    (void)base_vertex;
    (void)first_instance;
    NV_TRAP();
}

void nv_gpu_draw_indirect(NvGpu* gpu, NvGpuBuffer buffer, u64 offset)
{
    (void)gpu;
    (void)buffer;
    (void)offset;
    NV_TRAP();
}

void nv_gpu_dispatch(NvGpu* gpu, u32 x, u32 y, u32 z)
{
    (void)gpu;
    (void)x;
    (void)y;
    (void)z;
    NV_TRAP();
}

void nv_gpu_dispatch_indirect(NvGpu* gpu, NvGpuBuffer buffer, u64 offset)
{
    (void)gpu;
    (void)buffer;
    (void)offset;
    NV_TRAP();
}

b32 nv_gpu_resolve_timestamps(NvGpu* gpu, u32 count)
{
    (void)gpu;
    (void)count;
    NV_TRAP();
    return 0;
}

NvGpuTimestamps nv_gpu_poll_timestamps(NvGpu* gpu)
{
    (void)gpu;
    NV_TRAP();
    return (NvGpuTimestamps){0};
}

b32 nv_gpu_copy_for_readback(NvGpu* gpu, u32 slot, NvGpuBuffer source, u64 offset, umm size)
{
    (void)gpu;
    (void)slot;
    (void)source;
    (void)offset;
    (void)size;
    NV_TRAP();
    return 0;
}

NvGpuReadback nv_gpu_poll_readback(NvGpu* gpu, u32 slot)
{
    (void)gpu;
    (void)slot;
    NV_TRAP();
    return (NvGpuReadback){0};
}
