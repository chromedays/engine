#pragma once

// Scripted runs of the Windows Debug build (docs/specs/d3d12_port.md, "Debug exports and native tests"): `<app>.exe --script <file>`
// runs the file's commands, one a line, in the frames of the app, and ends the process with 1 when the log holds an error, else 0.
// Playwright calls the debug exports by name on the web; here `call` finds them in the table the app gives.
// A line that is empty or starts with # is skipped. `--out <dir>` is where a relative path of a command goes.
//   size <width> <height>        the window's client area in pixels, at pixel ratio 1
//   call <name> <number>...      a debug export, with its numbers; a result is logged
//   frames <n>                   n frames before the next command
//   screenshot <png> [viewport | <x> <y> <width> <height>]  the canvas (the app's viewport, or a rectangle of it) of this frame as a
//                                PNG file; the next command waits for it
//   move <x> <y>                 the pointer, in client pixels
//   mouse <down|up> [button]     a button of the mouse (left, middle or right) at the pointer
//   wheel <notches>              the wheel at the pointer, positive away from the user
//   key <name> [down|up]         a key (a letter, a digit, enter, escape, tab, backspace, delete, space, an arrow, home, end, f1 to f12);
//                                without down or up it goes down and up
//   modifiers [ctrl] [shift] [alt]  the modifier keys that are held from now on
//   text <characters>            typed text (the rest of the line, UTF-8), as the input method hands it over
//   close                        the window's close button, which saves the state
//   quit                         ends the run
// An app adds commands of its own through `command`.

#include "engine/base.h"

#if defined(_WIN32) && !defined(NDEBUG)

#include "engine/gpu.h"
#include "engine/window.h"

typedef struct NvDebugCall {
    const char* name;      // the export without its app's prefix
    const char* signature; // "arguments>result": i int, f float, s char*; v none, i int, f float, d double, s char*, u unsigned
    void* function;
} NvDebugCall;

typedef struct NvDebugScriptDesc {
    NvWindow* window;
    NvGpu* gpu;
    NvArena* arena; // the script's text is read into it
    const NvDebugCall* calls;
    u32 call_count;
    NvRect (*viewport)(void* user); // what `screenshot <png> viewport` cuts out
    // An app's own command: `words[0]` is its name, the rest its arguments. True when it ran it.
    b32 (*command)(void* user, const char* const* words, u32 count);
    // Asked every frame (and after each command): true while the app waits for something the next command needs (a readback, a
    // run of frames); the script goes on when it says false. It may finish what it was waiting for in the call.
    b32 (*busy)(void* user);
    void* user;
} NvDebugScriptDesc;

// Reads the command line; does nothing without --script.
void nv_debug_script_init(const NvDebugScriptDesc* desc);
// At the start of every frame.
void nv_debug_script_update(void);
// A path from a command: as it is when it names a drive or starts at a root, else under --out.
void nv_debug_script_resolve_path(char* out, umm capacity, const char* path);

#else

// An app calls it in every build; nv_debug_script_init (which takes a table only the Windows Debug build has) stays behind #if.
#define nv_debug_script_update() ((void)0)

#endif
