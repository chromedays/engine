#include "engine/win32.h"

#define COBJMACROS
#include <objbase.h>
#include <wincodec.h>

#include "engine/image.h"
#include "engine/log.h"

// Images are decoded by Windows Imaging Component (docs/specs/d3d12_port.md, "Platform layer"): JPEG and PNG, as the browser decodes
// them on the web, instead of a C image library. The decoder is a function call here, so a frame that decodes images does not yield.

global IWICImagingFactory* factory;

internal IWICImagingFactory* imaging_factory(void)
{
    if (factory)
        return factory;
    HRESULT result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    // RPC_E_CHANGED_MODE: something else already set the thread up, which is as good.
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        nv_log(NV_LOG_ERROR, "nv", "CoInitializeEx failed (0x%08lx)", (unsigned long)result);
        return NULL;
    }
    result = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(result)) {
        nv_log(NV_LOG_ERROR, "nv", "no Windows Imaging Component (0x%08lx)", (unsigned long)result);
        factory = NULL;
    }
    return factory;
}

NvImage nv_image_decode(NvArena* arena, const u8* bytes, umm size, const char* mime)
{
    NvImage image = {0};
    IWICImagingFactory* wic = imaging_factory();
    if (!wic)
        return image;

    IWICStream* stream = NULL;
    IWICBitmapDecoder* decoder = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* converter = NULL;
    HRESULT result = IWICImagingFactory_CreateStream(wic, &stream);
    if (SUCCEEDED(result))
        result = IWICStream_InitializeFromMemory(stream, (BYTE*)bytes, (DWORD)size);
    if (SUCCEEDED(result))
        result = IWICImagingFactory_CreateDecoderFromStream(wic, (IStream*)stream, NULL, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(result))
        result = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    // The pixels as the file has them: 8 bits a channel, rows from the top, no color management, not premultiplied.
    if (SUCCEEDED(result))
        result = IWICImagingFactory_CreateFormatConverter(wic, &converter);
    if (SUCCEEDED(result))
        result = IWICFormatConverter_Initialize(converter, (IWICBitmapSource*)frame, &GUID_WICPixelFormat32bppRGBA,
                                                WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    UINT width = 0, height = 0;
    if (SUCCEEDED(result))
        result = IWICFormatConverter_GetSize(converter, &width, &height);
    if (SUCCEEDED(result)) {
        umm mark = arena->used;
        u8* pixels = NV_PUSH_ARRAY(arena, (umm)width * height * 4, u8);
        result = IWICFormatConverter_CopyPixels(converter, NULL, width * 4, width * height * 4, pixels);
        if (SUCCEEDED(result))
            image = (NvImage){.ok = 1, .width = width, .height = height, .pixels = pixels};
        else
            arena->used = mark; // nothing of a failed decode stays
    }
    if (FAILED(result))
        nv_log(NV_LOG_ERROR, "nv", "image decode failed (%s, 0x%08lx)", mime, (unsigned long)result);

    if (converter)
        IWICFormatConverter_Release(converter);
    if (frame)
        IWICBitmapFrameDecode_Release(frame);
    if (decoder)
        IWICBitmapDecoder_Release(decoder);
    if (stream)
        IWICStream_Release(stream);
    return image;
}

b32 nv_image_write_png(const char* path, const u8* rgba, u32 width, u32 height)
{
    IWICImagingFactory* wic = imaging_factory();
    if (!wic)
        return 0;
    wchar_t wide_path[520];
    nv_win32_wide_from_utf8(path, wide_path, NV_ARRAY_COUNT(wide_path));

    IWICStream* stream = NULL;
    IWICBitmapEncoder* encoder = NULL;
    IWICBitmapFrameEncode* frame = NULL;
    IPropertyBag2* options = NULL;
    HRESULT result = IWICImagingFactory_CreateStream(wic, &stream);
    if (SUCCEEDED(result))
        result = IWICStream_InitializeFromFilename(stream, wide_path, GENERIC_WRITE);
    if (SUCCEEDED(result))
        result = IWICImagingFactory_CreateEncoder(wic, &GUID_ContainerFormatPng, NULL, &encoder);
    if (SUCCEEDED(result))
        result = IWICBitmapEncoder_Initialize(encoder, (IStream*)stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(result))
        result = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &options);
    if (SUCCEEDED(result))
        result = IWICBitmapFrameEncode_Initialize(frame, options);
    if (SUCCEEDED(result))
        result = IWICBitmapFrameEncode_SetSize(frame, width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    if (SUCCEEDED(result))
        result = IWICBitmapFrameEncode_SetPixelFormat(frame, &format);
    // The encoder may answer with the format it stores instead of the one asked for: PNG takes BGRA, not RGBA.
    b32 bgra = IsEqualGUID(&format, &GUID_WICPixelFormat32bppBGRA);
    if (SUCCEEDED(result) && !bgra && !IsEqualGUID(&format, &GUID_WICPixelFormat32bppRGBA))
        result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (SUCCEEDED(result) && !bgra) {
        result = IWICBitmapFrameEncode_WritePixels(frame, height, width * 4, width * height * 4, (BYTE*)rgba);
    } else if (SUCCEEDED(result)) {
        local_persist u8 row[8192 * 4];
        NV_ASSERT(width <= 8192);
        for (u32 y = 0; y < height && SUCCEEDED(result); ++y) {
            for (u32 x = 0; x < width; ++x) {
                row[x * 4 + 0] = rgba[((umm)y * width + x) * 4 + 2];
                row[x * 4 + 1] = rgba[((umm)y * width + x) * 4 + 1];
                row[x * 4 + 2] = rgba[((umm)y * width + x) * 4 + 0];
                row[x * 4 + 3] = rgba[((umm)y * width + x) * 4 + 3];
            }
            result = IWICBitmapFrameEncode_WritePixels(frame, 1, width * 4, width * 4, row);
        }
    }
    if (SUCCEEDED(result))
        result = IWICBitmapFrameEncode_Commit(frame);
    if (SUCCEEDED(result))
        result = IWICBitmapEncoder_Commit(encoder);
    if (FAILED(result))
        nv_log(NV_LOG_ERROR, "nv", "writing %s failed (0x%08lx)", path, (unsigned long)result);

    if (options)
        IPropertyBag2_Release(options);
    if (frame)
        IWICBitmapFrameEncode_Release(frame);
    if (encoder)
        IWICBitmapEncoder_Release(encoder);
    if (stream)
        IWICStream_Release(stream);
    return SUCCEEDED(result);
}
