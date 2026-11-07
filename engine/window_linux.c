// The Wayland headers come before the engine's: engine/base.h defines `global` as a macro, and
// struct wl_registry_listener has a field of that name (as windows.h has with `internal`).
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

#include "engine/window_linux.h"

#include "engine/log.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>

// The window on Linux is a Wayland window (docs/specs/visual_regression.md): an xdg_toplevel whose
// surface Dawn presents into. It does what the visual regression test needs and no more:
//
//   - The framebuffer size is fixed: NV_WINDOW_WIDTH and NV_WINDOW_HEIGHT when they say a size,
//     1280x720 otherwise. The compositor's configured size is
//     acknowledged, not obeyed, so a tiling compositor may stretch how the window looks while the
//     pixels drawn stay exactly the framebuffer's. A program that wants resizing wants more than
//     this backend has.
//   - No input: no seat is bound, so pointer and keyboard never arrive (the ImGui platform stays
//     the headless one). The one event that matters, the toplevel's close, ends the run.
//   - nv_window_run returns after nv_quit (headless's documented break with the same contract),
//     so a test program can report its result as the process's exit code.

global struct wl_display* wayland_display;
global struct wl_surface* wayland_surface;
global struct wl_compositor* wayland_compositor;
global struct xdg_wm_base* wayland_xdg_base;
global struct xdg_surface* wayland_xdg_surface;
global struct xdg_toplevel* wayland_toplevel;
global b32 wayland_configured; // the first xdg_surface.configure was acked: buffers may follow
global b32 close_asked;
global b32 quit_asked;
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

internal void registry_global(void* data, struct wl_registry* registry, u32 name, const char* interface, u32 version)
{
    (void)data;
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        wayland_compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
        wayland_xdg_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version < 2 ? version : 2);
}

internal void registry_global_remove(void* data, struct wl_registry* registry, u32 name)
{
    (void)data, (void)registry, (void)name; // nothing we bound goes away in a run this short
}

global const struct wl_registry_listener registry_listener = {registry_global, registry_global_remove};

internal void xdg_base_ping(void* data, struct xdg_wm_base* base, u32 serial)
{
    (void)data;
    xdg_wm_base_pong(base, serial);
}

global const struct xdg_wm_base_listener xdg_base_listener = {xdg_base_ping};

internal void xdg_surface_configure(void* data, struct xdg_surface* surface, u32 serial)
{
    (void)data;
    xdg_surface_ack_configure(surface, serial);
    wayland_configured = 1;
}

global const struct xdg_surface_listener xdg_surface_listener = {xdg_surface_configure};

// The size in a toplevel configure is the compositor's wish; the framebuffer size is ours (above).
internal void toplevel_configure(void* data, struct xdg_toplevel* toplevel, s32 width, s32 height, struct wl_array* states)
{
    (void)data, (void)toplevel, (void)width, (void)height, (void)states;
}

internal void toplevel_close(void* data, struct xdg_toplevel* toplevel)
{
    (void)data, (void)toplevel;
    close_asked = 1;
}

internal void toplevel_configure_bounds(void* data, struct xdg_toplevel* toplevel, s32 width, s32 height)
{
    (void)data, (void)toplevel, (void)width, (void)height;
}

internal void toplevel_wm_capabilities(void* data, struct xdg_toplevel* toplevel, struct wl_array* capabilities)
{
    (void)data, (void)toplevel, (void)capabilities;
}

global const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure, toplevel_close, toplevel_configure_bounds, toplevel_wm_capabilities};

internal void fail(const char* what)
{
    nv_log(NV_LOG_ERROR, "nv", "failed to create the Wayland window: %s", what);
    exit(1); // no window, no program: the caller of nv_window_create has nothing to fall back to
}

void nv_window_create(NvWindow* window, const char* title)
{
    window->canvas_selector = NULL;
    window->handle = NULL;
    window->pixel_ratio = 1.0f;
    framebuffer_width = size_from_env("NV_WINDOW_WIDTH", 1280);
    framebuffer_height = size_from_env("NV_WINDOW_HEIGHT", 720);

    wayland_display = wl_display_connect(NULL);
    if (!wayland_display)
        fail("no Wayland compositor to connect to (WAYLAND_DISPLAY)");
    struct wl_registry* registry = wl_display_get_registry(wayland_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wayland_display); // the globals are bound when this returns
    if (!wayland_compositor)
        fail("the compositor offers no wl_compositor");
    if (!wayland_xdg_base)
        fail("the compositor offers no xdg_wm_base (xdg-shell)");
    xdg_wm_base_add_listener(wayland_xdg_base, &xdg_base_listener, NULL);

    wayland_surface = wl_compositor_create_surface(wayland_compositor);
    wayland_xdg_surface = xdg_wm_base_get_xdg_surface(wayland_xdg_base, wayland_surface);
    xdg_surface_add_listener(wayland_xdg_surface, &xdg_surface_listener, NULL);
    wayland_toplevel = xdg_surface_get_toplevel(wayland_xdg_surface);
    xdg_toplevel_add_listener(wayland_toplevel, &toplevel_listener, NULL);
    xdg_toplevel_set_app_id(wayland_toplevel, title);
    xdg_toplevel_set_title(wayland_toplevel, title);
    wl_surface_commit(wayland_surface);
    // The protocol asks for the first configure to be acked before a buffer is attached; Dawn
    // attaches on the first present, which is after this.
    while (!wayland_configured) {
        if (wl_display_roundtrip(wayland_display) < 0)
            fail("the compositor closed the connection before the first configure");
    }
    nv_log(NV_LOG_INFO, "nv", "Wayland window: %ux%u framebuffer (fixed), app id \"%s\"", framebuffer_width,
           framebuffer_height, title);
}

b32 nv_window_linux_wayland(NvWindow* window, void** display, void** surface)
{
    (void)window; // one window per process, as the other backends keep it
    *display = wayland_display;
    *surface = wayland_surface;
    return wayland_display && wayland_surface;
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

void nv_window_system_text(char* out, umm capacity)
{
    struct utsname name;
    if (uname(&name) == 0)
        snprintf(out, capacity, "Linux %s (%s), Wayland", name.release, name.machine);
    else
        snprintf(out, capacity, "Linux, Wayland");
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
    // Kept and called when the run ends, as a closing window does; nothing hides it before that.
    window->on_hidden = hidden;
    window->on_hidden_data = userdata;
}

// One non-blocking turn of the compositor's event queue: the close event is all that is read.
internal void pump_events(void)
{
    while (wl_display_prepare_read(wayland_display) != 0)
        wl_display_dispatch_pending(wayland_display);
    wl_display_flush(wayland_display);
    struct pollfd polled = {.fd = wl_display_get_fd(wayland_display), .events = POLLIN};
    if (poll(&polled, 1, 0) > 0 && (polled.revents & POLLIN))
        wl_display_read_events(wayland_display);
    else
        wl_display_cancel_read(wayland_display);
    wl_display_dispatch_pending(wayland_display);
}

// NOTE: The headless backend's break with this function's contract, which is to never return:
// the loop runs until nv_quit or the window's close, then control comes back so the program can
// finish what it made and exit with its own code.
void nv_window_run(NvWindow* window, NvFrameFn frame, void* userdata)
{
    while (!quit_asked && !close_asked) {
        pump_events();
        if (quit_asked || close_asked)
            break;
        frame(userdata);
        wl_display_flush(wayland_display); // the present's commit is in the queue: send it now
    }
    if (window->on_hidden)
        window->on_hidden(window->on_hidden_data);
}

void nv_quit(void)
{
    quit_asked = 1;
}
