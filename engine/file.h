#pragma once

#include "engine/base.h"

// Files the executable carries (packed by nv_setup_executable's ASSETS or PRELOAD), read whole with the C file API.

// A file's bytes as read (nv_file_read, nv_storage_read). Zeroed when the read failed.
typedef struct NvFileData {
    b32 ok;
    u8* bytes;
    umm size;
} NvFileData;

// The path as the C file API takes it. The web's packed files are at "/assets/..." and "/data/..."; on Windows they are beside the
// executable (nv_setup_executable copies them), so a path that starts with "/" is relative to the executable's folder there. Any
// other path is as it is. `out` is cut to `capacity` and always terminated.
void nv_file_resolve_path(const char* path, char* out, umm capacity);

// The bytes of the file at `path` (resolved as above), pushed from `arena` (not aligned) with a 0 byte after them, so a text file is also a
// C string; `size` is the size without that byte. Fails, with the arena as it was, when the file is missing, cannot be read
// whole or does not fit in what is left of the arena. Logs nothing: the caller says what the file was for.
NvFileData nv_file_read(NvArena* arena, const char* path);
