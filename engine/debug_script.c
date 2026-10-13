#include <engine/debug_script.h>

#if defined(_WIN32) && !defined(NDEBUG)

#include <engine/file.h>
#include <engine/log.h>
#include <engine/window_win32.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRIPT_MAX_SIZE NV_KILOBYTES(64)
#define SCRIPT_MAX_WORDS 10
#define PATH_CAPACITY 520

typedef struct DebugScript {
    NvDebugScriptDesc desc;
    b32 active;
    char* next_line; // the next line to run
    u32 frames_left; // frames to run before the next command
    b32 quit;
    s32 pointer_x, pointer_y;
    char out_directory[PATH_CAPACITY]; // where a relative path goes; empty: where the program runs
} DebugScript;

global DebugScript script;

void nv_debug_script_init(const NvDebugScriptDesc* desc)
{
    char path[PATH_CAPACITY];
    if (!nv_window_win32_command_line_value("--script", path, sizeof(path)))
        return;
    NvFileData file = nv_file_read(desc->arena, path);
    if (!file.ok || file.size > SCRIPT_MAX_SIZE) {
        nv_log(NV_LOG_ERROR, "script", "cannot read %s", path);
        exit(2);
    }
    script.desc = *desc;
    script.active = 1;
    if (!nv_window_win32_command_line_value("--out", script.out_directory, sizeof(script.out_directory)))
        script.out_directory[0] = 0;
    script.next_line = (char*)file.bytes;
    nv_log(NV_LOG_INFO, "script", "running %s", path);
}

// Calls a debug export. The signatures are the ones of the apps' tables; a new one is a case here.
internal f64 call_export(const NvDebugCall* entry, const f64* a)
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
    if (SIGNATURE("iii>i")) return ((int (*)(int, int, int))function)((int)a[0], (int)a[1], (int)a[2]);
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

void nv_debug_script_resolve_path(char* out, umm capacity, const char* path)
{
    b32 absolute = path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
    if (absolute || !script.out_directory[0])
        snprintf(out, capacity, "%s", path);
    else
        snprintf(out, capacity, "%s/%s", script.out_directory, path);
}

internal void run_command(char* line)
{
    NvWindow* window = script.desc.window;
    NvGpu* gpu = script.desc.gpu;
    // A text command keeps the rest of the line as it is.
    if (strncmp(line, "text ", 5) == 0) {
        for (const char* p = line + 5; *p;) {
            u32 length = nv_utf8_length((u8)*p);
            u32 codepoint = length == 1 ? (u8)p[0]
                            : length == 2 ? (((u8)p[0] & 0x1Fu) << 6) | ((u8)p[1] & 0x3Fu)
                            : length == 3 ? (((u8)p[0] & 0x0Fu) << 12) | (((u8)p[1] & 0x3Fu) << 6) | ((u8)p[2] & 0x3Fu)
                                          : (((u8)p[0] & 0x07u) << 18) | (((u8)p[1] & 0x3Fu) << 12) | (((u8)p[2] & 0x3Fu) << 6) | ((u8)p[3] & 0x3Fu);
            nv_window_win32_inject_char(window, codepoint);
            p += length;
        }
        return;
    }

    char* words[SCRIPT_MAX_WORDS];
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

    const char* command = words[0];
    if (strcmp(command, "size") == 0 && count == 3) {
        nv_window_win32_set_fixed_size(window, (u32)atoi(words[1]), (u32)atoi(words[2]));
    } else if (strcmp(command, "frames") == 0 && count == 2) {
        script.frames_left = (u32)atoi(words[1]);
    } else if (strcmp(command, "screenshot") == 0 && count >= 2) {
        char path[PATH_CAPACITY];
        nv_debug_script_resolve_path(path, sizeof(path), words[1]);
        if (count == 3 && strcmp(words[2], "viewport") == 0 && script.desc.viewport) {
            NvRect v = script.desc.viewport(script.desc.user);
            nv_gpu_debug_screenshot(gpu, path, v.x, v.y, v.width, v.height);
        } else if (count == 6) {
            nv_gpu_debug_screenshot(gpu, path, (u32)atoi(words[2]), (u32)atoi(words[3]), (u32)atoi(words[4]), (u32)atoi(words[5]));
        } else {
            nv_gpu_debug_screenshot(gpu, path, 0, 0, 0, 0);
        }
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
        const NvDebugCall* entry = NULL;
        for (u32 i = 0; i < script.desc.call_count && !entry; ++i)
            entry = strcmp(script.desc.calls[i].name, words[1]) == 0 ? &script.desc.calls[i] : NULL;
        if (!entry) {
            nv_log(NV_LOG_ERROR, "script", "unknown export %s", words[1]);
            exit(2);
        }
        f64 arguments[SCRIPT_MAX_WORDS] = {0};
        for (u32 i = 2; i < count; ++i)
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
    } else if (!script.desc.command || !script.desc.command(script.desc.user, (const char* const*)words, count)) {
        nv_log(NV_LOG_ERROR, "script", "cannot run: %s", command);
        exit(2);
    }
}

internal b32 waiting(void)
{
    return (script.desc.busy && script.desc.busy(script.desc.user)) || nv_gpu_debug_screenshot_pending(script.desc.gpu);
}

void nv_debug_script_update(void)
{
    if (!script.active)
        return;
    if (waiting())
        return; // a picture is taken when the frame ends, a readback arrives in a frame or two: the next command waits
    if (script.frames_left) {
        --script.frames_left;
        return;
    }
    while (script.next_line && *script.next_line && !script.quit && !script.frames_left && !waiting()) {
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
        run_command(line);
    }
    if (script.quit || !script.next_line || !*script.next_line) {
        if (!script.frames_left && !waiting()) {
            u64 errors = nv_log_ring.arrived[NV_LOG_ERROR];
            u64 warnings = nv_log_ring.arrived[NV_LOG_WARNING];
            nv_log(NV_LOG_INFO, "script", "done: %llu error(s), %llu warning(s) in the log", (unsigned long long)errors,
                   (unsigned long long)warnings);
            exit(errors ? 1 : 0);
        }
    }
}

#endif
