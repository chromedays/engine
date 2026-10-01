#include "app.h"

#include <stdio.h>
#include <string.h>

// The Textures tab (docs/specs/textures.md): the textures the engine holds, with a thumbnail each,
// and the picked one large, with its mip levels, channels, or depth as gray over a range.

// A thumbnail's side, in CSS pixels before the UI scale.
#define THUMBNAIL 48.0f
// Wider than this (CSS pixels), the list and the picked texture sit side by side.
#define SIDE_BY_SIDE_WIDTH 700.0f

typedef struct TextureEntry {
    TextureKind kind;
    u32 index;           // TEXTURE_MATERIAL: renderer slot; TEXTURE_UI: NvImgui slot
    WGPUTexture texture; // NULL: nothing to show (the swapchain, a map not made yet)
    const char* name;
    u32 width, height, mip_count;
    u32 samples; // per pixel; 0 is 1
    WGPUTextureFormat format;
    u32 users;    // mesh nodes whose material uses it
    b32 in_use;
    b32 preview;  // can be drawn now
    const char* note; // why it has no preview
} TextureEntry;

internal b32 same_entry(const TextureViewer* viewer, const TextureEntry* entry)
{
    return viewer->selected_kind == entry->kind && viewer->selected_index == entry->index;
}

internal void checkerboard(ImDrawList* draw, ImVec2_c min, ImVec2_c max, f32 cell)
{
    ImDrawList_AddRectFilled(draw, min, max, 0xFF999999u, 0.0f, 0);
    u32 row = 0;
    for (f32 y = min.y; y < max.y; y += cell, ++row) {
        u32 column = row & 1;
        for (f32 x = min.x; x < max.x; x += cell, ++column) {
            if (column & 1)
                continue;
            ImVec2_c a = {x, y};
            ImVec2_c b = {x + cell < max.x ? x + cell : max.x, y + cell < max.y ? y + cell : max.y};
            ImDrawList_AddRectFilled(draw, a, b, 0xFF666666u, 0.0f, 0);
        }
    }
}

// The scene camera's planes and projection, for turning reverse-Z depth into distance.
internal void camera_depth(App* app, NvImguiPreview* preview)
{
    SceneView* view = app_view(app);
    NvNode* camera = nv_scene_get(view->scene, view->scene->active_camera);
    preview->near_z = camera->camera.near_z;
    preview->far_z = camera->camera.far_z;
    preview->depth = camera->camera.projection == NV_PROJECTION_ORTHOGRAPHIC ? NV_IMGUI_DEPTH_REVERSE_ORTHOGRAPHIC
                                                                             : NV_IMGUI_DEPTH_REVERSE_PERSPECTIVE;
}

// What a thumbnail or the detail draws of an entry. `full` applies the detail's mip, channel and
// range; thumbnails show the top level as it is (depth over its default range).
internal ImTextureID entry_image(App* app, const TextureEntry* entry, b32 full)
{
    TextureViewer* viewer = &app->textures;
    NvImguiPreview preview = {.texture = entry->texture};
    if (entry->kind == TEXTURE_DEPTH) {
        preview.mode = NV_IMGUI_PREVIEW_DEPTH;
        camera_depth(app, &preview);
        preview.range_min = 0.0f;
        preview.range_max = full ? viewer->depth_range : app_view(app)->camera_distance * 2.0f;
    } else if (entry->kind == TEXTURE_SHADOW) {
        preview.mode = NV_IMGUI_PREVIEW_DEPTH;
        preview.range_min = full ? viewer->shadow_range[0] : 0.0f;
        preview.range_max = full ? viewer->shadow_range[1] : 1.0f;
    } else if (full) {
        preview.mode = (NvImguiPreviewMode)viewer->channels;
        preview.mip = viewer->mip < entry->mip_count ? viewer->mip : entry->mip_count - 1;
    }
    return nv_imgui_preview(&app->imgui, &preview);
}

// The part of the texture shown at zoom 1: the scene's part of the scene's targets, else all of it.
internal void base_uv(App* app, const TextureEntry* entry, ImVec2_c* uv0, ImVec2_c* uv1)
{
    *uv0 = (ImVec2_c){0.0f, 0.0f};
    *uv1 = (ImVec2_c){1.0f, 1.0f};
    if ((entry->kind == TEXTURE_DEPTH || entry->kind == TEXTURE_SCENE) && app->renderer.target_width && app->renderer.target_height) {
        // The targets are allocated a little larger than the scene; the scene is their top-left part.
        *uv1 = (ImVec2_c){(f32)app->renderer.scene_width / (f32)app->renderer.target_width,
                          (f32)app->renderer.scene_height / (f32)app->renderer.target_height};
    }
}

// Fits `width` x `height` (the shown part's texels) into a box, keeping the aspect.
internal ImVec2_c fit(f32 width, f32 height, f32 box_width, f32 box_height)
{
    if (width <= 0.0f || height <= 0.0f)
        return (ImVec2_c){box_width, box_height};
    f32 scale = box_width / width;
    if (height * scale > box_height)
        scale = box_height / height;
    return (ImVec2_c){width * scale, height * scale};
}

internal void format_bytes(u64 bytes, char* out, umm size)
{
    if (bytes >= NV_MEGABYTES(1))
        snprintf(out, size, "%.1f MB", (f64)bytes / (f64)NV_MEGABYTES(1));
    else
        snprintf(out, size, "%.1f KB", (f64)bytes / 1024.0);
}

internal u64 entry_bytes(const TextureEntry* entry)
{
    return nv_gpu_texture_bytes(entry->width, entry->height, entry->mip_count, entry->format) * (entry->samples ? entry->samples : 1);
}

// Every texture, grouped by kind. Returns the count; `entries` holds at most `capacity`.
internal u32 gather(App* app, TextureEntry* entries, u32 capacity)
{
    NvRenderer* renderer = &app->renderer;
    NvScene* scene = app_view(app)->scene;
    u32 users[NV_MAX_TEXTURES] = {0};
    for (u32 i = 1; i <= scene->node_count; ++i) {
        NvNode* node = &scene->nodes[i];
        if (!(node->gen & 1) || !node->mesh.index)
            continue;
        ++users[renderer->materials[node->material.index].desc.base_color_texture.index];
    }

    u32 count = 0;
    for (u32 t = 0; t < renderer->texture_count && count < capacity; ++t) {
        NvRenderTexture* texture = &renderer->textures[t];
        entries[count++] = (TextureEntry){
            .kind = TEXTURE_MATERIAL,
            .index = t,
            .texture = texture->texture,
            .name = texture->name,
            .width = wgpuTextureGetWidth(texture->texture),
            .height = wgpuTextureGetHeight(texture->texture),
            .mip_count = wgpuTextureGetMipLevelCount(texture->texture),
            .format = wgpuTextureGetFormat(texture->texture),
            .users = users[t],
            .in_use = users[t] > 0,
            .preview = 1,
        };
    }

    NvShadowSettings* shadows = &renderer->shadows;
    if (renderer->shadow_texture && count < capacity) {
        b32 on = shadows->size != 0 && renderer->shadow_size == shadows->size;
        entries[count++] = (TextureEntry){
            .kind = TEXTURE_SHADOW,
            .texture = on ? renderer->shadow_texture : NULL,
            .name = "shadow map",
            .width = renderer->shadow_size,
            .height = renderer->shadow_size,
            .mip_count = 1,
            .format = wgpuTextureGetFormat(renderer->shadow_texture),
            .in_use = on,
            .preview = on,
            .note = on ? NULL : "Shadows are off: a 1x1 placeholder stays bound.",
        };
    }
    if (renderer->depth_texture && count < capacity) {
        b32 sampled = renderer->depth_texture_sampled;
        entries[count++] = (TextureEntry){
            .kind = TEXTURE_DEPTH,
            .texture = sampled ? renderer->depth_texture : NULL,
            .name = "depth target",
            .width = renderer->depth_width,
            .height = renderer->depth_height,
            .mip_count = 1,
            .samples = renderer->depth_samples,
            .format = WGPUTextureFormat_Depth32Float,
            .in_use = 1,
            .preview = sampled,
            .note = sampled ? NULL : "Made samplable on the next frame.",
        };
    }
    if (renderer->scene_color && count < capacity) {
        entries[count++] = (TextureEntry){
            .kind = TEXTURE_SCENE,
            .texture = renderer->scene_color,
            .name = "scene color",
            .width = renderer->target_width,
            .height = renderer->target_height,
            .mip_count = 1,
            .format = app->gpu.config_format,
            .in_use = 1,
            .preview = 1,
        };
    }
    if (renderer->msaa_color && count < capacity) {
        entries[count++] = (TextureEntry){
            .kind = TEXTURE_MSAA,
            .name = "msaa color target",
            .width = renderer->msaa_width,
            .height = renderer->msaa_height,
            .mip_count = 1,
            .samples = renderer->scene_samples,
            .format = renderer->msaa_format,
            .in_use = 1,
            .note = "No preview: it is discarded after every frame; the scene pass resolves it into the swapchain.",
        };
    }
    if (count < capacity) {
        entries[count++] = (TextureEntry){
            .kind = TEXTURE_SWAPCHAIN,
            .name = "swapchain (color target)",
            .width = app->gpu.width,
            .height = app->gpu.height,
            .mip_count = 1,
            .format = app->gpu.config_format,
            .in_use = 1,
            .note = "No preview: the UI pass is drawing into it, and a pass cannot sample its own target.",
        };
    }
    for (u32 i = 1; i < NV_IMGUI_MAX_TEXTURES && count < capacity; ++i) {
        NvImguiTexture* texture = &app->imgui.textures[i];
        if (!texture->texture)
            continue;
        entries[count++] = (TextureEntry){
            .kind = TEXTURE_UI,
            .index = i,
            .texture = texture->texture,
            .name = "font atlas",
            .width = wgpuTextureGetWidth(texture->texture),
            .height = wgpuTextureGetHeight(texture->texture),
            .mip_count = 1,
            .format = wgpuTextureGetFormat(texture->texture),
            .in_use = 1,
            .preview = 1,
        };
    }
    return count;
}

// A thumbnail of `entry` at the cursor, `side` pixels square, on a checkerboard.
internal void thumbnail(App* app, const TextureEntry* entry, f32 side)
{
    ImVec2_c origin = igGetCursorScreenPos();
    ImVec2_c box_max = {origin.x + side, origin.y + side};
    ImDrawList* draw = igGetWindowDrawList();
    igDummy((ImVec2_c){side, side});
    if (!entry->preview) {
        ImDrawList_AddRect(draw, origin, box_max, 0xFF555555u, 0.0f, 1.0f, 0);
        ImDrawList_AddLine(draw, origin, box_max, 0xFF555555u, 1.0f);
        return;
    }
    ImVec2_c uv0, uv1;
    base_uv(app, entry, &uv0, &uv1);
    ImVec2_c size = fit((f32)entry->width * (uv1.x - uv0.x), (f32)entry->height * (uv1.y - uv0.y), side, side);
    ImVec2_c min = {origin.x + (side - size.x) * 0.5f, origin.y + (side - size.y) * 0.5f};
    ImVec2_c max = {min.x + size.x, min.y + size.y};
    checkerboard(draw, min, max, 6.0f);
    ImDrawList_AddImage(draw, (ImTextureRef_c){NULL, entry_image(app, entry, 0)}, min, max, uv0, uv1, 0xFFFFFFFFu);
}

// Two short lines, so a phone's panel shows them whole: "1024x1024 RGBA8UnormSrgb" and
// "11 mips, 5.3 MB".
internal void describe(const TextureEntry* entry, char* size_line, char* memory_line, umm size)
{
    if (entry->samples > 1)
        snprintf(size_line, size, "%ux%u %s x%u", entry->width, entry->height, nv_gpu_format_name(entry->format), entry->samples);
    else
        snprintf(size_line, size, "%ux%u %s", entry->width, entry->height, nv_gpu_format_name(entry->format));
    char bytes[32];
    format_bytes(entry_bytes(entry), bytes, sizeof(bytes));
    snprintf(memory_line, size, "%u mip%s, %s", entry->mip_count, entry->mip_count == 1 ? "" : "s", bytes);
}

// Picks a texture and opens it (on a narrow panel the list gives way to it).
internal void pick(TextureViewer* viewer, TextureKind kind, u32 index)
{
    viewer->selected_kind = kind;
    viewer->selected_index = index;
    viewer->has_selection = 1;
    viewer->detail_open = 1;
    viewer->mip = 0;
    viewer->zoom = 1.0f;
    viewer->center[0] = viewer->center[1] = 0.5f;
}

// One row: the thumbnail, then the name, size, format, mips, memory and users. A tap opens it.
internal void row(App* app, const TextureEntry* entry, f32 height, u32 number)
{
    TextureViewer* viewer = &app->textures;
    igPushID_Int((int)number);
    ImVec2_c start = igGetCursorScreenPos();
    bool selected = same_entry(viewer, entry);
    if (igSelectable_Bool("##row", selected, ImGuiSelectableFlags_AllowOverlap, (ImVec2_c){0.0f, height}))
        pick(viewer, entry->kind, entry->index);
    if (number == 0)
        textures_record(viewer, TEXTURES_RECT_FIRST_ROW);
#if !defined(NDEBUG)
    if (number < TEXTURES_MAX_ROW_RECTS) {
        ImVec2_c min = igGetItemRectMin(), max = igGetItemRectMax();
        f32* r = viewer->row_rects[number];
        r[0] = min.x, r[1] = min.y, r[2] = max.x, r[3] = max.y;
    }
#endif
    // Only rows on screen make thumbnails, so the preview slots stay few however long the list.
    if (!igIsItemVisible()) {
        igPopID();
        return;
    }
    igSetCursorScreenPos(start);
    thumbnail(app, entry, height);
    igSameLine(0.0f, -1.0f);
    igBeginGroup();
    if (entry->in_use)
        igTextUnformatted(entry->name, NULL);
    else
        igTextDisabled("%s", entry->name);
    char size_line[96], memory_line[64];
    describe(entry, size_line, memory_line, sizeof(memory_line));
    igTextDisabled("%s", size_line);
    if (entry->kind == TEXTURE_MATERIAL)
        igTextDisabled("%s, %u user%s", memory_line, entry->users, entry->users == 1 ? "" : "s");
    else
        igTextDisabled("%s", memory_line);
    igEndGroup();
    igPopID();
}

internal void section(App* app, const char* label, TextureKind first, TextureKind last, const TextureEntry* entries,
                      u32 count, u32* number)
{
    TextureViewer* viewer = &app->textures;
    u64 bytes = 0;
    for (u32 i = 0; i < count; ++i) {
        if (entries[i].kind >= first && entries[i].kind <= last)
            bytes += entry_bytes(&entries[i]);
    }
    char memory[32];
    format_bytes(bytes, memory, sizeof(memory));
    // The search matches a texture's name and its section's: "materials" lists the whole section.
    search_section(app, label);
    b32 searching = search_active(app);
    if (searching) {
        u32 matching = 0;
        for (u32 i = 0; i < count; ++i) {
            const TextureEntry* entry = &entries[i];
            if (entry->kind >= first && entry->kind <= last && (viewer->show_unused || entry->in_use) &&
                search_match(app, entry->name))
                ++matching;
        }
        if (!matching)
            return;
        igSetNextItemOpen(true, ImGuiCond_Always);
    }
    char header[96];
    snprintf(header, sizeof(header), "%s (%s)###%s", label, memory, label);
    if (!igCollapsingHeader_TreeNodeFlags(header, ImGuiTreeNodeFlags_DefaultOpen))
        return;
    f32 height = THUMBNAIL * app->imgui.ui_scale;
    for (u32 i = 0; i < count; ++i) {
        const TextureEntry* entry = &entries[i];
        if (entry->kind < first || entry->kind > last)
            continue;
        if (!viewer->show_unused && !entry->in_use)
            continue;
        if (searching && !search_match(app, entry->name))
            continue;
        ++app->search.rows_now[SEARCH_TEXTURES];
        row(app, entry, height, (*number)++);
    }
}

// The nodes that use a material texture; a tap selects one and opens the Inspector.
internal void used_by(App* app, u32 texture)
{
    SceneView* view = app_view(app);
    NvScene* scene = view->scene;
    NvRenderer* renderer = &app->renderer;
    igSeparatorText("Used by");
    u32 shown = 0;
    for (u32 i = 1; i <= scene->node_count; ++i) {
        NvNode* node = &scene->nodes[i];
        if (!(node->gen & 1) || !node->mesh.index ||
            renderer->materials[node->material.index].desc.base_color_texture.index != texture)
            continue;
        // The stress scene's grid has thousands: the first few say enough.
        if (shown == 16) {
            igTextDisabled("...");
            break;
        }
        igPushID_Int((int)i);
        NvNodeId id = {i, node->gen};
        if (igSelectable_Bool(node->name, view->selected.index == i, 0, (ImVec2_c){0.0f, 0.0f})) {
            view->selected = id;
            app->open_inspector = 1;
        }
        if (shown == 0)
            textures_record(&app->textures, TEXTURES_RECT_FIRST_USER);
        igPopID();
        ++shown;
    }
    if (!shown)
        igTextDisabled("No node of this scene.");
}

internal void detail(App* app, const TextureEntry* entry)
{
    TextureViewer* viewer = &app->textures;
    // Zero means "not set yet": the defaults.
    if (viewer->zoom < 1.0f)
        viewer->zoom = 1.0f;
    if (viewer->depth_range <= 0.0f)
        viewer->depth_range = app_view(app)->camera_distance * 2.0f;
    if (viewer->shadow_range[1] <= viewer->shadow_range[0]) {
        viewer->shadow_range[0] = 0.0f;
        viewer->shadow_range[1] = 1.0f;
    }
    igTextUnformatted(entry->name, NULL);
    char size_line[96], memory_line[64];
    describe(entry, size_line, memory_line, sizeof(memory_line));
    igTextDisabled("%s, %s", size_line, memory_line);
    if (!entry->preview) {
        igTextWrapped("%s", entry->note ? entry->note : "No preview.");
        return;
    }

    b32 depth = entry->kind == TEXTURE_DEPTH || entry->kind == TEXTURE_SHADOW;
    igSetNextItemWidth(igGetContentRegionAvail().x * 0.5f);
    igSliderFloat("Zoom", &viewer->zoom, 1.0f, 16.0f, "%.1fx", ImGuiSliderFlags_Logarithmic);
    textures_record(viewer, TEXTURES_RECT_ZOOM);
    if (depth) {
        if (entry->kind == TEXTURE_DEPTH) {
            NvImguiPreview camera = {0};
            camera_depth(app, &camera);
            igSetNextItemWidth(igGetContentRegionAvail().x * 0.5f);
            igSliderFloat("White at", &viewer->depth_range, camera.near_z, camera.far_z, "%.1f m", ImGuiSliderFlags_Logarithmic);
            igSetItemTooltip("%s", "Distance from the camera shown white; nearer is darker");
        } else {
            igSetNextItemWidth(igGetContentRegionAvail().x * 0.5f);
            igDragFloatRange2("Range", &viewer->shadow_range[0], &viewer->shadow_range[1], 0.005f, 0.0f, 1.0f, "%.3f", NULL, 0);
            igSetItemTooltip("%s", "Stored depth shown black and white");
        }
        textures_record(viewer, TEXTURES_RECT_RANGE);
    } else {
        s32 max_mip = (s32)entry->mip_count - 1;
        if ((s32)viewer->mip > max_mip)
            viewer->mip = (u32)max_mip;
        if (max_mip > 0) {
            s32 mip = (s32)viewer->mip;
            char format[48];
            u32 w = entry->width >> viewer->mip, h = entry->height >> viewer->mip;
            snprintf(format, sizeof(format), "%%d (%ux%u)", w ? w : 1, h ? h : 1);
            igSetNextItemWidth(igGetContentRegionAvail().x * 0.5f);
            if (igSliderInt("Mip", &mip, 0, max_mip, format, ImGuiSliderFlags_AlwaysClamp))
                viewer->mip = (u32)mip;
            textures_record(viewer, TEXTURES_RECT_MIP);
        }
        // One tap each, rather than a combo's two.
        local_persist const char* channel_names[] = {"RGBA", "RGB", "R", "G", "B", "A"};
        for (u32 c = 0; c < NV_ARRAY_COUNT(channel_names); ++c) {
            if (c)
                ui_same_line_if_fits(igGetFrameHeight() + igGetStyle()->ItemInnerSpacing.x +
                                     igCalcTextSize(channel_names[c], NULL, false, -1.0f).x);
            igRadioButton_IntPtr(channel_names[c], &viewer->channels, (int)c);
            textures_record(viewer, (TexturesRect)(TEXTURES_RECT_CHANNELS + c));
        }
        ui_same_line_if_fits(igGetFrameHeight() + igGetStyle()->ItemInnerSpacing.x +
                             igCalcTextSize("Checkerboard", NULL, false, -1.0f).x);
        bool checker = !viewer->no_checkerboard;
        if (igCheckbox("Checkerboard", &checker))
            viewer->no_checkerboard = !checker;
    }

    // The image: the shown part fits the width, and a drag pans it (sideways on touch, since a
    // vertical finger drag scrolls the panel).
    ImVec2_c uv0, uv1;
    base_uv(app, entry, &uv0, &uv1);
    f32 part_w = (f32)entry->width * (uv1.x - uv0.x);
    f32 part_h = (f32)entry->height * (uv1.y - uv0.y);
    ImVec2_c avail = igGetContentRegionAvail();
    f32 minimum = 160.0f * app->imgui.ui_scale;
    f32 max_height = avail.y - igGetTextLineHeightWithSpacing();
    if (max_height < minimum)
        max_height = minimum;
    ImVec2_c size = fit(part_w, part_h, avail.x, max_height);
    f32 zoom = viewer->zoom < 1.0f ? 1.0f : viewer->zoom;
    f32 half = 0.5f / zoom;
    for (u32 axis = 0; axis < 2; ++axis) {
        if (viewer->center[axis] < half)
            viewer->center[axis] = half;
        if (viewer->center[axis] > 1.0f - half)
            viewer->center[axis] = 1.0f - half;
    }
    // The zoomed window within the base part.
    ImVec2_c w0 = {uv0.x + (uv1.x - uv0.x) * (viewer->center[0] - half), uv0.y + (uv1.y - uv0.y) * (viewer->center[1] - half)};
    ImVec2_c w1 = {uv0.x + (uv1.x - uv0.x) * (viewer->center[0] + half), uv0.y + (uv1.y - uv0.y) * (viewer->center[1] + half)};

    ImVec2_c min = igGetCursorScreenPos();
    igInvisibleButton("##image", size, 0);
    textures_record(viewer, TEXTURES_RECT_IMAGE);
    ImVec2_c max = {min.x + size.x, min.y + size.y};
    if (igIsItemActive() && size.x > 0.0f && size.y > 0.0f) {
        ImVec2_c delta = igGetIO_Nil()->MouseDelta;
        viewer->center[0] -= delta.x / size.x / zoom;
        viewer->center[1] -= delta.y / size.y / zoom;
    }
    ImDrawList* draw = igGetWindowDrawList();
    if (!depth && !viewer->no_checkerboard)
        checkerboard(draw, min, max, 8.0f);
    ImDrawList_AddImage(draw, (ImTextureRef_c){NULL, entry_image(app, entry, 1)}, min, max, w0, w1, 0xFFFFFFFFu);

    // The texel under the pointer, in the shown mip level.
    u32 level = depth ? 0 : viewer->mip;
    u32 level_w = entry->width >> level, level_h = entry->height >> level;
    if (!level_w)
        level_w = 1;
    if (!level_h)
        level_h = 1;
    ImVec2_c mouse = igGetIO_Nil()->MousePos;
    if ((igIsItemHovered(0) || igIsItemActive()) && size.x > 0.0f && size.y > 0.0f) {
        f32 u = w0.x + (w1.x - w0.x) * (mouse.x - min.x) / size.x;
        f32 v = w0.y + (w1.y - w0.y) * (mouse.y - min.y) / size.y;
        u32 x = (u32)(u * (f32)level_w), y = (u32)(v * (f32)level_h);
        igTextDisabled("texel (%u, %u) of %ux%u, uv (%.3f, %.3f)", x < level_w ? x : level_w - 1, y < level_h ? y : level_h - 1,
                       level_w, level_h, (f64)u, (f64)v);
    } else {
        igTextDisabled("%ux%u shown; drag to pan", level_w, level_h);
    }
}

void textures_tab(App* app)
{
    TextureViewer* viewer = &app->textures;
    viewer->shown_now = 1;
    NvArena* scratch = &app->scratch;
    umm mark = scratch->used;
    u32 capacity = NV_MAX_TEXTURES + NV_IMGUI_MAX_TEXTURES + 4;
    TextureEntry* entries = NV_PUSH_ARRAY(scratch, capacity, TextureEntry);
    u32 count = gather(app, entries, capacity);

    u64 total = 0;
    const TextureEntry* picked = NULL;
    for (u32 i = 0; i < count; ++i) {
        total += entries[i].kind == TEXTURE_SWAPCHAIN ? 0 : entry_bytes(&entries[i]);
        if (viewer->has_selection && same_entry(viewer, &entries[i]))
            picked = &entries[i];
    }
    viewer->listed = 0;

    // Wide (a desktop's bottom of a wide window): the list and the picked texture side by side.
    // Narrow (a dock, a phone): one or the other, with Back from the picked one to the list.
    ImVec2_c avail = igGetContentRegionAvail();
    b32 side_by_side = avail.x >= SIDE_BY_SIDE_WIDTH * app->imgui.ui_scale;
    b32 show_list = side_by_side || !picked || !viewer->detail_open;
    b32 show_detail = side_by_side || (picked && viewer->detail_open);
    // The search box filters the list; the picked texture alone has nothing to filter.
    if (show_list)
        search_panel_begin(app, SEARCH_TEXTURES);

    bool in_use_only = !viewer->show_unused;
    if (igCheckbox("In use only", &in_use_only))
        viewer->show_unused = !in_use_only;
    textures_record(viewer, TEXTURES_RECT_IN_USE);
    igSameLine(0.0f, -1.0f);
    char memory[32];
    format_bytes(total, memory, sizeof(memory));
    igTextDisabled("%u textures, %s", count, memory);
    igSetItemTooltip("%s", "GPU memory of the textures listed, not counting the swapchain (the browser owns it)");

#if !defined(NDEBUG)
    memset(viewer->row_rects, 0, sizeof(viewer->row_rects));
    memset(viewer->rects[TEXTURES_RECT_IMAGE], 0, sizeof(viewer->rects[0]));
#endif
    avail = igGetContentRegionAvail();
    if (show_list) {
        if (side_by_side)
            igBeginChild_Str("##texture list", (ImVec2_c){avail.x * 0.42f, 0.0f}, ImGuiChildFlags_Borders, 0);
        u32 number = 0;
        section(app, "Materials", TEXTURE_MATERIAL, TEXTURE_MATERIAL, entries, count, &number);
        section(app, "Render targets", TEXTURE_SHADOW, TEXTURE_SWAPCHAIN, entries, count, &number);
        section(app, "UI", TEXTURE_UI, TEXTURE_UI, entries, count, &number);
        viewer->listed = number;
        if (side_by_side) {
            igEndChild();
            igSameLine(0.0f, -1.0f);
        }
    }
    if (show_detail) {
        if (side_by_side)
            igBeginChild_Str("##texture detail", (ImVec2_c){0.0f, 0.0f}, ImGuiChildFlags_Borders, 0);
        else {
            if (igButton("< Textures", (ImVec2_c){0.0f, 0.0f}))
                viewer->detail_open = 0;
            textures_record(viewer, TEXTURES_RECT_BACK);
        }
        if (picked) {
            detail(app, picked);
            if (picked->kind == TEXTURE_MATERIAL)
                used_by(app, picked->index);
        } else {
            igTextDisabled("Pick a texture to see it large.");
        }
        if (side_by_side)
            igEndChild();
    }
    if (show_list)
        search_panel_end(app);
    scratch->used = mark;
}

void textures_inspector_thumbnail(App* app, NvTextureId texture)
{
    NvRenderTexture* slot = &app->renderer.textures[texture.index];
    f32 side = THUMBNAIL * app->imgui.ui_scale;
    NvImguiPreview preview = {.texture = slot->texture};
    ImTextureID id = nv_imgui_preview(&app->imgui, &preview);
    ImVec2_c min = igGetCursorScreenPos();
    checkerboard(igGetWindowDrawList(), min, (ImVec2_c){min.x + side, min.y + side}, 6.0f);
    if (igImageButton("##texture", (ImTextureRef_c){NULL, id}, (ImVec2_c){side, side}, (ImVec2_c){0.0f, 0.0f},
                      (ImVec2_c){1.0f, 1.0f}, (ImVec4_c){0.0f, 0.0f, 0.0f, 0.0f}, (ImVec4_c){1.0f, 1.0f, 1.0f, 1.0f})) {
        pick(&app->textures, TEXTURE_MATERIAL, texture.index);
        app->open_textures = 1;
    }
    textures_record(&app->textures, TEXTURES_RECT_INSPECTOR);
    igSetItemTooltip("%s", "Show it in the Textures tab");
    igSameLine(0.0f, -1.0f);
    igBeginGroup();
    igTextUnformatted(slot->name, NULL);
    igTextDisabled("Multiplied with the color.");
    igEndGroup();
}

void textures_record(TextureViewer* viewer, TexturesRect id)
{
#if !defined(NDEBUG)
    ImVec2_c min = igGetItemRectMin(), max = igGetItemRectMax();
    viewer->rects[id][0] = min.x;
    viewer->rects[id][1] = min.y;
    viewer->rects[id][2] = max.x;
    viewer->rects[id][3] = max.y;
#else
    (void)viewer, (void)id;
#endif
}
