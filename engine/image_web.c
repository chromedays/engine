#include "engine/image.h"

#include <emscripten/emscripten.h>

// Images are decoded by the browser (JPEG, PNG and WebP all work) instead of a C image library.

EM_ASYNC_JS(int, js_decode_image, (const u8* data, int size, const char* mime, int* width, int* height), {
    const bytes = HEAPU8.slice(data, data + size);
    try {
        const bitmap = await createImageBitmap(new Blob([bytes], {type: UTF8ToString(mime)}),
                                               {colorSpaceConversion: "none", premultiplyAlpha: "none"});
        const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
        const context = canvas.getContext("2d");
        context.drawImage(bitmap, 0, 0);
        Module.nvDecodedImage = context.getImageData(0, 0, bitmap.width, bitmap.height).data;
        HEAP32[width >> 2] = bitmap.width;
        HEAP32[height >> 2] = bitmap.height;
        return 1;
    } catch (error) {
        Module.nvLog(2, "nv", "image decode failed: " + (error && error.message || error));
        return 0;
    }
});

EM_JS(void, js_take_decoded_image, (u8* out), {
    HEAPU8.set(Module.nvDecodedImage, out);
    Module.nvDecodedImage = null;
});

NvImage nv_image_decode(NvArena* arena, const u8* bytes, umm size, const char* mime)
{
    s32 width = 0;
    s32 height = 0;
    if (!js_decode_image(bytes, (int)size, mime, &width, &height))
        return (NvImage){0};
    u8* pixels = NV_PUSH_ARRAY(arena, (umm)width * height * 4, u8);
    js_take_decoded_image(pixels);
    return (NvImage){.ok = 1, .width = (u32)width, .height = (u32)height, .pixels = pixels};
}
