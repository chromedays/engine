#include "engine/storage.h"

#include <emscripten/emscripten.h>

#include <stdio.h>

EM_JS_DEPS(nv_storage, "$FS,$IDBFS,$UTF8ToString,$stringToUTF8");

EM_ASYNC_JS(int, js_storage_mount, (const char* dir), {
    const path = UTF8ToString(dir);
    // IMPORTANT: The browser may clear site data under storage pressure unless it is persisted.
    // IMPORTANT: IDBFS aborts the whole runtime when there is no IndexedDB, so check first.
    let available = false;
    try {
        available = typeof indexedDB !== "undefined" && indexedDB !== null;
    } catch (error) {
    }
    if (!available) {
        Module.nvLog(1, "nv", "browser storage is unavailable: no IndexedDB");
        return 0;
    }
    if (navigator.storage && navigator.storage.persist) navigator.storage.persist().catch(() => {});
    try {
        FS.mkdir(path);
        FS.mount(IDBFS, {}, path);
        await new Promise((resolve, reject) => FS.syncfs(true, (error) => error ? reject(error) : resolve()));
        return 1;
    } catch (error) {
        Module.nvLog(1, "nv", "browser storage is unavailable: " + (error && error.message || error));
        return 0;
    }
});

// One sync runs at a time; a flush asked for meanwhile runs after it. `reload` reloads the page
// after the sync that covers this call.
EM_JS(void, js_storage_flush, (int reload), {
    const state = Module.nvStorage || (Module.nvStorage = {busy: false, again: false, reload: false, error: ""});
    if (reload) state.reload = true;
    if (state.busy) {
        state.again = true;
        return;
    }
    const run = () => {
        state.busy = true;
        const reload_after = state.reload;
        FS.syncfs(false, (error) => {
            state.busy = false;
            state.error = error ? String(error.message || error) : "";
            if (error) Module.nvLog(1, "nv", "saving to browser storage failed: " + state.error);
            if (state.again) {
                state.again = false;
                run();
            } else if (reload_after) {
                location.reload();
            }
        });
    };
    run();
});

EM_JS(void, js_storage_error, (char* out, int capacity), {
    const state = Module.nvStorage;
    stringToUTF8(state ? state.error : "", out, capacity);
});

internal void full_path(NvStorage* storage, const char* name, char* out, umm capacity)
{
    snprintf(out, capacity, "%s/%s", storage->dir, name);
}

void nv_storage_init(NvStorage* storage, const char* dir)
{
    storage->dir = dir;
    storage->available = js_storage_mount(dir);
}

b32 nv_storage_write(NvStorage* storage, const char* name, const void* bytes, u32 size)
{
    if (!storage->available)
        return 0;
    char path[256], temporary[256];
    full_path(storage, name, path, sizeof(path));
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE* file = fopen(temporary, "wb");
    if (!file)
        return 0;
    b32 written = fwrite(bytes, 1, size, file) == size;
    written = (fclose(file) == 0) && written;
    if (!written || rename(temporary, path) != 0) {
        remove(temporary);
        return 0;
    }
    return 1;
}

NvFileData nv_storage_read(NvStorage* storage, const char* name, NvArena* arena, u32 max_size)
{
    if (!storage->available)
        return (NvFileData){0};
    char path[256];
    full_path(storage, name, path, sizeof(path));
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
    char from_path[256], to_path[256];
    full_path(storage, from, from_path, sizeof(from_path));
    full_path(storage, to, to_path, sizeof(to_path));
    return rename(from_path, to_path) == 0;
}

b32 nv_storage_remove(NvStorage* storage, const char* name)
{
    if (!storage->available)
        return 0;
    char path[256];
    full_path(storage, name, path, sizeof(path));
    return remove(path) == 0;
}

void nv_storage_flush(NvStorage* storage)
{
    if (storage->available)
        js_storage_flush(0);
}

void nv_storage_flush_then_reload(NvStorage* storage)
{
    if (storage->available)
        js_storage_flush(1);
    else
        emscripten_run_script("location.reload()");
}

b32 nv_storage_exists(NvStorage* storage, const char* name)
{
    if (!storage->available)
        return 0;
    char path[256];
    full_path(storage, name, path, sizeof(path));
    FILE* file = fopen(path, "rb");
    if (file)
        fclose(file);
    return file != NULL;
}

const char* nv_storage_error(NvStorage* storage)
{
    local_persist char error[160];
    error[0] = 0;
    if (storage->available)
        js_storage_error(error, (int)sizeof(error));
    return error;
}
