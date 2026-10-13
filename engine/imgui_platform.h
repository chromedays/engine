#pragma once

// Engine-private: what imgui.c asks of the platform's input code (imgui_web.c) and what that code asks back. Apps do not include it.

#include "engine/imgui.h"

// Hooks the platform's input to ImGui, once the context exists: pointer, keys, text, the clipboard and the input method's place.
// Sets NvImgui.ui_scale (larger when the primary pointer is a finger) and the platform's name in the ImGui IO.
void nv_imgui_platform_init(NvImgui* imgui, NvWindow* window);

// Once a frame, before ImGui's frame starts: the on-screen keyboard and a touch that has been held long enough to be a press.
void nv_imgui_platform_update(NvImgui* imgui);

// Shows the mouse cursor ImGui asks for.
void nv_imgui_platform_set_cursor(ImGuiMouseCursor cursor);

// Whether input starting at (x, y), in CSS pixels, belongs to the view rather than to ImGui. Open popups keep their input.
b32 nv_imgui_in_view(NvImgui* imgui, f32 x, f32 y);

// A click or tap in the view that did not move: reaches NvImgui.view on the next frame.
void nv_imgui_view_tap(NvImgui* imgui, f32 x, f32 y);

// A mouse button going down or up, or the pointer moving, at (x, y) in CSS pixels: the part of it that drives the camera. A press
// in the view starts a drag (`button` 0 left orbits, 1 middle and 2 right pan: the DOM's numbering) and a left press that does
// not move is a tap; `mods` are the ImGuiMod_ bits held. Returns whether the view has the event, so ImGui does not get it.
typedef enum NvMouseEvent {
    NV_MOUSE_DOWN,
    NV_MOUSE_MOVE,
    NV_MOUSE_UP,
} NvMouseEvent;
b32 nv_imgui_view_mouse(NvImgui* imgui, NvMouseEvent event, s32 button, f32 x, f32 y, u32 mods);

// The wheel turning `scroll_pixels` (positive: away from the user, about 100 a notch) while the pointer is in the view zooms the
// camera and returns true; ImGui does not get it then.
b32 nv_imgui_view_wheel(NvImgui* imgui, f32 scroll_pixels);

// The touch that started as undecided is a press now: ImGui sees the left button go down where the finger started.
void nv_imgui_touch_press(NvImgui* imgui);
