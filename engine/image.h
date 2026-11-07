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
// bytes are no image. On the web this waits for the browser's decoder (Asyncify), so a frame that decodes images yields. On Linux
// stb_image decodes inside the arena (docs/specs/image_decoding.md): the arena needs room for the decoder's temporaries too, all of
// which it takes back before returning (only the pixels stay). Measured: 1.75 times the pixels for a baseline JPEG, 3.25 times for a
// progressive one (28 and 52 MB for 2048x2048).
NvImage nv_image_decode(NvArena* arena, const u8* bytes, umm size, const char* mime);

// The native builds only (the scripted runs' and the visual test's screenshots): `width` x `height` RGBA8 pixels, rows from the
// top, as a PNG file. False, and logs why, when the file cannot be written.
b32 nv_image_write_png(const char* path, const u8* rgba, u32 width, u32 height);
