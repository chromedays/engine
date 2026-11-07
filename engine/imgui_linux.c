// The Wayland headers come before the engine's: engine/base.h defines `global` as a macro, and
// struct wl_registry_listener has a field of that name (as windows.h has with `internal`).
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "engine/imgui_platform.h"
#include "engine/log.h"
#include "engine/window_linux.h"

#include <float.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// Dear ImGui's input on the Linux Wayland window (docs/specs/linux_wayland.md, "입력"): the seat's pointer becomes ImGui's mouse
// and, where it starts in the view, the camera drag (imgui.c), and its keyboard ImGui's keys, modifiers, text and focus, as
// imgui_win32.c does with the window's messages. The positions a wl_pointer reports are surface pixels, which are the CSS pixels
// ImGui and the app use: nothing is divided by the scale. The keyboard's keymap is read by libxkbcommon.
//
// This file makes its own wl_registry on the window's wl_display: Wayland gives each object its own listener, so the window
// (window_linux.c) needs no hook for input, and its event loop dispatches these events with its own.

typedef struct WaylandInput {
    NvImgui* imgui;
    struct wl_seat* seat;
    u32 seat_version;
    struct wl_pointer* pointer;
    struct wl_keyboard* keyboard;
    struct xkb_context* xkb;
    struct xkb_keymap* keymap; // the compositor's, sent with wl_keyboard.keymap
    struct xkb_state* xkb_state;
    u32 mods; // the ImGuiMod_ bits held
    // The compositor's key repeat (wl_keyboard.repeat_info): Wayland leaves repeating to the client. Only text repeats here;
    // ImGui repeats its own keys (Backspace, the arrows) while they are held.
    s32 repeat_rate; // characters a second; 0: no repeat
    s32 repeat_delay_ms;
    u32 repeat_key;  // the xkb keycode repeating, 0 for none
    char repeat_text[16];
    f64 repeat_next;
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

// The ImGuiMod_ bits held, for the view's taps (NvViewInput.tap_mods).
internal u32 modifier_bits(void)
{
    return input.mods & (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiMod_Alt);
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
// The keyboard
//

// The ImGui key of a keysym. Letters come lowercase (the level without Shift).
internal ImGuiKey key_from_keysym(xkb_keysym_t sym)
{
    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z)
        return (ImGuiKey)(ImGuiKey_A + (sym - XKB_KEY_a));
    if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z)
        return (ImGuiKey)(ImGuiKey_A + (sym - XKB_KEY_A));
    if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9)
        return (ImGuiKey)(ImGuiKey_0 + (sym - XKB_KEY_0));
    if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9)
        return (ImGuiKey)(ImGuiKey_Keypad0 + (sym - XKB_KEY_KP_0));
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F24)
        return (ImGuiKey)(ImGuiKey_F1 + (sym - XKB_KEY_F1));
    switch (sym) {
    case XKB_KEY_Tab: case XKB_KEY_ISO_Left_Tab: return ImGuiKey_Tab;
    case XKB_KEY_Left: case XKB_KEY_KP_Left: return ImGuiKey_LeftArrow;
    case XKB_KEY_Right: case XKB_KEY_KP_Right: return ImGuiKey_RightArrow;
    case XKB_KEY_Up: case XKB_KEY_KP_Up: return ImGuiKey_UpArrow;
    case XKB_KEY_Down: case XKB_KEY_KP_Down: return ImGuiKey_DownArrow;
    case XKB_KEY_Page_Up: case XKB_KEY_KP_Page_Up: return ImGuiKey_PageUp;
    case XKB_KEY_Page_Down: case XKB_KEY_KP_Page_Down: return ImGuiKey_PageDown;
    case XKB_KEY_Home: case XKB_KEY_KP_Home: return ImGuiKey_Home;
    case XKB_KEY_End: case XKB_KEY_KP_End: return ImGuiKey_End;
    case XKB_KEY_Insert: case XKB_KEY_KP_Insert: return ImGuiKey_Insert;
    case XKB_KEY_Delete: case XKB_KEY_KP_Delete: return ImGuiKey_Delete;
    case XKB_KEY_BackSpace: return ImGuiKey_Backspace;
    case XKB_KEY_space: return ImGuiKey_Space;
    case XKB_KEY_Return: return ImGuiKey_Enter;
    case XKB_KEY_KP_Enter: return ImGuiKey_KeypadEnter;
    case XKB_KEY_Escape: return ImGuiKey_Escape;
    case XKB_KEY_apostrophe: return ImGuiKey_Apostrophe;
    case XKB_KEY_comma: return ImGuiKey_Comma;
    case XKB_KEY_minus: return ImGuiKey_Minus;
    case XKB_KEY_period: return ImGuiKey_Period;
    case XKB_KEY_slash: return ImGuiKey_Slash;
    case XKB_KEY_semicolon: return ImGuiKey_Semicolon;
    case XKB_KEY_equal: return ImGuiKey_Equal;
    case XKB_KEY_bracketleft: return ImGuiKey_LeftBracket;
    case XKB_KEY_backslash: return ImGuiKey_Backslash;
    case XKB_KEY_bracketright: return ImGuiKey_RightBracket;
    case XKB_KEY_grave: return ImGuiKey_GraveAccent;
    case XKB_KEY_KP_Decimal: case XKB_KEY_KP_Separator: return ImGuiKey_KeypadDecimal;
    case XKB_KEY_KP_Divide: return ImGuiKey_KeypadDivide;
    case XKB_KEY_KP_Multiply: return ImGuiKey_KeypadMultiply;
    case XKB_KEY_KP_Subtract: return ImGuiKey_KeypadSubtract;
    case XKB_KEY_KP_Add: return ImGuiKey_KeypadAdd;
    case XKB_KEY_KP_Equal: return ImGuiKey_KeypadEqual;
    case XKB_KEY_Shift_L: return ImGuiKey_LeftShift;
    case XKB_KEY_Shift_R: return ImGuiKey_RightShift;
    case XKB_KEY_Control_L: return ImGuiKey_LeftCtrl;
    case XKB_KEY_Control_R: return ImGuiKey_RightCtrl;
    case XKB_KEY_Alt_L: case XKB_KEY_Meta_L: return ImGuiKey_LeftAlt;
    case XKB_KEY_Alt_R: case XKB_KEY_Meta_R: case XKB_KEY_ISO_Level3_Shift: return ImGuiKey_RightAlt;
    case XKB_KEY_Super_L: return ImGuiKey_LeftSuper;
    case XKB_KEY_Super_R: return ImGuiKey_RightSuper;
    case XKB_KEY_Caps_Lock: return ImGuiKey_CapsLock;
    case XKB_KEY_Num_Lock: return ImGuiKey_NumLock;
    case XKB_KEY_Scroll_Lock: return ImGuiKey_ScrollLock;
    case XKB_KEY_Print: return ImGuiKey_PrintScreen;
    case XKB_KEY_Pause: return ImGuiKey_Pause;
    case XKB_KEY_Menu: return ImGuiKey_Menu;
    default: return ImGuiKey_None;
    }
}

// A letter or digit key by its place on the keyboard (US QWERTY), for layouts whose keys give no Latin letter (Russian, Greek): the
// shortcuts still work, on the keys they would be on.
internal ImGuiKey key_from_place(u32 evdev)
{
    local_persist const char rows[3][11] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    local_persist const u32 starts[3] = {KEY_Q, KEY_A, KEY_Z};
    for (u32 r = 0; r < 3; ++r) {
        u32 length = (u32)strlen(rows[r]);
        if (evdev >= starts[r] && evdev < starts[r] + length)
            return (ImGuiKey)(ImGuiKey_A + (rows[r][evdev - starts[r]] - 'a'));
    }
    if (evdev >= KEY_1 && evdev <= KEY_9)
        return (ImGuiKey)(ImGuiKey_1 + (evdev - KEY_1));
    if (evdev == KEY_0)
        return ImGuiKey_0;
    return ImGuiKey_None;
}

// The ImGui key of a key on the keyboard: its keysym without modifiers, so Shift+1 is ImGuiKey_1 and a shortcut follows the
// layout, as Windows's virtual keys do. A keypad key takes the keysym it has now instead, so Num Lock decides between digits and
// moving.
internal ImGuiKey key_from_keycode(xkb_keycode_t keycode)
{
    const xkb_keysym_t* syms = NULL;
    xkb_layout_index_t layout = xkb_state_key_get_layout(input.xkb_state, keycode);
    int count = xkb_keymap_key_get_syms_by_level(input.keymap, keycode, layout, 0, &syms);
    xkb_keysym_t sym = count > 0 ? syms[0] : XKB_KEY_NoSymbol;
    if (sym >= XKB_KEY_KP_Space && sym <= XKB_KEY_KP_Equal)
        sym = xkb_state_key_get_one_sym(input.xkb_state, keycode);
    ImGuiKey key = key_from_keysym(sym);
    return key != ImGuiKey_None ? key : key_from_place(keycode - 8);
}

internal void keyboard_keymap(void* data, struct wl_keyboard* keyboard, u32 format, s32 fd, u32 size)
{
    (void)data, (void)keyboard;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        nv_log(NV_LOG_WARNING, "nv", "the compositor's keymap is not an XKB one: no keyboard");
        return;
    }
    // Private: a seat from version 7 on maps it read-only for every client.
    char* text = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (text == MAP_FAILED) {
        nv_log(NV_LOG_WARNING, "nv", "the compositor's keymap could not be read: no keyboard");
        return;
    }
    struct xkb_keymap* keymap = xkb_keymap_new_from_string(input.xkb, text, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(text, size);
    if (!keymap) {
        nv_log(NV_LOG_WARNING, "nv", "the compositor's keymap did not compile: no keyboard");
        return;
    }
    struct xkb_state* state = xkb_state_new(keymap);
    if (!state) {
        xkb_keymap_unref(keymap);
        return;
    }
    xkb_state_unref(input.xkb_state);
    xkb_keymap_unref(input.keymap);
    input.keymap = keymap;
    input.xkb_state = state;
    input.repeat_key = 0;
}

internal void keyboard_enter(void* data, struct wl_keyboard* keyboard, u32 serial, struct wl_surface* surface, struct wl_array* keys)
{
    (void)data, (void)keyboard, (void)serial, (void)surface, (void)keys; // keys held on the way in are not pressed here
    ImGuiIO_AddFocusEvent(igGetIO_Nil(), 1);
}

internal void keyboard_leave(void* data, struct wl_keyboard* keyboard, u32 serial, struct wl_surface* surface)
{
    (void)data, (void)keyboard, (void)serial, (void)surface;
    input.repeat_key = 0;
    ImGuiIO_AddFocusEvent(igGetIO_Nil(), 0); // ImGui lets go of every key it thought held
}

internal void add_text(const char* text)
{
    ImGuiIO_AddInputCharactersUTF8(igGetIO_Nil(), text);
}

internal void keyboard_key(void* data, struct wl_keyboard* keyboard, u32 serial, u32 time, u32 key, u32 state)
{
    (void)data, (void)keyboard, (void)serial, (void)time;
    if (!input.xkb_state)
        return;
    xkb_keycode_t keycode = key + 8; // evdev codes are XKB's minus 8
    b32 down = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    ImGuiKey imgui_key = key_from_keycode(keycode);
    if (imgui_key != ImGuiKey_None)
        ImGuiIO_AddKeyEvent(igGetIO_Nil(), imgui_key, down);
    if (!down) {
        if (keycode == input.repeat_key)
            input.repeat_key = 0;
        return;
    }
    // Text, but not while Ctrl or Alt makes the key a shortcut (AltGr is another modifier, so it still types); no control
    // characters (Enter, Tab, Backspace, Escape are keys).
    input.repeat_key = 0;
    if (input.mods & (ImGuiMod_Ctrl | ImGuiMod_Alt))
        return;
    char text[16];
    int length = xkb_state_key_get_utf8(input.xkb_state, keycode, text, sizeof(text));
    if (length <= 0 || (u8)text[0] < 0x20 || text[0] == 0x7F)
        return;
    add_text(text);
    if (input.repeat_rate > 0 && xkb_keymap_key_repeats(input.keymap, keycode)) {
        input.repeat_key = keycode;
        memcpy(input.repeat_text, text, sizeof(text));
        input.repeat_next = nv_time_seconds() + (f64)input.repeat_delay_ms / 1000.0;
    }
}

internal void keyboard_modifiers(void* data, struct wl_keyboard* keyboard, u32 serial, u32 depressed, u32 latched, u32 locked,
                                 u32 group)
{
    (void)data, (void)keyboard, (void)serial;
    if (!input.xkb_state)
        return;
    xkb_state_update_mask(input.xkb_state, depressed, latched, locked, 0, 0, group);
    struct xkb_state* state = input.xkb_state;
    u32 mods = 0;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= ImGuiMod_Ctrl;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= ImGuiMod_Shift;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= ImGuiMod_Alt;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= ImGuiMod_Super;
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, (mods & ImGuiMod_Ctrl) != 0);
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Shift, (mods & ImGuiMod_Shift) != 0);
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Alt, (mods & ImGuiMod_Alt) != 0);
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Super, (mods & ImGuiMod_Super) != 0);
    input.mods = mods;
}

internal void keyboard_repeat_info(void* data, struct wl_keyboard* keyboard, s32 rate, s32 delay)
{
    (void)data, (void)keyboard;
    input.repeat_rate = rate > 0 ? rate : 0;
    input.repeat_delay_ms = delay > 0 ? delay : 0;
}

global const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
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
    b32 has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (has_keyboard && !input.keyboard) {
        input.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(input.keyboard, &keyboard_listener, NULL);
    } else if (!has_keyboard && input.keyboard) {
        if (input.seat_version >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
            wl_keyboard_release(input.keyboard);
        else
            wl_keyboard_destroy(input.keyboard);
        input.keyboard = NULL;
        input.repeat_key = 0;
        ImGuiIO_AddFocusEvent(igGetIO_Nil(), 0);
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
    input.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
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

// Text the held key repeats, at the compositor's rate. The frames may be slower than the rate (or stop while the window is
// covered), so a frame catches up a few characters at most and the rest is dropped rather than typed in a burst.
void nv_imgui_platform_update(NvImgui* imgui)
{
    (void)imgui;
    if (!input.repeat_key || input.repeat_rate <= 0)
        return;
    f64 now = nv_time_seconds();
    f64 interval = 1.0 / (f64)input.repeat_rate;
    for (u32 typed = 0; now >= input.repeat_next && typed < 4; ++typed) {
        add_text(input.repeat_text);
        input.repeat_next += interval;
    }
    if (now >= input.repeat_next)
        input.repeat_next = now + interval;
}

void nv_imgui_platform_set_cursor(ImGuiMouseCursor cursor)
{
    (void)cursor; // the compositor's cursor stays (docs/specs/linux_wayland.md, "나중": cursor-shape-v1)
}
