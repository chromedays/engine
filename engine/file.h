#pragma once

#include "engine/base.h"

// Files the executable carries (packed by nv_setup_executable's ASSETS or PRELOAD), read whole with the C file API.

// The bytes of the file at `path`, pushed from `arena` (16-byte aligned) with a 0 byte after them, so a text file is also a
// C string; `size` gets the size without that byte. NULL, with the arena as it was, when the file is missing, cannot be read
// whole or does not fit in what is left of the arena. Logs nothing: the caller says what the file was for.
u8* nv_file_read(NvArena* arena, const char* path, umm* size);
