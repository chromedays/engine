#include "engine/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>

// The window on Linux is no window at all (headless): a fixed framebuffer size the GPU backend
// renders into offscreen textures. The size comes from NV_HEADLESS_WIDTH and NV_HEADLESS_HEIGHT
// when they say a size, and is 1920x1080 otherwise.

global u32 framebuffer_width;
global u32 framebuffer_height;

internal u32 size_from_env(const char* name, u32 fallback)
{
    const char* value = getenv(name);
    if (!value)
        return fallback;
    long parsed = strtol(value, NULL, 10);
    return parsed > 0 ? (u32)parsed : fallback;
}

void nv_window_create(NvWindow* window, const char* title)
{
    (void)title; // no window to title
    window->canvas_selector = NULL;
    window->handle = NULL;
    window->pixel_ratio = 1.0f;
    framebuffer_width = size_from_env("NV_HEADLESS_WIDTH", 1920);
    framebuffer_height = size_from_env("NV_HEADLESS_HEIGHT", 1080);
}

void nv_window_framebuffer_size(NvWindow* window, u32* width, u32* height)
{
    window->pixel_ratio = 1.0f;
    *width = framebuffer_width;
    *height = framebuffer_height;
}

void nv_window_download_text(char* out, umm capacity)
{
    if (capacity)
        out[0] = 0; // nothing was downloaded: the executable is the program
}

b32 nv_window_is_mac(void)
{
    return 0;
}

void nv_window_system_text(char* out, umm capacity)
{
    struct utsname name;
    if (uname(&name) == 0)
        snprintf(out, capacity, "Linux %s (%s), headless", name.release, name.machine);
    else
        snprintf(out, capacity, "Linux, headless");
}

f64 nv_time_seconds(void)
{
    local_persist struct timespec first;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!first.tv_sec && !first.tv_nsec)
        first = now;
    return (f64)(now.tv_sec - first.tv_sec) + (f64)(now.tv_nsec - first.tv_nsec) / 1000000000.0;
}

void nv_window_on_hidden(NvWindow* window, void (*hidden)(void* userdata), void* userdata)
{
    // Nothing ever hides: the callback is kept, as the header promises, and never called.
    window->on_hidden = hidden;
    window->on_hidden_data = userdata;
}

// NOTE: A deliberate break with this function's contract, which is to never return: headless a run
// is a fixed number of frames (NV_HEADLESS_FRAMES, 300 by default), then control comes back so the
// program can finish what it made and exit.
void nv_window_run(NvWindow* window, NvFrameFn frame, void* userdata)
{
    (void)window;
    u32 frames = size_from_env("NV_HEADLESS_FRAMES", 300);
    for (u32 i = 0; i < frames; ++i)
        frame(userdata);
}
