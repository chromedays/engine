#include "engine/win32.h"

#define COBJMACROS
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>

#include "engine/gpu.h"
#include "engine/image.h"
#include "engine/log.h"
#include "engine/strings.h"
#include "engine/window.h"
#include "engine/window_win32.h"

#include <stdio.h>
#include <string.h>

// The Direct3D 12 backend of engine/gpu.h (docs/specs/d3d12_port.md). The objects of the API are handles into pools of D3D12
// objects, kept here, and everything WebGPU does for the caller that D3D12 does not (resource states and their barriers,
// descriptor heaps, frames in flight, uploads, deferred release) is done here too: the engine's code writes no barrier.

// The D3D12 loader reads these two from the executable and loads D3D12Core.dll from that folder instead of the OS's runtime, so
// the runtime's version is the one the headers and the build pin (cmake/shaders.cmake), not whatever Windows Update brought.
__declspec(dllexport) const UINT D3D12SDKVersion = NV_AGILITY_SDK_VERSION;
__declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";

#define FRAMES_IN_FLIGHT 2
#define BACK_BUFFER_COUNT 3
#define MAX_MIPS 16
#define UPLOAD_RING_BYTES NV_MEGABYTES(64) // one for each frame in flight
#define VIEW_HEAP_DESCRIPTORS 32768        // shader-visible: the descriptors of the bind groups
#define SAMPLER_HEAP_DESCRIPTORS 64        // shader-visible, one for each sampler of the pool
#define MAX_BARRIERS 256
#define MAX_DEFERRED 8192
#define MAX_ROOT_SIGNATURES 64
#define MAX_GROUP_USES NV_GPU_MAX_ENTRIES
#define UPLOAD_TEXTURE_ALIGNMENT D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT // 512
#define FIRST_INSTANCE_SPACE 8 // the register space of the root constant (engine/shaders/renderer_frame.slang)

// WebGPU's default limits for the buffers (maxBufferSize, maxStorageBufferBindingSize): the effects size their particle
// buffers by them, and the shaders the two builds share keep to the web's limits (docs/specs/d3d12_port.md, "Minimum").
#define SHARED_MAX_BUFFER_BYTES (256ull << 20)
#define SHARED_MAX_STORAGE_BINDING_BYTES (128ull << 20)

//
// Formats
//

const char* nv_gpu_format_name(NvGpuFormat format)
{
    switch (format) {
    case NV_GPU_FORMAT_BGRA8:      return "BGRA8Unorm";
    case NV_GPU_FORMAT_BGRA8_SRGB: return "BGRA8UnormSrgb";
    case NV_GPU_FORMAT_RGBA8:      return "RGBA8Unorm";
    case NV_GPU_FORMAT_RGBA8_SRGB: return "RGBA8UnormSrgb";
    case NV_GPU_FORMAT_RGBA16F:    return "RGBA16Float";
    case NV_GPU_FORMAT_DEPTH16:    return "Depth16Unorm";
    case NV_GPU_FORMAT_DEPTH32F:   return "Depth32Float";
    default:                       return "other format";
    }
}

// The format of a color view, and of a depth texture's depth-stencil view.
internal DXGI_FORMAT dxgi_format(NvGpuFormat format)
{
    switch (format) {
    case NV_GPU_FORMAT_BGRA8:      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case NV_GPU_FORMAT_BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case NV_GPU_FORMAT_RGBA8:      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case NV_GPU_FORMAT_RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case NV_GPU_FORMAT_RGBA16F:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case NV_GPU_FORMAT_DEPTH16:    return DXGI_FORMAT_D16_UNORM;
    case NV_GPU_FORMAT_DEPTH32F:   return DXGI_FORMAT_D32_FLOAT;
    default:                       return DXGI_FORMAT_UNKNOWN;
    }
}

// The format of a depth texture's resource when it can be sampled: typeless, so the depth-stencil view and the shader view differ.
internal DXGI_FORMAT dxgi_typeless_depth(NvGpuFormat format)
{
    return format == NV_GPU_FORMAT_DEPTH16 ? DXGI_FORMAT_R16_TYPELESS : DXGI_FORMAT_R32_TYPELESS;
}

// The format a shader reads a depth texture in.
internal DXGI_FORMAT dxgi_depth_shader_format(NvGpuFormat format)
{
    return format == NV_GPU_FORMAT_DEPTH16 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R32_FLOAT;
}

internal b32 is_depth_format(NvGpuFormat format)
{
    return format == NV_GPU_FORMAT_DEPTH16 || format == NV_GPU_FORMAT_DEPTH32F;
}

//
// Handle pools
//
// A pool has a fixed number of slots. A slot's generation is odd while it holds an object and even while it is free; a handle
// carries the generation it was made with. Slot 0 is never used, so a zeroed handle is the null handle.
//

typedef struct Pool {
    u32 capacity;
    umm item_size;
    u8* items;
    u32* generations;
    u32* next_free;
    u32 free_head; // index of the first free slot, 0 for none
    u32 top;       // slots from `top` up have never been used
} Pool;

#define DEFINE_POOL(name, Item, count)                       \
    global Item name##_items[count];                         \
    global u32 name##_generations[count];                    \
    global u32 name##_next_free[count];                      \
    global Pool name##_pool = {.capacity = count, .item_size = sizeof(Item), .items = (u8*)name##_items, \
                               .generations = name##_generations, .next_free = name##_next_free, .top = 1}

// Takes a slot; returns its index and, in `generation`, the one a handle to it carries.
internal u32 pool_take(Pool* pool, u32* generation)
{
    u32 index = pool->free_head;
    if (index) {
        pool->free_head = pool->next_free[index];
    } else {
        NV_ASSERT(pool->top < pool->capacity);
        index = pool->top++;
    }
    ++pool->generations[index];
    NV_ASSERT(pool->generations[index] & 1);
    *generation = pool->generations[index];
    memset(pool->items + (umm)index * pool->item_size, 0, pool->item_size);
    return index;
}

// The slot of a handle that is live: asserts on a null, released or reused handle.
internal void* pool_item(const Pool* pool, u32 index, u32 generation)
{
    (void)generation; // only the assert below reads it
    NV_ASSERT(index > 0 && index < pool->top);
    NV_ASSERT(pool->generations[index] == generation && (generation & 1));
    return pool->items + (umm)index * pool->item_size;
}

internal void pool_give(Pool* pool, u32 index, u32 generation)
{
    pool_item(pool, index, generation);
    ++pool->generations[index];
    pool->next_free[index] = pool->free_head;
    pool->free_head = index;
}

//
// Objects
//

// What a bind group entry or a vertex, index or indirect buffer needs its resource to be: the states of D3D12_RESOURCE_STATES.
#define STATE_COMMON D3D12_RESOURCE_STATE_COMMON
#define STATE_BUFFER_READ_SHADER (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
#define STATE_TEXTURE_READ_SHADER (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)

typedef struct Buffer {
    ID3D12Resource* resource;
    u64 size; // as created: rounded up to 4, and to 256 for a uniform buffer
    u32 usage;
    D3D12_GPU_VIRTUAL_ADDRESS address;
    D3D12_RESOURCE_STATES state;           // after the commands recorded so far
    D3D12_RESOURCE_STATES submitted_state; // after the commands already submitted: what the upload list finds it in
    b32 touched;                           // on the frame's list of resources whose state moved
    u32 group_references;                  // bind groups that use it: they keep it alive after the program releases it, as in WebGPU
    b32 released;                          // the program released it; it goes when the last group does
} Buffer;

typedef struct Texture {
    ID3D12Resource* resource;
    NvGpuTextureDesc desc; // the label is dropped
    D3D12_RESOURCE_STATES state[MAX_MIPS];
    D3D12_RESOURCE_STATES submitted_state[MAX_MIPS];
    b32 touched;
    u32 group_references;
    b32 released;
    b32 sampled_depth; // a depth texture made typeless so a shader can read it
} Texture;

typedef struct View {
    NvGpuTexture texture;
    u32 base_mip;
    u32 mip_count;
    b32 depth_only;
    NvGpuFormat format; // of its render target view or shader view: the texture's, except the swapchain's sRGB one
} View;

typedef struct Sampler {
    u32 descriptor; // its place in the sampler heap: the index of its handle
} Sampler;

typedef struct Layout {
    NvGpuLayoutEntry entries[NV_GPU_MAX_ENTRIES];
    u32 count;
    u32 table_count;   // entries in the CBV/SRV/UAV table, in entry order
    u32 sampler_count; // entries in the sampler table (at most one)
    u32 dynamic_count; // uniform entries given with an offset: root constant buffers
    u32 block_size;    // descriptors a group of this layout takes in the view heap: table_count rounded up to a size class
} Layout;

// One thing a group makes the GPU read or write, for the barriers: a buffer, or the mips of a texture.
typedef struct GroupUse {
    b32 is_texture;
    NvGpuBuffer buffer;
    NvGpuTexture texture;
    u32 base_mip, mip_count;
    D3D12_RESOURCE_STATES state;
} GroupUse;

typedef struct Group {
    NvGpuLayout layout;
    u32 block;                   // first descriptor of its table in the view heap; ~0u when it has no table
    u32 block_class;             // its size class
    u32 sampler_descriptor;      // the sampler's place in the sampler heap; ~0u when it has none
    NvGpuBuffer dynamic_buffer[2];
    u64 dynamic_size[2];
    GroupUse uses[MAX_GROUP_USES];
    u32 use_count;
} Group;

typedef struct RootSignature {
    ID3D12RootSignature* root_signature;
    b32 is_compute;
    NvGpuLayout layouts[NV_GPU_MAX_LAYOUTS];
    u32 layout_count;
    // The root parameter of each group's pieces; ~0u for a piece the layout does not have.
    u32 table_parameter[NV_GPU_MAX_LAYOUTS];
    u32 sampler_parameter[NV_GPU_MAX_LAYOUTS];
    u32 dynamic_parameter[NV_GPU_MAX_LAYOUTS][2];
    u32 first_instance_parameter; // ~0u for a compute pipeline
} RootSignature;

typedef struct Pipeline {
    b32 is_compute;
    ID3D12PipelineState* state;
    u32 root_signature; // index into the root signature cache
    D3D12_PRIMITIVE_TOPOLOGY topology;
    u32 vertex_stride;
} Pipeline;

DEFINE_POOL(buffer, Buffer, 2048);
DEFINE_POOL(texture, Texture, 512);
DEFINE_POOL(view, View, 1024);
DEFINE_POOL(sampler, Sampler, 32);
DEFINE_POOL(layout, Layout, 64);
DEFINE_POOL(group, Group, 1024);
DEFINE_POOL(pipeline, Pipeline, 128);

//
// The device
//

typedef enum ReadbackState {
    READBACK_IDLE,    // no request, or the last was taken
    READBACK_COPIED,  // the copy is in the frame's commands
    READBACK_PENDING, // the copy was submitted; the GPU has not finished it
    READBACK_READY,   // finished, not yet taken
    READBACK_TAKEN,   // taken; the bytes stay until the next request
} ReadbackState;

typedef struct Readback {
    ReadbackState state;
    ID3D12Resource* resource;
    u64 capacity;
    umm size;
    u64 fence_value;
    const void* mapped;
} Readback;

typedef struct Deferred {
    IUnknown* object;
    u64 fence_value; // released once the fence has passed it
    u32 view_block;  // when `object` is NULL: a block of the view heap to give back, and its size class
    u32 view_block_size;
} Deferred;

typedef struct FrameSlot {
    ID3D12CommandAllocator* upload_allocator;
    ID3D12CommandAllocator* main_allocator;
    ID3D12Resource* ring;
    u8* ring_mapped;
    u64 fence_value; // signaled when the GPU has finished the slot's commands
} FrameSlot;

// A screenshot asked for (nv_gpu_debug_screenshot), taken by the frame it was asked in.
typedef struct ScreenshotRequest {
    b32 pending;
    char path[520];
    u32 x, y, width, height;
    ID3D12Resource* readback; // the canvas copied out, after the frame's commands: rows of `pitch` bytes
    u32 pitch;
} ScreenshotRequest;

// A group set on the pass, and whether its root arguments and barriers are done for the pipeline and the states now.
typedef struct BoundGroup {
    NvGpuGroup group;
    u32 dynamic_offsets[2];
    u32 dynamic_count;
    b32 dirty;         // not yet set on the list for the current root signature
    u64 checked_epoch; // the state epoch its resources were last checked against
} BoundGroup;

typedef struct Backend {
    NvGpu* gpu;
    IDXGIFactory6* factory;
    IDXGIAdapter1* adapter;
    ID3D12DeviceFactory* device_factory; // of the Agility SDK beside the executable; NULL when it could not be opened
    ID3D12Device* device;
    ID3D12CommandQueue* queue;
    IDXGISwapChain3* swapchain;
    ID3D12InfoQueue* info_queue; // Debug builds
    b32 device_lost;

    // The fence, and the frames in flight.
    ID3D12Fence* fence;
    HANDLE fence_event;
    ID3D12Fence* flush_fence; // for the writes run alone (flush_uploads), which the main fence must not count
    u64 flush_counter;
    u64 fence_counter; // the last value signaled
    FrameSlot slots[FRAMES_IN_FLIGHT];
    u32 slot;          // the slot recording now
    ID3D12GraphicsCommandList* upload_list; // this slot's writes: run before the frame's commands
    ID3D12GraphicsCommandList* list;        // the frame's commands
    u64 ring_used;
    b32 in_frame;

    // The swapchain's buffers, as textures and views of the pools.
    NvGpuTexture back_texture[BACK_BUFFER_COUNT];
    NvGpuView back_view[BACK_BUFFER_COUNT];
    u32 back_index;

    // Descriptors. The view heap holds the descriptors of the bind groups (shader-visible, in blocks of 1, 2, 4, 8 or 16);
    // render target and depth views of the views of the pool have a place each in the two heaps the CPU reads.
    ID3D12DescriptorHeap* view_heap;
    ID3D12DescriptorHeap* sampler_heap;
    ID3D12DescriptorHeap* rtv_heap;
    ID3D12DescriptorHeap* dsv_heap;
    u32 view_increment, sampler_increment, rtv_increment, dsv_increment;
    u32 view_top; // descriptors from here up have never been given out
    u32 free_blocks[5]; // heads of the free lists of the size classes; ~0u for none. A free block's first descriptor's place holds the next.
    u32* free_next;     // [VIEW_HEAP_DESCRIPTORS]: the next free block of the same class

    Deferred deferred[MAX_DEFERRED];
    u32 deferred_count;

    // Resources whose state moved since the last submit, to make submitted_state follow state.
    NvGpuBuffer touched_buffers[1024];
    u32 touched_buffer_count;
    NvGpuTexture touched_textures[1024];
    u32 touched_texture_count;

    D3D12_RESOURCE_BARRIER barriers[MAX_BARRIERS];
    u32 barrier_count;
    u64 state_epoch; // counts the transitions made: a bound group is checked again when it changes

    // Pass times: the query heap, and the buffer they are resolved into (a readback heap: the CPU reads it where it lies).
    ID3D12QueryHeap* timestamp_heap;
    ID3D12Resource* timestamp_readback;
    const u64* timestamp_mapped;
    u64 timestamp_frequency;
    u32 timestamp_count; // asked for by this frame; 0 for none
    u32 timestamp_written; // the slots a pass of this frame wrote
    ReadbackState timestamp_state;
    u64 timestamp_fence;

    Readback readbacks[NV_GPU_MAX_READBACKS];

    ID3D12CommandSignature* draw_signature;
    ID3D12CommandSignature* dispatch_signature;

    RootSignature root_signatures[MAX_ROOT_SIGNATURES];
    u32 root_signature_count;

    // The pass being recorded and what is bound in it.
    b32 in_render_pass;
    b32 in_compute_pass;
    NvGpuRenderPassDesc pass;
    u32 pass_width, pass_height;
    u32 pass_timestamp_end;
    b32 pass_timed;
    NvGpuPipeline pipeline;
    u32 root_signature_in_use; // 1-based index of the root signature set on the list; 0 for none
    BoundGroup bound[NV_GPU_MAX_LAYOUTS];
    ScreenshotRequest screenshot;
    NvGpuBuffer vertex_buffer;
    NvGpuBuffer index_buffer;
    b32 index_32_bit;
    b32 vertex_buffer_dirty, index_buffer_dirty;
    u32 first_instance;
    b32 first_instance_valid;
    b32 needs_uav_barrier;
    wchar_t event_name[128];
} Backend;

global Backend backend;

//
// Small helpers
//

internal void check(HRESULT result, const char* what)
{
    if (FAILED(result)) {
        nv_log(NV_LOG_ERROR, "d3d12", "%s failed (0x%08lx)", what, (unsigned long)result);
        NV_ASSERT(!"a D3D12 call failed");
    }
}

internal void set_name(ID3D12Object* object, const char* label)
{
    if (!label || !label[0])
        return;
    wchar_t wide[128];
    nv_win32_wide_from_utf8(label, wide, NV_ARRAY_COUNT(wide));
    ID3D12Object_SetName(object, wide);
}

internal u64 round_up(u64 value, u64 multiple)
{
    return (value + multiple - 1) / multiple * multiple;
}

internal D3D12_CPU_DESCRIPTOR_HANDLE cpu_start(ID3D12DescriptorHeap* heap)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &handle);
    return handle;
}

internal D3D12_GPU_DESCRIPTOR_HANDLE gpu_start(ID3D12DescriptorHeap* heap)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle;
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &handle);
    return handle;
}

internal D3D12_CPU_DESCRIPTOR_HANDLE view_cpu(u32 descriptor)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = cpu_start(backend.view_heap);
    handle.ptr += (SIZE_T)descriptor * backend.view_increment;
    return handle;
}

internal D3D12_GPU_DESCRIPTOR_HANDLE view_gpu(u32 descriptor)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = gpu_start(backend.view_heap);
    handle.ptr += (UINT64)descriptor * backend.view_increment;
    return handle;
}

internal D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu(u32 descriptor)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = gpu_start(backend.sampler_heap);
    handle.ptr += (UINT64)descriptor * backend.sampler_increment;
    return handle;
}

internal Buffer* buffer_of(NvGpuBuffer handle)
{
    return pool_item(&buffer_pool, handle.index, handle.generation);
}

internal Texture* texture_of(NvGpuTexture handle)
{
    return pool_item(&texture_pool, handle.index, handle.generation);
}

internal View* view_of(NvGpuView handle)
{
    return pool_item(&view_pool, handle.index, handle.generation);
}

internal Layout* layout_of(NvGpuLayout handle)
{
    return pool_item(&layout_pool, handle.index, handle.generation);
}

internal Group* group_of(NvGpuGroup handle)
{
    return pool_item(&group_pool, handle.index, handle.generation);
}

internal Pipeline* pipeline_of(NvGpuPipeline handle)
{
    return pool_item(&pipeline_pool, handle.index, handle.generation);
}

//
// Descriptor blocks of the view heap
//

// The size class of a block that holds `count` descriptors: 0 for 1, 1 for 2, 2 for up to 4, 3 for up to 8, 4 for up to 16.
internal u32 block_class(u32 count)
{
    NV_ASSERT(count >= 1 && count <= 16);
    u32 size_class = 0;
    while ((1u << size_class) < count)
        ++size_class;
    return size_class;
}

internal u32 block_alloc(u32 size_class)
{
    u32 block = backend.free_blocks[size_class];
    if (block != ~0u) {
        backend.free_blocks[size_class] = backend.free_next[block];
        return block;
    }
    u32 size = 1u << size_class;
    u32 start = (backend.view_top + size - 1) & ~(size - 1);
    NV_ASSERT(start + size <= VIEW_HEAP_DESCRIPTORS);
    backend.view_top = start + size;
    return start;
}

internal void block_give(u32 block, u32 size_class)
{
    backend.free_next[block] = backend.free_blocks[size_class];
    backend.free_blocks[size_class] = block;
}

//
// Deferred release
//
// WebGPU lets the program release an object the GPU is still using; D3D12 does not, so a release waits until the fence has passed
// the next submit, which holds every command that could name the object.
//

internal void defer_release(IUnknown* object)
{
    NV_ASSERT(backend.deferred_count < MAX_DEFERRED);
    backend.deferred[backend.deferred_count++] = (Deferred){.object = object, .fence_value = backend.fence_counter + 1};
}

internal void defer_block(u32 block, u32 size_class)
{
    NV_ASSERT(backend.deferred_count < MAX_DEFERRED);
    backend.deferred[backend.deferred_count++] =
        (Deferred){.fence_value = backend.fence_counter + 1, .view_block = block, .view_block_size = size_class};
}

internal void process_deferred(void)
{
    u64 completed = ID3D12Fence_GetCompletedValue(backend.fence);
    u32 kept = 0;
    for (u32 i = 0; i < backend.deferred_count; ++i) {
        Deferred entry = backend.deferred[i];
        if (entry.fence_value > completed)
            backend.deferred[kept++] = entry;
        else if (entry.object)
            IUnknown_Release(entry.object);
        else
            block_give(entry.view_block, entry.view_block_size);
    }
    backend.deferred_count = kept;
}

//
// Barriers
//
// The backend keeps each resource's state (each mip's, for a texture) as the commands recorded so far leave it, and puts the
// transition in front of every command that needs another (a draw, a dispatch, a copy, the start of a pass): the program's code
// writes no barrier. `submitted_state` is the state the GPU finds it in when the frame's commands start, which is what the writes
// (the upload list, which runs first) see.
//

internal void flush_barriers(void)
{
    if (backend.barrier_count) {
        ID3D12GraphicsCommandList_ResourceBarrier(backend.list, backend.barrier_count, backend.barriers);
        backend.barrier_count = 0;
    }
}

internal void queue_barrier(D3D12_RESOURCE_BARRIER barrier)
{
    if (backend.barrier_count == MAX_BARRIERS)
        flush_barriers();
    backend.barriers[backend.barrier_count++] = barrier;
}

internal D3D12_RESOURCE_BARRIER transition_barrier(ID3D12Resource* resource, u32 subresource, D3D12_RESOURCE_STATES before,
                                                   D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = subresource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

internal void touch_buffer(NvGpuBuffer handle, Buffer* buffer)
{
    if (!buffer->touched && backend.touched_buffer_count < NV_ARRAY_COUNT(backend.touched_buffers)) {
        buffer->touched = 1;
        backend.touched_buffers[backend.touched_buffer_count++] = handle;
    }
}

internal void touch_texture(NvGpuTexture handle, Texture* texture)
{
    if (!texture->touched && backend.touched_texture_count < NV_ARRAY_COUNT(backend.touched_textures)) {
        texture->touched = 1;
        backend.touched_textures[backend.touched_texture_count++] = handle;
    }
}

internal void transition_buffer(NvGpuBuffer handle, D3D12_RESOURCE_STATES state)
{
    Buffer* buffer = buffer_of(handle);
    if (buffer->state == state)
        return;
    queue_barrier(transition_barrier(buffer->resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, buffer->state, state));
    buffer->state = state;
    touch_buffer(handle, buffer);
    ++backend.state_epoch;
}

// Moves `count` mips from `first` (0: every one) to `state`.
internal void transition_texture(NvGpuTexture handle, u32 first, u32 count, D3D12_RESOURCE_STATES state)
{
    Texture* texture = texture_of(handle);
    if (count == 0)
        count = texture->desc.mip_count - first;
    NV_ASSERT(first + count <= texture->desc.mip_count);
    b32 all = first == 0 && count == texture->desc.mip_count;
    b32 same = 1;
    for (u32 mip = first; mip < first + count; ++mip)
        same = same && texture->state[mip] == texture->state[first];
    if (all && same) {
        if (texture->state[0] == state)
            return;
        queue_barrier(transition_barrier(texture->resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, texture->state[0], state));
    } else {
        b32 moved = 0;
        for (u32 mip = first; mip < first + count; ++mip) {
            if (texture->state[mip] == state)
                continue;
            queue_barrier(transition_barrier(texture->resource, mip, texture->state[mip], state));
            moved = 1;
        }
        if (!moved)
            return;
    }
    for (u32 mip = first; mip < first + count; ++mip)
        texture->state[mip] = state;
    touch_texture(handle, texture);
    ++backend.state_epoch;
}

// The frame's commands are submitted: what they left is what the next frame's writes find.
internal void commit_states(void)
{
    for (u32 i = 0; i < backend.touched_buffer_count; ++i) {
        NvGpuBuffer handle = backend.touched_buffers[i];
        if (buffer_generations[handle.index] != handle.generation)
            continue; // released since
        Buffer* buffer = &buffer_items[handle.index];
        buffer->submitted_state = buffer->state;
        buffer->touched = 0;
    }
    backend.touched_buffer_count = 0;
    for (u32 i = 0; i < backend.touched_texture_count; ++i) {
        NvGpuTexture handle = backend.touched_textures[i];
        if (texture_generations[handle.index] != handle.generation)
            continue;
        Texture* texture = &texture_items[handle.index];
        memcpy(texture->submitted_state, texture->state, sizeof(texture->state));
        texture->touched = 0;
    }
    backend.touched_texture_count = 0;
}

//
// Frames, uploads and submitting
//

internal void wait_for_fence(ID3D12Fence* fence, u64 value)
{
    if (ID3D12Fence_GetCompletedValue(fence) >= value)
        return;
    check(ID3D12Fence_SetEventOnCompletion(fence, value, backend.fence_event), "SetEventOnCompletion");
    WaitForSingleObject(backend.fence_event, INFINITE);
}

// Waits until the GPU has finished everything submitted.
internal void wait_idle(void)
{
    ++backend.fence_counter;
    check(ID3D12CommandQueue_Signal(backend.queue, backend.fence, backend.fence_counter), "Signal");
    wait_for_fence(backend.fence, backend.fence_counter);
}

internal void report_device_removed(void)
{
    if (backend.device_lost)
        return;
    HRESULT reason = ID3D12Device_GetDeviceRemovedReason(backend.device);
    if (FAILED(reason)) {
        backend.device_lost = 1;
        nv_log(NV_LOG_ERROR, "d3d12", "device removed (0x%08lx): the GPU was reset or went away; frames stop", (unsigned long)reason);
    }
}

// Starts recording slot `slot`: waits for the GPU to be done with the commands it held two frames ago, then reuses its memory.
internal void begin_slot(u32 slot)
{
    FrameSlot* s = &backend.slots[slot];
    wait_for_fence(backend.fence, s->fence_value);
    check(ID3D12CommandAllocator_Reset(s->upload_allocator), "upload allocator Reset");
    check(ID3D12CommandAllocator_Reset(s->main_allocator), "main allocator Reset");
    check(ID3D12GraphicsCommandList_Reset(backend.upload_list, s->upload_allocator, NULL), "upload list Reset");
    check(ID3D12GraphicsCommandList_Reset(backend.list, s->main_allocator, NULL), "main list Reset");
    backend.slot = slot;
    backend.ring_used = 0;
    process_deferred();

    ID3D12DescriptorHeap* heaps[2] = {backend.view_heap, backend.sampler_heap};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(backend.list, 2, heaps);
    // A new list has nothing set.
    backend.pipeline = (NvGpuPipeline){0};
    backend.root_signature_in_use = 0;
    memset(backend.bound, 0, sizeof(backend.bound));
    backend.vertex_buffer = (NvGpuBuffer){0};
    backend.index_buffer = (NvGpuBuffer){0};
    backend.first_instance_valid = 0;
    backend.needs_uav_barrier = 0;
}

// Runs the writes recorded so far and waits for them, so the ring is free again: for a write that does not fit what is left of it.
internal void flush_uploads(void)
{
    check(ID3D12GraphicsCommandList_Close(backend.upload_list), "upload list Close");
    ID3D12CommandList* lists[1] = {(ID3D12CommandList*)backend.upload_list};
    ID3D12CommandQueue_ExecuteCommandLists(backend.queue, 1, lists);
    ++backend.flush_counter;
    check(ID3D12CommandQueue_Signal(backend.queue, backend.flush_fence, backend.flush_counter), "Signal");
    wait_for_fence(backend.flush_fence, backend.flush_counter);
    FrameSlot* s = &backend.slots[backend.slot];
    check(ID3D12CommandAllocator_Reset(s->upload_allocator), "upload allocator Reset");
    check(ID3D12GraphicsCommandList_Reset(backend.upload_list, s->upload_allocator, NULL), "upload list Reset");
    backend.ring_used = 0;
}

// `size` bytes of this frame's upload ring, which the GPU reads when the upload list runs. Returns where, as an offset into
// the ring's resource, and the CPU's pointer to write through.
internal u8* ring_alloc(u64 size, u64 alignment, u64* offset)
{
    NV_ASSERT(size <= UPLOAD_RING_BYTES);
    u64 start = round_up(backend.ring_used, alignment);
    if (start + size > UPLOAD_RING_BYTES) {
        flush_uploads();
        start = 0;
    }
    backend.ring_used = start + size;
    *offset = start;
    return backend.slots[backend.slot].ring_mapped + start;
}

// Everything the frame recorded goes to the GPU: the writes first, then the commands. The next slot starts.
// `present` also puts the frame on the screen.
internal void submit(b32 present)
{
    flush_barriers();
    check(ID3D12GraphicsCommandList_Close(backend.upload_list), "upload list Close");
    check(ID3D12GraphicsCommandList_Close(backend.list), "main list Close");
    ID3D12CommandList* lists[2] = {(ID3D12CommandList*)backend.upload_list, (ID3D12CommandList*)backend.list};
    ID3D12CommandQueue_ExecuteCommandLists(backend.queue, 2, lists);
    if (present) {
        HRESULT presented = IDXGISwapChain3_Present(backend.swapchain, 1, 0);
        if (presented == DXGI_ERROR_DEVICE_REMOVED || presented == DXGI_ERROR_DEVICE_RESET)
            report_device_removed();
        else if (FAILED(presented))
            nv_log(NV_LOG_ERROR, "d3d12", "Present failed (0x%08lx)", (unsigned long)presented);
    }
    ++backend.fence_counter;
    check(ID3D12CommandQueue_Signal(backend.queue, backend.fence, backend.fence_counter), "Signal");
    backend.slots[backend.slot].fence_value = backend.fence_counter;

    for (u32 i = 0; i < NV_GPU_MAX_READBACKS; ++i) {
        Readback* readback = &backend.readbacks[i];
        if (readback->state == READBACK_COPIED) {
            readback->state = READBACK_PENDING;
            readback->fence_value = backend.fence_counter;
        }
    }
    if (backend.timestamp_count) {
        backend.timestamp_count = 0;
        backend.timestamp_state = READBACK_PENDING;
        backend.timestamp_fence = backend.fence_counter;
    }
    commit_states();
    begin_slot((backend.slot + 1) % FRAMES_IN_FLIGHT);
}

// Defined with the textures below.
internal NvGpuView create_view_as(NvGpu* gpu, NvGpuTexture texture, const NvGpuViewDesc* desc, NvGpuFormat format);

//
// Creating the device
//

// The Agility SDK's runtime beside the executable, asked for by name. The executable also exports the same version and folder
// (D3D12SDKVersion above), which is what the loader falls back on; the factory makes a mismatch an error here, and it is what
// finds the debug layer that comes with the SDK.
internal void open_device_factory(void)
{
    ID3D12SDKConfiguration1* configuration = NULL;
    HRESULT result = D3D12GetInterface(&CLSID_D3D12SDKConfiguration, &IID_ID3D12SDKConfiguration1, (void**)&configuration);
    if (FAILED(result)) {
        nv_log(NV_LOG_WARNING, "nv", "no D3D12 SDK configuration (0x%08lx): the runtime is the one the loader finds", (unsigned long)result);
        return;
    }
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (slash)
        wcscpy(slash + 1, L"D3D12\\");
    char folder[MAX_PATH * 3];
    nv_win32_utf8_from_wide(exe, folder, sizeof(folder));
    result = ID3D12SDKConfiguration1_CreateDeviceFactory(configuration, D3D12SDKVersion, folder, &IID_ID3D12DeviceFactory,
                                                         (void**)&backend.device_factory);
    if (FAILED(result))
        nv_log(NV_LOG_WARNING, "nv", "no D3D12 device factory for Agility SDK %u in %s (0x%08lx)", (u32)D3D12SDKVersion, folder, (unsigned long)result);
    ID3D12SDKConfiguration1_Release(configuration);
}

internal HRESULT create_d3d12_device(IUnknown* adapter, D3D_FEATURE_LEVEL level, ID3D12Device** device)
{
    if (backend.device_factory)
        return ID3D12DeviceFactory_CreateDevice(backend.device_factory, adapter, level, &IID_ID3D12Device, (void**)device);
    return D3D12CreateDevice(adapter, level, &IID_ID3D12Device, (void**)device);
}

// What a check of an adapter found wanting, or nothing.
typedef struct AdapterCheck {
    b32 ok;
    ID3D12Device* device;
    char reason[160];
} AdapterCheck;

// Feature level 12_2 and shader model 6.6: the program's minimum (docs/specs/d3d12_port.md, "Minimum").
internal AdapterCheck check_adapter(IDXGIAdapter1* adapter)
{
    AdapterCheck result = {0};
    ID3D12Device* device = NULL;
    HRESULT created = create_d3d12_device((IUnknown*)adapter, D3D_FEATURE_LEVEL_12_2, &device);
    if (FAILED(created)) {
        snprintf(result.reason, sizeof(result.reason), "no Direct3D 12 feature level 12_2 (0x%08lx)", (unsigned long)created);
        return result;
    }
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {.HighestShaderModel = D3D_SHADER_MODEL_6_6};
    HRESULT asked = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
    if (FAILED(asked) || shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_6) {
        snprintf(result.reason, sizeof(result.reason), "shader model %u.%u at most, not 6.6",
                 FAILED(asked) ? 5u : (u32)(shader_model.HighestShaderModel >> 4), FAILED(asked) ? 1u : (u32)(shader_model.HighestShaderModel & 15));
        ID3D12Device_Release(device);
        return result;
    }
    result.ok = 1;
    result.device = device;
    return result;
}

// The first adapter, high performance first, that passes the check and is not software. Logs each one looked at and why it was
// skipped; `skipped` collects the same for the message that says no GPU will do.
internal b32 pick_adapter(char* skipped, umm skipped_capacity)
{
    skipped[0] = 0;
    for (u32 i = 0;; ++i) {
        IDXGIAdapter1* adapter = NULL;
        if (FAILED(IDXGIFactory6_EnumAdapterByGpuPreference(backend.factory, i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &IID_IDXGIAdapter1,
                                                            (void**)&adapter)))
            break;
        DXGI_ADAPTER_DESC1 desc;
        IDXGIAdapter1_GetDesc1(adapter, &desc);
        char name[128];
        nv_win32_utf8_from_wide(desc.Description, name, sizeof(name));
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            nv_log(NV_LOG_INFO, "nv", "adapter %u: %s, skipped: software", i, name);
            IDXGIAdapter1_Release(adapter);
            continue;
        }
        AdapterCheck check_result = check_adapter(adapter);
        if (!check_result.ok) {
            nv_log(NV_LOG_INFO, "nv", "adapter %u: %s, skipped: %s", i, name, check_result.reason);
            size_t used = strlen(skipped);
            snprintf(skipped + used, skipped_capacity - used, "%s: %s\n", name, check_result.reason);
            IDXGIAdapter1_Release(adapter);
            continue;
        }
        nv_log(NV_LOG_INFO, "nv", "adapter %u: %s, selected (%.0f MB of its own memory)", i, name,
               (f64)desc.DedicatedVideoMemory / (1024.0 * 1024.0));
        backend.adapter = adapter;
        backend.device = check_result.device;
        snprintf(backend.gpu->adapter_name, sizeof(backend.gpu->adapter_name), "%s", name);
        return 1;
    }
    return 0;
}

// The runtime has to be the Agility SDK's, from the folder beside the executable (D3D12SDKPath above): when it is not, the build
// and the headers would not describe what runs, so the program says so and stops instead of running on two runtimes.
internal b32 runtime_is_ours(void)
{
    HMODULE core = GetModuleHandleW(L"D3D12Core.dll");
    wchar_t loaded[MAX_PATH] = {0};
    if (core)
        GetModuleFileNameW(core, loaded, MAX_PATH);
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (slash)
        wcscpy(slash + 1, L"D3D12\\D3D12Core.dll");
    char loaded_utf8[MAX_PATH * 3];
    nv_win32_utf8_from_wide(loaded, loaded_utf8, sizeof(loaded_utf8));
    if (!core || _wcsicmp(loaded, exe) != 0) {
        nv_log(NV_LOG_ERROR, "nv", "the Direct3D 12 runtime is %s, not the D3D12Core.dll beside the executable (Agility SDK %u)",
               core ? loaded_utf8 : "not loaded", (u32)D3D12SDKVersion);
        return 0;
    }
    nv_log(NV_LOG_INFO, "nv", "Direct3D 12 runtime: %s (Agility SDK %u)", loaded_utf8, (u32)D3D12SDKVersion);
    return 1;
}

internal void show_no_gpu_message(const char* skipped)
{
    nv_strings_set_language(nv_strings_system_language()); // the app's own choice is not made yet
    char text[1024];
    snprintf(text, sizeof(text), T("No GPU runs this program: it needs Direct3D 12 feature level 12_2 and shader model 6.6.\n\n%s"),
             skipped[0] ? skipped : T("No GPU was found."));
    wchar_t wide_text[1024];
    nv_win32_wide_from_utf8(text, wide_text, NV_ARRAY_COUNT(wide_text));
    MessageBoxW(NULL, wide_text, L"nv", MB_OK | MB_ICONERROR);
}

// The debug layer and the GPU-based validation (Debug builds): the counterpart of WebGPU's validation, which the browser always
// has on. They have to be on before the device exists.
internal void enable_debug_layer(void)
{
#if !defined(NDEBUG)
    // NV_D3D12_VALIDATION=0 turns both off: a run that measures time (the scripted benchmark) cannot afford them, and the GPU's passes
    // and buffer copies are what it measures.
    wchar_t setting[8];
    DWORD setting_length = GetEnvironmentVariableW(L"NV_D3D12_VALIDATION", setting, NV_ARRAY_COUNT(setting));
    if (setting_length > 0 && setting_length < NV_ARRAY_COUNT(setting) && setting[0] == L'0') {
        nv_log(NV_LOG_INFO, "nv", "D3D12 debug layer off (NV_D3D12_VALIDATION=0)");
        return;
    }
    ID3D12Debug* debug = NULL;
    HRESULT debug_result = backend.device_factory
        ? ID3D12DeviceFactory_GetConfigurationInterface(backend.device_factory, &CLSID_D3D12Debug, &IID_ID3D12Debug, (void**)&debug)
        : D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debug);
    if (SUCCEEDED(debug_result)) {
        ID3D12Debug_EnableDebugLayer(debug);
        ID3D12Debug1* debug1 = NULL;
        if (SUCCEEDED(ID3D12Debug_QueryInterface(debug, &IID_ID3D12Debug1, (void**)&debug1))) {
            ID3D12Debug1_SetEnableGPUBasedValidation(debug1, TRUE);
            ID3D12Debug1_Release(debug1);
        }
        ID3D12Debug_Release(debug);
        nv_log(NV_LOG_INFO, "nv", "D3D12 debug layer and GPU-based validation on");
    } else {
        nv_log(NV_LOG_WARNING, "nv", "no D3D12 debug layer: no validation messages (0x%08lx)", (unsigned long)debug_result);
    }
#endif
}

internal void setup_info_queue(void)
{
#if !defined(NDEBUG)
    if (FAILED(ID3D12Device_QueryInterface(backend.device, &IID_ID3D12InfoQueue, (void**)&backend.info_queue)))
        return;
    // The messages are read once a frame (drain_info_queue). Clearing a target with a color other than the one it was made with
    // is what every pass here does on purpose.
    D3D12_MESSAGE_ID denied[] = {D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                                 D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE};
    D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE};
    D3D12_INFO_QUEUE_FILTER filter = {0};
    filter.DenyList.NumIDs = NV_ARRAY_COUNT(denied);
    filter.DenyList.pIDList = denied;
    filter.DenyList.NumSeverities = NV_ARRAY_COUNT(severities);
    filter.DenyList.pSeverityList = severities;
    ID3D12InfoQueue_PushStorageFilter(backend.info_queue, &filter);
#endif
}

// The debug layer's messages into the log, as errors, warnings or notes, once a frame.
internal void drain_info_queue(void)
{
    if (!backend.info_queue)
        return;
    u64 count = ID3D12InfoQueue_GetNumStoredMessagesAllowedByRetrievalFilter(backend.info_queue);
    for (u64 i = 0; i < count; ++i) {
        SIZE_T size = 0;
        ID3D12InfoQueue_GetMessage(backend.info_queue, i, NULL, &size);
        if (size == 0 || size > 8192)
            continue;
        u8 storage[8192] = {0};
        D3D12_MESSAGE* message = (D3D12_MESSAGE*)storage;
        if (FAILED(ID3D12InfoQueue_GetMessage(backend.info_queue, i, message, &size)))
            continue;
        NvLogLevel level = (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION || message->Severity == D3D12_MESSAGE_SEVERITY_ERROR) ? NV_LOG_ERROR
                           : message->Severity == D3D12_MESSAGE_SEVERITY_WARNING ? NV_LOG_WARNING
                                                                                 : NV_LOG_INFO;
        nv_log(level, "d3d12", "%s", message->pDescription);
    }
    ID3D12InfoQueue_ClearStoredMessages(backend.info_queue);
}

// The machine as far as Windows tells: CPU, GPU and display.
internal void log_system(NvGpu* gpu)
{
    char system[256];
    nv_window_system_text(system, sizeof(system));
    nv_log(NV_LOG_INFO, "nv", "system: %s", system);

    DXGI_ADAPTER_DESC1 desc;
    IDXGIAdapter1_GetDesc1(backend.adapter, &desc);
    char name[128];
    nv_win32_utf8_from_wide(desc.Description, name, sizeof(name));
    nv_log(NV_LOG_INFO, "nv", "GPU: %s, vendor id 0x%04x, device id 0x%04x, %.0f MB of its own memory, feature level 12_2, shader model 6.6, pass timing (timestamp queries) %s",
           name, desc.VendorId, desc.DeviceId, (f64)desc.DedicatedVideoMemory / (1024.0 * 1024.0),
           gpu->has_timestamps ? "available" : "not available");

    f32 ratio = nv_window_pixel_ratio(gpu->window);
    nv_log(NV_LOG_INFO, "nv", "display: screen %dx%d, window %ux%u DIPs, pixel ratio %.2f, framebuffer %ux%u pixels",
           GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), (u32)((f32)gpu->width / ratio + 0.5f),
           (u32)((f32)gpu->height / ratio + 0.5f), (f64)ratio, gpu->width, gpu->height);
}

//
// The swapchain
//

// The swapchain's buffers as textures and views of the pools, so the frame draws into them like into any target. The buffers are
// BGRA8 and the view is the sRGB one, as on the web: a flip-model swapchain cannot be sRGB but its render target view can.
internal void make_back_buffers(void)
{
    NvGpu* gpu = backend.gpu;
    for (u32 i = 0; i < BACK_BUFFER_COUNT; ++i) {
        ID3D12Resource* resource = NULL;
        check(IDXGISwapChain3_GetBuffer(backend.swapchain, i, &IID_ID3D12Resource, (void**)&resource), "GetBuffer");
        set_name((ID3D12Object*)resource, "swapchain buffer");
        u32 generation;
        u32 index = pool_take(&texture_pool, &generation);
        Texture* texture = pool_item(&texture_pool, index, generation);
        texture->resource = resource;
        texture->desc = (NvGpuTextureDesc){
            .width = gpu->width, .height = gpu->height, .mip_count = 1, .samples = 1, .format = gpu->config_format,
            .usage = NV_GPU_TEXTURE_RENDER_TARGET,
        };
        backend.back_texture[i] = (NvGpuTexture){index, generation};
        backend.back_view[i] = create_view_as(gpu, backend.back_texture[i], NULL, gpu->surface_format);
    }
}

internal void release_back_buffers(void)
{
    for (u32 i = 0; i < BACK_BUFFER_COUNT; ++i) {
        nv_gpu_release_view(backend.gpu, backend.back_view[i]);
        ID3D12Resource_Release(texture_of(backend.back_texture[i])->resource);
        pool_give(&texture_pool, backend.back_texture[i].index, backend.back_texture[i].generation);
        backend.back_view[i] = (NvGpuView){0};
        backend.back_texture[i] = (NvGpuTexture){0};
    }
}

internal void log_swapchain(const NvGpu* gpu)
{
    nv_log(NV_LOG_INFO, "nv", "swapchain and color target: %ux%u, canvas format %s, render view %s, present mode Fifo (vsync)", gpu->width,
           gpu->height, nv_gpu_format_name(gpu->config_format), nv_gpu_format_name(gpu->surface_format));
}

internal void resize_swapchain(NvGpu* gpu, u32 width, u32 height)
{
    // The GPU may still be drawing into the buffers: this is the one place the backend waits for it (a resize is rare).
    wait_idle();
    process_deferred();
    release_back_buffers();
    gpu->width = width;
    gpu->height = height;
    if (width == 0 || height == 0)
        return;
    check(IDXGISwapChain3_ResizeBuffers(backend.swapchain, BACK_BUFFER_COUNT, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
    make_back_buffers();
    log_swapchain(gpu);
}

internal b32 create_swapchain(NvGpu* gpu)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {
        .Width = gpu->width,
        .Height = gpu->height,
        .Format = dxgi_format(gpu->config_format),
        .SampleDesc = {1, 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = BACK_BUFFER_COUNT,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
    };
    IDXGISwapChain1* swapchain1 = NULL;
    HRESULT result = IDXGIFactory6_CreateSwapChainForHwnd(backend.factory, (IUnknown*)backend.queue, (HWND)gpu->window->handle, &desc, NULL,
                                                          NULL, &swapchain1);
    if (FAILED(result)) {
        nv_log(NV_LOG_ERROR, "nv", "CreateSwapChainForHwnd failed (0x%08lx)", (unsigned long)result);
        return 0;
    }
    // The window is not switched to full screen by Alt+Enter: the program has no such mode.
    IDXGIFactory6_MakeWindowAssociation(backend.factory, (HWND)gpu->window->handle, DXGI_MWA_NO_ALT_ENTER);
    result = IDXGISwapChain1_QueryInterface(swapchain1, &IID_IDXGISwapChain3, (void**)&backend.swapchain);
    IDXGISwapChain1_Release(swapchain1);
    if (FAILED(result)) {
        nv_log(NV_LOG_ERROR, "nv", "no IDXGISwapChain3 (0x%08lx)", (unsigned long)result);
        return 0;
    }
    make_back_buffers();
    log_swapchain(gpu);
    return 1;
}

//
// The device's objects
//

internal ID3D12Resource* create_buffer_resource(D3D12_HEAP_TYPE heap_type, u64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                                                const char* label)
{
    D3D12_HEAP_PROPERTIES heap = {.Type = heap_type};
    D3D12_RESOURCE_DESC desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {1, 0},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = flags,
    };
    ID3D12Resource* resource = NULL;
    HRESULT result = ID3D12Device_CreateCommittedResource(backend.device, &heap, D3D12_HEAP_FLAG_NONE, &desc, state, NULL,
                                                          &IID_ID3D12Resource, (void**)&resource);
    check(result, "CreateCommittedResource (buffer)");
    set_name((ID3D12Object*)resource, label);
    return resource;
}

internal b32 create_objects(NvGpu* gpu)
{
    ID3D12Device* device = backend.device;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    check(ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&backend.queue), "CreateCommandQueue");
    check(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&backend.fence), "CreateFence");
    check(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&backend.flush_fence), "CreateFence");
    backend.fence_event = CreateEventW(NULL, FALSE, FALSE, NULL);

    // Two of everything the CPU writes while the GPU reads the other.
    for (u32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        FrameSlot* slot = &backend.slots[i];
        check(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                  (void**)&slot->upload_allocator), "CreateCommandAllocator");
        check(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                  (void**)&slot->main_allocator), "CreateCommandAllocator");
        slot->ring = create_buffer_resource(D3D12_HEAP_TYPE_UPLOAD, UPLOAD_RING_BYTES, D3D12_RESOURCE_FLAG_NONE,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, "upload ring");
        D3D12_RANGE nothing_read = {0, 0};
        check(ID3D12Resource_Map(slot->ring, 0, &nothing_read, (void**)&slot->ring_mapped), "Map (upload ring)");
    }
    FrameSlot* first = &backend.slots[0];
    check(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, first->upload_allocator, NULL,
                                         &IID_ID3D12GraphicsCommandList, (void**)&backend.upload_list), "CreateCommandList");
    check(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, first->main_allocator, NULL,
                                         &IID_ID3D12GraphicsCommandList, (void**)&backend.list), "CreateCommandList");
    // They are created recording; begin_slot starts them over, so close them first.
    ID3D12GraphicsCommandList_Close(backend.upload_list);
    ID3D12GraphicsCommandList_Close(backend.list);

    // The descriptor heaps.
    D3D12_DESCRIPTOR_HEAP_DESC view_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = VIEW_HEAP_DESCRIPTORS,
                                            .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_HEAP_DESC sampler_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, .NumDescriptors = SAMPLER_HEAP_DESCRIPTORS,
                                               .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .NumDescriptors = (UINT)view_pool.capacity};
    D3D12_DESCRIPTOR_HEAP_DESC dsv_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV, .NumDescriptors = (UINT)view_pool.capacity};
    check(ID3D12Device_CreateDescriptorHeap(device, &view_desc, &IID_ID3D12DescriptorHeap, (void**)&backend.view_heap), "CreateDescriptorHeap");
    check(ID3D12Device_CreateDescriptorHeap(device, &sampler_desc, &IID_ID3D12DescriptorHeap, (void**)&backend.sampler_heap), "CreateDescriptorHeap");
    check(ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void**)&backend.rtv_heap), "CreateDescriptorHeap");
    check(ID3D12Device_CreateDescriptorHeap(device, &dsv_desc, &IID_ID3D12DescriptorHeap, (void**)&backend.dsv_heap), "CreateDescriptorHeap");
    backend.view_increment = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    backend.sampler_increment = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    backend.rtv_increment = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    backend.dsv_increment = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    for (u32 i = 0; i < NV_ARRAY_COUNT(backend.free_blocks); ++i)
        backend.free_blocks[i] = ~0u;
    local_persist u32 free_next[VIEW_HEAP_DESCRIPTORS]; // the free lists' links, one for each descriptor
    backend.free_next = free_next;

    // The indirect draw and dispatch: their argument buffers have the layout of D3D12_DRAW_ARGUMENTS (vertex count, instance count,
    // first vertex, first instance, as WebGPU's) and D3D12_DISPATCH_ARGUMENTS.
    D3D12_INDIRECT_ARGUMENT_DESC draw_argument = {.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW};
    D3D12_COMMAND_SIGNATURE_DESC draw_desc = {.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS), .NumArgumentDescs = 1, .pArgumentDescs = &draw_argument};
    check(ID3D12Device_CreateCommandSignature(device, &draw_desc, NULL, &IID_ID3D12CommandSignature, (void**)&backend.draw_signature), "CreateCommandSignature");
    D3D12_INDIRECT_ARGUMENT_DESC dispatch_argument = {.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH};
    D3D12_COMMAND_SIGNATURE_DESC dispatch_desc = {.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS), .NumArgumentDescs = 1, .pArgumentDescs = &dispatch_argument};
    check(ID3D12Device_CreateCommandSignature(device, &dispatch_desc, NULL, &IID_ID3D12CommandSignature, (void**)&backend.dispatch_signature), "CreateCommandSignature");

    // Pass times: any direct queue can write timestamps.
    gpu->has_timestamps = 1;
    D3D12_QUERY_HEAP_DESC query_desc = {.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP, .Count = NV_GPU_MAX_TIMESTAMPS};
    check(ID3D12Device_CreateQueryHeap(device, &query_desc, &IID_ID3D12QueryHeap, (void**)&backend.timestamp_heap), "CreateQueryHeap");
    backend.timestamp_readback = create_buffer_resource(D3D12_HEAP_TYPE_READBACK, NV_GPU_MAX_TIMESTAMPS * sizeof(u64), D3D12_RESOURCE_FLAG_NONE,
                                                        D3D12_RESOURCE_STATE_COPY_DEST, "timestamp readback");
    D3D12_RANGE everything = {0, NV_GPU_MAX_TIMESTAMPS * sizeof(u64)};
    check(ID3D12Resource_Map(backend.timestamp_readback, 0, &everything, (void**)&backend.timestamp_mapped), "Map (timestamps)");
    check(ID3D12CommandQueue_GetTimestampFrequency(backend.queue, &backend.timestamp_frequency), "GetTimestampFrequency");
    return 1;
}

b32 nv_gpu_create(NvGpu* gpu, NvWindow* window)
{
    *gpu = (NvGpu){0};
    backend = (Backend){0};
    gpu->window = window;
    gpu->backend_name = "D3D12";
    gpu->max_buffer_bytes = SHARED_MAX_BUFFER_BYTES;
    gpu->max_storage_binding_bytes = SHARED_MAX_STORAGE_BINDING_BYTES;
    backend.gpu = gpu;

    open_device_factory();
    enable_debug_layer();
    // NOTE: Not DXGI_CREATE_FACTORY_DEBUG: that asks for the DXGI debug layer of the "Graphics Tools" Windows feature, which this program
    // does not need; the D3D12 debug layer comes with the Agility SDK (its d3d12SDKLayers.dll, in the D3D12 folder).
    HRESULT result = CreateDXGIFactory2(0, &IID_IDXGIFactory6, (void**)&backend.factory);
    if (FAILED(result)) {
        nv_log(NV_LOG_ERROR, "nv", "CreateDXGIFactory2 failed (0x%08lx)", (unsigned long)result);
        return 0;
    }

    char skipped[512];
    if (!pick_adapter(skipped, sizeof(skipped))) {
        nv_log(NV_LOG_ERROR, "nv", "no GPU runs this program: it needs Direct3D 12 feature level 12_2 and shader model 6.6");
        show_no_gpu_message(skipped);
        return 0;
    }
    if (!runtime_is_ours()) {
        show_no_gpu_message("The D3D12 runtime in the D3D12 folder beside the program could not be used.");
        return 0;
    }
    setup_info_queue();
    if (!create_objects(gpu))
        return 0;

    gpu->config_format = NV_GPU_FORMAT_BGRA8;
    gpu->surface_format = NV_GPU_FORMAT_BGRA8_SRGB;
    nv_window_framebuffer_size(window, &gpu->width, &gpu->height);
    if (gpu->width == 0 || gpu->height == 0) {
        // Created minimized: the swapchain needs a size, and the first frame resizes it.
        gpu->width = gpu->height = 1;
    }
    if (!create_swapchain(gpu))
        return 0;
    log_system(gpu);
    begin_slot(0);
    return 1;
}

//
// Buffers
//

NvGpuBuffer nv_gpu_create_buffer(NvGpu* gpu, const NvGpuBufferDesc* desc)
{
    (void)gpu;
    u64 size = round_up(desc->size, 4);
    if (desc->usage & NV_GPU_BUFFER_UNIFORM)
        size = round_up(size, 256); // a constant buffer view covers a multiple of 256 bytes
    D3D12_RESOURCE_FLAGS flags = (desc->usage & NV_GPU_BUFFER_STORAGE) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource* resource = create_buffer_resource(D3D12_HEAP_TYPE_DEFAULT, size, flags, D3D12_RESOURCE_STATE_COMMON, desc->label);
    u32 generation;
    u32 index = pool_take(&buffer_pool, &generation);
    Buffer* buffer = pool_item(&buffer_pool, index, generation);
    buffer->resource = resource;
    buffer->size = size;
    buffer->usage = desc->usage;
    buffer->address = ID3D12Resource_GetGPUVirtualAddress(resource);
    buffer->state = buffer->submitted_state = D3D12_RESOURCE_STATE_COMMON;
    NvGpuBuffer handle = {index, generation};
    if (desc->data) {
        // The bytes, then zeros up to the buffer's rounded size.
        u64 offset;
        u64 rounded = round_up(desc->size, 4);
        u8* staging = ring_alloc(rounded, 16, &offset);
        memcpy(staging, desc->data, (umm)desc->size);
        memset(staging + desc->size, 0, (umm)(rounded - desc->size));
        D3D12_RESOURCE_BARRIER before = transition_barrier(resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, buffer->submitted_state, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_RESOURCE_BARRIER after = transition_barrier(resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, buffer->submitted_state);
        ID3D12GraphicsCommandList_ResourceBarrier(backend.upload_list, 1, &before);
        ID3D12GraphicsCommandList_CopyBufferRegion(backend.upload_list, resource, 0, backend.slots[backend.slot].ring, offset, rounded);
        ID3D12GraphicsCommandList_ResourceBarrier(backend.upload_list, 1, &after);
    }
    return handle;
}

internal void destroy_buffer(NvGpuBuffer handle)
{
    defer_release((IUnknown*)buffer_of(handle)->resource);
    pool_give(&buffer_pool, handle.index, handle.generation);
}

// A bind group keeps what it binds alive: a UI preview made this frame may be drawn after the renderer released its texture.
void nv_gpu_release_buffer(NvGpu* gpu, NvGpuBuffer handle)
{
    (void)gpu;
    Buffer* buffer = buffer_of(handle);
    buffer->released = 1;
    if (buffer->group_references == 0)
        destroy_buffer(handle);
}

void nv_gpu_write_buffer(NvGpu* gpu, NvGpuBuffer handle, u64 offset, const void* bytes, umm size)
{
    (void)gpu;
    NV_ASSERT(size % 4 == 0);
    Buffer* buffer = buffer_of(handle);
    NV_ASSERT(offset + size <= buffer->size);
    const u8* source = bytes;
    while (size) {
        umm chunk = size < UPLOAD_RING_BYTES ? size : UPLOAD_RING_BYTES;
        u64 ring_offset;
        u8* staging = ring_alloc(chunk, 16, &ring_offset);
        memcpy(staging, source, chunk);
        // The writes run before the frame's commands, so the buffer is in the state the last frame left it.
        D3D12_RESOURCE_BARRIER before = transition_barrier(buffer->resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, buffer->submitted_state, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_RESOURCE_BARRIER after = transition_barrier(buffer->resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, buffer->submitted_state);
        ID3D12GraphicsCommandList_ResourceBarrier(backend.upload_list, 1, &before);
        ID3D12GraphicsCommandList_CopyBufferRegion(backend.upload_list, buffer->resource, offset, backend.slots[backend.slot].ring, ring_offset, chunk);
        ID3D12GraphicsCommandList_ResourceBarrier(backend.upload_list, 1, &after);
        source += chunk;
        offset += chunk;
        size -= chunk;
    }
}

//
// Textures
//

NvGpuTexture nv_gpu_create_texture(NvGpu* gpu, const NvGpuTextureDesc* desc)
{
    (void)gpu;
    NV_ASSERT(desc->width && desc->height && desc->format != NV_GPU_FORMAT_NONE);
    u32 mip_count = desc->mip_count ? desc->mip_count : 1;
    u32 samples = desc->samples ? desc->samples : 1;
    NV_ASSERT(mip_count <= MAX_MIPS && (samples == 1 || samples == 4) && (samples == 1 || mip_count == 1));

    b32 depth = is_depth_format(desc->format);
    b32 sampled = (desc->usage & NV_GPU_TEXTURE_SAMPLED) != 0;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    DXGI_FORMAT format = dxgi_format(desc->format);
    if (desc->usage & NV_GPU_TEXTURE_RENDER_TARGET)
        flags |= depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (depth) {
        // A depth texture only a pass writes is compact when no shader may read it (the web gives the sampling usage only while the
        // Textures tab shows it); one a shader reads is typeless, so the depth view and the shader view can differ.
        if (sampled)
            format = dxgi_typeless_depth(desc->format);
        else
            flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }
    D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width = desc->width,
        .Height = desc->height,
        .DepthOrArraySize = 1,
        .MipLevels = (UINT16)mip_count,
        .Format = format,
        .SampleDesc = {samples, 0},
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = flags,
    };
    ID3D12Resource* resource = NULL;
    check(ID3D12Device_CreateCommittedResource(backend.device, &heap, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                               &IID_ID3D12Resource, (void**)&resource), "CreateCommittedResource (texture)");
    set_name((ID3D12Object*)resource, desc->label);

    u32 generation;
    u32 index = pool_take(&texture_pool, &generation);
    Texture* texture = pool_item(&texture_pool, index, generation);
    texture->resource = resource;
    texture->desc = *desc;
    texture->desc.label = NULL;
    texture->desc.mip_count = mip_count;
    texture->desc.samples = samples;
    texture->sampled_depth = depth && sampled;
    return (NvGpuTexture){index, generation};
}

internal void destroy_texture(NvGpuTexture handle)
{
    defer_release((IUnknown*)texture_of(handle)->resource);
    pool_give(&texture_pool, handle.index, handle.generation);
}

void nv_gpu_release_texture(NvGpu* gpu, NvGpuTexture handle)
{
    (void)gpu;
    Texture* texture = texture_of(handle);
    texture->released = 1;
    if (texture->group_references == 0)
        destroy_texture(handle);
}

NvGpuTextureDesc nv_gpu_texture_desc(const NvGpu* gpu, NvGpuTexture handle)
{
    (void)gpu;
    return texture_of(handle)->desc;
}

void nv_gpu_write_texture(NvGpu* gpu, NvGpuTexture handle, u32 mip, u32 x, u32 y, u32 width, u32 height, const void* texels,
                          u32 bytes_per_row)
{
    (void)gpu;
    Texture* texture = texture_of(handle);
    NV_ASSERT(mip < texture->desc.mip_count && width && height);
    u32 texel_bytes = nv_gpu_format_bytes(texture->desc.format);
    u32 row_bytes = width * texel_bytes;
    u32 pitch = (u32)round_up(row_bytes, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    // As many rows at a time as the ring can take (a large texture is written in several pieces).
    u32 rows_per_piece = (u32)((UPLOAD_RING_BYTES / 2) / pitch);
    NV_ASSERT(rows_per_piece > 0);
    const u8* source = texels;
    for (u32 row = 0; row < height; row += rows_per_piece) {
        u32 rows = height - row < rows_per_piece ? height - row : rows_per_piece;
        u64 ring_offset;
        u8* staging = ring_alloc((u64)rows * pitch, UPLOAD_TEXTURE_ALIGNMENT, &ring_offset);
        for (u32 r = 0; r < rows; ++r)
            memcpy(staging + (umm)r * pitch, source + (umm)(row + r) * bytes_per_row, row_bytes);

        D3D12_TEXTURE_COPY_LOCATION destination = {.pResource = texture->resource, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                                                   .SubresourceIndex = mip};
        D3D12_TEXTURE_COPY_LOCATION origin = {.pResource = backend.slots[backend.slot].ring, .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        origin.PlacedFootprint.Offset = ring_offset;
        origin.PlacedFootprint.Footprint = (D3D12_SUBRESOURCE_FOOTPRINT){
            .Format = dxgi_format(texture->desc.format), .Width = width, .Height = rows, .Depth = 1, .RowPitch = pitch};
        D3D12_RESOURCE_BARRIER before = transition_barrier(texture->resource, mip, texture->submitted_state[mip], D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_RESOURCE_BARRIER after = transition_barrier(texture->resource, mip, D3D12_RESOURCE_STATE_COPY_DEST, texture->submitted_state[mip]);
        ID3D12GraphicsCommandList_ResourceBarrier(backend.upload_list, 1, &before);
        ID3D12GraphicsCommandList_CopyTextureRegion(backend.upload_list, &destination, x, y + row, 0, &origin, NULL);
        ID3D12GraphicsCommandList_ResourceBarrier(backend.upload_list, 1, &after);
    }
}

//
// Views and samplers
//

internal D3D12_CPU_DESCRIPTOR_HANDLE heap_cpu(ID3D12DescriptorHeap* heap, u32 increment, u32 index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = cpu_start(heap);
    handle.ptr += (SIZE_T)index * increment;
    return handle;
}

internal NvGpuView create_view_as(NvGpu* gpu, NvGpuTexture texture_handle, const NvGpuViewDesc* desc, NvGpuFormat format)
{
    (void)gpu;
    Texture* texture = texture_of(texture_handle);
    u32 base_mip = desc ? desc->base_mip : 0;
    u32 mip_count = desc && desc->mip_count ? desc->mip_count : texture->desc.mip_count - base_mip;
    NV_ASSERT(base_mip + mip_count <= texture->desc.mip_count);
    u32 generation;
    u32 index = pool_take(&view_pool, &generation);
    View* view = pool_item(&view_pool, index, generation);
    view->texture = texture_handle;
    view->base_mip = base_mip;
    view->mip_count = mip_count;
    view->depth_only = desc && desc->depth_only;
    view->format = format;

    // A texture a pass draws into has its render target (or depth) view made now, in the heap's place of the same number.
    if (texture->desc.usage & NV_GPU_TEXTURE_RENDER_TARGET) {
        b32 multisampled = texture->desc.samples > 1;
        if (is_depth_format(texture->desc.format)) {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {.Format = dxgi_format(format), .ViewDimension = multisampled ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D};
            if (!multisampled)
                dsv.Texture2D.MipSlice = base_mip;
            ID3D12Device_CreateDepthStencilView(backend.device, texture->resource, &dsv, heap_cpu(backend.dsv_heap, backend.dsv_increment, index));
        } else {
            D3D12_RENDER_TARGET_VIEW_DESC rtv = {.Format = dxgi_format(format), .ViewDimension = multisampled ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D};
            if (!multisampled)
                rtv.Texture2D.MipSlice = base_mip;
            ID3D12Device_CreateRenderTargetView(backend.device, texture->resource, &rtv, heap_cpu(backend.rtv_heap, backend.rtv_increment, index));
        }
    }
    return (NvGpuView){index, generation};
}

NvGpuView nv_gpu_create_view(NvGpu* gpu, NvGpuTexture texture, const NvGpuViewDesc* desc)
{
    return create_view_as(gpu, texture, desc, texture_of(texture)->desc.format);
}

void nv_gpu_release_view(NvGpu* gpu, NvGpuView handle)
{
    (void)gpu;
    view_of(handle); // asserts on a stale handle
    pool_give(&view_pool, handle.index, handle.generation);
}

NvGpuSampler nv_gpu_create_sampler(NvGpu* gpu, const NvGpuSamplerDesc* desc)
{
    (void)gpu;
    D3D12_SAMPLER_DESC sampler_desc = {
        .Filter = desc->compare ? (desc->mip_linear ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT)
                                : (desc->mip_linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT),
        .AddressU = desc->repeat ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .AddressV = desc->repeat ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        .MaxAnisotropy = 1,
        .ComparisonFunc = desc->compare ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_NEVER, // 1 where the point is not behind the map's depth
        .MinLOD = 0.0f,
        .MaxLOD = D3D12_FLOAT32_MAX,
    };
    u32 generation;
    u32 index = pool_take(&sampler_pool, &generation);
    NV_ASSERT(index < SAMPLER_HEAP_DESCRIPTORS);
    ((Sampler*)pool_item(&sampler_pool, index, generation))->descriptor = index;
    ID3D12Device_CreateSampler(backend.device, &sampler_desc, heap_cpu(backend.sampler_heap, backend.sampler_increment, index));
    return (NvGpuSampler){index, generation};
}

void nv_gpu_release_sampler(NvGpu* gpu, NvGpuSampler handle)
{
    (void)gpu;
    pool_item(&sampler_pool, handle.index, handle.generation);
    pool_give(&sampler_pool, handle.index, handle.generation);
}

//
// Bind groups
//

NvGpuLayout nv_gpu_create_layout(NvGpu* gpu, const NvGpuLayoutEntry* entries, u32 count)
{
    (void)gpu;
    NV_ASSERT(count <= NV_GPU_MAX_ENTRIES);
    u32 generation;
    u32 index = pool_take(&layout_pool, &generation);
    Layout* layout = pool_item(&layout_pool, index, generation);
    layout->count = count;
    memcpy(layout->entries, entries, count * sizeof(entries[0]));
    for (u32 i = 0; i < count; ++i) {
        const NvGpuLayoutEntry* entry = &entries[i];
        if (entry->kind == NV_GPU_BINDING_SAMPLER || entry->kind == NV_GPU_BINDING_SAMPLER_COMPARISON)
            ++layout->sampler_count;
        else if (entry->kind == NV_GPU_BINDING_UNIFORM && entry->dynamic_offset)
            ++layout->dynamic_count;
        else
            ++layout->table_count;
        if (entry->kind == NV_GPU_BINDING_STORAGE_READ || entry->kind == NV_GPU_BINDING_STORAGE_READ_WRITE)
            NV_ASSERT(entry->stride > 0); // a structured buffer's view needs the size of an element
    }
    NV_ASSERT(layout->sampler_count <= 1 && layout->dynamic_count <= 2); // what the groups hold
    layout->block_size = layout->table_count ? block_class(layout->table_count) : 0;
    return (NvGpuLayout){index, generation};
}

void nv_gpu_release_layout(NvGpu* gpu, NvGpuLayout handle)
{
    (void)gpu;
    layout_of(handle);
    pool_give(&layout_pool, handle.index, handle.generation);
}

internal const NvGpuGroupEntry* find_group_entry(const NvGpuGroupEntry* entries, u32 count, u32 binding)
{
    for (u32 i = 0; i < count; ++i) {
        if (entries[i].binding == binding)
            return &entries[i];
    }
    NV_ASSERT(!"a bind group has no entry for a binding of its layout");
    return NULL;
}

internal D3D12_SHADER_RESOURCE_VIEW_DESC texture_srv_desc(const View* view, const Texture* texture, NvGpuBindingKind kind)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
    b32 depth = kind == NV_GPU_BINDING_TEXTURE_DEPTH || kind == NV_GPU_BINDING_TEXTURE_DEPTH_MS;
    if (depth) {
        NV_ASSERT(texture->sampled_depth && is_depth_format(texture->desc.format));
        srv.Format = dxgi_depth_shader_format(texture->desc.format);
    } else {
        NV_ASSERT(!is_depth_format(texture->desc.format));
        srv.Format = dxgi_format(view->format);
    }
    if (kind == NV_GPU_BINDING_TEXTURE_DEPTH_MS) {
        NV_ASSERT(texture->desc.samples > 1);
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    } else {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = view->base_mip;
        srv.Texture2D.MipLevels = view->mip_count;
    }
    return srv;
}

NvGpuGroup nv_gpu_create_group(NvGpu* gpu, NvGpuLayout layout_handle, const NvGpuGroupEntry* entries, u32 count)
{
    (void)gpu;
    Layout* layout = layout_of(layout_handle);
    u32 generation;
    u32 index = pool_take(&group_pool, &generation);
    Group* group = pool_item(&group_pool, index, generation);
    group->layout = layout_handle;
    group->block = ~0u;
    group->sampler_descriptor = ~0u;
    if (layout->table_count) {
        group->block_class = layout->block_size;
        group->block = block_alloc(layout->block_size);
    }

    u32 position = 0; // in the table
    u32 dynamic = 0;
    for (u32 i = 0; i < layout->count; ++i) {
        const NvGpuLayoutEntry* entry = &layout->entries[i];
        const NvGpuGroupEntry* given = find_group_entry(entries, count, entry->binding);
        switch (entry->kind) {
        case NV_GPU_BINDING_SAMPLER:
        case NV_GPU_BINDING_SAMPLER_COMPARISON: {
            Sampler* sampler = pool_item(&sampler_pool, given->sampler.index, given->sampler.generation);
            group->sampler_descriptor = sampler->descriptor;
            break;
        }
        case NV_GPU_BINDING_UNIFORM: {
            Buffer* buffer = buffer_of(given->buffer);
            NV_ASSERT(given->size <= buffer->size);
            if (entry->dynamic_offset) {
                group->dynamic_buffer[dynamic] = given->buffer;
                group->dynamic_size[dynamic] = given->size;
                ++dynamic;
            } else {
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {.BufferLocation = buffer->address, .SizeInBytes = (UINT)round_up(given->size, 256)};
                NV_ASSERT(cbv.SizeInBytes <= buffer->size);
                ID3D12Device_CreateConstantBufferView(backend.device, &cbv, view_cpu(group->block + position));
                group->uses[group->use_count++] = (GroupUse){.buffer = given->buffer, .state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
                ++position;
            }
            break;
        }
        case NV_GPU_BINDING_STORAGE_READ: {
            Buffer* buffer = buffer_of(given->buffer);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = {
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            };
            srv.Buffer.NumElements = (UINT)(given->size / entry->stride);
            srv.Buffer.StructureByteStride = entry->stride;
            ID3D12Device_CreateShaderResourceView(backend.device, buffer->resource, &srv, view_cpu(group->block + position));
            group->uses[group->use_count++] = (GroupUse){.buffer = given->buffer, .state = STATE_BUFFER_READ_SHADER};
            ++position;
            break;
        }
        case NV_GPU_BINDING_STORAGE_READ_WRITE: {
            Buffer* buffer = buffer_of(given->buffer);
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {.Format = DXGI_FORMAT_UNKNOWN, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER};
            uav.Buffer.NumElements = (UINT)(given->size / entry->stride);
            uav.Buffer.StructureByteStride = entry->stride;
            ID3D12Device_CreateUnorderedAccessView(backend.device, buffer->resource, NULL, &uav, view_cpu(group->block + position));
            group->uses[group->use_count++] = (GroupUse){.buffer = given->buffer, .state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
            ++position;
            break;
        }
        case NV_GPU_BINDING_TEXTURE_FLOAT:
        case NV_GPU_BINDING_TEXTURE_DEPTH:
        case NV_GPU_BINDING_TEXTURE_DEPTH_MS: {
            View* view = view_of(given->view);
            Texture* texture = texture_of(view->texture);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = texture_srv_desc(view, texture, entry->kind);
            ID3D12Device_CreateShaderResourceView(backend.device, texture->resource, &srv, view_cpu(group->block + position));
            group->uses[group->use_count++] = (GroupUse){.is_texture = 1, .texture = view->texture, .base_mip = view->base_mip,
                                                         .mip_count = entry->kind == NV_GPU_BINDING_TEXTURE_DEPTH_MS ? 1 : view->mip_count,
                                                         .state = STATE_TEXTURE_READ_SHADER};
            ++position;
            break;
        }
        }
    }
    NV_ASSERT(position == layout->table_count && dynamic == layout->dynamic_count);
    // A dynamic uniform buffer is read at the offset the group is set with: its barrier is the buffer's, like the table's.
    for (u32 k = 0; k < dynamic; ++k)
        group->uses[group->use_count++] = (GroupUse){.buffer = group->dynamic_buffer[k], .state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
    for (u32 i = 0; i < group->use_count; ++i) {
        const GroupUse* use = &group->uses[i];
        if (use->is_texture)
            ++texture_of(use->texture)->group_references;
        else
            ++buffer_of(use->buffer)->group_references;
    }
    return (NvGpuGroup){index, generation};
}

void nv_gpu_release_group(NvGpu* gpu, NvGpuGroup handle)
{
    (void)gpu;
    Group* group = group_of(handle);
    if (group->block != ~0u)
        defer_block(group->block, group->block_class);
    for (u32 i = 0; i < group->use_count; ++i) {
        const GroupUse* use = &group->uses[i];
        if (use->is_texture) {
            Texture* texture = texture_of(use->texture);
            if (--texture->group_references == 0 && texture->released)
                destroy_texture(use->texture);
        } else {
            Buffer* buffer = buffer_of(use->buffer);
            if (--buffer->group_references == 0 && buffer->released)
                destroy_buffer(use->buffer);
        }
    }
    pool_give(&group_pool, handle.index, handle.generation);
}

//
// Root signatures and pipelines
//

internal D3D12_DESCRIPTOR_RANGE_TYPE range_type(NvGpuBindingKind kind)
{
    switch (kind) {
    case NV_GPU_BINDING_UNIFORM:                return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    case NV_GPU_BINDING_STORAGE_READ_WRITE:     return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    case NV_GPU_BINDING_SAMPLER:
    case NV_GPU_BINDING_SAMPLER_COMPARISON:     return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    default:                                    return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    }
}

// The root signature of pipelines that take `layouts` as their groups: group g is register space g, as a table of its buffers and
// textures, a table of its sampler and a root constant buffer for each uniform buffer given with an offset. A graphics pipeline
// has one more root parameter at the end, the first instance (engine/shaders/renderer_frame.slang). Pipelines of the same layouts
// share one.
internal u32 get_root_signature(const NvGpuLayout* layouts, u32 layout_count, b32 is_compute)
{
    NV_ASSERT(layout_count > 0 && layout_count <= NV_GPU_MAX_LAYOUTS);
    for (u32 i = 0; i < backend.root_signature_count; ++i) {
        RootSignature* cached = &backend.root_signatures[i];
        if (cached->is_compute != is_compute || cached->layout_count != layout_count)
            continue;
        b32 same = 1;
        for (u32 g = 0; g < layout_count; ++g)
            same = same && cached->layouts[g].index == layouts[g].index && cached->layouts[g].generation == layouts[g].generation;
        if (same)
            return i;
    }
    NV_ASSERT(backend.root_signature_count < MAX_ROOT_SIGNATURES);
    u32 slot = backend.root_signature_count++;
    RootSignature* rs = &backend.root_signatures[slot];
    rs->is_compute = is_compute;
    rs->layout_count = layout_count;
    memcpy(rs->layouts, layouts, layout_count * sizeof(layouts[0]));
    rs->first_instance_parameter = ~0u;

    D3D12_ROOT_PARAMETER1 parameters[NV_GPU_MAX_LAYOUTS * 4 + 1] = {0};
    D3D12_DESCRIPTOR_RANGE1 table_ranges[NV_GPU_MAX_LAYOUTS][NV_GPU_MAX_ENTRIES];
    D3D12_DESCRIPTOR_RANGE1 sampler_ranges[NV_GPU_MAX_LAYOUTS];
    u32 parameter_count = 0;
    for (u32 g = 0; g < layout_count; ++g) {
        Layout* layout = layout_of(layouts[g]);
        rs->table_parameter[g] = rs->sampler_parameter[g] = ~0u;
        rs->dynamic_parameter[g][0] = rs->dynamic_parameter[g][1] = ~0u;
        u32 table = 0;
        u32 dynamic = 0;
        for (u32 i = 0; i < layout->count; ++i) {
            const NvGpuLayoutEntry* entry = &layout->entries[i];
            D3D12_DESCRIPTOR_RANGE_TYPE type = range_type(entry->kind);
            if (type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER) {
                sampler_ranges[g] = (D3D12_DESCRIPTOR_RANGE1){.RangeType = type, .NumDescriptors = 1, .BaseShaderRegister = entry->binding,
                                                              .RegisterSpace = g, .OffsetInDescriptorsFromTableStart = 0};
            } else if (entry->kind == NV_GPU_BINDING_UNIFORM && entry->dynamic_offset) {
                rs->dynamic_parameter[g][dynamic++] = parameter_count;
                D3D12_ROOT_PARAMETER1* parameter = &parameters[parameter_count++];
                parameter->ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                parameter->Descriptor = (D3D12_ROOT_DESCRIPTOR1){.ShaderRegister = entry->binding, .RegisterSpace = g};
                parameter->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            } else {
                table_ranges[g][table] = (D3D12_DESCRIPTOR_RANGE1){.RangeType = type, .NumDescriptors = 1, .BaseShaderRegister = entry->binding,
                                                                   .RegisterSpace = g, .OffsetInDescriptorsFromTableStart = table};
                ++table;
            }
        }
        if (table) {
            rs->table_parameter[g] = parameter_count;
            D3D12_ROOT_PARAMETER1* parameter = &parameters[parameter_count++];
            parameter->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter->DescriptorTable = (D3D12_ROOT_DESCRIPTOR_TABLE1){.NumDescriptorRanges = table, .pDescriptorRanges = table_ranges[g]};
            parameter->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        if (layout->sampler_count) {
            rs->sampler_parameter[g] = parameter_count;
            D3D12_ROOT_PARAMETER1* parameter = &parameters[parameter_count++];
            parameter->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter->DescriptorTable = (D3D12_ROOT_DESCRIPTOR_TABLE1){.NumDescriptorRanges = 1, .pDescriptorRanges = &sampler_ranges[g]};
            parameter->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
    }
    if (!is_compute) {
        rs->first_instance_parameter = parameter_count;
        D3D12_ROOT_PARAMETER1* parameter = &parameters[parameter_count++];
        parameter->ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter->Constants = (D3D12_ROOT_CONSTANTS){.ShaderRegister = 0, .RegisterSpace = FIRST_INSTANCE_SPACE, .Num32BitValues = 1};
        parameter->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {.Version = D3D_ROOT_SIGNATURE_VERSION_1_1};
    desc.Desc_1_1.NumParameters = parameter_count;
    desc.Desc_1_1.pParameters = parameters;
    desc.Desc_1_1.Flags = is_compute ? D3D12_ROOT_SIGNATURE_FLAG_NONE : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* blob = NULL;
    ID3DBlob* errors = NULL;
    HRESULT result = D3D12SerializeVersionedRootSignature(&desc, &blob, &errors);
    if (FAILED(result)) {
        nv_log(NV_LOG_ERROR, "d3d12", "root signature: %s", errors ? (const char*)ID3D10Blob_GetBufferPointer(errors) : "serializing failed");
        NV_ASSERT(!"a root signature did not serialize");
    }
    check(ID3D12Device_CreateRootSignature(backend.device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                           &IID_ID3D12RootSignature, (void**)&rs->root_signature), "CreateRootSignature");
    ID3D10Blob_Release(blob);
    if (errors)
        ID3D10Blob_Release(errors);
    return slot;
}

// The DXIL of one entry point of a shader.
internal D3D12_SHADER_BYTECODE shader_entry(const NvGpuShaderCode* shader, const char* name)
{
    for (u32 i = 0; i < shader->entry_count; ++i) {
        if (strcmp(shader->entries[i].name, name) == 0)
            return (D3D12_SHADER_BYTECODE){.pShaderBytecode = shader->entries[i].bytes, .BytecodeLength = shader->entries[i].size};
    }
    nv_log(NV_LOG_ERROR, "d3d12", "a shader has no entry point named %s", name);
    NV_ASSERT(!"a shader has no such entry point");
    return (D3D12_SHADER_BYTECODE){0};
}

internal DXGI_FORMAT vertex_format(NvGpuVertexFormat format)
{
    switch (format) {
    case NV_GPU_VERTEX_FLOAT2:   return DXGI_FORMAT_R32G32_FLOAT;
    case NV_GPU_VERTEX_FLOAT3:   return DXGI_FORMAT_R32G32B32_FLOAT;
    case NV_GPU_VERTEX_FLOAT4:   return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case NV_GPU_VERTEX_UINT16X4: return DXGI_FORMAT_R16G16B16A16_UINT;
    case NV_GPU_VERTEX_UNORM8X4: return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    NV_INVALID_CODE_PATH;
    return DXGI_FORMAT_UNKNOWN;
}

internal D3D12_COMPARISON_FUNC comparison(NvGpuCompare compare)
{
    switch (compare) {
    case NV_GPU_COMPARE_ALWAYS:  return D3D12_COMPARISON_FUNC_ALWAYS;
    case NV_GPU_COMPARE_LESS:    return D3D12_COMPARISON_FUNC_LESS;
    case NV_GPU_COMPARE_GREATER: return D3D12_COMPARISON_FUNC_GREATER;
    }
    NV_INVALID_CODE_PATH;
    return D3D12_COMPARISON_FUNC_ALWAYS;
}

internal D3D12_RENDER_TARGET_BLEND_DESC blend_desc(NvGpuBlend blend)
{
    D3D12_RENDER_TARGET_BLEND_DESC desc = {
        .SrcBlend = D3D12_BLEND_ONE, .DestBlend = D3D12_BLEND_ZERO, .BlendOp = D3D12_BLEND_OP_ADD,
        .SrcBlendAlpha = D3D12_BLEND_ONE, .DestBlendAlpha = D3D12_BLEND_ZERO, .BlendOpAlpha = D3D12_BLEND_OP_ADD,
        .LogicOp = D3D12_LOGIC_OP_NOOP, .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
    };
    switch (blend) {
    case NV_GPU_BLEND_NONE:
        break;
    case NV_GPU_BLEND_ALPHA:
        desc.BlendEnable = TRUE;
        desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case NV_GPU_BLEND_ADD:
        desc.BlendEnable = TRUE;
        desc.SrcBlend = D3D12_BLEND_ONE;
        desc.DestBlend = D3D12_BLEND_ONE;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_ONE;
        break;
    case NV_GPU_BLEND_ADD_KEEP_ALPHA:
        desc.BlendEnable = TRUE;
        desc.SrcBlend = D3D12_BLEND_ONE;
        desc.DestBlend = D3D12_BLEND_ONE;
        desc.SrcBlendAlpha = D3D12_BLEND_ZERO;
        desc.DestBlendAlpha = D3D12_BLEND_ONE;
        break;
    }
    return desc;
}

NvGpuPipeline nv_gpu_create_render_pipeline(NvGpu* gpu, const NvGpuRenderPipelineDesc* desc)
{
    (void)gpu;
    u32 root_signature = get_root_signature(desc->layouts, desc->layout_count, 0);

    D3D12_INPUT_ELEMENT_DESC elements[NV_GPU_MAX_ATTRIBUTES];
    NV_ASSERT(desc->attribute_count <= NV_GPU_MAX_ATTRIBUTES);
    for (u32 i = 0; i < desc->attribute_count; ++i) {
        // Slang's HLSL names every vertex input TEXCOORD<location>.
        elements[i] = (D3D12_INPUT_ELEMENT_DESC){
            .SemanticName = "TEXCOORD",
            .SemanticIndex = desc->attributes[i].location,
            .Format = vertex_format(desc->attributes[i].format),
            .InputSlot = 0,
            .AlignedByteOffset = desc->attributes[i].offset,
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
        };
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {0};
    pso.pRootSignature = backend.root_signatures[root_signature].root_signature;
    pso.VS = shader_entry(desc->shader, desc->vertex_entry);
    if (desc->fragment_entry)
        pso.PS = shader_entry(desc->shader, desc->fragment_entry);
    pso.BlendState.RenderTarget[0] = blend_desc(desc->blend);
    pso.SampleMask = UINT_MAX;
    // NOTE: Thin lines (the debug lines) are 1-pixel aliased lines. WebGPU's, on the baseline's Vulkan, are antialiased by the samples
    // at one pixel's width, and D3D12's MultisampleEnable makes antialiased quads 1.4 pixels wide: neither matches, and the aliased
    // ones are the closer (docs/specs/d3d12_port.md, "Found").
    pso.RasterizerState = (D3D12_RASTERIZER_DESC){
        .FillMode = D3D12_FILL_MODE_SOLID,
        .CullMode = desc->cull == NV_GPU_CULL_BACK ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE,
        .FrontCounterClockwise = TRUE,
        .DepthBias = desc->depth_bias,
        .DepthBiasClamp = 0.0f,
        .SlopeScaledDepthBias = desc->depth_bias_slope,
        .DepthClipEnable = TRUE,
    };
    if (desc->depth_format != NV_GPU_FORMAT_NONE) {
        pso.DepthStencilState.DepthEnable = TRUE;
        pso.DepthStencilState.DepthWriteMask = desc->depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.DepthFunc = comparison(desc->depth_compare);
        pso.DSVFormat = dxgi_format(desc->depth_format);
    }
    pso.InputLayout = (D3D12_INPUT_LAYOUT_DESC){.pInputElementDescs = desc->vertex_stride ? elements : NULL,
                                                .NumElements = desc->vertex_stride ? desc->attribute_count : 0};
    pso.PrimitiveTopologyType = desc->topology == NV_GPU_TOPOLOGY_LINES ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    if (desc->color_format != NV_GPU_FORMAT_NONE) {
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = dxgi_format(desc->color_format);
    }
    pso.SampleDesc = (DXGI_SAMPLE_DESC){desc->samples ? desc->samples : 1, 0};

    u32 generation;
    u32 index = pool_take(&pipeline_pool, &generation);
    Pipeline* pipeline = pool_item(&pipeline_pool, index, generation);
    pipeline->root_signature = root_signature;
    pipeline->topology = desc->topology == NV_GPU_TOPOLOGY_LINES ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    pipeline->vertex_stride = desc->vertex_stride;
    check(ID3D12Device_CreateGraphicsPipelineState(backend.device, &pso, &IID_ID3D12PipelineState, (void**)&pipeline->state),
          "CreateGraphicsPipelineState");
    set_name((ID3D12Object*)pipeline->state, desc->label);
    return (NvGpuPipeline){index, generation};
}

NvGpuPipeline nv_gpu_create_compute_pipeline(NvGpu* gpu, const NvGpuComputePipelineDesc* desc)
{
    (void)gpu;
    u32 root_signature = get_root_signature(desc->layouts, desc->layout_count, 1);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {0};
    pso.pRootSignature = backend.root_signatures[root_signature].root_signature;
    pso.CS = shader_entry(desc->shader, desc->entry);
    u32 generation;
    u32 index = pool_take(&pipeline_pool, &generation);
    Pipeline* pipeline = pool_item(&pipeline_pool, index, generation);
    pipeline->is_compute = 1;
    pipeline->root_signature = root_signature;
    check(ID3D12Device_CreateComputePipelineState(backend.device, &pso, &IID_ID3D12PipelineState, (void**)&pipeline->state),
          "CreateComputePipelineState");
    set_name((ID3D12Object*)pipeline->state, desc->label);
    return (NvGpuPipeline){index, generation};
}

void nv_gpu_release_pipeline(NvGpu* gpu, NvGpuPipeline handle)
{
    (void)gpu;
    defer_release((IUnknown*)pipeline_of(handle)->state);
    pool_give(&pipeline_pool, handle.index, handle.generation);
}

//
// Frames and passes
//

NvGpuFrame nv_gpu_begin_frame(NvGpu* gpu)
{
    NV_ASSERT(!backend.in_frame);
    if (backend.device_lost)
        return (NvGpuFrame){0};

    u32 width = 0;
    u32 height = 0;
    nv_window_framebuffer_size(gpu->window, &width, &height);
    if (width == 0 || height == 0) {
        if (backend.back_texture[0].index) {
            wait_idle();
            release_back_buffers();
        }
        gpu->width = width;
        gpu->height = height;
        return (NvGpuFrame){0}; // minimized
    }
    if (!backend.back_texture[0].index || width != gpu->width || height != gpu->height) {
        if (backend.back_texture[0].index) {
            resize_swapchain(gpu, width, height);
        } else {
            gpu->width = width;
            gpu->height = height;
            check(IDXGISwapChain3_ResizeBuffers(backend.swapchain, BACK_BUFFER_COUNT, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
            make_back_buffers();
            log_swapchain(gpu);
        }
    }
    backend.back_index = IDXGISwapChain3_GetCurrentBackBufferIndex(backend.swapchain);
    backend.in_frame = 1;
    return (NvGpuFrame){.ok = 1, .view = backend.back_view[backend.back_index]};
}

#if !defined(NDEBUG)
void nv_gpu_debug_screenshot(NvGpu* gpu, const char* path, u32 x, u32 y, u32 width, u32 height)
{
    NV_ASSERT(!backend.screenshot.pending);
    ScreenshotRequest* request = &backend.screenshot;
    snprintf(request->path, sizeof(request->path), "%s", path);
    request->x = x;
    request->y = y;
    request->width = width ? width : gpu->width;
    request->height = height ? height : gpu->height;
    request->pending = 1;
}

b32 nv_gpu_debug_screenshot_pending(const NvGpu* gpu)
{
    (void)gpu;
    return backend.screenshot.pending;
}

// The canvas goes to a readback buffer at the end of the frame's commands, before it is presented.
internal void record_screenshot_copy(void)
{
    ScreenshotRequest* request = &backend.screenshot;
    NvGpu* gpu = backend.gpu;
    NV_ASSERT(request->x + request->width <= gpu->width && request->y + request->height <= gpu->height);
    request->pitch = (u32)round_up(gpu->width * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    request->readback = create_buffer_resource(D3D12_HEAP_TYPE_READBACK, (u64)request->pitch * gpu->height, D3D12_RESOURCE_FLAG_NONE,
                                               D3D12_RESOURCE_STATE_COPY_DEST, "screenshot");
    NvGpuTexture canvas = backend.back_texture[backend.back_index];
    transition_texture(canvas, 0, 0, D3D12_RESOURCE_STATE_COPY_SOURCE);
    flush_barriers();
    D3D12_TEXTURE_COPY_LOCATION destination = {.pResource = request->readback, .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    destination.PlacedFootprint.Footprint = (D3D12_SUBRESOURCE_FOOTPRINT){
        .Format = dxgi_format(gpu->config_format), .Width = gpu->width, .Height = gpu->height, .Depth = 1, .RowPitch = request->pitch};
    D3D12_TEXTURE_COPY_LOCATION source = {.pResource = texture_of(canvas)->resource, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    ID3D12GraphicsCommandList_CopyTextureRegion(backend.list, &destination, 0, 0, 0, &source, NULL);
}

// The frame has been submitted: waits for it, then writes the part asked for as a PNG (the canvas is BGRA).
internal void write_screenshot(void)
{
    ScreenshotRequest* request = &backend.screenshot;
    wait_idle();
    u8* mapped = NULL;
    D3D12_RANGE everything = {0, (SIZE_T)request->pitch * backend.gpu->height};
    check(ID3D12Resource_Map(request->readback, 0, &everything, (void**)&mapped), "Map (screenshot)");
    umm bytes = (umm)request->width * request->height * 4;
    local_persist u8 rgba[NV_MEGABYTES(64)];
    NV_ASSERT(bytes <= sizeof(rgba));
    for (u32 row = 0; row < request->height; ++row) {
        const u8* in = mapped + (umm)(request->y + row) * request->pitch + (umm)request->x * 4;
        u8* out = rgba + (umm)row * request->width * 4;
        for (u32 column = 0; column < request->width; ++column) {
            out[column * 4 + 0] = in[column * 4 + 2];
            out[column * 4 + 1] = in[column * 4 + 1];
            out[column * 4 + 2] = in[column * 4 + 0];
            out[column * 4 + 3] = 255;
        }
    }
    D3D12_RANGE nothing_written = {0, 0};
    ID3D12Resource_Unmap(request->readback, 0, &nothing_written);
    ID3D12Resource_Release(request->readback);
    nv_image_write_png(request->path, rgba, request->width, request->height);
    nv_log(NV_LOG_INFO, "nv", "screenshot %s (%ux%u)", request->path, request->width, request->height);
    request->readback = NULL;
    request->pending = 0;
}
#endif

void nv_gpu_end_frame(NvGpu* gpu)
{
    (void)gpu;
    NV_ASSERT(backend.in_frame && !backend.in_render_pass && !backend.in_compute_pass);
#if !defined(NDEBUG)
    if (backend.screenshot.pending)
        record_screenshot_copy();
#endif
    transition_texture(backend.back_texture[backend.back_index], 0, 0, D3D12_RESOURCE_STATE_PRESENT);
    if (backend.timestamp_count) {
        // A slot no pass wrote this frame reads as the time of now, so a pass that did not run takes no time (the web reads a 0).
        for (u32 i = 0; i < backend.timestamp_count; ++i) {
            if (!(backend.timestamp_written & (1u << i)))
                ID3D12GraphicsCommandList_EndQuery(backend.list, backend.timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, i);
        }
        ID3D12GraphicsCommandList_ResolveQueryData(backend.list, backend.timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, backend.timestamp_count,
                                                   backend.timestamp_readback, 0);
    }
    backend.timestamp_written = 0;
    backend.in_frame = 0;
    submit(1);
#if !defined(NDEBUG)
    if (backend.screenshot.pending)
        write_screenshot();
#endif
    drain_info_queue();
}

internal void begin_event(const char* label)
{
    if (label)
        ID3D12GraphicsCommandList_BeginEvent(backend.list, 1, label, (UINT)strlen(label) + 1); // 1: PIX's ANSI event
}

internal void write_timestamp(b32 wanted, u32 slot)
{
    if (!wanted || !backend.gpu->has_timestamps)
        return;
    NV_ASSERT(slot < NV_GPU_MAX_TIMESTAMPS);
    ID3D12GraphicsCommandList_EndQuery(backend.list, backend.timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, slot);
    backend.timestamp_written |= 1u << slot;
}

internal D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle(NvGpuView handle)
{
    return heap_cpu(backend.rtv_heap, backend.rtv_increment, handle.index);
}

internal D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle(NvGpuView handle)
{
    return heap_cpu(backend.dsv_heap, backend.dsv_increment, handle.index);
}

void nv_gpu_begin_render_pass(NvGpu* gpu, const NvGpuRenderPassDesc* desc)
{
    (void)gpu;
    NV_ASSERT(!backend.in_render_pass && !backend.in_compute_pass);
    backend.pass = *desc;
    begin_event(desc->label);

    D3D12_CPU_DESCRIPTOR_HANDLE color_handle = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE depth_handle = {0};
    if (desc->color.index) {
        View* view = view_of(desc->color);
        NV_ASSERT(view->mip_count == 1);
        transition_texture(view->texture, view->base_mip, 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
        color_handle = rtv_handle(desc->color);
        const NvGpuTextureDesc* texture = &texture_of(view->texture)->desc;
        backend.pass_width = texture->width >> view->base_mip ? texture->width >> view->base_mip : 1;
        backend.pass_height = texture->height >> view->base_mip ? texture->height >> view->base_mip : 1;
    }
    if (desc->depth.index) {
        View* view = view_of(desc->depth);
        transition_texture(view->texture, view->base_mip, 1, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        depth_handle = dsv_handle(desc->depth);
        const NvGpuTextureDesc* texture = &texture_of(view->texture)->desc;
        backend.pass_width = texture->width >> view->base_mip ? texture->width >> view->base_mip : 1;
        backend.pass_height = texture->height >> view->base_mip ? texture->height >> view->base_mip : 1;
    }
    flush_barriers();
    if (desc->color.index && desc->color_load == NV_GPU_LOAD_CLEAR)
        ID3D12GraphicsCommandList_ClearRenderTargetView(backend.list, color_handle, desc->clear_color, 0, NULL);
    if (desc->depth.index && desc->depth_load == NV_GPU_LOAD_CLEAR)
        ID3D12GraphicsCommandList_ClearDepthStencilView(backend.list, depth_handle, D3D12_CLEAR_FLAG_DEPTH, desc->clear_depth, 0, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(backend.list, desc->color.index ? 1 : 0, desc->color.index ? &color_handle : NULL, FALSE,
                                                 desc->depth.index ? &depth_handle : NULL);
    // As in WebGPU, a pass starts with the whole attachment as viewport and scissor.
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, (f32)backend.pass_width, (f32)backend.pass_height, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, (LONG)backend.pass_width, (LONG)backend.pass_height};
    ID3D12GraphicsCommandList_RSSetViewports(backend.list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(backend.list, 1, &scissor);
    write_timestamp(desc->timestamps, desc->timestamp_begin);

    backend.in_render_pass = 1;
    backend.pipeline = (NvGpuPipeline){0};
    memset(backend.bound, 0, sizeof(backend.bound));
    backend.vertex_buffer = (NvGpuBuffer){0};
    backend.index_buffer = (NvGpuBuffer){0};
    backend.first_instance_valid = 0;
}

void nv_gpu_begin_compute_pass(NvGpu* gpu, const NvGpuComputePassDesc* desc)
{
    (void)gpu;
    NV_ASSERT(!backend.in_render_pass && !backend.in_compute_pass);
    begin_event(desc->label);
    write_timestamp(desc->timestamps, desc->timestamp_begin);
    backend.pass_timed = desc->timestamps;
    backend.pass_timestamp_end = desc->timestamp_end;
    backend.in_compute_pass = 1;
    backend.pipeline = (NvGpuPipeline){0};
    memset(backend.bound, 0, sizeof(backend.bound));
}

void nv_gpu_end_pass(NvGpu* gpu)
{
    (void)gpu;
    if (backend.in_render_pass) {
        const NvGpuRenderPassDesc* desc = &backend.pass;
        if (desc->resolve.index) {
            // The multisampled color is resolved into the single-sample one at the end of the pass.
            View* source = view_of(desc->color);
            View* target = view_of(desc->resolve);
            transition_texture(source->texture, source->base_mip, 1, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            transition_texture(target->texture, target->base_mip, 1, D3D12_RESOURCE_STATE_RESOLVE_DEST);
            flush_barriers();
            ID3D12GraphicsCommandList_ResolveSubresource(backend.list, texture_of(target->texture)->resource, target->base_mip,
                                                         texture_of(source->texture)->resource, source->base_mip,
                                                         dxgi_format(texture_of(source->texture)->desc.format));
        }
        write_timestamp(desc->timestamps, desc->timestamp_end);
        backend.in_render_pass = 0;
    } else {
        NV_ASSERT(backend.in_compute_pass);
        write_timestamp(backend.pass_timed, backend.pass_timestamp_end);
        backend.in_compute_pass = 0;
    }
    ID3D12GraphicsCommandList_EndEvent(backend.list);
}

//
// Commands inside a pass
//

void nv_gpu_set_pipeline(NvGpu* gpu, NvGpuPipeline handle)
{
    (void)gpu;
    Pipeline* pipeline = pipeline_of(handle);
    NV_ASSERT(pipeline->is_compute == backend.in_compute_pass && (backend.in_render_pass || backend.in_compute_pass));
    backend.pipeline = handle;
    if (backend.root_signature_in_use != pipeline->root_signature + 1) {
        RootSignature* rs = &backend.root_signatures[pipeline->root_signature];
        if (pipeline->is_compute)
            ID3D12GraphicsCommandList_SetComputeRootSignature(backend.list, rs->root_signature);
        else
            ID3D12GraphicsCommandList_SetGraphicsRootSignature(backend.list, rs->root_signature);
        backend.root_signature_in_use = pipeline->root_signature + 1;
        // Changing the root signature unbinds everything the old one had.
        for (u32 i = 0; i < NV_GPU_MAX_LAYOUTS; ++i)
            backend.bound[i].dirty = backend.bound[i].group.index != 0;
        backend.first_instance_valid = 0;
    }
    ID3D12GraphicsCommandList_SetPipelineState(backend.list, pipeline->state);
    if (!pipeline->is_compute) {
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(backend.list, pipeline->topology);
        backend.vertex_buffer_dirty = backend.vertex_buffer.index != 0; // the stride is the pipeline's
    }
}

void nv_gpu_set_group(NvGpu* gpu, u32 index, NvGpuGroup group, const u32* dynamic_offsets, u32 dynamic_count)
{
    (void)gpu;
    NV_ASSERT(index < NV_GPU_MAX_LAYOUTS && dynamic_count <= 2);
    group_of(group); // asserts on a stale handle
    BoundGroup* bound = &backend.bound[index];
    bound->group = group;
    bound->dynamic_count = dynamic_count;
    for (u32 i = 0; i < dynamic_count; ++i)
        bound->dynamic_offsets[i] = dynamic_offsets[i];
    bound->dirty = 1;
    bound->checked_epoch = ~0ull;
}

void nv_gpu_set_vertex_buffer(NvGpu* gpu, NvGpuBuffer buffer)
{
    (void)gpu;
    NV_ASSERT(backend.in_render_pass);
    buffer_of(buffer);
    backend.vertex_buffer = buffer;
    backend.vertex_buffer_dirty = 1;
}

void nv_gpu_set_index_buffer(NvGpu* gpu, NvGpuBuffer buffer, b32 is_32_bit)
{
    (void)gpu;
    NV_ASSERT(backend.in_render_pass);
    buffer_of(buffer);
    backend.index_buffer = buffer;
    backend.index_32_bit = is_32_bit;
    backend.index_buffer_dirty = 1;
}

void nv_gpu_set_viewport(NvGpu* gpu, f32 x, f32 y, f32 width, f32 height)
{
    (void)gpu;
    NV_ASSERT(backend.in_render_pass);
    D3D12_VIEWPORT viewport = {x, y, width, height, 0.0f, 1.0f};
    ID3D12GraphicsCommandList_RSSetViewports(backend.list, 1, &viewport);
}

void nv_gpu_set_scissor(NvGpu* gpu, u32 x, u32 y, u32 width, u32 height)
{
    (void)gpu;
    NV_ASSERT(backend.in_render_pass);
    D3D12_RECT scissor = {(LONG)x, (LONG)y, (LONG)(x + width), (LONG)(y + height)};
    ID3D12GraphicsCommandList_RSSetScissorRects(backend.list, 1, &scissor);
}

// What a draw or a dispatch needs before it: the root arguments of the groups set since the root signature, the barriers of
// everything they bind and of the vertex and index buffers, and those buffers set on the list.
internal void prepare_command(b32 compute)
{
    NV_ASSERT(backend.pipeline.index);
    Pipeline* pipeline = pipeline_of(backend.pipeline);
    RootSignature* rs = &backend.root_signatures[pipeline->root_signature];

    // The barriers come first: the debug layer checks the state of a resource where the root argument names it (the descriptors
    // are declared static while set), so a table set before its resources are in their states is a validation error.
    for (u32 g = 0; g < rs->layout_count; ++g) {
        BoundGroup* bound = &backend.bound[g];
        NV_ASSERT(bound->group.index); // every group of the pipeline's layouts is set
        Group* group = group_of(bound->group);
        NV_ASSERT(group->layout.index == rs->layouts[g].index && group->layout.generation == rs->layouts[g].generation);
        if (bound->checked_epoch == backend.state_epoch)
            continue;
        for (u32 i = 0; i < group->use_count; ++i) {
            const GroupUse* use = &group->uses[i];
            if (use->is_texture)
                transition_texture(use->texture, use->base_mip, use->mip_count, use->state);
            else
                transition_buffer(use->buffer, use->state);
        }
    }
    if (!compute) {
        if (backend.vertex_buffer.index)
            transition_buffer(backend.vertex_buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        if (backend.index_buffer.index)
            transition_buffer(backend.index_buffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
    flush_barriers();
    for (u32 g = 0; g < rs->layout_count; ++g)
        backend.bound[g].checked_epoch = backend.state_epoch;

    for (u32 g = 0; g < rs->layout_count; ++g) {
        BoundGroup* bound = &backend.bound[g];
        if (!bound->dirty)
            continue;
        Group* group = group_of(bound->group);
        if (rs->table_parameter[g] != ~0u) {
            D3D12_GPU_DESCRIPTOR_HANDLE table = view_gpu(group->block);
            if (compute)
                ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(backend.list, rs->table_parameter[g], table);
            else
                ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(backend.list, rs->table_parameter[g], table);
        }
        if (rs->sampler_parameter[g] != ~0u) {
            D3D12_GPU_DESCRIPTOR_HANDLE table = sampler_gpu(group->sampler_descriptor);
            if (compute)
                ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(backend.list, rs->sampler_parameter[g], table);
            else
                ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(backend.list, rs->sampler_parameter[g], table);
        }
        for (u32 k = 0; k < bound->dynamic_count; ++k) {
            NV_ASSERT(rs->dynamic_parameter[g][k] != ~0u);
            D3D12_GPU_VIRTUAL_ADDRESS address = buffer_of(group->dynamic_buffer[k])->address + bound->dynamic_offsets[k];
            if (compute)
                ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(backend.list, rs->dynamic_parameter[g][k], address);
            else
                ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(backend.list, rs->dynamic_parameter[g][k], address);
        }
        bound->dirty = 0;
    }

    if (!compute) {
        if (backend.vertex_buffer.index && backend.vertex_buffer_dirty) {
            Buffer* buffer = buffer_of(backend.vertex_buffer);
            D3D12_VERTEX_BUFFER_VIEW view = {.BufferLocation = buffer->address, .SizeInBytes = (UINT)buffer->size, .StrideInBytes = pipeline->vertex_stride};
            ID3D12GraphicsCommandList_IASetVertexBuffers(backend.list, 0, 1, &view);
            backend.vertex_buffer_dirty = 0;
        }
        if (backend.index_buffer.index && backend.index_buffer_dirty) {
            Buffer* buffer = buffer_of(backend.index_buffer);
            D3D12_INDEX_BUFFER_VIEW view = {.BufferLocation = buffer->address, .SizeInBytes = (UINT)buffer->size,
                                            .Format = backend.index_32_bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT};
            ID3D12GraphicsCommandList_IASetIndexBuffer(backend.list, &view);
            backend.index_buffer_dirty = 0;
        }
    }
}

// The first instance goes to the shaders as a root constant (it is not part of SV_InstanceID in D3D12).
internal void set_first_instance(u32 value)
{
    RootSignature* rs = &backend.root_signatures[pipeline_of(backend.pipeline)->root_signature];
    if (backend.first_instance_valid && backend.first_instance == value)
        return;
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(backend.list, rs->first_instance_parameter, value, 0);
    backend.first_instance = value;
    backend.first_instance_valid = 1;
}

void nv_gpu_draw(NvGpu* gpu, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance)
{
    (void)gpu;
    NV_ASSERT(backend.in_render_pass);
    prepare_command(0);
    set_first_instance(first_instance);
    flush_barriers();
    ID3D12GraphicsCommandList_DrawInstanced(backend.list, vertex_count, instance_count, first_vertex, 0);
}

void nv_gpu_draw_indexed(NvGpu* gpu, u32 index_count, u32 first_index, s32 base_vertex, u32 first_instance)
{
    (void)gpu;
    NV_ASSERT(backend.in_render_pass && backend.index_buffer.index);
    prepare_command(0);
    set_first_instance(first_instance);
    flush_barriers();
    ID3D12GraphicsCommandList_DrawIndexedInstanced(backend.list, index_count, 1, first_index, base_vertex, 0);
}

void nv_gpu_draw_indirect(NvGpu* gpu, NvGpuBuffer buffer, u64 offset)
{
    (void)gpu;
    NV_ASSERT(backend.in_render_pass);
    prepare_command(0);
    transition_buffer(buffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    flush_barriers();
    ID3D12GraphicsCommandList_ExecuteIndirect(backend.list, backend.draw_signature, 1, buffer_of(buffer)->resource, offset, NULL, 0);
}

// A dispatch sees what the one before it wrote: a UAV barrier between them (WebGPU's dispatches in a pass are ordered so).
internal void uav_barrier_if_needed(void)
{
    if (backend.needs_uav_barrier) {
        D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV};
        queue_barrier(barrier);
        backend.needs_uav_barrier = 0;
    }
}

void nv_gpu_dispatch(NvGpu* gpu, u32 x, u32 y, u32 z)
{
    (void)gpu;
    NV_ASSERT(backend.in_compute_pass);
    prepare_command(1);
    uav_barrier_if_needed();
    flush_barriers();
    ID3D12GraphicsCommandList_Dispatch(backend.list, x, y, z);
    backend.needs_uav_barrier = 1;
}

void nv_gpu_dispatch_indirect(NvGpu* gpu, NvGpuBuffer buffer, u64 offset)
{
    (void)gpu;
    NV_ASSERT(backend.in_compute_pass);
    prepare_command(1);
    transition_buffer(buffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    uav_barrier_if_needed();
    flush_barriers();
    ID3D12GraphicsCommandList_ExecuteIndirect(backend.list, backend.dispatch_signature, 1, buffer_of(buffer)->resource, offset, NULL, 0);
    backend.needs_uav_barrier = 1;
}

//
// Reading back
//

b32 nv_gpu_resolve_timestamps(NvGpu* gpu, u32 count)
{
    (void)gpu;
    NV_ASSERT(count > 0 && count <= NV_GPU_MAX_TIMESTAMPS);
    if (backend.timestamp_state != READBACK_IDLE)
        return 0;
    backend.timestamp_count = count;
    return 1;
}

NvGpuTimestamps nv_gpu_poll_timestamps(NvGpu* gpu)
{
    (void)gpu;
    if (backend.timestamp_state != READBACK_PENDING || ID3D12Fence_GetCompletedValue(backend.fence) < backend.timestamp_fence)
        return (NvGpuTimestamps){0};
    backend.timestamp_state = READBACK_IDLE;
    NvGpuTimestamps result = {.ok = 1};
    for (u32 i = 0; i < NV_GPU_MAX_TIMESTAMPS; ++i)
        result.nanoseconds[i] = (u64)((f64)backend.timestamp_mapped[i] * 1.0e9 / (f64)backend.timestamp_frequency);
    return result;
}

b32 nv_gpu_copy_for_readback(NvGpu* gpu, u32 slot, NvGpuBuffer source, u64 offset, umm size)
{
    (void)gpu;
    NV_ASSERT(slot < NV_GPU_MAX_READBACKS && !backend.in_render_pass && !backend.in_compute_pass);
    Readback* readback = &backend.readbacks[slot];
    if (readback->state != READBACK_IDLE && readback->state != READBACK_TAKEN)
        return 0;
    readback->state = READBACK_IDLE;
    u64 rounded = round_up(size, 4);
    if (rounded > readback->capacity) {
        if (readback->resource)
            defer_release((IUnknown*)readback->resource);
        readback->resource = create_buffer_resource(D3D12_HEAP_TYPE_READBACK, rounded, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "readback");
        readback->capacity = rounded;
        D3D12_RANGE everything = {0, (SIZE_T)rounded};
        void* mapped = NULL;
        check(ID3D12Resource_Map(readback->resource, 0, &everything, &mapped), "Map (readback)");
        readback->mapped = mapped;
    }
    readback->size = (umm)rounded;

    transition_buffer(source, D3D12_RESOURCE_STATE_COPY_SOURCE);
    flush_barriers();
    ID3D12GraphicsCommandList_CopyBufferRegion(backend.list, readback->resource, 0, buffer_of(source)->resource, offset, rounded);
    readback->state = READBACK_COPIED;
    if (!backend.in_frame) {
        // Outside a frame the copy is submitted at once.
        submit(0);
    }
    return 1;
}

NvGpuReadback nv_gpu_poll_readback(NvGpu* gpu, u32 slot)
{
    (void)gpu;
    NV_ASSERT(slot < NV_GPU_MAX_READBACKS);
    Readback* readback = &backend.readbacks[slot];
    if (readback->state != READBACK_PENDING || ID3D12Fence_GetCompletedValue(backend.fence) < readback->fence_value)
        return (NvGpuReadback){0};
    readback->state = READBACK_TAKEN;
    return (NvGpuReadback){.ok = 1, .bytes = readback->mapped, .size = readback->size};
}

// Not implemented on this backend yet (the headless Linux renderer is its first user): texture
// readback on Windows arrives with the first Windows caller. Copying a texture into a readback
// buffer is the shape it takes, as nv_gpu_debug_screenshot's does.
b32 nv_gpu_copy_texture_for_readback(NvGpu* gpu, u32 slot, NvGpuTexture texture)
{
    (void)gpu, (void)slot, (void)texture;
    NV_ASSERT(!"nv_gpu_copy_texture_for_readback is not implemented on the D3D12 backend");
    return 0;
}

#if !defined(NDEBUG)
void nv_gpu_debug_raise_error(NvGpu* gpu)
{
    (void)gpu;
    // A buffer with no size is not valid: the debug layer says so, as the browser's validation does for the same kind of mistake.
    D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = 0, .Height = 1, .DepthOrArraySize = 1, .MipLevels = 1,
                                .SampleDesc = {1, 0}, .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource* resource = NULL;
    if (SUCCEEDED(ID3D12Device_CreateCommittedResource(backend.device, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                                       &IID_ID3D12Resource, (void**)&resource)))
        ID3D12Resource_Release(resource);
}
#endif
