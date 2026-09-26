#pragma once

#include <webgpu/webgpu.h>

typedef struct EngWindow EngWindow;
typedef struct GLFWwindow GLFWwindow;

/* Creates a WebGPU surface for the window (GLFW native handles, or the HTML canvas on the web). */
WGPUSurface eng_create_surface(WGPUInstance instance, const EngWindow* window);

#if defined(__APPLE__)
/* Implemented in surface_metal.m; returns a CAMetalLayer attached to the window. */
void* eng_get_or_create_metal_layer(GLFWwindow* window);
#endif
