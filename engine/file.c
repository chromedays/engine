#include "engine/file.h"

#include <stdio.h>

NvFileData nv_file_read(NvArena* arena, const char* path)
{
    FILE* file = fopen(path, "rb");
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
