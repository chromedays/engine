#include "engine/file.h"

#include <stdio.h>

u8* nv_file_read(NvArena* arena, const char* path, umm* size)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return NULL;
    u8* bytes = NULL;
    umm mark = arena->used;
    if (fseek(file, 0, SEEK_END) == 0) {
        long length = ftell(file);
        // The room left, less what aligning to 16 may skip.
        umm room = arena->size - arena->used;
        if (length >= 0 && (umm)length + 1 + 15 <= room && fseek(file, 0, SEEK_SET) == 0) {
            bytes = nv_arena_push(arena, (umm)length + 1, 16); // zeroed, so the byte after the file is 0
            if (fread(bytes, 1, (umm)length, file) == (umm)length) {
                *size = (umm)length;
            } else {
                arena->used = mark;
                bytes = NULL;
            }
        }
    }
    fclose(file);
    return bytes;
}
