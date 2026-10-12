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

// The touch that started as undecided is a press now: ImGui sees the left button go down where the finger started.
void nv_imgui_touch_press(NvImgui* imgui);
