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

// The window on Linux is a Wayland window (docs/specs/linux_wayland.md, "창"): an xdg_toplevel whose surface Dawn presents into.
//
//   - Its size is the compositor's (xdg_toplevel.configure, in surface pixels, which are the "CSS pixels" of window.h); when the
//     compositor leaves it to the window, 1600x900 at first (as on Windows) and the last size after that.
//   - The framebuffer is that size times the surface's preferred integer scale (wl_surface.preferred_buffer_scale), which is the
//     pixel ratio; the buffer scale told to the compositor is the same.
//   - A fixed size (nv_window_linux_set_fixed_size, or NV_WINDOW_WIDTH and NV_WINDOW_HEIGHT) is a framebuffer of exactly that
//     many pixels at scale 1, whatever the compositor configures, for pictures compared pixel for pixel (the visual test).
//   - The hidden callback runs when the window stops being the active one, when it is suspended (another workspace, minimized)
//     and when it closes; a suspended window draws nothing until it is back.
//   - No input here: that is imgui_linux.c's.
//   - nv_window_run returns after nv_quit or the close (headless's documented break with the same contract), so a program can
//     report its result as the process's exit code.

global struct wl_display* wayland_display;
global struct wl_surface* wayland_surface;
global struct wl_compositor* wayland_compositor;
global struct xdg_wm_base* wayland_xdg_base;
global struct xdg_surface* wayland_xdg_surface;
global struct xdg_toplevel* wayland_toplevel;
global b32 wayland_configured; // the first xdg_surface.configure was acked: buffers may follow
global b32 close_asked;
global b32 quit_asked;
// The frame callback asked for with the last frame: NULL once the compositor answered (it wants the next frame).
global struct wl_callback* frame_callback;
global NvWindow* the_window; // one window per process, as the other backends keep it
// The window's size in surface pixels: the compositor's, applied when its configure is acked.
global u32 window_width;
global u32 window_height;
// The last toplevel configure, waiting for its xdg_surface.configure.
global s32 configured_width;
global s32 configured_height;
global b32 configured_activated;
global b32 configured_suspended;
global b32 activated;
global b32 suspended;
global u32 preferred_scale = 1; // wl_surface.preferred_buffer_scale
global u32 buffer_scale = 1;    // the one told to the compositor (wl_surface.set_buffer_scale)
global b32 scale_set_this_frame; // a buffer scale waits for the frame's own buffer (see nv_window_run)
global b32 fixed_size;
global u32 fixed_width;
global u32 fixed_height;

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
    // wl_compositor v6 gives wl_surface.preferred_buffer_scale, xdg_wm_base v6 the toplevel's suspended state. An older compositor
    // gives neither, and the window goes on at scale 1 and never suspended.
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        wayland_compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 6 ? version : 6);
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
        wayland_xdg_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version < 6 ? version : 6);
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

internal void call_hidden(void)
{
    if (the_window && the_window->on_hidden)
        the_window->on_hidden(the_window->on_hidden_data);
}

// The toplevel's configure comes first and the xdg_surface's ends it: the size and the states take effect when it is acked. A
// window that stops being the active one or is suspended gets the hidden callback (as Windows does on WM_ACTIVATEAPP and on
// minimizing), the last moment to save before the user may be gone.
internal void xdg_surface_configure(void* data, struct xdg_surface* surface, u32 serial)
{
    (void)data;
    xdg_surface_ack_configure(surface, serial);
    if (configured_width > 0 && configured_height > 0) {
        window_width = (u32)configured_width;
        window_height = (u32)configured_height;
    }
    b32 hidden = (activated && !configured_activated) || (!suspended && configured_suspended);
    activated = configured_activated;
    suspended = configured_suspended;
    wayland_configured = 1;
    if (hidden)
        call_hidden();
}

global const struct xdg_surface_listener xdg_surface_listener = {xdg_surface_configure};

// A size of 0 leaves it to the window, which keeps the one it has.
internal void toplevel_configure(void* data, struct xdg_toplevel* toplevel, s32 width, s32 height, struct wl_array* states)
{
    (void)data, (void)toplevel;
    configured_width = width;
    configured_height = height;
    configured_activated = 0;
    configured_suspended = 0;
    const u32* state;
    wl_array_for_each(state, states) {
        if (*state == XDG_TOPLEVEL_STATE_ACTIVATED)
            configured_activated = 1;
        else if (*state == XDG_TOPLEVEL_STATE_SUSPENDED)
            configured_suspended = 1;
    }
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

internal void surface_enter(void* data, struct wl_surface* surface, struct wl_output* output)
{
    (void)data, (void)surface, (void)output;
}

internal void surface_leave(void* data, struct wl_surface* surface, struct wl_output* output)
{
    (void)data, (void)surface, (void)output;
}

// The scale the compositor would like the window's buffers in: the screen's, rounded up on a fractional one.
internal void surface_preferred_buffer_scale(void* data, struct wl_surface* surface, s32 factor)
{
    (void)data, (void)surface;
    preferred_scale = factor > 0 ? (u32)factor : 1;
}

internal void surface_preferred_buffer_transform(void* data, struct wl_surface* surface, u32 transform)
{
    (void)data, (void)surface, (void)transform; // the buffers stay upright; the compositor turns them
}

global const struct wl_surface_listener surface_listener = {surface_enter, surface_leave, surface_preferred_buffer_scale,
                                                            surface_preferred_buffer_transform};

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
    the_window = window;
    window_width = 1600; // the compositor's configure says otherwise when it has a size in mind
    window_height = 900;
    // A fixed size by hand: both variables say a size.
    u32 env_width = size_from_env("NV_WINDOW_WIDTH", 0);
    u32 env_height = size_from_env("NV_WINDOW_HEIGHT", 0);
    if (env_width && env_height) {
        fixed_size = 1;
        fixed_width = env_width;
        fixed_height = env_height;
    }

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
    wl_surface_add_listener(wayland_surface, &surface_listener, NULL);
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
    if (fixed_size)
        nv_log(NV_LOG_INFO, "nv", "Wayland window: %ux%u framebuffer (fixed: NV_WINDOW_WIDTH, NV_WINDOW_HEIGHT), app id \"%s\"", fixed_width,
               fixed_height, title);
    else
        nv_log(NV_LOG_INFO, "nv", "Wayland window: %ux%u (the compositor's size), app id \"%s\"", window_width, window_height, title);
}

void nv_window_linux_set_fixed_size(NvWindow* window, u32 width, u32 height)
{
    fixed_size = 1;
    fixed_width = width;
    fixed_height = height;
    window->pixel_ratio = 1.0f;
    if (buffer_scale != 1) {
        wl_surface_set_buffer_scale(wayland_surface, 1);
        buffer_scale = 1;
        scale_set_this_frame = 1;
    }
    nv_log(NV_LOG_INFO, "nv", "Wayland window: %ux%u framebuffer (fixed), whatever the compositor configures", width, height);
}

b32 nv_window_linux_wayland(NvWindow* window, void** display, void** surface)
{
    (void)window; // one window per process, as the other backends keep it
    *display = wayland_display;
    *surface = wayland_surface;
    return wayland_display && wayland_surface;
}

// The GPU backend asks at the start of each frame, so a new size or scale is the frame's: its buffer is the first of that size.
void nv_window_framebuffer_size(NvWindow* window, u32* width, u32* height)
{
    if (fixed_size) {
        window->pixel_ratio = 1.0f;
        *width = fixed_width;
        *height = fixed_height;
        return;
    }
    if (preferred_scale != buffer_scale) {
        // Double-buffered: it takes effect with the frame's present, whose buffer is the size below (a multiple of the scale).
        wl_surface_set_buffer_scale(wayland_surface, (s32)preferred_scale);
        buffer_scale = preferred_scale;
        scale_set_this_frame = 1;
    }
    window->pixel_ratio = (f32)buffer_scale;
    *width = window_width * buffer_scale;
    *height = window_height * buffer_scale;
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
    // Called when the window stops being the active one or is suspended (xdg_surface_configure), and when the run ends.
    window->on_hidden = hidden;
    window->on_hidden_data = userdata;
}

// One turn of the compositor's event queue, waiting up to `timeout_ms` for an event (0: not at all).
internal void pump_events(int timeout_ms)
{
    while (wl_display_prepare_read(wayland_display) != 0)
        wl_display_dispatch_pending(wayland_display);
    wl_display_flush(wayland_display);
    struct pollfd polled = {.fd = wl_display_get_fd(wayland_display), .events = POLLIN};
    if (poll(&polled, 1, timeout_ms) > 0 && (polled.revents & POLLIN))
        wl_display_read_events(wayland_display);
    else
        wl_display_cancel_read(wayland_display);
    wl_display_dispatch_pending(wayland_display);
}

internal void frame_done(void* data, struct wl_callback* callback, u32 time)
{
    (void)data, (void)time;
    wl_callback_destroy(callback);
    frame_callback = NULL;
}

global const struct wl_callback_listener frame_listener = {frame_done};

// How long a frame waits for the compositor's frame callback before it is drawn anyway. NOTE: A compositor sends none to a window
// that is covered or hidden; without this, such a window would draw nothing more, and a program that needs its frames (the visual
// test, opened behind a fullscreen window) would never end. The present cannot block meanwhile: it is Mailbox (gpu_webgpu.c).
#define FRAME_WAIT_SECONDS 1.0

// NOTE: The headless backend's break with this function's contract, which is to never return:
// the loop runs until nv_quit or the window's close, then control comes back so the program can
// finish what it made and exit with its own code.
//
// Each frame asks the compositor for a frame callback and the next frame waits for it, reading events meanwhile, so the frames
// keep the compositor's pace and a close or a key is read whether or not the window is shown. A suspended window draws nothing: the
// loop sleeps until an event (the window back, the close).
void nv_window_run(NvWindow* window, NvFrameFn frame, void* userdata)
{
    f64 asked_at = 0.0;
    while (!quit_asked && !close_asked) {
        pump_events(0);
        while (suspended && !quit_asked && !close_asked)
            pump_events(-1);
        while (frame_callback && !quit_asked && !close_asked) {
            f64 left = FRAME_WAIT_SECONDS - (nv_time_seconds() - asked_at);
            if (left <= 0.0)
                break;
            pump_events((int)(left * 1000.0) + 1);
        }
        if (quit_asked || close_asked)
            break;
        if (frame_callback) { // not answered: the window is covered or hidden
            wl_callback_destroy(frame_callback);
            frame_callback = NULL;
        }
        // Asked before the frame, so the present's commit carries it; the commit after it carries it when the frame presented
        // nothing (a commit with nothing else pending changes nothing).
        frame_callback = wl_surface_frame(wayland_surface);
        wl_callback_add_listener(frame_callback, &frame_listener, NULL);
        asked_at = nv_time_seconds();
        frame(userdata);
        // A buffer scale set this frame went with its present; without one, it must not reach the compositor with the old buffer,
        // whose size need not be a multiple of it (a protocol error). The frame callback then waits for the next present instead.
        if (!scale_set_this_frame)
            wl_surface_commit(wayland_surface);
        scale_set_this_frame = 0;
        wl_display_flush(wayland_display);
    }
    if (window->on_hidden)
        window->on_hidden(window->on_hidden_data);
}

void nv_quit(void)
{
    quit_asked = 1;
}
