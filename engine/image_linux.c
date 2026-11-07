#include "engine/image.h"
#include "engine/log.h"

#include <stb_image_write.h>

// Images on Linux. Decoding waits for a decoder this build does not carry yet (the web uses the
// browser's, Windows uses WIC): it fails and says so, so a scene with textures fails loudly at
// load instead of rendering wrong. Writing a PNG is stb_image_write (its implementation is its
// own translation unit, made in CMakeLists.txt like cgltf's).

NvImage nv_image_decode(NvArena* arena, const u8* bytes, umm size, const char* mime)
{
    (void)arena, (void)bytes, (void)size;
    nv_log(NV_LOG_ERROR, "nv", "image decoding (%s) is not implemented on Linux yet", mime ? mime : "unknown");
    return (NvImage){0};
}

b32 nv_image_write_png(const char* path, const u8* rgba, u32 width, u32 height)
{
    if (!stbi_write_png(path, (int)width, (int)height, 4, rgba, (int)(width * 4))) {
        nv_log(NV_LOG_ERROR, "nv", "writing a PNG (%s) failed", path);
        return 0;
    }
    return 1;
}
