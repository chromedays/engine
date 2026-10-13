#pragma once

// Engine-private, Windows only: what the platform sources (window_win32.c, storage_win32.c, ...) all need. Include <windows.h> before
// the engine's headers (base.h's `internal` macro is not for Windows' headers).

#include <windows.h>

#include "engine/base.h"

// UTF-8 text to the UTF-16 Win32 wants, and back. `capacity` is in characters of the output, with the terminator; text that does
// not fit gives an empty string, which is always terminated. Both return the length written, without the terminator.
static inline int nv_win32_wide_from_utf8(const char* text, wchar_t* out, int capacity)
{
    int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, out, capacity);
    if (length == 0) {
        out[0] = 0;
        return 0;
    }
    return length - 1;
}

static inline int nv_win32_utf8_from_wide(const wchar_t* text, char* out, int capacity)
{
    int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, out, capacity, NULL, NULL);
    if (length == 0) {
        out[0] = 0;
        return 0;
    }
    return length - 1;
}
