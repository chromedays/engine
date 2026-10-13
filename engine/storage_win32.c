#include "engine/win32.h"

#include "engine/log.h"
#include "engine/storage.h"

#include <stdio.h>
#include <string.h>

// Files that outlive the program, on Windows (docs/specs/d3d12_port.md, "Platform layer"): the folder %LOCALAPPDATA%\nv\<target>\.
// A write goes to a temporary file that replaces the old one in one step, and reaches the disk before it returns, so there is
// nothing to flush.

#define PATH_CAPACITY 520

global char directory[PATH_CAPACITY];
global char last_error[160];

// <the executable's file name without ".exe">: what tells the targets' folders apart.
internal void target_name(char* out, umm capacity)
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    wchar_t* dot = wcsrchr((wchar_t*)name, L'.');
    if (dot)
        *dot = 0;
    nv_win32_utf8_from_wide(name, out, (int)capacity);
}

internal void full_path(const char* name, char* out, umm capacity)
{
    snprintf(out, capacity, "%s\\%s", directory, name);
}

internal void set_error(const char* what, const char* path)
{
    snprintf(last_error, sizeof(last_error), "%s %s (error %lu)", what, path, GetLastError());
    nv_log(NV_LOG_WARNING, "nv", "%s", last_error);
}

void nv_storage_init(NvStorage* storage, const char* dir)
{
    (void)dir; // the web's mount point; here every target has its folder
    storage->dir = directory;
    storage->available = 0;
#if !defined(NDEBUG)
    // A Debug build can be told where to keep its files (NV_STORAGE_DIR: a folder that exists), so a scripted run starts from empty
    // storage, as every browser launch of the web harness does, and leaves the developer's own save alone.
    wchar_t folder[MAX_PATH];
    DWORD folder_length = GetEnvironmentVariableW(L"NV_STORAGE_DIR", folder, MAX_PATH);
    if (folder_length > 0 && folder_length < MAX_PATH) {
        nv_win32_utf8_from_wide(folder, directory, sizeof(directory));
        storage->available = 1;
        return;
    }
#endif
    wchar_t base[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        nv_log(NV_LOG_WARNING, "nv", "storage is unavailable: no %%LOCALAPPDATA%%");
        return;
    }
    char base_utf8[PATH_CAPACITY];
    nv_win32_utf8_from_wide(base, base_utf8, sizeof(base_utf8));
    char target[64];
    target_name(target, sizeof(target));
    snprintf(directory, sizeof(directory), "%s\\nv\\%s", base_utf8, target);

    // The folders one after the other: nv, then the target's.
    wchar_t wide[PATH_CAPACITY];
    nv_win32_wide_from_utf8(directory, wide, NV_ARRAY_COUNT(wide));
    for (wchar_t* c = wide + wcslen(base) + 1; *c; ++c) {
        if (*c == L'\\') {
            *c = 0;
            CreateDirectoryW(wide, NULL);
            *c = L'\\';
        }
    }
    if (!CreateDirectoryW(wide, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        set_error("cannot create", directory);
        return;
    }
    storage->available = 1;
}

b32 nv_storage_write(NvStorage* storage, const char* name, const void* bytes, u32 size)
{
    if (!storage->available)
        return 0;
    char path[PATH_CAPACITY], temporary[PATH_CAPACITY];
    full_path(name, path, sizeof(path));
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    wchar_t wide_path[PATH_CAPACITY], wide_temporary[PATH_CAPACITY];
    nv_win32_wide_from_utf8(path, wide_path, NV_ARRAY_COUNT(wide_path));
    nv_win32_wide_from_utf8(temporary, wide_temporary, NV_ARRAY_COUNT(wide_temporary));

    HANDLE file = CreateFileW(wide_temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        set_error("cannot write", temporary);
        return 0;
    }
    DWORD written = 0;
    b32 ok = WriteFile(file, bytes, size, &written, NULL) && written == size && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(wide_temporary, wide_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        set_error("cannot write", path);
        DeleteFileW(wide_temporary);
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
    wchar_t wide_from[PATH_CAPACITY], wide_to[PATH_CAPACITY];
    nv_win32_wide_from_utf8(from_path, wide_from, NV_ARRAY_COUNT(wide_from));
    nv_win32_wide_from_utf8(to_path, wide_to, NV_ARRAY_COUNT(wide_to));
    return MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

b32 nv_storage_remove(NvStorage* storage, const char* name)
{
    if (!storage->available)
        return 0;
    char path[PATH_CAPACITY];
    full_path(name, path, sizeof(path));
    wchar_t wide[PATH_CAPACITY];
    nv_win32_wide_from_utf8(path, wide, NV_ARRAY_COUNT(wide));
    return DeleteFileW(wide) != 0;
}

b32 nv_storage_exists(NvStorage* storage, const char* name)
{
    if (!storage->available)
        return 0;
    char path[PATH_CAPACITY];
    full_path(name, path, sizeof(path));
    wchar_t wide[PATH_CAPACITY];
    nv_win32_wide_from_utf8(path, wide, NV_ARRAY_COUNT(wide));
    return GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
}

void nv_storage_flush(NvStorage* storage)
{
    (void)storage; // every write is on the disk when it returns
}

// Starts the program again with the same command line, then ends this one: the page reloads on the web.
void nv_storage_flush_then_reload(NvStorage* storage)
{
    (void)storage;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process = {0};
    // The command line is a writable copy: CreateProcess may change it.
    wchar_t command_line[2048];
    wcsncpy(command_line, GetCommandLineW(), NV_ARRAY_COUNT(command_line) - 1);
    command_line[NV_ARRAY_COUNT(command_line) - 1] = 0;
    if (CreateProcessW(exe, command_line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    ExitProcess(0);
}

const char* nv_storage_error(NvStorage* storage)
{
    (void)storage;
    return last_error;
}
