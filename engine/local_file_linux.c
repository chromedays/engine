#include "engine/local_file.h"
#include "engine/log.h"

#include <stdio.h>
#include <string.h>

// Files through the user's own dialogs, on Linux: there are no dialogs headless, so nothing can
// be opened or saved this way. Asking fails at once and a poll reports it, rather than waiting
// forever for a dialog that will never come.

global NvLocalFile state;
global b32 failed; // a failure is waiting to be reported by the next poll

b32 nv_local_file_open(const char* extension)
{
    (void)extension;
    if (state.status == NV_LOCAL_FILE_BUSY)
        return 0;
    nv_log(NV_LOG_WARNING, "nv", "file dialogs are not available headless");
    snprintf(state.error, sizeof(state.error), "file dialogs are not available headless");
    failed = 1;
    return 1;
}

b32 nv_local_file_reload(void)
{
    return 0; // no file was ever kept
}

b32 nv_local_file_save(const char* name, const void* bytes, umm size, b32 save_as)
{
    (void)name, (void)bytes, (void)size, (void)save_as;
    if (state.status == NV_LOCAL_FILE_BUSY)
        return 0;
    nv_log(NV_LOG_WARNING, "nv", "file dialogs are not available headless");
    snprintf(state.error, sizeof(state.error), "file dialogs are not available headless");
    failed = 1;
    return 1;
}

void nv_local_file_poll(NvLocalFile* file)
{
    if (failed) {
        failed = 0;
        state.status = NV_LOCAL_FILE_IDLE; // reported by this poll, as local_file.h promises
        *file = state;
        file->status = NV_LOCAL_FILE_FAILED;
        return;
    }
    state.status = NV_LOCAL_FILE_IDLE;
    *file = state;
}

NvFileData nv_local_file_take(NvArena* arena, umm max_size)
{
    (void)arena, (void)max_size;
    return (NvFileData){0}; // nothing was ever read
}
