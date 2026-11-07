#pragma once

// Engine-private: the Wayland objects of the window window_linux.c made, which gpu_webgpu.c turns
// into its surface, and the fixed size the visual test asks for. App code does not include this header.

#include "engine/window.h"

// The window's wl_display and wl_surface, as void pointers so this header needs no Wayland header.
// False when the window was not made (or not by window_linux.c).
b32 nv_window_linux_wayland(NvWindow* window, void** display, void** surface);

// From now on the framebuffer is `width` x `height` pixels at a pixel ratio of 1, whatever size and scale the compositor
// configures: for pictures compared pixel for pixel (tests/visual/visual_test.c), however the program was started. Call it right
// after nv_window_create. NV_WINDOW_WIDTH and NV_WINDOW_HEIGHT fix the size the same way by hand.
void nv_window_linux_set_fixed_size(NvWindow* window, u32 width, u32 height);
