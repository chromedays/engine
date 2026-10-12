#include "engine/imgui.h"
#include "engine/imgui_platform.h"
#include "engine/file.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/window.h"
#include "shaders/imgui_wgsl.h"
#include "shaders/preview_color_wgsl.h"
#include "shaders/preview_depth_ms_wgsl.h"
#include "shaders/preview_depth_wgsl.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// NOTE: imgui.h spells this as a macro that cimgui does not carry over.
#define IMGUI_RESET_RENDER_STATE ((ImDrawCallback)(intptr_t)-8)

// The shader is engine/shaders/imgui.slang: the build makes imgui_wgsl from it.
_Static_assert(sizeof(NvMat4) == 64, "shaders/imgui.slang reads a 64-byte uniform block");

//
// Input shared by the platforms
//

// Whether input starting at page position (x, y) belongs to the view rather than to ImGui. Open
// popups (a combo's list can reach over the viewport) keep their input.
b32 nv_imgui_in_view(NvImgui* imgui, f32 x, f32 y)
{
    f32 ratio = nv_window_pixel_ratio(imgui->window);
    NvRect r = imgui->view_rect;
    f32 px = x * ratio;
    f32 py = y * ratio;
    return px >= (f32)r.x && py >= (f32)r.y && px < (f32)(r.x + r.width) && py < (f32)(r.y + r.height) &&
           !igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void nv_imgui_view_tap(NvImgui* imgui, f32 x, f32 y)
{
    imgui->view_pending.tapped = 1;
    imgui->view_pending.tap_x = x;
    imgui->view_pending.tap_y = y;
    // As they were at the press: a tap can be decided frames after the keys were let go.
    imgui->view_pending.tap_mods = imgui->view_press_mods;
}
//
// Rendering
//

// Engine textures in the UI (nv_imgui_preview). Same vertex stage; the fragment stage shows the
// texture as the slot's Params say. Colors reach an sRGB-encoding view, so a value meant to show
// as stored is linearized first: channels of non-sRGB textures, alpha, and depth grays.
// The shaders are engine/shaders/preview_*.slang: the build makes preview_color_wgsl and the others from them.

typedef struct PreviewParams {
    f32 range[4];
    u32 mode[4];
} PreviewParams;
_Static_assert(sizeof(PreviewParams) == 32, "the preview shaders read 32 bytes of parameters");

internal WGPURenderPipeline create_pipeline(WGPUDevice device, NvGpuFormat format, const char* code, const char* label)
{
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    module_desc.nextInChain = &wgsl.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &module_desc);

    WGPUVertexAttribute attributes[3] = {
        {.format = WGPUVertexFormat_Float32x2, .offset = offsetof(ImDrawVert, pos), .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x2, .offset = offsetof(ImDrawVert, uv), .shaderLocation = 1},
        {.format = WGPUVertexFormat_Unorm8x4, .offset = offsetof(ImDrawVert, col), .shaderLocation = 2},
    };
    WGPUVertexBufferLayout vertex_layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertex_layout.stepMode = WGPUVertexStepMode_Vertex;
    vertex_layout.arrayStride = sizeof(ImDrawVert);
    vertex_layout.attributeCount = NV_ARRAY_COUNT(attributes);
    vertex_layout.attributes = attributes;

    WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
    color_target.format = nv_gpu_wgpu_format(format);
    color_target.blend = &blend;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = (WGPUStringView){"fs_main", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){label, WGPU_STRLEN};
    desc.vertex.module = module;
    desc.vertex.entryPoint = (WGPUStringView){"vs_main", WGPU_STRLEN};
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex_layout;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.primitive.cullMode = WGPUCullMode_None;
    desc.fragment = &fragment;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
    wgpuShaderModuleRelease(module);
    return pipeline;
}

internal WGPUBuffer create_buffer(WGPUDevice device, WGPUBufferUsage usage, umm size)
{
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.usage = usage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    return wgpuDeviceCreateBuffer(device, &desc);
}

internal void upload_texture_rect(NvImgui* imgui, ImTextureData* tex, u32 x, u32 y, u32 w, u32 h)
{
    NvImguiTexture* slot = &imgui->textures[ImTextureData_GetTexID(tex)];
    u32 pitch = (u32)ImTextureData_GetPitch(tex);

    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = slot->texture;
    destination.origin = (WGPUOrigin3D){x, y, 0};
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = pitch;
    layout.rowsPerImage = h;
    WGPUExtent3D size = {w, h, 1};
    umm data_size = (umm)pitch * (h - 1) + (umm)w * 4;
    wgpuQueueWriteTexture(imgui->gpu->queue, &destination, ImTextureData_GetPixelsAt(tex, (int)x, (int)y),
                          data_size, &layout, &size);
}

// Serves ImGui's texture requests (font atlas creation, glyph uploads, destruction).
internal void update_texture(NvImgui* imgui, ImTextureData* tex)
{
    WGPUDevice device = imgui->gpu->device;

    if (tex->Status == ImTextureStatus_WantCreate) {
        NV_ASSERT(tex->Format == ImTextureFormat_RGBA32);
        u32 index = 1;
        while (index < NV_IMGUI_MAX_TEXTURES && imgui->textures[index].texture)
            ++index;
        NV_ASSERT(index < NV_IMGUI_MAX_TEXTURES);
        NvImguiTexture* slot = &imgui->textures[index];

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label = (WGPUStringView){"imgui texture", WGPU_STRLEN};
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        desc.size = (WGPUExtent3D){(u32)tex->Width, (u32)tex->Height, 1};
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        slot->texture = wgpuDeviceCreateTexture(device, &desc);
        slot->view = wgpuTextureCreateView(slot->texture, NULL);

        WGPUBindGroupEntry entries[3] = {
            {.binding = 0, .buffer = imgui->uniform_buffer, .size = sizeof(NvMat4)},
            {.binding = 1, .sampler = imgui->sampler},
            {.binding = 2, .textureView = slot->view},
        };
        WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(imgui->pipeline, 0);
        WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        group_desc.layout = layout;
        group_desc.entryCount = NV_ARRAY_COUNT(entries);
        group_desc.entries = entries;
        slot->bind_group = wgpuDeviceCreateBindGroup(device, &group_desc);
        wgpuBindGroupLayoutRelease(layout);

        ImTextureData_SetTexID(tex, index);
        upload_texture_rect(imgui, tex, 0, 0, (u32)tex->Width, (u32)tex->Height);
        ImTextureData_SetStatus(tex, ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        ImTextureRect r = tex->UpdateRect;
        upload_texture_rect(imgui, tex, r.x, r.y, r.w, r.h);
        ImTextureData_SetStatus(tex, ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        // Waiting for an unused frame keeps the texture alive while queued draws may use it.
        NvImguiTexture* slot = &imgui->textures[ImTextureData_GetTexID(tex)];
        wgpuBindGroupRelease(slot->bind_group);
        wgpuTextureViewRelease(slot->view);
        wgpuTextureRelease(slot->texture);
        *slot = (NvImguiTexture){0};
        ImTextureData_SetTexID(tex, 0);
        ImTextureData_SetStatus(tex, ImTextureStatus_Destroyed);
    }
}

internal void set_render_state(NvImgui* imgui, WGPURenderPassEncoder pass)
{
    wgpuRenderPassEncoderSetPipeline(pass, imgui->pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, imgui->vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, imgui->index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
}

//
// API
//

void nv_imgui_init(NvImgui* imgui, NvGpu* gpu, NvWindow* window, NvArena* arena)
{
    _Static_assert(sizeof(ImDrawIdx) == 2, "the index buffer is bound as Uint16");
    *imgui = (NvImgui){0};
    imgui->gpu = gpu;
    imgui->window = window;
    imgui->vertices = NV_PUSH_ARRAY(arena, NV_IMGUI_MAX_VERTICES, ImDrawVert);
    imgui->indices = NV_PUSH_ARRAY(arena, NV_IMGUI_MAX_INDICES, ImDrawIdx);
    imgui->clipboard = NV_PUSH_ARRAY(arena, NV_IMGUI_CLIPBOARD_SIZE, char);

    igCreateContext(NULL);
    ImGuiIO* io = igGetIO_Nil();
    io->IniFilename = NULL; // no filesystem to persist window layout to
    io->BackendRendererName = "nv_webgpu";
    io->BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

    nv_imgui_platform_init(imgui, window);

    ImGuiStyle* style = igGetStyle();
    ImGuiStyle_ScaleAllSizes(style, imgui->ui_scale);
    style->FontScaleMain = imgui->ui_scale;

    WGPUDevice device = gpu->device;
    imgui->pipeline = create_pipeline(device, gpu->surface_format, imgui_wgsl, "imgui");
    imgui->preview_pipeline = create_pipeline(device, gpu->surface_format, preview_color_wgsl, "imgui preview");
    imgui->depth_pipeline = create_pipeline(device, gpu->surface_format, preview_depth_wgsl, "imgui depth preview");
    imgui->depth_ms_pipeline = create_pipeline(device, gpu->surface_format, preview_depth_ms_wgsl, "imgui multisampled depth preview");
    imgui->uniform_buffer = create_buffer(device, WGPUBufferUsage_Uniform, sizeof(NvMat4));
    imgui->vertex_buffer = create_buffer(device, WGPUBufferUsage_Vertex, NV_IMGUI_MAX_VERTICES * sizeof(ImDrawVert));
    imgui->index_buffer = create_buffer(device, WGPUBufferUsage_Index, NV_IMGUI_MAX_INDICES * sizeof(ImDrawIdx));

    WGPUSamplerDescriptor sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    imgui->sampler = wgpuDeviceCreateSampler(device, &sampler_desc);
}

void nv_imgui_touch_press(NvImgui* imgui)
{
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddMousePosEvent(io, imgui->touch_start_x, imgui->touch_start_y);
    ImGuiIO_AddMouseButtonEvent(io, 0, 1);
    ImGuiIO_AddMousePosEvent(io, imgui->touch_x, imgui->touch_y);
    imgui->touch_gesture = NV_TOUCH_PRESS;
}

// Decides a press that has waited for `view_grab` (see NvImgui). A grabbed press becomes an ImGui
// left button from where it went down; what the view gathered from it meanwhile is dropped.
internal void decide_view_press(NvImgui* imgui)
{
    ImGuiIO* io = igGetIO_Nil();
    b32 touch = imgui->view_grab_touch;
    b32 down = touch ? imgui->touch_gesture == NV_TOUCH_VIEW && imgui->view_touch_count > 0 : imgui->view_mouse_button == 1;
    b32 multi = touch && imgui->view_touch_multi;
    b32 grab = !multi && imgui->view_grab(imgui->view_grab_data);
    if (grab) {
        imgui->view_pending = (NvViewInput){0};
        if (down && touch) {
            imgui->touch_start_x = imgui->view_press_x;
            imgui->touch_start_y = imgui->view_press_y;
            imgui->touch_x = imgui->view_touch_x[0];
            imgui->touch_y = imgui->view_touch_y[0];
            imgui->view_touch_count = 0;
            nv_imgui_touch_press(imgui);
        } else if (down) {
            imgui->view_mouse_button = 0;
            ImGuiIO_AddMousePosEvent(io, imgui->view_press_x, imgui->view_press_y);
            ImGuiIO_AddMouseButtonEvent(io, 0, 1);
            ImGuiIO_AddMousePosEvent(io, imgui->view_mouse_x, imgui->view_mouse_y);
        }
    } else if (imgui->view_tap_held) {
        nv_imgui_view_tap(imgui, imgui->view_press_x, imgui->view_press_y);
    }
    imgui->view_tap_held = 0;
    if (!down && touch)
        ImGuiIO_AddMousePosEvent(io, -FLT_MAX, -FLT_MAX);
}

internal void release_preview(NvImguiPreviewSlot* slot)
{
    wgpuBindGroupRelease(slot->bind_group);
    wgpuBufferRelease(slot->params);
    wgpuTextureViewRelease(slot->view);
    wgpuTextureRelease(slot->key.texture);
    *slot = (NvImguiPreviewSlot){0};
}

internal b32 same_preview(const NvImguiPreview* a, const NvImguiPreview* b)
{
    return a->texture == b->texture && a->mip == b->mip && a->mode == b->mode && a->depth == b->depth &&
           a->range_min == b->range_min && a->range_max == b->range_max && a->near_z == b->near_z &&
           a->far_z == b->far_z;
}

internal b32 is_depth_format(NvGpuFormat format)
{
    return format == NV_GPU_FORMAT_DEPTH16 || format == NV_GPU_FORMAT_DEPTH32F;
}

internal b32 is_srgb_format(NvGpuFormat format)
{
    return format == NV_GPU_FORMAT_RGBA8_SRGB || format == NV_GPU_FORMAT_BGRA8_SRGB;
}

ImTextureID nv_imgui_preview(NvImgui* imgui, const NvImguiPreview* preview)
{
    NV_ASSERT(preview->texture);
    s32 free_slot = -1;
    for (u32 i = 0; i < NV_IMGUI_MAX_PREVIEWS; ++i) {
        NvImguiPreviewSlot* slot = &imgui->previews[i];
        if (!slot->key.texture) {
            if (free_slot < 0)
                free_slot = (s32)i;
        } else if (same_preview(&slot->key, preview)) {
            slot->used_frame = imgui->frame;
            return (ImTextureID)(NV_IMGUI_MAX_TEXTURES + i);
        }
    }
    NV_ASSERT(free_slot >= 0);
    NvImguiPreviewSlot* slot = &imgui->previews[free_slot];
    WGPUDevice device = imgui->gpu->device;
    NvGpuFormat format = nv_gpu_format_from_wgpu(wgpuTextureGetFormat(preview->texture));
    b32 depth = is_depth_format(format);
    NV_ASSERT(depth == (preview->mode == NV_IMGUI_PREVIEW_DEPTH));
    b32 multisampled = wgpuTextureGetSampleCount(preview->texture) > 1;
    NV_ASSERT(!multisampled || depth); // only depth is shown multisampled (sample 0)
    NV_ASSERT(preview->mip < wgpuTextureGetMipLevelCount(preview->texture));
    NV_ASSERT(wgpuTextureGetUsage(preview->texture) & WGPUTextureUsage_TextureBinding);

    slot->key = *preview;
    slot->depth = depth;
    slot->multisampled = multisampled;
    slot->used_frame = imgui->frame;
    wgpuTextureAddRef(preview->texture);

    WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_desc.baseMipLevel = preview->mip;
    view_desc.mipLevelCount = 1;
    if (depth)
        view_desc.aspect = WGPUTextureAspect_DepthOnly;
    slot->view = wgpuTextureCreateView(preview->texture, &view_desc);

    PreviewParams params = {
        .range = {preview->range_min, preview->range_max, preview->near_z, preview->far_z},
        .mode = {(u32)preview->mode, !depth && !is_srgb_format(format), (u32)preview->depth, 0},
    };
    slot->params = create_buffer(device, WGPUBufferUsage_Uniform, sizeof(params));
    wgpuQueueWriteBuffer(imgui->gpu->queue, slot->params, 0, &params, sizeof(params));

    WGPUBindGroupEntry entries[4] = {
        {.binding = 0, .buffer = imgui->uniform_buffer, .size = sizeof(NvMat4)},
        {.binding = 3, .buffer = slot->params, .size = sizeof(params)},
        {.binding = 2, .textureView = slot->view},
        {.binding = 1, .sampler = imgui->sampler}, // left out for depth: that pipeline has no sampler
    };
    WGPURenderPipeline pipeline = depth ? (multisampled ? imgui->depth_ms_pipeline : imgui->depth_pipeline) : imgui->preview_pipeline;
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group_desc.layout = layout;
    group_desc.entryCount = depth ? 3 : 4;
    group_desc.entries = entries;
    slot->bind_group = wgpuDeviceCreateBindGroup(device, &group_desc);
    wgpuBindGroupLayoutRelease(layout);
    return (ImTextureID)(NV_IMGUI_MAX_TEXTURES + (u32)free_slot);
}

u32 nv_imgui_preview_count(NvImgui* imgui)
{
    u32 count = 0;
    for (u32 i = 0; i < NV_IMGUI_MAX_PREVIEWS; ++i)
        count += imgui->previews[i].key.texture != NULL;
    return count;
}

void nv_imgui_new_frame(NvImgui* imgui, f32 delta_seconds)
{
    // NOTE: WebGPU keeps what submitted work uses alive, so a slot could go as soon as it is unused;
    // the few frames only save remaking slots that come back (a thumbnail scrolled out and in).
    ++imgui->frame;
    for (u32 i = 0; i < NV_IMGUI_MAX_PREVIEWS; ++i) {
        NvImguiPreviewSlot* slot = &imgui->previews[i];
        if (slot->key.texture && imgui->frame - slot->used_frame > NV_IMGUI_PREVIEW_FRAMES)
            release_preview(slot);
    }
    ImGuiIO* io = igGetIO_Nil();
    f32 scale = nv_window_pixel_ratio(imgui->window);
    io->DisplaySize = (ImVec2_c){(f32)imgui->gpu->width / scale, (f32)imgui->gpu->height / scale};
    io->DisplayFramebufferScale = (ImVec2_c){scale, scale};
    io->DeltaTime = delta_seconds > 0.0f ? delta_seconds : 1.0f / 60.0f;

    nv_imgui_platform_update(imgui);
    // The scrolling of a touch is applied this frame.
    imgui->touch_scroll = imgui->touch_scroll_pending;
    imgui->touch_scroll_pending = 0.0f;
    // A press waiting for view_grab keeps what it gathers until it is decided.
    if (imgui->view_grab_wait && --imgui->view_grab_wait == 0)
        decide_view_press(imgui);
    if (imgui->view_grab_wait) {
        imgui->view = (NvViewInput){0};
    } else {
        imgui->view = imgui->view_pending;
        imgui->view_pending = (NvViewInput){0};
    }

    igNewFrame();
}

b32 nv_imgui_set_font(NvImgui* imgui, void* ttf, u32 size, f32 pixel_size)
{
    (void)imgui;
    if (size == 0)
        return 0; // ImGui asserts on empty font data
    ImGuiIO* io = igGetIO_Nil();
    ImFontConfig* config = ImFontConfig_ImFontConfig();
    config->FontDataOwnedByAtlas = false; // the caller keeps the bytes
    // Size 0: the font is dynamic, drawn at whatever size the style asks for.
    ImFont* font = ImFontAtlas_AddFontFromMemoryTTF(io->Fonts, ttf, (int)size, 0.0f, config, NULL);
    ImFontConfig_destroy(config);
    if (!font)
        return 0;
    io->FontDefault = font;
    igGetStyle()->FontSizeBase = pixel_size;
    return 1;
}

#define UI_FONT_FILE "/assets/fonts/Pretendard-Regular.ttf"
#define UI_FONT_SIZE 14.0f // CSS pixels, before the touch scale

b32 nv_imgui_load_ui_font(NvImgui* imgui, NvArena* arena)
{
    NvFileData font = nv_file_read(arena, UI_FONT_FILE);
    if (!font.ok)
        nv_log(NV_LOG_WARNING, "imgui", "font %s is missing or could not be read", UI_FONT_FILE);
    if (!font.ok || !nv_imgui_set_font(imgui, font.bytes, (u32)font.size, UI_FONT_SIZE)) {
        nv_log(NV_LOG_WARNING, "imgui", "the UI font could not be loaded: using the built-in font");
        return 0;
    }
    return 1;
}

void nv_imgui_fit_text(const char* text, f32 room, char* out, umm capacity)
{
    NV_ASSERT(capacity >= 4); // room for "..." and the terminator
    // The whole text, when it fits both the buffer and the room.
    umm length = strlen(text);
    if (length < capacity) {
        memcpy(out, text, length + 1);
        if (igCalcTextSize(out, NULL, false, -1.0f).x <= room)
            return;
    }
    // Else the longest prefix of whole characters that fits with "..." after it, in the buffer and the room. Candidates are
    // the characters' starts up to `limit`; the empty prefix is taken when nothing fits ("..." alone). Each candidate is
    // written into `out` to be measured, which has room for it.
    umm limit = nv_utf8_fit(text, (u32)(capacity - 4));
    umm lo = 0, hi = limit + 1; // the answer is in [lo, hi): lo fits (or is the empty prefix), hi does not (or is past the limit)
    while (lo + 1 < hi) {
        umm mid = (lo + hi) / 2;
        while (mid > lo && ((u8)text[mid] & 0xC0) == 0x80)
            --mid; // back to the start of a character
        if (mid == lo) {
            // No character starts in (lo, middle]: try the first one after it, if it comes before hi.
            mid = lo + 1;
            while (mid < hi && ((u8)text[mid] & 0xC0) == 0x80)
                ++mid;
            if (mid >= hi)
                break;
        }
        memcpy(out, text, mid);
        memcpy(out + mid, "...", 4);
        if (igCalcTextSize(out, NULL, false, -1.0f).x <= room)
            lo = mid;
        else
            hi = mid;
    }
    memcpy(out, text, lo);
    memcpy(out + lo, "...", 4);
}

NvBuildLabel nv_imgui_draw_build_label(NvImgui* imgui, NvRect viewport, const char* text, f32 extra_width, const char* subject)
{
    const ImU32 backdrop = 0x99000000u;
    ImDrawList* draw = igGetBackgroundDrawList(NULL); // behind every window: a popup that overlaps the label covers it
    f32 ratio = nv_window_pixel_ratio(imgui->window);
    ImVec2_c pos = {(f32)viewport.x / ratio + 6.0f, (f32)viewport.y / ratio + 6.0f};
    ImVec2_c size = igCalcTextSize(text, NULL, false, -1.0f);
    ImVec2_c min = {pos.x - 4.0f, pos.y - 2.0f};
    ImVec2_c max = {pos.x + size.x + extra_width + 4.0f, pos.y + size.y + 2.0f};
    ImDrawList_AddRectFilled(draw, min, max, backdrop, 3.0f, 0);
    ImDrawList_AddText_Vec2(draw, pos, 0xFFFFFFFFu, text, NULL);

    char fitted[512];
    nv_imgui_fit_text(subject, (f32)viewport.width / ratio - 20.0f, fitted, sizeof(fitted));
    ImVec2_c subject_size = igCalcTextSize(fitted, NULL, false, -1.0f);
    ImVec2_c subject_pos = {pos.x, max.y + 3.0f};
    ImDrawList_AddRectFilled(draw, (ImVec2_c){pos.x - 4.0f, subject_pos.y - 2.0f},
                             (ImVec2_c){pos.x + subject_size.x + 4.0f, subject_pos.y + subject_size.y + 2.0f}, backdrop, 3.0f, 0);
    ImDrawList_AddText_Vec2(draw, subject_pos, 0xFFBBBBBBu, fitted, NULL);

    return (NvBuildLabel){
        .text_end = pos.x + size.x,
        .text_top = pos.y,
        .text_height = size.y,
        .box = {min.x, min.y, max.x, max.y},
    };
}

bool nv_imgui_begin_panel(NvImgui* imgui, const char* name, NvRect rect)
{
    return nv_imgui_begin_panel_ex(imgui, name, rect, 0);
}

bool nv_imgui_begin_panel_ex(NvImgui* imgui, const char* name, NvRect rect, ImGuiWindowFlags extra_flags)
{
    // ImGui works in CSS pixels; `rect` is in framebuffer pixels.
    f32 scale = igGetIO_Nil()->DisplayFramebufferScale.x;
    igSetNextWindowPos((ImVec2_c){(f32)rect.x / scale, (f32)rect.y / scale}, ImGuiCond_Always, (ImVec2_c){0.0f, 0.0f});
    igSetNextWindowSize((ImVec2_c){(f32)rect.width / scale, (f32)rect.height / scale}, ImGuiCond_Always);
    igSetNextWindowBgAlpha(1.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | extra_flags;
    bool open = igBegin(name, NULL, flags);
    nv_imgui_touch_scroll(imgui);
    return open;
}

void nv_imgui_touch_scroll(NvImgui* imgui)
{
    // ImGui has no touch scrolling; on_touch collects it for the window under the finger.
    if (imgui->touch_scroll != 0.0f && igIsWindowHovered(0))
        igSetScrollY_Float(igGetScrollY() - imgui->touch_scroll);
}

void nv_imgui_render(NvImgui* imgui, WGPUCommandEncoder encoder, WGPUTextureView target)
{
    nv_imgui_platform_set_cursor(igGetMouseCursor());
    igRender();
    ImDrawData* draw_data = igGetDrawData();

    if (draw_data->Textures) {
        for (s32 i = 0; i < draw_data->Textures->Size; ++i) {
            ImTextureData* tex = draw_data->Textures->Data[i];
            if (tex->Status != ImTextureStatus_OK)
                update_texture(imgui, tex);
        }
    }

    ImVec2_c pos = draw_data->DisplayPos;
    ImVec2_c scale = draw_data->FramebufferScale;
    // Clip against the real target size; DisplaySize * scale can round past it.
    f32 fb_width = (f32)imgui->gpu->width;
    f32 fb_height = (f32)imgui->gpu->height;
    if (fb_width <= 0.0f || fb_height <= 0.0f || draw_data->TotalIdxCount == 0)
        return;

    // Gather every list into one vertex and one index upload.
    NV_ASSERT(draw_data->TotalVtxCount <= NV_IMGUI_MAX_VERTICES);
    NV_ASSERT(draw_data->TotalIdxCount <= NV_IMGUI_MAX_INDICES);
    u32 vertex_count = 0;
    u32 index_count = 0;
    for (s32 i = 0; i < draw_data->CmdLists.Size; ++i) {
        ImDrawList* list = draw_data->CmdLists.Data[i];
        memcpy(imgui->vertices + vertex_count, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(imgui->indices + index_count, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vertex_count += (u32)list->VtxBuffer.Size;
        index_count += (u32)list->IdxBuffer.Size;
    }
    // Buffer writes must be a multiple of 4 bytes; an odd index count gets one padding index.
    umm index_bytes = ((umm)index_count * sizeof(ImDrawIdx) + 3) & ~(umm)3;
    NV_ASSERT(index_bytes <= NV_IMGUI_MAX_INDICES * sizeof(ImDrawIdx));
    WGPUQueue queue = imgui->gpu->queue;
    wgpuQueueWriteBuffer(queue, imgui->vertex_buffer, 0, imgui->vertices, vertex_count * sizeof(ImDrawVert));
    wgpuQueueWriteBuffer(queue, imgui->index_buffer, 0, imgui->indices, index_bytes);

    // Maps ImGui's display rectangle (top-left origin, y down) to clip space.
    f32 l = pos.x;
    f32 r = pos.x + draw_data->DisplaySize.x;
    f32 t = pos.y;
    f32 b = pos.y + draw_data->DisplaySize.y;
    NvMat4 mvp = {{
        2.0f / (r - l), 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / (t - b), 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        (r + l) / (l - r), (t + b) / (b - t), 0.5f, 1.0f,
    }};
    wgpuQueueWriteBuffer(queue, imgui->uniform_buffer, 0, &mvp, sizeof(mvp));

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = target;
    color.loadOp = WGPULoadOp_Load;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.label = (WGPUStringView){"imgui", WGPU_STRLEN};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    set_render_state(imgui, pass);
    WGPURenderPipeline bound = imgui->pipeline;

    u32 vertex_offset = 0;
    u32 index_offset = 0;
    for (s32 i = 0; i < draw_data->CmdLists.Size; ++i) {
        ImDrawList* list = draw_data->CmdLists.Data[i];
        for (s32 c = 0; c < list->CmdBuffer.Size; ++c) {
            ImDrawCmd* cmd = &list->CmdBuffer.Data[c];
            if (cmd->UserCallback) {
                if (cmd->UserCallback == IMGUI_RESET_RENDER_STATE) {
                    set_render_state(imgui, pass);
                    bound = imgui->pipeline;
                } else
                    cmd->UserCallback(list, cmd);
                continue;
            }

            f32 x0 = (cmd->ClipRect.x - pos.x) * scale.x;
            f32 y0 = (cmd->ClipRect.y - pos.y) * scale.y;
            f32 x1 = (cmd->ClipRect.z - pos.x) * scale.x;
            f32 y1 = (cmd->ClipRect.w - pos.y) * scale.y;
            if (x0 < 0.0f) x0 = 0.0f;
            if (y0 < 0.0f) y0 = 0.0f;
            if (x1 > fb_width) x1 = fb_width;
            if (y1 > fb_height) y1 = fb_height;
            if (x1 <= x0 || y1 <= y0)
                continue;
            wgpuRenderPassEncoderSetScissorRect(pass, (u32)x0, (u32)y0, (u32)(x1 - x0), (u32)(y1 - y0));

            ImTextureID texture = ImDrawCmd_GetTexID(cmd);
            WGPURenderPipeline pipeline = imgui->pipeline;
            WGPUBindGroup group = NULL;
            if (texture >= NV_IMGUI_MAX_TEXTURES) {
                NV_ASSERT(texture < NV_IMGUI_MAX_TEXTURES + NV_IMGUI_MAX_PREVIEWS);
                NvImguiPreviewSlot* slot = &imgui->previews[texture - NV_IMGUI_MAX_TEXTURES];
                NV_ASSERT(slot->bind_group);
                pipeline = slot->depth ? (slot->multisampled ? imgui->depth_ms_pipeline : imgui->depth_pipeline) : imgui->preview_pipeline;
                group = slot->bind_group;
            } else {
                NV_ASSERT(texture > 0 && imgui->textures[texture].bind_group);
                group = imgui->textures[texture].bind_group;
            }
            if (pipeline != bound) {
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                bound = pipeline;
            }
            wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, NULL);
            wgpuRenderPassEncoderDrawIndexed(pass, cmd->ElemCount, 1, index_offset + cmd->IdxOffset,
                                             (s32)(vertex_offset + cmd->VtxOffset), 0);
        }
        vertex_offset += (u32)list->VtxBuffer.Size;
        index_offset += (u32)list->IdxBuffer.Size;
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}
