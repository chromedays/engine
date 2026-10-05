#include "engine/local_file.h"

#include <emscripten/emscripten.h>

#include <string.h>

EM_JS_DEPS(nv_local_file, "$UTF8ToString,$stringToUTF8");

// The state lives in Module.nvLocalFile. Promises only change it; nothing calls into WebAssembly from them. Status numbers
// are NvLocalFileStatus's.
EM_JS(void, js_local_file_state, (void), {
    if (!Module.nvLocalFile)
        Module.nvLocalFile = {status: 0, handle: null, name: "", error: "", bytes: null, modified: 0, input: false};
});

EM_JS(int, js_local_file_open, (const char* extension), {
    const state = Module.nvLocalFile;
    // A file input that was closed without a file may never say so (no "cancel" event in older browsers): it does not block.
    if (state.status === 1 && !state.input) return 0;
    const ext = UTF8ToString(extension);
    const finish = (status, error) => {
        state.status = status;
        state.error = error || "";
        state.input = false;
    };
    const failed = (error) => finish(error && error.name === "AbortError" ? 4 : 5, String(error && error.message || error));
    const read = (file, handle) => file.arrayBuffer().then((buffer) => {
        state.bytes = new Uint8Array(buffer);
        state.name = file.name;
        state.modified = file.lastModified;
        state.handle = handle;
        finish(2);
    });
    state.status = 1;
    state.error = "";
    state.bytes = null;
    if (window.showOpenFilePicker) {
        const types = [{description: ext + " files", accept: {"text/plain": [ext]}}];
        window.showOpenFilePicker({types: types}).then((handles) => handles[0].getFile().then((file) => read(file, handles[0])))
            .catch(failed);
    } else {
        const input = document.createElement("input");
        input.type = "file";
        input.accept = ext;
        input.addEventListener("change", () => {
            const file = input.files && input.files[0];
            if (file) read(file, null).catch(failed);
            else finish(4);
        });
        input.addEventListener("cancel", () => finish(4));
        state.input = true;
        input.click();
    }
    return 1;
});

EM_JS(int, js_local_file_reload, (void), {
    const state = Module.nvLocalFile;
    if ((state.status === 1 && !state.input) || !state.handle) return 0;
    const handle = state.handle;
    state.status = 1;
    state.error = "";
    state.bytes = null;
    state.input = false;
    handle.getFile().then((file) => file.arrayBuffer().then((buffer) => {
        state.bytes = new Uint8Array(buffer);
        state.name = file.name;
        state.modified = file.lastModified;
        state.status = 2;
    })).catch((error) => {
        state.status = 5;
        state.error = String(error && error.message || error);
    });
    return 1;
});

EM_JS(int, js_local_file_save, (const char* name, const u8* bytes, int size, int save_as), {
    const state = Module.nvLocalFile;
    if (state.status === 1 && !state.input) return 0;
    const copy = HEAPU8.slice(bytes, bytes + size);
    const suggested = UTF8ToString(name);
    const finish = (status, error) => {
        state.status = status;
        state.error = error || "";
    };
    const failed = (error) => finish(error && error.name === "AbortError" ? 4 : 5, String(error && error.message || error));
    const write = (handle) => handle.createWritable()
        .then((stream) => stream.write(copy).then(() => stream.close()))
        .then(() => handle.getFile())
        .then((file) => {
            state.handle = handle;
            state.name = file.name;
            state.modified = file.lastModified;
            finish(3);
        });
    state.status = 1;
    state.error = "";
    state.input = false;
    if (!save_as && state.handle) {
        const handle = state.handle;
        handle.getFile().then((file) => {
            if (file.lastModified !== state.modified) {
                finish(5, "the file changed on disk since it was read: reload it, or save it as another file");
                return;
            }
            return write(handle);
        }).catch(failed);
    } else if (window.showSaveFilePicker) {
        const dot = suggested.lastIndexOf(".");
        const ext = dot >= 0 ? suggested.slice(dot) : ".txt";
        const types = [{description: ext + " files", accept: {"text/plain": [ext]}}];
        window.showSaveFilePicker({suggestedName: suggested, types: types}).then(write).catch(failed);
    } else {
        const url = URL.createObjectURL(new Blob([copy], {type: "application/octet-stream"}));
        const link = document.createElement("a");
        link.href = url;
        link.download = suggested;
        link.click();
        setTimeout(() => URL.revokeObjectURL(url), 10000);
        state.name = suggested;
        finish(3);
    }
    return 1;
});

EM_JS(int, js_local_file_poll, (int* kept, char* name, int name_size, char* error, int error_size), {
    const state = Module.nvLocalFile;
    HEAP32[kept >> 2] = state.handle ? 1 : 0;
    stringToUTF8(state.name, name, name_size);
    stringToUTF8(state.error, error, error_size);
    // A file input that may never answer (closed, in a browser with no "cancel" event) is not reported as busy.
    const status = state.status === 1 && state.input ? 0 : state.status;
    if (status >= 3) state.status = 0; // a result is reported once; READ waits for js_local_file_take
    return status;
});

EM_JS(int, js_local_file_size, (void), {
    const state = Module.nvLocalFile;
    return state.bytes ? state.bytes.length : -1;
});

// Copies the bytes to `out` (when it is not 0) and drops them.
EM_JS(void, js_local_file_take, (u8* out), {
    const state = Module.nvLocalFile;
    if (out && state.bytes) HEAPU8.set(state.bytes, out);
    state.bytes = null;
    if (state.status === 2) state.status = 0;
});

b32 nv_local_file_open(const char* extension)
{
    js_local_file_state();
    return js_local_file_open(extension);
}

b32 nv_local_file_reload(void)
{
    js_local_file_state();
    return js_local_file_reload();
}

b32 nv_local_file_save(const char* name, const void* bytes, umm size, b32 save_as)
{
    js_local_file_state();
    return js_local_file_save(name, bytes, (int)size, save_as);
}

void nv_local_file_poll(NvLocalFile* file)
{
    js_local_file_state();
    int kept = 0;
    file->status = (NvLocalFileStatus)js_local_file_poll(&kept, file->name, sizeof(file->name), file->error, sizeof(file->error));
    file->kept = kept;
    nv_utf8_trim(file->name);
    nv_utf8_trim(file->error);
}

NvFileData nv_local_file_take(NvArena* arena, umm max_size)
{
    js_local_file_state();
    int size = js_local_file_size();
    if (size < 0 || (umm)size > max_size || (umm)size + 1 > arena->size - arena->used) {
        js_local_file_take(0);
        return (NvFileData){0};
    }
    u8* bytes = nv_arena_push(arena, (umm)size + 1, 1); // zeroed, so the byte after the file is 0
    js_local_file_take(bytes);
    return (NvFileData){.ok = true, .bytes = bytes, .size = (umm)size};
}
