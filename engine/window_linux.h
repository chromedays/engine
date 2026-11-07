#pragma once

// Engine-private: the Wayland objects of the window window_linux.c made, which gpu_webgpu.c turns
// into its surface. App code does not include this header.

#include "engine/window.h"

// The window's wl_display and wl_surface, as void pointers so this header needs no Wayland header.
// False when the window was not made (or not by window_linux.c).
b32 nv_window_linux_wayland(NvWindow* window, void** display, void** surface);
