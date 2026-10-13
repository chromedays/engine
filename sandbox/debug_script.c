#include "sandbox.h"

#if defined(_WIN32) && !defined(NDEBUG)

#include <engine/file.h>
#include <engine/window_win32.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A scripted run of the Windows Debug build (docs/specs/d3d12_port.md, "Debug exports and native tests"): `sandbox.exe --script
// <file>` runs the file's commands, one a line, in the frames of the sandbox, and ends the process with 1 when the log holds an
// error, else 0. The same cases drive Playwright on the web build, which calls the debug exports by name; here `call` finds them
// in sandbox_debug_calls (main.c). A line that is empty or starts with # is skipped.
//   size <width> <height>        the window's client area in pixels, at pixel ratio 1
//   call <name> <number>...      a debug export, with its numbers (as the web calls Module._sandbox_debug_<name>); a result is logged
//   frames <n>                   n frames before the next command
//   wait-fixed <n>               frames until the fixed game clock (`call fixed_dt`) has run n frames
//   benchmark <file>             runs the shown scene's benchmark (the Stress tab's Run) and writes its results table (the Copy button's text) to the
//                                file when it is over; the next command waits for it
//   screenshot <png> [viewport | <x> <y> <width> <height>]  the canvas (the scene's viewport, or a rectangle of it) of this frame as a
//                                PNG file; the next command waits for it. A relative path is in the folder `--out` gives
//   case <id> <dt> <counters 0|1>  starts <out>/<id>.json, the dumps of an effects case; case-frame <frame> <full|counters> adds the
//                                particle dump of that frame (the next command waits for it), case-end closes the file
//   move <x> <y>                 the pointer, in client pixels
//   mouse <down|up> [button]     a button of the mouse (left, middle or right) at the pointer
//   wheel <notches>              the wheel at the pointer, positive away from the user
//   key <name> [down|up]         a key (a letter, a digit, enter, escape, tab, backspace, delete, space, an arrow, home, end, f1 to f12);
//                                without down or up it goes down and up
//   modifiers [ctrl] [shift] [alt]  the modifier keys that are held from now on
//   text <characters>            typed text (the rest of the line, UTF-8), as the input method hands it over
//   close                        the window's close button, which saves the state
//   quit                         ends the run

#define SCRIPT_MAX_SIZE NV_KILOBYTES(64)
#define SCRIPT_MAX_ARGUMENTS 8
#define PATH_CAPACITY 520

typedef struct DebugScript {
    b32 active;
    char* next_line; // the next line to run
    u32 frames_left; // frames to run before the next command
    b32 quit;
    s32 pointer_x, pointer_y;
    char out_directory[PATH_CAPACITY]; // where a relative path goes; empty: where the program runs

    // An effects case being written (docs/specs/d3d12_port.md, "Validation"): the particle dump of a frame, in the web harness's JSON.
    FILE* case_file;
    b32 case_has_frame;
    b32 dump_pending;
    u32 dump_frame;
    b32 waiting_fixed;    // until the fixed clock (the `fixed_dt` export) has run this many frames
    u32 fixed_frames_wanted;
    b32 waiting_benchmark; // until the benchmark run (the `benchmark` command) is over
    char benchmark_path[PATH_CAPACITY];
} DebugScript;

global DebugScript script;

void debug_script_init(Sandbox* sandbox)
{
    char path[520];
    if (!nv_window_win32_command_line_value("--script", path, sizeof(path)))
        return;
    NvFileData file = nv_file_read(&sandbox->permanent, path);
    if (!file.ok || file.size > SCRIPT_MAX_SIZE) {
        nv_log(NV_LOG_ERROR, "script", "cannot read %s", path);
        exit(2);
    }
    script.active = 1;
    if (!nv_window_win32_command_line_value("--out", script.out_directory, sizeof(script.out_directory)))
        script.out_directory[0] = 0;
    script.next_line = (char*)file.bytes;
    nv_log(NV_LOG_INFO, "script", "running %s", path);
}

// Calls a debug export. The signatures are the ones of the exports in main.c's table; a new one is a case here.
internal f64 call_export(const DebugCall* entry, const f64* a)
{
    const char* signature = entry->signature;
    void* function = entry->function;
    #define SIGNATURE(text) (strcmp(signature, text) == 0)
    if (SIGNATURE(">i")) return ((int (*)(void))function)();
    if (SIGNATURE(">f")) return ((float (*)(void))function)();
    if (SIGNATURE(">u")) return ((unsigned (*)(void))function)();
    if (SIGNATURE(">v")) { ((void (*)(void))function)(); return 0.0; }
    if (SIGNATURE("i>i")) return ((int (*)(int))function)((int)a[0]);
    if (SIGNATURE("i>f")) return ((float (*)(int))function)((int)a[0]);
    if (SIGNATURE("i>v")) { ((void (*)(int))function)((int)a[0]); return 0.0; }
    if (SIGNATURE("f>v")) { ((void (*)(float))function)((float)a[0]); return 0.0; }
    if (SIGNATURE("ii>i")) return ((int (*)(int, int))function)((int)a[0], (int)a[1]);
    if (SIGNATURE("ii>f")) return ((float (*)(int, int))function)((int)a[0], (int)a[1]);
    if (SIGNATURE("ii>v")) { ((void (*)(int, int))function)((int)a[0], (int)a[1]); return 0.0; }
    if (SIGNATURE("iii>v")) { ((void (*)(int, int, int))function)((int)a[0], (int)a[1], (int)a[2]); return 0.0; }
    if (SIGNATURE("iii>d")) return ((double (*)(int, int, int))function)((int)a[0], (int)a[1], (int)a[2]);
    if (SIGNATURE("ifif>v")) { ((void (*)(int, float, int, float))function)((int)a[0], (float)a[1], (int)a[2], (float)a[3]); return 0.0; }
    if (SIGNATURE("fffi>f")) return ((float (*)(float, float, float, int))function)((float)a[0], (float)a[1], (float)a[2], (int)a[3]);
    if (SIGNATURE("iiiii>v")) {
        ((void (*)(int, int, int, int, int))function)((int)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4]);
        return 0.0;
    }
    #undef SIGNATURE
    nv_log(NV_LOG_ERROR, "script", "no way to call %s (signature %s)", entry->name, signature);
    exit(2);
}

internal u32 virtual_key_from_name(const char* name)
{
    struct { const char* name; u32 key; } named[] = {
        {"backspace", 0x08}, {"tab", 0x09}, {"enter", 0x0D}, {"escape", 0x1B}, {"space", 0x20}, {"end", 0x23}, {"home", 0x24},
        {"left", 0x25}, {"up", 0x26}, {"right", 0x27}, {"down", 0x28}, {"delete", 0x2E},
    };
    for (u32 i = 0; i < NV_ARRAY_COUNT(named); ++i) {
        if (strcmp(name, named[i].name) == 0)
            return named[i].key;
    }
    if (name[0] == 'f' && name[1] >= '1' && name[1] <= '9')
        return 0x70 + (u32)atoi(name + 1) - 1; // F1 is 0x70
    if (name[1] == 0 && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9')))
        return (u32)(name[0] >= 'a' ? name[0] - 'a' + 'A' : name[0]);
    nv_log(NV_LOG_ERROR, "script", "unknown key %s", name);
    exit(2);
}

// A path from a command: as it is when it names a drive or starts at a root, else under --out.
internal void resolve_path(char* out, umm capacity, const char* path)
{
    b32 absolute = path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
    if (absolute || !script.out_directory[0])
        snprintf(out, capacity, "%s", path);
    else
        snprintf(out, capacity, "%s/%s", script.out_directory, path);
}

internal void write_number(FILE* file, f64 value)
{
    if (value == floor(value) && fabs(value) < 1e15)
        fprintf(file, "%lld", (long long)value);
    else
        fprintf(file, "%.9g", value);
}

// The dump of one frame, as the web harness writes it (tools/d3d12_port/capture.mjs, dumpOnce).
internal void write_dump(Sandbox* sandbox)
{
    NvVfx* vfx = &sandbox->vfx;
    FILE* file = script.case_file;
    fprintf(file, "%s\"%u\":{\"capacity\":%u,\"counters\":[", script.case_has_frame ? "," : "", script.dump_frame, vfx->capacity);
    for (u32 i = 0; i < 16; ++i)
        fprintf(file, "%s%u", i ? "," : "", nv_vfx_debug_dump_counter(vfx, i));
    fprintf(file, "],\"simulateArgs\":[");
    for (u32 i = 0; i < 4; ++i)
        fprintf(file, "%s%u", i ? "," : "", nv_vfx_debug_dump_args(vfx, 0, i));
    fprintf(file, "],\"drawArgs\":[");
    for (u32 i = 0; i < 8; ++i)
        fprintf(file, "%s%u", i ? "," : "", nv_vfx_debug_dump_args(vfx, 1, i));
    NvVfxStats stats = nv_vfx_stats(vfx);
    fprintf(file, "],\"violations\":%u,\"segments\":%u,\"decals\":%u,\"particles\":[", nv_vfx_debug_dump_violations(vfx), stats.segments,
            stats.decals);
    u32 count = nv_vfx_debug_dump_particle_count(vfx);
    for (u32 i = 0; i < count; ++i) {
        fprintf(file, "%s[", i ? "," : "");
        for (u32 field = 0; field < 12; ++field) {
            fprintf(file, "%s", field ? "," : "");
            write_number(file, nv_vfx_debug_dump_particle(vfx, i, field));
        }
        fprintf(file, "]");
    }
    fprintf(file, "]}");
    script.case_has_frame = 1;
}

internal void run_command(Sandbox* sandbox, char* line)
{
    // A text command keeps the rest of the line as it is.
    if (strncmp(line, "text ", 5) == 0) {
        for (const char* p = line + 5; *p;) {
            u32 length = nv_utf8_length((u8)*p);
            u32 codepoint = length == 1 ? (u8)p[0]
                            : length == 2 ? (((u8)p[0] & 0x1Fu) << 6) | ((u8)p[1] & 0x3Fu)
                            : length == 3 ? (((u8)p[0] & 0x0Fu) << 12) | (((u8)p[1] & 0x3Fu) << 6) | ((u8)p[2] & 0x3Fu)
                                          : (((u8)p[0] & 0x07u) << 18) | (((u8)p[1] & 0x3Fu) << 12) | (((u8)p[2] & 0x3Fu) << 6) | ((u8)p[3] & 0x3Fu);
            nv_window_win32_inject_char(&sandbox->window, codepoint);
            p += length;
        }
        return;
    }

    char* words[2 + SCRIPT_MAX_ARGUMENTS];
    u32 count = 0;
    for (char* p = line; *p && count < NV_ARRAY_COUNT(words);) {
        while (*p == ' ' || *p == '\t')
            *p++ = 0;
        if (!*p)
            break;
        words[count++] = p;
        while (*p && *p != ' ' && *p != '\t')
            ++p;
    }
    if (count == 0 || words[0][0] == '#')
        return;

    NvWindow* window = &sandbox->window;
    const char* command = words[0];
    if (strcmp(command, "size") == 0 && count == 3) {
        nv_window_win32_set_fixed_size(window, (u32)atoi(words[1]), (u32)atoi(words[2]));
    } else if (strcmp(command, "frames") == 0 && count == 2) {
        script.frames_left = (u32)atoi(words[1]);
    } else if (strcmp(command, "screenshot") == 0 && count >= 2) {
        char path[PATH_CAPACITY];
        resolve_path(path, sizeof(path), words[1]);
        if (count == 3 && strcmp(words[2], "viewport") == 0) {
            NvRect v = sandbox->layout.viewport;
            nv_gpu_debug_screenshot(&sandbox->gpu, path, v.x, v.y, v.width, v.height);
        } else if (count == 6) {
            nv_gpu_debug_screenshot(&sandbox->gpu, path, (u32)atoi(words[2]), (u32)atoi(words[3]), (u32)atoi(words[4]), (u32)atoi(words[5]));
        } else {
            nv_gpu_debug_screenshot(&sandbox->gpu, path, 0, 0, 0, 0);
        }
    } else if (strcmp(command, "benchmark") == 0 && count == 2) {
        benchmark_start(sandbox);
        if (!sandbox->benchmark.running) {
            nv_log(NV_LOG_ERROR, "script", "the shown scene has no benchmark to run");
            exit(2);
        }
        resolve_path(script.benchmark_path, sizeof(script.benchmark_path), words[1]);
        script.waiting_benchmark = 1;
    } else if (strcmp(command, "wait-fixed") == 0 && count == 2) {
        script.waiting_fixed = 1;
        script.fixed_frames_wanted = (u32)atoi(words[1]);
    } else if (strcmp(command, "case") == 0 && count == 4) {
        char path[PATH_CAPACITY], file_name[160];
        snprintf(file_name, sizeof(file_name), "%s.json", words[1]);
        resolve_path(path, sizeof(path), file_name);
        script.case_file = fopen(path, "wb");
        if (!script.case_file) {
            nv_log(NV_LOG_ERROR, "script", "cannot write %s", path);
            exit(2);
        }
        script.case_has_frame = 0;
        fprintf(script.case_file, "{\"case\":\"%s\",\"dt\":%.17g,\"counters\":%s,\"frames\":{", words[1], atof(words[2]),
                atoi(words[3]) ? "true" : "false");
    } else if (strcmp(command, "case-frame") == 0 && count == 3 && script.case_file) {
        script.dump_frame = (u32)atoi(words[1]);
        nv_vfx_debug_dump_start(&sandbox->vfx, strcmp(words[2], "full") == 0);
        script.dump_pending = 1;
    } else if (strcmp(command, "case-end") == 0 && script.case_file) {
        fprintf(script.case_file, "}}");
        fclose(script.case_file);
        script.case_file = NULL;
    } else if (strcmp(command, "move") == 0 && count == 3) {
        script.pointer_x = atoi(words[1]);
        script.pointer_y = atoi(words[2]);
        nv_window_win32_inject_mouse(window, script.pointer_x, script.pointer_y, -1, 0);
    } else if (strcmp(command, "mouse") == 0 && count >= 2) {
        s32 button = count >= 3 ? (strcmp(words[2], "right") == 0 ? 2 : strcmp(words[2], "middle") == 0 ? 1 : 0) : 0;
        nv_window_win32_inject_mouse(window, script.pointer_x, script.pointer_y, button, strcmp(words[1], "down") == 0);
    } else if (strcmp(command, "wheel") == 0 && count == 2) {
        nv_window_win32_inject_wheel(window, script.pointer_x, script.pointer_y, atoi(words[1]));
    } else if (strcmp(command, "key") == 0 && count >= 2) {
        u32 key = virtual_key_from_name(words[1]);
        if (count == 2 || strcmp(words[2], "down") == 0)
            nv_window_win32_inject_key(window, key, 1);
        if (count == 2 || strcmp(words[2], "up") == 0)
            nv_window_win32_inject_key(window, key, 0);
    } else if (strcmp(command, "modifiers") == 0) {
        u32 modifiers = 0;
        for (u32 i = 1; i < count; ++i)
            modifiers |= strcmp(words[i], "ctrl") == 0 ? 1u : strcmp(words[i], "shift") == 0 ? 2u : strcmp(words[i], "alt") == 0 ? 4u : 0u;
        nv_window_win32_inject_modifiers(modifiers);
    } else if (strcmp(command, "close") == 0) {
        nv_window_win32_inject_close(window);
        script.frames_left = ~0u; // the window closes after this frame
    } else if (strcmp(command, "call") == 0 && count >= 2) {
        const DebugCall* entry = NULL;
        for (u32 i = 0; i < sandbox_debug_call_count && !entry; ++i)
            entry = strcmp(sandbox_debug_calls[i].name, words[1]) == 0 ? &sandbox_debug_calls[i] : NULL;
        if (!entry) {
            nv_log(NV_LOG_ERROR, "script", "unknown export %s", words[1]);
            exit(2);
        }
        f64 arguments[SCRIPT_MAX_ARGUMENTS] = {0};
        for (u32 i = 2; i < count && i - 2 < SCRIPT_MAX_ARGUMENTS; ++i)
            arguments[i - 2] = atof(words[i]);
        char result_kind = entry->signature[strlen(entry->signature) - 1];
        if (result_kind == 's') {
            nv_log(NV_LOG_INFO, "script", "%s = \"%s\"", entry->name, ((char* (*)(void))entry->function)());
        } else {
            f64 result = call_export(entry, arguments);
            if (result_kind != 'v')
                nv_log(NV_LOG_INFO, "script", "%s = %.9g", entry->name, result);
        }
    } else if (strcmp(command, "quit") == 0) {
        script.quit = 1;
    } else {
        nv_log(NV_LOG_ERROR, "script", "cannot run: %s", command);
        exit(2);
    }
}

void debug_script_update(Sandbox* sandbox)
{
    if (!script.active)
        return;
    if (script.dump_pending) {
        if (nv_vfx_debug_dump_state(&sandbox->vfx) != NV_VFX_DUMP_READY)
            return; // the readbacks arrive in a frame or two: the next command waits for them
        write_dump(sandbox);
        nv_vfx_debug_dump_finish(&sandbox->vfx);
        script.dump_pending = 0;
    }
    if (nv_gpu_debug_screenshot_pending(&sandbox->gpu))
        return; // the picture is taken when this frame ends: the next command waits for it
    if (script.waiting_fixed) {
        // The frames the fixed clock has run, which the `frames` export tells.
        for (u32 i = 0; i < sandbox_debug_call_count; ++i) {
            if (strcmp(sandbox_debug_calls[i].name, "frames") == 0) {
                if (((int (*)(void))sandbox_debug_calls[i].function)() < (int)script.fixed_frames_wanted)
                    return;
            }
        }
        script.waiting_fixed = 0;
    }
    if (script.waiting_benchmark) {
        if (sandbox->benchmark.running)
            return;
        char table[4096];
        benchmark_table_text(sandbox, table, sizeof(table));
        FILE* file = fopen(script.benchmark_path, "wb");
        if (!file) {
            nv_log(NV_LOG_ERROR, "script", "cannot write %s", script.benchmark_path);
            exit(2);
        }
        fputs(table, file);
        fclose(file);
        script.waiting_benchmark = 0;
    }
    if (script.frames_left) {
        --script.frames_left;
        return;
    }
    while (script.next_line && *script.next_line && !script.quit && !script.frames_left && !script.dump_pending && !script.waiting_fixed && !script.waiting_benchmark &&
           !nv_gpu_debug_screenshot_pending(&sandbox->gpu)) {
        char* line = script.next_line;
        char* end = strchr(line, '\n');
        if (end) {
            *end = 0;
            script.next_line = end + 1;
        } else {
            script.next_line = NULL;
        }
        // The line may end in a carriage return.
        umm length = strlen(line);
        if (length && line[length - 1] == '\r')
            line[length - 1] = 0;
        run_command(sandbox, line);
    }
    if (script.quit || !script.next_line || !*script.next_line) {
        if (!nv_gpu_debug_screenshot_pending(&sandbox->gpu) && !script.frames_left && !script.dump_pending && !script.waiting_fixed && !script.waiting_benchmark) {
            u64 errors = nv_log_ring.arrived[NV_LOG_ERROR];
            u64 warnings = nv_log_ring.arrived[NV_LOG_WARNING];
            nv_log(NV_LOG_INFO, "script", "done: %llu error(s), %llu warning(s) in the log", (unsigned long long)errors,
                   (unsigned long long)warnings);
            exit(errors ? 1 : 0);
        }
    }
}

#endif
