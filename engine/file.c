#include "engine/file.h"

#include <stdio.h>
#include <string.h>

#if NV_PLATFORM_WINDOWS
#include "engine/win32.h"

// The folder of the executable, with a '\' at the end, found once.
internal const char* executable_folder(void)
{
    local_persist char folder[520];
    if (!folder[0]) {
        wchar_t path[MAX_PATH];
        DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
        while (length && path[length - 1] != L'\\')
            --length;
        path[length] = 0;
        nv_win32_utf8_from_wide(path, folder, sizeof(folder));
    }
    return folder;
}

void nv_file_resolve_path(const char* path, char* out, umm capacity)
{
    if (path[0] == '/')
        snprintf(out, capacity, "%s%s", executable_folder(), path + 1);
    else
        snprintf(out, capacity, "%s", path);
}
#elif NV_PLATFORM_LINUX
#include <unistd.h>

// The folder of the executable, with a '/' at the end, found once; "" when the system does not say (or the path is too long).
internal const char* executable_folder(void)
{
    local_persist char folder[520];
    local_persist b32 found;
    if (!found) {
        found = 1;
        ssize_t length = readlink("/proc/self/exe", folder, sizeof(folder) - 1);
        if (length < 0 || length >= (ssize_t)sizeof(folder) - 1)
            length = 0;
        while (length && folder[length - 1] != '/')
            --length;
        folder[length] = 0;
    }
    return folder;
}

// The packed files ("/assets/...", "/data/...") are beside the executable and are looked for there first. A real absolute path
// also starts with '/' here (a save folder, a file a test reads), and is used as it is when the executable's folder has no such
// file.
void nv_file_resolve_path(const char* path, char* out, umm capacity)
{
    if (path[0] == '/' && executable_folder()[0]) {
        snprintf(out, capacity, "%s%s", executable_folder(), path + 1);
        if (access(out, F_OK) == 0)
            return;
    }
    snprintf(out, capacity, "%s", path);
}
#else
void nv_file_resolve_path(const char* path, char* out, umm capacity)
{
    snprintf(out, capacity, "%s", path);
}
#endif

NvFileData nv_file_read(NvArena* arena, const char* path)
{
    char resolved[520];
    nv_file_resolve_path(path, resolved, sizeof(resolved));
    FILE* file = fopen(resolved, "rb");
    if (!file)
        return (NvFileData){0};
    NvFileData result = {0};
    umm mark = arena->used;
    if (fseek(file, 0, SEEK_END) == 0) {
        long length = ftell(file);
        umm room = arena->size - arena->used;
        if (length >= 0 && (umm)length + 1 <= room && fseek(file, 0, SEEK_SET) == 0) {
            u8* bytes = nv_arena_push(arena, (umm)length + 1, 1); // zeroed, so the byte after the file is 0
            if (fread(bytes, 1, (umm)length, file) == (umm)length)
                result = (NvFileData){.ok = 1, .bytes = bytes, .size = (umm)length};
            else
                arena->used = mark;
        }
    }
    fclose(file);
    return result;
}
