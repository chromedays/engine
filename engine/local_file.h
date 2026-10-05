#pragma once

#include "engine/base.h"
#include "engine/file.h"

// Files on the user's computer, opened and saved through the browser's own dialogs (docs/specs/local_files.md). Where the
// browser has the File System Access API (Chrome and Edge on a desktop), the file opened or saved last is kept: it can be read
// again without a dialog (nv_local_file_reload) and written in place (nv_local_file_save without `save_as`). Elsewhere a file
// is opened through a file input and saved by downloading it. One operation runs at a time, and its result comes back on a
// later frame through nv_local_file_poll, so the frame never waits on a dialog. A dialog needs a recent click or tap: call
// these from a button's frame.

typedef enum NvLocalFileStatus {
    NV_LOCAL_FILE_IDLE,     // nothing asked, or the last result was reported
    NV_LOCAL_FILE_BUSY,     // a dialog, a read or a write is under way
    NV_LOCAL_FILE_READ,     // a file was read: take its bytes with nv_local_file_take
    NV_LOCAL_FILE_WRITTEN,  // the bytes were written (or handed to the browser as a download)
    NV_LOCAL_FILE_CANCELED, // the dialog was closed without a file
    NV_LOCAL_FILE_FAILED,   // NvLocalFile.error says why
} NvLocalFileStatus;

typedef struct NvLocalFile {
    NvLocalFileStatus status;
    b32 kept;        // a file is kept: nv_local_file_reload and saving in place work
    char name[128];  // the kept file's name (no folder), or the last one read or written
    char error[160]; // for NV_LOCAL_FILE_FAILED, in English
} NvLocalFile;

// Shows the open dialog for files ending in `extension` (".abproj"). False when another operation is under way.
b32 nv_local_file_open(const char* extension);

// Reads the kept file again. False when none is kept or another operation is under way.
b32 nv_local_file_reload(void);

// Writes `size` bytes (copied at once) to the kept file, or with `save_as` (or no kept file) to one the save dialog picks,
// suggesting `name`. Writing in place fails, without writing, when the file changed on disk since it was read or written
// here: something else (a text editor) wrote it, and that is not overwritten. False when another operation is under way.
b32 nv_local_file_save(const char* name, const void* bytes, umm size, b32 save_as);

// Fills `file` with where things are, once a frame. A result (READ, WRITTEN, CANCELED, FAILED) is reported by one poll and
// the status is IDLE after it; after READ, take the bytes before the next operation.
void nv_local_file_poll(NvLocalFile* file);

// The bytes of the file read last, pushed from `arena` with a 0 byte after them (as nv_file_read). Fails, with the arena as
// it was, when there are none or they do not fit in `max_size` or the arena. The bytes are dropped either way.
NvFileData nv_local_file_take(NvArena* arena, umm max_size);
