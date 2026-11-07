#include "engine/log.h"
#include "engine/storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Files that outlive the program, on Linux (docs/specs/linux_wayland.md): the folder $XDG_DATA_HOME/nv/<target>/, by default
// ~/.local/share/nv/<target>/, as Windows keeps %LOCALAPPDATA%\nv\<target>\. A write goes to a temporary file that replaces the
// old one in one rename, so there is nothing to flush: every write is on the disk when it returns.

#define PATH_CAPACITY 520

global char directory[PATH_CAPACITY];
global char last_error[160];

// The path of `name` in the folder. False when it does not fit: a path cut short would name another file.
internal b32 full_path(const char* name, char* out, umm capacity)
{
    int length = snprintf(out, capacity, "%s/%s", directory, name);
    return length >= 0 && (umm)length < capacity;
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

// <the executable's file name>: what tells the targets' folders apart, as on Windows.
internal void target_name(char* out, umm capacity)
{
    char path[PATH_CAPACITY];
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    path[length > 0 ? length : 0] = 0;
    const char* name = strrchr(path, '/');
    snprintf(out, capacity, "%s", name && name[1] ? name + 1 : "nv");
}

void nv_storage_init(NvStorage* storage, const char* dir)
{
    (void)dir; // the web's mount point; here every target has its folder
    storage->dir = directory;
    storage->available = 0;
    directory[0] = 0;
#if NV_DEBUG
    // A Debug build can be told where to keep its files (NV_STORAGE_DIR), so a test run starts from empty storage and leaves the
    // developer's own save alone, as on Windows.
    const char* chosen = getenv("NV_STORAGE_DIR");
    if (chosen && chosen[0])
        snprintf(directory, sizeof(directory), "%s", chosen);
#endif
    if (!directory[0]) {
        char target[64];
        target_name(target, sizeof(target));
        // The XDG base directory rules: a relative $XDG_DATA_HOME is invalid and ignored.
        const char* data_home = getenv("XDG_DATA_HOME");
        const char* home = getenv("HOME");
        if (data_home && data_home[0] == '/') {
            snprintf(directory, sizeof(directory), "%s/nv/%s", data_home, target);
        } else if (home && home[0]) {
            snprintf(directory, sizeof(directory), "%s/.local/share/nv/%s", home, target);
        } else {
            nv_log(NV_LOG_WARNING, "nv", "storage is unavailable: no $HOME");
            return;
        }
    }
    char copy[PATH_CAPACITY];
    snprintf(copy, sizeof(copy), "%s", directory);
    if (!make_directories(copy)) {
        set_error("cannot create", directory);
        return;
    }
    storage->available = 1;
    nv_log(NV_LOG_INFO, "nv", "storage: %s", directory);
}

b32 nv_storage_write(NvStorage* storage, const char* name, const void* bytes, u32 size)
{
    if (!storage->available)
        return 0;
    char path[PATH_CAPACITY], temporary[PATH_CAPACITY + 8];
    if (!full_path(name, path, sizeof(path))) {
        set_error("path too long:", name);
        return 0;
    }
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
    if (!full_path(name, path, sizeof(path)))
        return (NvFileData){0};
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
    if (!full_path(from, from_path, sizeof(from_path)) || !full_path(to, to_path, sizeof(to_path)))
        return 0;
    return rename(from_path, to_path) == 0; // a rename replaces, as storage.h promises
}

b32 nv_storage_remove(NvStorage* storage, const char* name)
{
    if (!storage->available)
        return 0;
    char path[PATH_CAPACITY];
    return full_path(name, path, sizeof(path)) && remove(path) == 0;
}

b32 nv_storage_exists(NvStorage* storage, const char* name)
{
    if (!storage->available)
        return 0;
    char path[PATH_CAPACITY];
    return full_path(name, path, sizeof(path)) && access(path, F_OK) == 0;
}

void nv_storage_flush(NvStorage* storage)
{
    (void)storage; // every write is on the disk when it returns
}

// Starts the program again with the same command line, as the page reloads on the web: the process becomes a new run of the same
// executable (execv keeps nothing of this one). Every write is on the disk already.
void nv_storage_flush_then_reload(NvStorage* storage)
{
    (void)storage;
    // The command line as it was: /proc/self/cmdline holds the arguments, each ended by a 0 byte.
    local_persist char line[8192];
    char* arguments[128];
    u32 count = 0;
    FILE* file = fopen("/proc/self/cmdline", "rb");
    umm size = file ? fread(line, 1, sizeof(line) - 1, file) : 0;
    if (file)
        fclose(file);
    line[size] = 0;
    for (umm at = 0; at < size && count < NV_ARRAY_COUNT(arguments) - 1; at += strlen(line + at) + 1)
        arguments[count++] = line + at;
    arguments[count] = NULL;
    if (count)
        execv("/proc/self/exe", arguments);
    // Going on would write the old state over the files just removed, so the program ends instead.
    nv_log(NV_LOG_ERROR, "nv", "starting again failed (%s): run the program again", count ? strerror(errno) : "no command line");
    exit(1);
}

const char* nv_storage_error(NvStorage* storage)
{
    (void)storage;
    return last_error;
}
