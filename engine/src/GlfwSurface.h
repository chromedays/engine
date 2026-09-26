#pragma once

#include <webgpu/webgpu.h>

struct GLFWwindow;

namespace engine {

// Creates a WebGPU surface for a GLFW window using the platform's native handles.
WGPUSurface createGlfwSurface(WGPUInstance instance, GLFWwindow* window);

#if defined(__APPLE__)
// Implemented in GlfwSurfaceMetal.mm; returns a CAMetalLayer attached to the window.
void* getOrCreateMetalLayer(GLFWwindow* window);
#endif

} // namespace engine
