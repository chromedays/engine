#include "engine/win32.h"

#include "engine/local_file.h"

#include <commdlg.h>
#include <stdio.h>
#include <string.h>

// Files on the user's computer, on Windows (docs/specs/d3d12_port.md, "Platform layer"): the common open and save dialogs. They are
// modal, so an operation is finished when the call returns, and its result waits for the next nv_local_file_poll, as on the web.
// The file opened or saved last is kept by its path.

#define PATH_CAPACITY 520
#define READ_CAPACITY NV_MEGABYTES(1) // a file larger than this is read as far as its size, and refused by nv_local_file_take

typedef struct LocalFileState {
    NvLocalFileStatus status;
    wchar_t path[PATH_CAPACITY]; // the kept file's; empty when none
    char name[128];
    char error[160];
    u64 modified; // the kept file's last write, as FILETIME ticks, when it was read or written here
    u8 bytes[READ_CAPACITY];
    s64 size; // of the bytes read last; -1 when there are none
} LocalFileState;

global LocalFileState state = {.size = -1};

internal void set_name(const wchar_t* path)
{
    const wchar_t* name = wcsrchr(path, L'\\');
    nv_win32_utf8_from_wide(name ? name + 1 : path, state.name, sizeof(state.name));
    nv_utf8_trim(state.name);
}

internal void fail(const char* what)
{
    state.status = NV_LOCAL_FILE_FAILED;
    snprintf(state.error, sizeof(state.error), "%s (error %lu)", what, GetLastError());
}

internal u64 last_write_time(const wchar_t* path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data))
        return 0;
    return ((u64)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
}

// Reads the kept file into `bytes`.
internal void read_kept_file(void)
{
    state.size = -1;
    HANDLE file = CreateFileW(state.path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        fail("cannot open the file");
        return;
    }
    LARGE_INTEGER size;
    GetFileSizeEx(file, &size);
    DWORD read = 0;
    b32 ok = size.QuadPart <= READ_CAPACITY ? ReadFile(file, state.bytes, (DWORD)size.QuadPart, &read, NULL) && read == size.QuadPart : 1;
    CloseHandle(file);
    if (!ok) {
        fail("cannot read the file");
        return;
    }
    state.size = size.QuadPart;
    state.modified = last_write_time(state.path);
    set_name(state.path);
    state.status = NV_LOCAL_FILE_READ;
}

#if !defined(NDEBUG)
// A Debug build can be told where the dialogs would have put the file, so a test needs no one at them: the environment variable
// NV_LOCAL_FILE names the path.
internal b32 path_from_test(wchar_t* out, umm capacity)
{
    DWORD length = GetEnvironmentVariableW(L"NV_LOCAL_FILE", out, (DWORD)capacity);
    return length > 0 && length < capacity;
}
#endif

b32 nv_local_file_open(const char* extension)
{
    // The filter is pairs of text ending in two NULs: "<extension> files", "*<extension>".
    wchar_t wide_extension[32];
    nv_win32_wide_from_utf8(extension, wide_extension, NV_ARRAY_COUNT(wide_extension));
    wchar_t filter[96] = {0};
    int length = swprintf(filter, NV_ARRAY_COUNT(filter) - 2, L"%ls files", wide_extension);
    swprintf(filter + length + 1, NV_ARRAY_COUNT(filter) - length - 2, L"*%ls", wide_extension);

    wchar_t path[PATH_CAPACITY] = {0};
#if !defined(NDEBUG)
    if (path_from_test(path, NV_ARRAY_COUNT(path))) {
        wcscpy(state.path, path);
        read_kept_file();
        return 1;
    }
#endif
    OPENFILENAMEW dialog = {
        .lStructSize = sizeof(dialog),
        .hwndOwner = GetActiveWindow(),
        .lpstrFilter = filter,
        .lpstrFile = path,
        .nMaxFile = NV_ARRAY_COUNT(path),
        .Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR,
    };
    if (!GetOpenFileNameW(&dialog)) {
        state.status = NV_LOCAL_FILE_CANCELED;
        return 1;
    }
    wcscpy(state.path, path);
    read_kept_file();
    return 1;
}

b32 nv_local_file_reload(void)
{
    if (!state.path[0])
        return 0;
    read_kept_file();
    return 1;
}

internal void write_file(const wchar_t* path, const void* bytes, umm size)
{
    wchar_t temporary[PATH_CAPACITY + 8];
    swprintf(temporary, NV_ARRAY_COUNT(temporary), L"%ls.tmp", path);
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        fail("cannot write the file");
        return;
    }
    DWORD written = 0;
    b32 ok = WriteFile(file, bytes, (DWORD)size, &written, NULL) && written == size;
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fail("cannot write the file");
        DeleteFileW(temporary);
        return;
    }
    wcscpy(state.path, path);
    state.modified = last_write_time(path);
    set_name(path);
    state.status = NV_LOCAL_FILE_WRITTEN;
}

b32 nv_local_file_save(const char* name, const void* bytes, umm size, b32 save_as)
{
    if (!save_as && state.path[0]) {
        // Written in place, unless something else wrote the file since: that is not overwritten.
        if (last_write_time(state.path) != state.modified) {
            state.status = NV_LOCAL_FILE_FAILED;
            snprintf(state.error, sizeof(state.error), "the file changed on disk since it was read: reload it, or save it as another file");
            return 1;
        }
        wchar_t path[PATH_CAPACITY];
        wcscpy(path, state.path);
        write_file(path, bytes, size);
        return 1;
    }

    wchar_t suggested[PATH_CAPACITY] = {0};
#if !defined(NDEBUG)
    if (path_from_test(suggested, NV_ARRAY_COUNT(suggested))) {
        write_file(suggested, bytes, size);
        return 1;
    }
#endif
    nv_win32_wide_from_utf8(name, suggested, NV_ARRAY_COUNT(suggested));
    const wchar_t* dot = wcsrchr(suggested, L'.');
    wchar_t extension[32] = L"txt";
    if (dot)
        wcsncpy(extension, dot + 1, NV_ARRAY_COUNT(extension) - 1);
    wchar_t filter[96] = {0};
    int length = swprintf(filter, NV_ARRAY_COUNT(filter) - 2, L".%ls files", extension);
    swprintf(filter + length + 1, NV_ARRAY_COUNT(filter) - length - 2, L"*.%ls", extension);

    OPENFILENAMEW dialog = {
        .lStructSize = sizeof(dialog),
        .hwndOwner = GetActiveWindow(),
        .lpstrFilter = filter,
        .lpstrFile = suggested,
        .nMaxFile = NV_ARRAY_COUNT(suggested),
        .lpstrDefExt = extension,
        .Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR,
    };
    if (!GetSaveFileNameW(&dialog)) {
        state.status = NV_LOCAL_FILE_CANCELED;
        return 1;
    }
    write_file(suggested, bytes, size);
    return 1;
}

void nv_local_file_poll(NvLocalFile* file)
{
    file->status = state.status;
    file->kept = state.path[0] != 0;
    memcpy(file->name, state.name, sizeof(file->name));
    memcpy(file->error, state.error, sizeof(file->error));
    // A result is reported once; READ waits for nv_local_file_take.
    if (state.status >= NV_LOCAL_FILE_WRITTEN)
        state.status = NV_LOCAL_FILE_IDLE;
}

NvFileData nv_local_file_take(NvArena* arena, umm max_size)
{
    s64 size = state.size;
    state.size = -1;
    if (state.status == NV_LOCAL_FILE_READ)
        state.status = NV_LOCAL_FILE_IDLE;
    if (size < 0 || (umm)size > max_size || (umm)size > READ_CAPACITY || (umm)size + 1 > arena->size - arena->used)
        return (NvFileData){0};
    u8* bytes = nv_arena_push(arena, (umm)size + 1, 1); // zeroed, so the byte after the file is 0
    memcpy(bytes, state.bytes, (umm)size);
    return (NvFileData){.ok = 1, .bytes = bytes, .size = (umm)size};
}
