#pragma once

#include "nv/base.h"

// Files that outlive the page: a directory of Emscripten's in-memory file system mounted on
// IndexedDB (IDBFS). Files there are read and written with the C file API as usual; nothing
// reaches IndexedDB until nv_storage_flush copies it there (docs/specs/save.md).
typedef struct NvStorage {
    const char* dir; // the mount point, e.g. "/nv-save"
    b32 available;   // mounted, and the saved files were read back; 0 when the browser refused
} NvStorage;

// Mounts `dir` and waits (through Asyncify) until the files saved earlier are back in memory. Also
// asks the browser to keep the data under storage pressure. Leaves `available` 0 on failure.
void nv_storage_init(NvStorage* storage, const char* dir);

// Writes `name` in the directory through a temporary file and a rename, so a failed write never
// replaces the old file. Returns 0 on failure.
b32 nv_storage_write(NvStorage* storage, const char* name, const void* bytes, u32 size);

// Reads `name` into memory pushed from `arena`. Returns its size (0 if it is missing or empty or
// larger than `max_size`); `bytes` points at it.
u32 nv_storage_read(NvStorage* storage, const char* name, NvArena* arena, u32 max_size, u8** bytes);

// Renames or deletes a file in the directory. Return 0 on failure.
b32 nv_storage_rename(NvStorage* storage, const char* from, const char* to);
b32 nv_storage_remove(NvStorage* storage, const char* name);

// Starts copying the directory to IndexedDB and returns without waiting. A flush asked for while
// one runs starts when that one ends.
void nv_storage_flush(NvStorage* storage);

// The last flush's error, or "" (for display).
const char* nv_storage_error(NvStorage* storage);
