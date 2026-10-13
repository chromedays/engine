#include "engine/file.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
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
