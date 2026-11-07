// The Wayland headers come before the engine's: engine/base.h defines `global` as a macro, and
// struct wl_registry_listener has a field of that name (as windows.h has with `internal`).
#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include "engine/imgui_platform.h"
#include "engine/log.h"
#include "engine/window_linux.h"

#include <float.h>
#include <string.h>

// Dear ImGui's input on the Linux Wayland window (docs/specs/linux_wayland.md, "입력"): the seat's pointer becomes ImGui's mouse
// and, where it starts in the view, the camera drag (imgui.c), as imgui_win32.c does with the window's messages. The positions a
// wl_pointer reports are surface pixels, which are the CSS pixels ImGui and the app use: nothing is divided by the scale.
//
// This file makes its own wl_registry on the window's wl_display: Wayland gives each object its own listener, so the window
// (window_linux.c) needs no hook for input, and its event loop dispatches these events with its own.

typedef struct WaylandInput {
    NvImgui* imgui;
    struct wl_seat* seat;
    u32 seat_version;
    struct wl_pointer* pointer;
    f32 x, y;               // the pointer's last place in the surface, in CSS pixels
    u32 mouse_buttons_down; // bit i is mouse button i of ImGui's numbering
    // One pointer frame's scrolling, applied at its wl_pointer.frame: a wheel notch can come as a value, a discrete step (seat
    // v5 to v7) and a value120 (v8) at once, and only one of them is used.
    struct {
        f64 value;
        s32 value120;
        s32 discrete;
        b32 any, has_value120, has_discrete;
    } axis[2];
} WaylandInput;

global WaylandInput input;

// The ImGuiMod_ bits held, for the view's taps (NvViewInput.tap_mods). NOTE: None until the keyboard is read.
internal u32 modifier_bits(void)
{
    return 0;
}

//
// The pointer
//

internal void pointer_moved(f32 x, f32 y)
{
    input.x = x;
    input.y = y;
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_Mouse);
    ImGuiIO_AddMousePosEvent(io, x, y);
    nv_imgui_view_mouse(input.imgui, NV_MOUSE_MOVE, 0, x, y, modifier_bits());
}

internal void pointer_enter(void* data, struct wl_pointer* pointer, u32 serial, struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
{
    (void)data, (void)pointer, (void)serial, (void)surface;
    pointer_moved((f32)wl_fixed_to_double(x), (f32)wl_fixed_to_double(y));
}

internal void pointer_leave(void* data, struct wl_pointer* pointer, u32 serial, struct wl_surface* surface)
{
    (void)data, (void)pointer, (void)serial, (void)surface;
    // A held button keeps the moves coming past the edge (Wayland's implicit grab); the pointer only leaves when none is.
    if (!input.mouse_buttons_down)
        ImGuiIO_AddMousePosEvent(igGetIO_Nil(), -FLT_MAX, -FLT_MAX);
}

internal void pointer_motion(void* data, struct wl_pointer* pointer, u32 time, wl_fixed_t x, wl_fixed_t y)
{
    (void)data, (void)pointer, (void)time;
    pointer_moved((f32)wl_fixed_to_double(x), (f32)wl_fixed_to_double(y));
}

internal void pointer_button(void* data, struct wl_pointer* pointer, u32 serial, u32 time, u32 button, u32 state)
{
    (void)data, (void)pointer, (void)serial, (void)time;
    s32 dom_button = button == BTN_LEFT ? 0 : button == BTN_MIDDLE ? 1 : button == BTN_RIGHT ? 2 : -1;
    if (dom_button < 0)
        return; // the side buttons are nothing the editor uses
    s32 imgui_button = dom_button == 0 ? 0 : dom_button == 1 ? 2 : 1; // DOM: left, middle, right; ImGui: left, right, middle
    b32 down = state == WL_POINTER_BUTTON_STATE_PRESSED;
    if (down)
        input.mouse_buttons_down |= 1u << imgui_button;
    else
        input.mouse_buttons_down &= ~(1u << imgui_button);
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_Mouse);
    ImGuiIO_AddMousePosEvent(io, input.x, input.y);
    if (!nv_imgui_view_mouse(input.imgui, down ? NV_MOUSE_DOWN : NV_MOUSE_UP, dom_button, input.x, input.y, modifier_bits()))
        ImGuiIO_AddMouseButtonEvent(io, imgui_button, down);
}

// The scrolling one frame collected, in notches: a value120 (120 a notch), else a discrete step, else (a touchpad, which has
// neither) the value, which is in surface pixels and about 10 a notch. Wayland's axes grow down and to the right.
internal void apply_axes(void)
{
    f32 notches[2] = {0.0f, 0.0f};
    for (u32 a = 0; a < 2; ++a) {
        if (!input.axis[a].any)
            continue;
        if (input.axis[a].has_value120)
            notches[a] = (f32)input.axis[a].value120 / 120.0f;
        else if (input.axis[a].has_discrete)
            notches[a] = (f32)input.axis[a].discrete;
        else
            notches[a] = (f32)input.axis[a].value / 10.0f;
    }
    memset(input.axis, 0, sizeof(input.axis));
    f32 down = notches[WL_POINTER_AXIS_VERTICAL_SCROLL];
    f32 right = notches[WL_POINTER_AXIS_HORIZONTAL_SCROLL];
    if (down == 0.0f && right == 0.0f)
        return;
    // The vertical wheel zooms the view under the pointer (positive: toward the user, about 100 pixels a notch, as the DOM's
    // deltaY); anywhere else ImGui scrolls, its wheel growing up and to the left.
    if (down != 0.0f && right == 0.0f && nv_imgui_view_wheel(input.imgui, down * 100.0f))
        return;
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_Mouse);
    ImGuiIO_AddMouseWheelEvent(io, -right, -down);
}

internal void pointer_axis(void* data, struct wl_pointer* pointer, u32 time, u32 axis, wl_fixed_t value)
{
    (void)data, (void)pointer, (void)time;
    if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        return;
    input.axis[axis].value += wl_fixed_to_double(value);
    input.axis[axis].any = 1;
    if (input.seat_version < WL_POINTER_FRAME_SINCE_VERSION)
        apply_axes(); // no frames before v5: every event stands alone
}

internal void pointer_frame(void* data, struct wl_pointer* pointer)
{
    (void)data, (void)pointer;
    apply_axes();
}

internal void pointer_axis_source(void* data, struct wl_pointer* pointer, u32 source)
{
    (void)data, (void)pointer, (void)source;
}

internal void pointer_axis_stop(void* data, struct wl_pointer* pointer, u32 time, u32 axis)
{
    (void)data, (void)pointer, (void)time, (void)axis;
}

internal void pointer_axis_discrete(void* data, struct wl_pointer* pointer, u32 axis, s32 discrete)
{
    (void)data, (void)pointer;
    if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        return;
    input.axis[axis].discrete += discrete;
    input.axis[axis].has_discrete = 1;
    input.axis[axis].any = 1;
}

internal void pointer_axis_value120(void* data, struct wl_pointer* pointer, u32 axis, s32 value120)
{
    (void)data, (void)pointer;
    if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        return;
    input.axis[axis].value120 += value120;
    input.axis[axis].has_value120 = 1;
    input.axis[axis].any = 1;
}

// Named fields: a newer libwayland's listener has more (axis_relative_direction, v9), which a seat bound at v8 never sends.
global const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
    .axis_value120 = pointer_axis_value120,
};

//
// The seat
//

internal void seat_capabilities(void* data, struct wl_seat* seat, u32 capabilities)
{
    (void)data;
    b32 has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (has_pointer && !input.pointer) {
        input.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(input.pointer, &pointer_listener, NULL);
    } else if (!has_pointer && input.pointer) {
        if (input.seat_version >= WL_POINTER_RELEASE_SINCE_VERSION)
            wl_pointer_release(input.pointer);
        else
            wl_pointer_destroy(input.pointer);
        input.pointer = NULL;
        input.mouse_buttons_down = 0;
        ImGuiIO_AddMousePosEvent(igGetIO_Nil(), -FLT_MAX, -FLT_MAX);
    }
}

internal void seat_name(void* data, struct wl_seat* seat, const char* name)
{
    (void)data, (void)seat, (void)name;
}

global const struct wl_seat_listener seat_listener = {seat_capabilities, seat_name};

// The first seat: one is all the editor uses. Version 8 brings wl_pointer.axis_value120; an older seat goes without it.
internal void registry_global(void* data, struct wl_registry* registry, u32 name, const char* interface, u32 version)
{
    (void)data;
    if (strcmp(interface, wl_seat_interface.name) == 0 && !input.seat) {
        input.seat_version = version < 8 ? version : 8;
        input.seat = wl_registry_bind(registry, name, &wl_seat_interface, input.seat_version);
        wl_seat_add_listener(input.seat, &seat_listener, NULL);
    }
}

internal void registry_global_remove(void* data, struct wl_registry* registry, u32 name)
{
    (void)data, (void)registry, (void)name; // a seat that goes away takes its capabilities first
}

global const struct wl_registry_listener registry_listener = {registry_global, registry_global_remove};

//
// The platform's functions
//

void nv_imgui_platform_init(NvImgui* imgui, NvWindow* window)
{
    input.imgui = imgui;
    igGetIO_Nil()->BackendPlatformName = "nv_wayland";
    imgui->ui_scale = 1.0f; // a mouse: the touch UI is the browser's

    void* display = NULL;
    void* surface = NULL;
    if (!nv_window_linux_wayland(window, &display, &surface))
        return;
    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    // The seat is bound when this returns; its capabilities (and so the pointer) come with the window's next events.
    wl_display_roundtrip(display);
    if (!input.seat)
        nv_log(NV_LOG_WARNING, "nv", "the compositor offers no wl_seat: no pointer or keyboard");
}

void nv_imgui_platform_update(NvImgui* imgui)
{
    (void)imgui;
}

void nv_imgui_platform_set_cursor(ImGuiMouseCursor cursor)
{
    (void)cursor; // the compositor's cursor stays (docs/specs/linux_wayland.md, "나중": cursor-shape-v1)
}
