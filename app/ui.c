#include "app.h"

#include <stdio.h>
#include <string.h>

//
// Scene tab
//

// Deeper than this, the tree stops and says how much is hidden (the stress chain is 1000 deep).
#define TREE_MAX_DEPTH 24

internal void node_tree(App* app, SceneView* view, u32 index, u32 depth)
{
    NvScene* scene = view->scene;
    NvNode* node = &scene->nodes[index];
    NvNodeId id = {index, node->gen};
    if (depth >= TREE_MAX_DEPTH) {
        u32 hidden = 0;
        for (u32 i = index; i; i = scene->nodes[i].first_child)
            ++hidden;
        igTextDisabled("... %u more levels", hidden);
        return;
    }

    // Big groups start closed: thousands of rows would cost more than what they display.
    u32 children = 0;
    for (u32 child = node->first_child; child; child = scene->nodes[child].next_sibling)
        ++children;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children <= 16 && depth < 2)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (!children)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (view->selected.index == id.index && view->selected.gen == id.gen)
        flags |= ImGuiTreeNodeFlags_Selected;

    b32 open = children > 16 ? igTreeNodeEx_Ptr((void*)(umm)index, flags, "%s (%u)", node->name, children)
                             : igTreeNodeEx_Ptr((void*)(umm)index, flags, "%s", node->name);
    if (igIsItemClicked(ImGuiMouseButton_Left) && !igIsItemToggledOpen()) {
        view->selected = id;
        app->open_inspector = 1;
    }
    if (open && children) {
        for (u32 child = node->first_child; child; child = scene->nodes[child].next_sibling)
            node_tree(app, view, child, depth + 1);
        igTreePop();
    }
}

void ui_scene_tab(App* app)
{
    SceneView* view = app_view(app);
    for (u32 root = view->scene->first_root; root; root = view->scene->nodes[root].next_sibling)
        node_tree(app, view, root, 0);
}

//
// Inspector tab
//

internal void animator_section(App* app, NvAnimator* animator)
{
    igSeparatorText("Animator");

    // Clips of this skeleton, without the root-motion copies (app_play picks those).
    NvClipId current = animator->layers[0].clip.index ? app_regular_clip(app, animator->layers[0].clip) : (NvClipId){0};
    u32 clip_count = nv_anim_clip_count();
    for (u32 c = 1; c <= clip_count; ++c) {
        NvClipId clip = {c};
        if (nv_anim_clip_skeleton(clip).index != animator->skeleton.index || nv_anim_clip_has_root_motion(clip))
            continue;
        if (igSelectable_Bool(nv_anim_clip_name(clip), clip.index == current.index, 0, (ImVec2_c){0, 0}))
            app_play(app, clip);
    }
    igBeginDisabled(!app->playing); // a jump is something that happens while playing
    if (igButton("Jump", (ImVec2_c){-1.0f, 0.0f}))
        app_jump(app);
    igEndDisabled();

    igSliderFloat("Speed", &animator->layers[0].speed, 0.0f, 2.0f, "%.2fx", 0);
    igSliderFloat("Fade", &app->fade_seconds, 0.0f, 1.0f, "%.2f s", 0);
    const char* names[APP_MAX_CLIPS];
    for (u32 i = 0; i < app->clip_count; ++i)
        names[i] = nv_anim_clip_name(app->clips[i]);
    igCombo_Str_arr("Blend", &app->blend_clip, names, (int)app->clip_count, -1);
    igSliderFloat("Weight", &app->blend_weight, 0.0f, 1.0f, "%.2f", 0);

    f32 total = 0.0f;
    for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
        if (animator->layers[l].clip.index)
            total += animator->layers[l].weight;
    }
    for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
        NvAnimLayer* layer = &animator->layers[l];
        if (!layer->clip.index)
            continue;
        char overlay[64];
        snprintf(overlay, sizeof(overlay), "%s %.2f", nv_anim_clip_name(layer->clip), layer->weight);
        igProgressBar(total > 0.0f ? layer->weight / total : 0.0f, (ImVec2_c){-1.0f, 0.0f}, overlay);
    }

    if (igCheckbox("Root motion", &app->root_motion) && app->jump == JUMP_NONE)
        app_play(app, app_regular_clip(app, animator->layers[0].clip));
    igBeginDisabled(!app->root_motion);
    igSliderFloat("Turn", &app->turn_rate, -1.5f, 1.5f, "%.2f rad/s", 0);
    if (igButton("Back to center", (ImVec2_c){-1.0f, 0.0f}))
        app_back_to_center(app);
    igEndDisabled();
    igCheckbox("Look at target", &app->look_at);
}

internal void attach_section(App* app, NvNode* node)
{
    igSeparatorText("Attach");
    NvAnimator* animator = nv_anim_get(node->attach.animator);
    const NvJointDesc* joints = nv_anim_joints(animator->skeleton);
    if (igBeginCombo("Joint", joints[node->attach.joint].name, 0)) {
        for (u32 j = 0; j < animator->joint_count; ++j) {
            bool current = j == node->attach.joint;
            if (igSelectable_Bool(joints[j].name, current, 0, (ImVec2_c){0, 0}))
                node->attach.joint = j;
            if (current)
                igSetItemDefaultFocus();
        }
        igEndCombo();
    }
    if (app->shown == SCENE_SHOWCASE && node == nv_scene_get(app->scene, app->sword))
        igCheckbox("Visible", &app->show_sword);
}

// Keeps the next item, `width` wide, on this line when it fits, so a row of them wraps on a
// narrow (phone) panel instead of running off its edge.
void ui_same_line_if_fits(f32 width)
{
    igSameLine(0.0f, -1.0f);
    if (igGetContentRegionAvail().x < width)
        igNewLine();
}

// The same for the next checkbox or radio button labeled `label`.
internal void same_line_if_fits(const char* label)
{
    ImVec2_c text = igCalcTextSize(label, NULL, true, -1.0f);
    ui_same_line_if_fits(igGetFrameHeight() + igGetStyle()->ItemInnerSpacing.x + text.x);
}

// The View tab's Anti-aliasing section (docs/specs/msaa.md).
internal void msaa_ui(App* app)
{
    igSeparatorText("Anti-aliasing");
    local_persist const char* names[] = {"Off", "MSAA 4x"};
    s32 index = app->renderer.msaa == 4 ? 1 : 0;
    if (igCombo_Str_arr("Edges", &index, names, 2, -1))
        app->renderer.msaa = index ? 4 : 1;
}

// The View tab's Shadows section (docs/specs/shadows.md).
internal void shadow_ui(App* app)
{
    NvShadowSettings* shadows = &app->renderer.shadows;
    igSeparatorText("Shadows");
    local_persist const u32 sizes[] = {0, 512, 1024, 2048};
    local_persist const char* size_names[] = {"Off", "512", "1024", "2048"};
    s32 size_index = 0;
    for (u32 i = 0; i < NV_ARRAY_COUNT(sizes); ++i) {
        if (shadows->size == sizes[i])
            size_index = (s32)i;
    }
    if (igCombo_Str_arr("Map size", &size_index, size_names, (int)NV_ARRAY_COUNT(sizes), -1))
        shadows->size = sizes[size_index];
    igBeginDisabled(!shadows->size);
    local_persist const char* format_names[] = {"32-bit float", "16-bit"};
    s32 format = (s32)shadows->format;
    if (igCombo_Str_arr("Format", &format, format_names, 2, -1))
        shadows->format = (NvShadowFormat)format;
    local_persist const char* filter_names[] = {"Low", "High"};
    s32 filter = (s32)shadows->filter;
    if (igCombo_Str_arr("Filter", &filter, filter_names, 2, -1))
        shadows->filter = (NvShadowFilter)filter;
    igSliderFloat("Distance##shadow", &shadows->distance, 5.0f, 100.0f, "%.0f m", 0);
    bool show_box = shadows->show_box != 0;
    if (igCheckbox("Show light box", &show_box))
        shadows->show_box = show_box;
    igEndDisabled();
}

void ui_inspector_tab(App* app)
{
    SceneView* view = app_view(app);
    if (!view->selected.index) {
        igTextDisabled("Select a node in the Scene tab.");
        return;
    }
    NvNode* node = nv_scene_get(view->scene, view->selected);
    igInputText("Name", node->name, sizeof(node->name), 0, NULL, NULL);
    if (view->selected.index != view->scene->active_camera.index) {
        // The gizmo in the viewport; W, E and R switch the operation there too.
        s32* operation = (s32*)&app->gizmo_operation;
        igRadioButton_IntPtr("Move", operation, GIZMO_MOVE);
        same_line_if_fits("Rotate");
        igRadioButton_IntPtr("Rotate", operation, GIZMO_ROTATE);
        same_line_if_fits("Scale");
        igRadioButton_IntPtr("Scale", operation, GIZMO_SCALE);
        same_line_if_fits("Local");
        igBeginDisabled(app->gizmo_operation == GIZMO_SCALE);
        igCheckbox("Local", &app->gizmo_local);
        igEndDisabled();
        same_line_if_fits("Snap");
        igCheckbox("Snap", &app->gizmo_snap);
    }
    igDragFloat3("Position", &node->position.x, 0.02f, 0.0f, 0.0f, "%.2f", 0);
    // Rotation as pitch (X), yaw (Y) and roll (Z) in degrees; the quaternion is only rewritten when
    // edited, so looking at a node never changes it.
    NvVec3 euler = nv_quat_to_euler(node->rotation);
    f32 degrees[3] = {euler.x * 180.0f / NV_PI, euler.y * 180.0f / NV_PI, euler.z * 180.0f / NV_PI};
    // Tiny negatives (float noise, -0) would show as "-0.0".
    for (u32 i = 0; i < 3; ++i) {
        if (fabsf(degrees[i]) < 0.05f)
            degrees[i] = 0.0f;
    }
    if (igDragFloat3("Rotation", degrees, 0.5f, 0.0f, 0.0f, "%.1f", 0))
        node->rotation = nv_quat_from_euler(nv_vec3(degrees[0] * NV_PI / 180.0f, degrees[1] * NV_PI / 180.0f, degrees[2] * NV_PI / 180.0f));
    igDragFloat3("Scale", &node->scale.x, 0.01f, 0.01f, 100.0f, "%.2f", 0);

    if (node->mesh.index) {
        igSeparatorText("Mesh");
        f32 color[4];
        for (u32 i = 0; i < 4; ++i)
            color[i] = app->renderer.materials[node->material.index].desc.base_color[i];
        if (igColorEdit4("Color", color, ImGuiColorEditFlags_Float))
            nv_renderer_set_material_color(&app->renderer, node->material, color);
        NvTextureId texture = app->renderer.materials[node->material.index].desc.base_color_texture;
        if (texture.index)
            textures_inspector_thumbnail(app, texture);
    }
    if (node->camera.projection) {
        igSeparatorText("Camera");
        igSliderAngle("Field of view", &node->camera.fov_y, 20.0f, 100.0f, "%.0f deg", 0);
    }
    if (node->light.type) {
        igSeparatorText("Light");
        igColorEdit3("Color##light", &node->light.color.x, ImGuiColorEditFlags_Float);
        igSliderFloat("Intensity", &node->light.intensity, 0.0f, 3.0f, "%.2f", 0);
    }
    if (node->attach.animator.index)
        attach_section(app, node);
    // The full animator controls drive the showcase character; others show what they play.
    NvAnimatorId animator = app_node_animator(view->scene, view->selected);
    if (animator.index == app->animator.index) {
        animator_section(app, nv_anim_get(animator));
    } else if (animator.index) {
        igSeparatorText("Animator");
        NvAnimLayer* layer = &nv_anim_get(animator)->layers[0];
        if (layer->clip.index)
            igText("%s  %.2f / %.2f s", nv_anim_clip_name(layer->clip), layer->time, nv_anim_clip_duration(layer->clip));
    }
}

//
// View tab
//

void ui_view_tab(App* app)
{
    ImGuiIO* io = igGetIO_Nil();
    SceneView* view = app_view(app);
    local_persist const char* scenes[SCENE_COUNT] = {"Showcase", "Stress"};
    int shown = (int)app->shown;
    if (igCombo_Str_arr("Scene", &shown, scenes, SCENE_COUNT, -1))
        app_show_scene(app, (SceneKind)shown);
    igText("%.0f FPS (%.2f ms)", io->Framerate, 1000.0f / io->Framerate);
    igTextDisabled("%s build, commit %s", NV_BUILD_NAME, NV_GIT_COMMIT);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){0.6f, 0.6f, 0.6f, 1.0f});
    igTextWrapped("%s", NV_GIT_SUBJECT);
    igPopStyleColor(1);
    igSliderAngle("Camera yaw", &view->camera_yaw, -180.0f, 180.0f, "%.0f deg", 0);
    igSliderAngle("Camera pitch", &view->camera_pitch, -10.0f, 80.0f, "%.0f deg", 0);
    igSliderFloat("Distance", &view->camera_distance, 1.0f, 100.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
    igCheckbox("Camera follows selection", &view->follow_selection);
    if (app->shown == SCENE_SHOWCASE) {
        igCheckbox("Show bones", &app->show_bones);
        igSliderFloat("Planet orbit", &app->orbit_speed, -3.0f, 3.0f, "%.2f rad/s", 0);
    }
    msaa_ui(app);
    shadow_ui(app);
    save_ui(app);
}

// The number of warnings and errors not yet seen, for the Console tab's label and the badge.
internal void format_unseen(u32 unseen, char* out, umm capacity)
{
    if (unseen > 99)
        snprintf(out, capacity, "99+");
    else
        snprintf(out, capacity, "%u", unseen);
}

// Copies `text` into `out`, cut and ended with "..." if it is wider than `room` pixels. The cut
// is the longest one that fits (a binary search over the text's UTF-8 characters), never inside a
// character.
internal void fit_text(const char* text, f32 room, char* out, umm capacity)
{
    umm length = strlen(text);
    if (length >= capacity - 4)
        length = capacity - 5;
    memcpy(out, text, length);
    out[length] = 0;
    if (igCalcTextSize(out, NULL, false, -1.0f).x <= room)
        return;
    umm lo = 0, hi = length; // the longest prefix that fits is in [lo, hi)
    while (lo + 1 < hi) {
        umm mid = (lo + hi) / 2;
        while (mid > lo && ((u8)text[mid] & 0xC0) == 0x80)
            --mid; // the start of a character
        if (mid == lo) {
            lo = hi - 1; // no character start between: stop
            break;
        }
        char candidate[512];
        snprintf(candidate, sizeof(candidate), "%.*s...", (int)mid, text);
        if (igCalcTextSize(candidate, NULL, false, -1.0f).x <= room)
            lo = mid;
        else
            hi = mid;
    }
    snprintf(out, capacity, "%.*s...", (int)lo, text);
}

// The build type in the viewport's top-left corner, so a Debug page is never mistaken for Release.
// Warnings and errors that arrived while the Console tab was not shown add a badge, a dot and a
// count; a tap on the label then opens the Console tab (pick in main.c, through `badge_box`).
void ui_build_label(App* app)
{
    char text[80];
    snprintf(text, sizeof(text), "%s build %s%s", NV_BUILD_NAME, NV_GIT_COMMIT,
             app->playing && app->shown == SCENE_SHOWCASE ? " \xC2\xB7 Playing" : "");
    NvLogLevel worst;
    u32 unseen = console_unseen(app, &worst);
    char count[8];
    format_unseen(unseen, count, sizeof(count));

    ImDrawList* draw = igGetForegroundDrawList_ViewportPtr(NULL);
    ImVec2_c size = igCalcTextSize(text, NULL, false, -1.0f);
    // The viewport's corner, in CSS pixels: below the phone's top bar, right of the desktop's left dock.
    f32 ratio = app->window.pixel_ratio > 0.0f ? app->window.pixel_ratio : 1.0f;
    ImVec2_c pos = {(f32)app->layout.viewport.x / ratio + 6.0f, (f32)app->layout.viewport.y / ratio + 6.0f};
    f32 radius = igGetFontSize() * 0.3f;
    f32 badge_width = 0.0f;
    if (unseen)
        badge_width = 8.0f + radius * 2.0f + 4.0f + igCalcTextSize(count, NULL, false, -1.0f).x;
    ImVec2_c min = {pos.x - 4.0f, pos.y - 2.0f};
    ImVec2_c max = {pos.x + size.x + badge_width + 4.0f, pos.y + size.y + 2.0f};
    ImDrawList_AddRectFilled(draw, min, max, 0x99000000u, 3.0f, 0);
    ImDrawList_AddText_Vec2(draw, pos, 0xFFFFFFFFu, text, NULL);
    // The commit's subject line under it, cut to the viewport's width. Not part of the tap box.
    {
        char subject[sizeof(NV_GIT_SUBJECT) + 4];
        fit_text(NV_GIT_SUBJECT, (f32)app->layout.viewport.width / ratio - 20.0f, subject, sizeof(subject));
        ImVec2_c subject_size = igCalcTextSize(subject, NULL, false, -1.0f);
        ImVec2_c subject_pos = {pos.x, max.y + 3.0f};
        ImDrawList_AddRectFilled(draw, (ImVec2_c){pos.x - 4.0f, subject_pos.y - 2.0f},
                                 (ImVec2_c){pos.x + subject_size.x + 4.0f, subject_pos.y + subject_size.y + 2.0f}, 0x99000000u, 3.0f, 0);
        ImDrawList_AddText_Vec2(draw, subject_pos, 0xFFBBBBBBu, subject, NULL);
    }
    if (unseen) {
        ImU32 color = console_level_color(worst);
        f32 x = pos.x + size.x + 8.0f;
        ImDrawList_AddCircleFilled(draw, (ImVec2_c){x + radius, pos.y + size.y * 0.5f}, radius, color, 12);
        ImDrawList_AddText_Vec2(draw, (ImVec2_c){x + radius * 2.0f + 4.0f, pos.y}, color, count, NULL);
    }

    // The box a tap counts in: the label, grown to a size a finger can hit.
    f32* box = app->badge_box;
    if (!unseen) {
        box[0] = box[1] = box[2] = box[3] = 0.0f;
        return;
    }
    f32 minimum = 32.0f * app->imgui.ui_scale;
    box[0] = min.x;
    box[1] = min.y;
    box[2] = max.x;
    box[3] = max.y;
    for (u32 axis = 0; axis < 2; ++axis) {
        f32 extra = minimum - (box[axis + 2] - box[axis]);
        if (extra > 0.0f) {
            box[axis] -= extra * 0.5f;
            box[axis + 2] += extra * 0.5f;
        }
    }
}

void ui_play_button(App* app, ImVec2_c size)
{
    f32* box = app->play_box;
    box[0] = box[1] = box[2] = box[3] = 0.0f;
    // Play and Stop belong to the showcase; the stress scene always runs.
    if (app->shown != SCENE_SHOWCASE)
        return;
    if (igButton(app->playing ? "Stop###play" : "Play###play", size)) {
        if (app->playing)
            app_stop_playing(app);
        else
            app_start_playing(app);
    }
    ImVec2_c min = igGetItemRectMin(), max = igGetItemRectMax();
    box[0] = min.x;
    box[1] = min.y;
    box[2] = max.x;
    box[3] = max.y;
}

void ui_playing_note(App* app)
{
    if (!app->playing || app->shown != SCENE_SHOWCASE)
        return;
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){0.55f, 0.85f, 1.0f, 1.0f});
    igTextWrapped("Playing: edits are lost on Stop.");
    igPopStyleColor(1);
}

b32 ui_push_play_tint(App* app)
{
    // A tinted panel while the showcase plays, so edits that will be lost are not mistaken for
    // edits that stay.
    if (!app->playing || app->shown != SCENE_SHOWCASE)
        return 0;
    igPushStyleColor_Vec4(ImGuiCol_WindowBg, (ImVec4_c){0.05f, 0.12f, 0.20f, 1.0f});
    return 1;
}

void ui_pop_play_tint(b32 pushed)
{
    if (pushed)
        igPopStyleColor(1);
}

b32 ui_begin_console_tab(App* app)
{
    // NOTE: The label counts warnings and errors that arrived while the tab was not shown. Its id
    // (###console) stays the same, so the tab keeps its place as the count changes.
    NvLogLevel worst;
    u32 unseen = console_unseen(app, &worst);
    char label[48] = "Console###console";
    if (unseen) {
        // A phone's tab bar has no room for the number (the tabs would scroll): the color says
        // it, and the badge on the build label counts.
        if (!console_is_compact()) {
            char count[8];
            format_unseen(unseen, count, sizeof(count));
            snprintf(label, sizeof(label), "Console (%s)###console", count);
        }
        igPushStyleColor_U32(ImGuiCol_Text, console_level_color(worst));
    }
    ImGuiTabItemFlags flags = app->open_console ? ImGuiTabItemFlags_SetSelected : 0;
    app->open_console = 0;
    b32 open = igBeginTabItem(label, NULL, flags);
    if (unseen)
        igPopStyleColor(1);
    console_record(&app->console, CONSOLE_RECT_TAB);
    return open;
}

b32 ui_begin_textures_tab(App* app)
{
    ImGuiTabItemFlags flags = app->open_textures ? ImGuiTabItemFlags_SetSelected : 0;
    app->open_textures = 0;
    b32 open = igBeginTabItem("Textures", NULL, flags);
    textures_record(&app->textures, TEXTURES_RECT_TAB);
    return open;
}

NvRect ui_rect(f32 x0, f32 y0, f32 x1, f32 y1, f32 ratio)
{
    u32 left = (u32)(x0 * ratio + 0.5f), top = (u32)(y0 * ratio + 0.5f);
    u32 right = (u32)(x1 * ratio + 0.5f), bottom = (u32)(y1 * ratio + 0.5f);
    if (right < left)
        right = left;
    if (bottom < top)
        bottom = top;
    return (NvRect){left, top, right - left, bottom - top};
}

internal u32 clamp_u32(u32 value, u32 lo, u32 hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

// The scene's resolution and where its image goes in the viewport (docs/specs/resolution.md).
internal NvSceneOutput scene_output(const Resolution* resolution, NvRect viewport)
{
    NvSceneOutput out = {.image = viewport, .pixel_size = 1.0f};
    if (!viewport.width || !viewport.height) {
        out.width = out.height = 1;
        return out;
    }
    if (resolution->mode == RESOLUTION_FIXED) {
        u32 width = clamp_u32(resolution->fixed_width, RESOLUTION_MIN, RESOLUTION_MAX);
        u32 height = clamp_u32(resolution->fixed_height, RESOLUTION_MIN, RESOLUTION_MAX);
        out.width = width;
        out.height = height;
        // The largest whole multiple that fits, so every block is the same size; shrunk to fit,
        // keeping the aspect ratio, only when even the size itself does not.
        u32 fit_x = viewport.width / width, fit_y = viewport.height / height;
        u32 multiple = fit_x < fit_y ? fit_x : fit_y;
        u32 image_width, image_height;
        if (multiple >= 1) {
            out.pixel_size = (f32)multiple;
            image_width = width * multiple;
            image_height = height * multiple;
        } else {
            f32 sx = (f32)viewport.width / (f32)width, sy = (f32)viewport.height / (f32)height;
            out.pixel_size = sx < sy ? sx : sy;
            image_width = clamp_u32((u32)((f32)width * out.pixel_size + 0.5f), 1, viewport.width);
            image_height = clamp_u32((u32)((f32)height * out.pixel_size + 0.5f), 1, viewport.height);
        }
        out.image = (NvRect){viewport.x + (viewport.width - image_width) / 2, viewport.y + (viewport.height - image_height) / 2,
                             image_width, image_height};
        return out;
    }
    u32 divisor = clamp_u32(resolution->divisor, 1, 4);
    out.width = (viewport.width + divisor - 1) / divisor;
    out.height = (viewport.height + divisor - 1) / divisor;
    out.pixel_size = (f32)divisor;
    return out;
}

void app_layout(App* app)
{
    f32 ratio = app->window.pixel_ratio > 0.0f ? app->window.pixel_ratio : 1.0f;
    f32 width = (f32)app->gpu.width / ratio;
    f32 height = (f32)app->gpu.height / ratio;
    if (app->ui_mode == UI_PHONE)
        phone_layout(app, width, height, ratio);
    else
        desktop_layout(app, width, height, ratio);
    app->layout.scene = scene_output(&app->resolution, app->layout.viewport);
}

void app_build_ui(App* app)
{
    // The Console tab counts what arrived while it was not shown: it was shown last frame or not.
    app->console.shown_last = app->console.shown_now;
    app->console.shown_now = 0;
    app->textures.shown_last = app->textures.shown_now;
    app->textures.shown_now = 0;
    ui_build_label(app);
    if (app->ui_mode == UI_PHONE)
        phone_build_ui(app);
    else
        desktop_build_ui(app);
}
