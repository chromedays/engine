#include "engine/image.h"
#include "engine/log.h"

#include <stdio.h>

// Images on Linux. Decoding waits for a decoder this build does not carry yet (the web uses the
// browser's, Windows uses WIC): it fails and says so, so a scene with textures fails loudly at
// load instead of rendering wrong. Writing a PNG is plain libpng-shaped work the first caller
// brings; no caller on this platform yet, so it fails the same way.

NvImage nv_image_decode(NvArena* arena, const u8* bytes, umm size, const char* mime)
{
    (void)arena, (void)bytes, (void)size;
    nv_log(NV_LOG_ERROR, "nv", "image decoding (%s) is not implemented on Linux yet", mime ? mime : "unknown");
    return (NvImage){0};
}

b32 nv_image_write_png(const char* path, const u8* rgba, u32 width, u32 height)
{
    (void)rgba, (void)width, (void)height;
    nv_log(NV_LOG_ERROR, "nv", "writing a PNG (%s) is not implemented on Linux yet", path);
    return 0;
}
