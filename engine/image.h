#pragma once

#include "engine/base.h"

// An image decoded to 8-bit RGBA, rows from the top, with no color conversion and no premultiplied alpha.
typedef struct NvImage {
    b32 ok;
    u32 width;
    u32 height;
    u8* pixels; // width * height * 4 bytes, pushed on the arena
} NvImage;

// Decodes the JPEG or PNG bytes of a file held in memory. `mime` names the format ("image/jpeg"). Fails, and logs why, when the
// bytes are no image. On the web this waits for the browser's decoder (Asyncify), so a frame that decodes images yields.
NvImage nv_image_decode(NvArena* arena, const u8* bytes, umm size, const char* mime);

// Windows only (the scripted runs' screenshots): `width` x `height` RGBA8 pixels, rows from the top, as a PNG file. False, and logs
// why, when the file cannot be written.
b32 nv_image_write_png(const char* path, const u8* rgba, u32 width, u32 height);
