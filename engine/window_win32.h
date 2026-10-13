#pragma once

// Engine-private: what window_win32.c lends the Windows input code (imgui_win32.c) and the GPU backend. Apps do not include it.

#include "engine/window.h"

// Every message of the window goes to the hook before the default handling. It returns true, and the result to give back, to
// consume the message. `hwnd` is the HWND; the message's numbers are WPARAM and LPARAM as they came.
typedef b32 (*NvWin32MessageHook)(void* userdata, void* hwnd, u32 message, u64 wparam, s64 lparam, s64* result);
void nv_window_win32_set_message_hook(NvWindow* window, NvWin32MessageHook hook, void* userdata);

// Sets the client area to `width` x `height` pixels and pins the pixel ratio to 1, whatever the screen's scale: for the scripted
// runs, whose pictures are compared pixel for pixel (docs/specs/d3d12_port.md, "Validation").
void nv_window_win32_set_fixed_size(NvWindow* window, u32 width, u32 height);

// True while the window is minimized: there is nothing to draw, and the message loop waits.
b32 nv_window_win32_minimized(void);

// The value that follows `option` on the command line ("--script" for "--script file.txt"), as UTF-8 in `out`. False when the option
// is not there or has no value.
b32 nv_window_win32_command_line_value(const char* option, char* out, umm capacity);

// Whether a key is down: the system's answer, or the modifiers a scripted run holds (nv_window_win32_inject_modifiers).
b32 nv_window_win32_key_is_down(int virtual_key);

#if !defined(NDEBUG)
// For the scripted runs (sandbox/debug_script.c): messages as the input devices would send them, to the window's procedure. Positions are
// client pixels. `button` is 0 left, 1 middle, 2 right, -1 for a move.
void nv_window_win32_inject_mouse(NvWindow* window, s32 x, s32 y, s32 button, b32 down);
void nv_window_win32_inject_wheel(NvWindow* window, s32 x, s32 y, s32 notches); // positive: away from the user
void nv_window_win32_inject_key(NvWindow* window, u32 virtual_key, b32 down);
void nv_window_win32_inject_char(NvWindow* window, u32 codepoint);
// The modifiers the injected keys act with (bit 0 Ctrl, 1 Shift, 2 Alt): GetKeyState does not see injected keys.
void nv_window_win32_inject_modifiers(u32 modifiers);
// Asks the window to close, as its close button does.
void nv_window_win32_inject_close(NvWindow* window);
// Whether the run has injected input: the pointer is then not the real one, which is somewhere else, so "left the window" is ignored.
b32 nv_window_win32_input_is_injected(void);
#endif
