#pragma once

#include <stdbool.h>
#include <webgpu/webgpu.h>

typedef struct EngWindow EngWindow;

/* Owns the WebGPU instance, adapter, device, queue and the window surface. */
typedef struct EngGpu {
    EngWindow* window;
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surface_format;
    uint32_t width;
    uint32_t height;

    WGPUTexture current_texture;
    WGPUTextureView current_view;
} EngGpu;

bool eng_gpu_create(EngGpu* gpu, EngWindow* window);
void eng_gpu_destroy(EngGpu* gpu);

/* Reconfigures the surface when the framebuffer size changed. */
void eng_gpu_resize(EngGpu* gpu, uint32_t width, uint32_t height);

/* Acquires the next swapchain texture view. Returns NULL if the frame should be skipped. */
WGPUTextureView eng_gpu_begin_frame(EngGpu* gpu);
void eng_gpu_end_frame(EngGpu* gpu);
