#include "engine/log.h"
#include "engine/storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Files that outlive the program, on Linux: the directory storage.h is handed, made if missing.
// A write goes to a temporary file that replaces the old one in one rename, so there is nothing
// to flush: every write is on the disk when it returns.

#define PATH_CAPACITY 520

global char directory[PATH_CAPACITY];
global char last_error[160];

internal void full_path(const char* name, char* out, umm capacity)
{
    snprintf(out, capacity, "%s/%s", directory, name);
}

internal void set_error(const char* what, const char* path)
{
    // Bounded precisions: the error buffer is smaller than a full path, by design.
    snprintf(last_error, sizeof(last_error), "%.40s %.100s", what, path);
    nv_log(NV_LOG_WARNING, "nv", "%s", last_error);
}

// Makes `path` and every folder above it, as mkdir -p does.
internal b32 make_directories(char* path)
{
    for (char* c = path + 1; *c; ++c) {
        if (*c == '/') {
            *c = 0;
            if (mkdir(path, 0777) != 0 && access(path, F_OK) != 0)
                return 0;
            *c = '/';
        }
    }
    return mkdir(path, 0777) == 0 || access(path, F_OK) == 0;
}

void nv_storage_init(NvStorage* storage, const char* dir)
{
    snprintf(directory, sizeof(directory), "%s", dir);
    storage->dir = directory;
    storage->available = 0;
    char copy[PATH_CAPACITY];
    snprintf(copy, sizeof(copy), "%s", directory);
    if (!make_directories(copy)) {
        set_error("cannot create", directory);
        return;
    }
    storage->available = 1;
}

b32 nv_storage_write(NvStorage* storage, const char* name, const void* bytes, u32 size)
{
    if (!storage->available)
        return 0;
    char path[PATH_CAPACITY], temporary[PATH_CAPACITY + 8];
    full_path(name, path, sizeof(path));
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE* file = fopen(temporary, "wb");
    if (!file) {
        set_error("cannot write", temporary);
        return 0;
    }
    b32 ok = fwrite(bytes, 1, size, file) == size && fclose(file) == 0;
    if (!ok || rename(temporary, path) != 0) {
        set_error("cannot write", path);
        remove(temporary);
        return 0;
    }
    last_error[0] = 0;
    return 1;
}

NvFileData nv_storage_read(NvStorage* storage, const char* name, NvArena* arena, u32 max_size)
{
    if (!storage->available)
        return (NvFileData){0};
    char path[PATH_CAPACITY];
    full_path(name, path, sizeof(path));
    umm mark = arena->used;
    NvFileData file = nv_file_read(arena, path);
    if (file.ok && (file.size == 0 || file.size > max_size)) {
        arena->used = mark;
        return (NvFileData){0};
    }
    return file;
}

b32 nv_storage_rename(NvStorage* storage, const char* from, const char* to)
{
    if (!storage->available)
        return 0;
    char from_path[PATH_CAPACITY], to_path[PATH_CAPACITY];
    full_path(from, from_path, sizeof(from_path));
    full_path(to, to_path, sizeof(to_path));
    return rename(from_path, to_path) == 0; // a rename replaces, as storage.h promises
}

b32 nv_storage_remove(NvStorage* storage, const char* name)
{
    if (!storage->available)
        return 0;
    char path[PATH_CAPACITY];
    full_path(name, path, sizeof(path));
    return remove(path) == 0;
}

b32 nv_storage_exists(NvStorage* storage, const char* name)
{
    if (!storage->available)
        return 0;
    char path[PATH_CAPACITY];
    full_path(name, path, sizeof(path));
    return access(path, F_OK) == 0;
}

void nv_storage_flush(NvStorage* storage)
{
    (void)storage; // every write is on the disk when it returns
}

void nv_storage_flush_then_reload(NvStorage* storage)
{
    (void)storage;
    // There is no page to reload: the web's "start over" has no meaning for a program that can
    // simply be run again, so this only says so.
    nv_log(NV_LOG_WARNING, "nv", "nv_storage_flush_then_reload does nothing on Linux");
}

const char* nv_storage_error(NvStorage* storage)
{
    (void)storage;
    return last_error;
}
