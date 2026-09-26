#pragma once

#include <webgpu/webgpu.h>

typedef struct GLFWwindow GLFWwindow;

/* Creates a WebGPU surface for a GLFW window using the platform's native handles. */
WGPUSurface eng_create_glfw_surface(WGPUInstance instance, GLFWwindow* window);

#if defined(__APPLE__)
/* Implemented in glfw_surface_metal.m; returns a CAMetalLayer attached to the window. */
void* eng_get_or_create_metal_layer(GLFWwindow* window);
#endif
