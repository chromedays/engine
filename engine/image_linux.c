#include "engine/image.h"
#include "engine/log.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <limits.h>
#include <string.h>

// Images on Linux (docs/specs/image_decoding.md). The web decodes with the browser and Windows with WIC; Linux has neither, so JPEG
// and PNG are decoded by stb_image, to 8-bit RGBA with no color management, as the other two do. Writing a PNG is stb_image_write.
// Both implementations are translation units of their own, made in CMakeLists.txt like cgltf's.
//
// stb_image allocates from the arena nv_image_decode is given, never from the heap: its implementation's STBI_MALLOC,
// STBI_REALLOC_SIZED and STBI_FREE are the nv_image_stb_* functions below. A decoder's temporaries are large (about 52 MB for a
// 2048x2048 progressive JPEG), and in the caller's scratch arena they are taken back with the rest of it. Blocks are pushed one
// after the other and never freed one by one; the newest one grows in place, which is how stb grows a PNG's data and its zlib
// output. Once the decode ends, the pixels move down to where the decode started and everything after them goes.

// The arena of the decode under way (NULL outside one), the newest block, and the request that did not fit.
global NvArena* decode_arena;
global u8* newest_block;
global umm refused_size;

#define STB_ALIGN 16

// Declared by stb_image's implementation (CMakeLists.txt), which calls them for every allocation; nothing else does.
void* nv_image_stb_malloc(size_t size);
void* nv_image_stb_realloc(void* block, size_t old_size, size_t new_size);
void nv_image_stb_free(void* block);

void* nv_image_stb_malloc(size_t size)
{
    NvArena* arena = decode_arena;
    NV_ASSERT(arena); // stb_image is only called from nv_image_decode
    umm start = (arena->used + (STB_ALIGN - 1)) & ~(umm)(STB_ALIGN - 1);
    // Not NV_PUSH: a full arena is a decode that fails ("outofmem"), not a bug, and stb's blocks need no zeroing.
    if (start > arena->size || size > arena->size - start) {
        refused_size = size;
        return NULL;
    }
    arena->used = start + size;
    newest_block = arena->base + start;
    return newest_block;
}

void* nv_image_stb_realloc(void* block, size_t old_size, size_t new_size)
{
    NvArena* arena = decode_arena;
    NV_ASSERT(arena);
    if (block && block == newest_block) {
        umm start = (umm)((u8*)block - arena->base);
        if (new_size > arena->size - start) {
            refused_size = new_size;
            return NULL;
        }
        arena->used = start + new_size;
        return block;
    }
    u8* moved = nv_image_stb_malloc(new_size);
    if (moved && block)
        memcpy(moved, block, old_size < new_size ? old_size : new_size);
    return moved;
}

void nv_image_stb_free(void* block)
{
    (void)block; // the arena takes everything back when the decode ends
}

NvImage nv_image_decode(NvArena* arena, const u8* bytes, umm size, const char* mime)
{
    const char* named = mime ? mime : "unknown type";
    if (size > INT_MAX) {
        nv_log(NV_LOG_ERROR, "nv", "image decode failed (%s): %llu bytes is too large", named, (unsigned long long)size);
        return (NvImage){0};
    }
    NV_ASSERT(!decode_arena); // one decode at a time: the allocator's state is global
    umm mark = arena->used;
    decode_arena = arena;
    newest_block = NULL;
    refused_size = 0;
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(bytes, (int)size, &width, &height, &channels, 4);
    decode_arena = NULL;
    if (!decoded) {
        if (refused_size)
            nv_log(NV_LOG_ERROR, "nv", "image decode failed (%s): the arena has no room for a block of %llu bytes (%llu of its %llu bytes were in use when the decode started)",
                   named, (unsigned long long)refused_size, (unsigned long long)mark, (unsigned long long)arena->size);
        else
            nv_log(NV_LOG_ERROR, "nv", "image decode failed (%s): %s", named, stbi_failure_reason());
        arena->used = mark;
        return (NvImage){0};
    }
    // The pixels are somewhere after `mark`, among stb's temporaries: they move to the start, and the temporaries go.
    umm pixel_bytes = (umm)width * (umm)height * 4;
    u8* pixels = arena->base + mark;
    memmove(pixels, decoded, pixel_bytes);
    arena->used = mark + pixel_bytes;
    return (NvImage){.ok = 1, .width = (u32)width, .height = (u32)height, .pixels = pixels};
}

b32 nv_image_write_png(const char* path, const u8* rgba, u32 width, u32 height)
{
    if (!stbi_write_png(path, (int)width, (int)height, 4, rgba, (int)(width * 4))) {
        nv_log(NV_LOG_ERROR, "nv", "writing a PNG (%s) failed", path);
        return 0;
    }
    return 1;
}
