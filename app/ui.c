#include "app.h"

#include <stdio.h>
#include <string.h>

//
// Scene tab
//

// Deeper than this, the tree stops and says how much is hidden (the stress chain is 1000 deep).
#define TREE_MAX_DEPTH 24

// The Scene tree's filter marks (Search.tree): a node matches, or is on the way to one.
#define TREE_SELF 1
#define TREE_PATH 2
// Rows listed under the "more levels" row of a path that goes deeper than the tree draws.
#define TREE_DEEP_MAX 50

// Marks the nodes the query matches and their parents; again only when the query, the scene or (every
// half second) the names may have changed.
internal void tree_marks(App* app, SceneView* view)
{
    Search* s = &app->search;
    NvScene* scene = view->scene;
    u32 key = 2166136261u;
    for (const char* c = s->queries[SEARCH_SCENE]; *c; ++c)
        key = (key ^ (u8)*c) * 16777619u;
    key = (key ^ scene->node_count) * 16777619u;
    key = (key ^ (u32)app->shown) * 16777619u;
    s32 frame = igGetFrameCount();
    if (key == s->tree_key && frame - s->tree_frame < 30)
        return;
    s->tree_key = key;
    s->tree_frame = frame;
    s->tree_matches = 0;
    memset(s->tree, 0, scene->node_count + 1);
    for (u32 i = 1; i <= scene->node_count; ++i) {
        if (!(scene->nodes[i].gen & 1) || !search_match(app, scene->nodes[i].name))
            continue;
        s->tree[i] |= TREE_SELF;
        ++s->tree_matches;
        for (u32 p = i; p && !(s->tree[p] & TREE_PATH); p = scene->nodes[p].parent)
            s->tree[p] |= TREE_PATH;
    }
}

internal void tree_select(App* app, SceneView* view, u32 index)
{
    view->selected = (NvNodeId){index, view->scene->nodes[index].gen};
    app->open_inspector = 1;
}

// Below the depth the tree draws: the matches under `index`, flat, so a match at the end of the
// 1000-deep chain can still be picked.
internal void tree_deep_matches(App* app, SceneView* view, u32 index)
{
    Search* s = &app->search;
    NvScene* scene = view->scene;
    u32 hidden = 0;
    for (u32 i = index; i; i = scene->nodes[i].first_child)
        ++hidden;
    igTextDisabled("... %u more levels", hidden);
    igIndent(igGetTreeNodeToLabelSpacing());
    u32 listed = 0;
    u32 n = index;
    for (;;) {
        if ((s->tree[n] & TREE_SELF) && listed < TREE_DEEP_MAX) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;
            if (view->selected.index == n && view->selected.gen == scene->nodes[n].gen)
                flags |= ImGuiTreeNodeFlags_Selected;
            igTreeNodeEx_Ptr((void*)(umm)n, flags, "%s", scene->nodes[n].name);
            if (igIsItemClicked(ImGuiMouseButton_Left))
                tree_select(app, view, n);
            ImVec2_c min = igGetItemRectMin();
            search_mark(app, scene->nodes[n].name, (ImVec2_c){min.x + igGetTreeNodeToLabelSpacing(), min.y}, igGetFrameHeight());
            ++listed;
            ++s->tree_drawn;
            ++s->rows_now[SEARCH_SCENE];
        }
        if (scene->nodes[n].first_child) {
            n = scene->nodes[n].first_child;
            continue;
        }
        while (n != index && !scene->nodes[n].next_sibling)
            n = scene->nodes[n].parent;
        if (n == index)
            break;
        n = scene->nodes[n].next_sibling;
    }
    igUnindent(igGetTreeNodeToLabelSpacing());
}

internal void node_tree(App* app, SceneView* view, u32 index, u32 depth)
{
    Search* s = &app->search;
    NvScene* scene = view->scene;
    NvNode* node = &scene->nodes[index];
    NvNodeId id = {index, node->gen};
    b32 filtering = s->tree_filtering;
    if (filtering) {
        if (!(s->tree[index] & TREE_PATH) || s->rows_now[SEARCH_SCENE] >= SEARCH_TREE_MAX_ROWS)
            return;
    }
    if (depth >= TREE_MAX_DEPTH) {
        if (filtering) {
            tree_deep_matches(app, view, index);
        } else {
            u32 hidden = 0;
            for (u32 i = index; i; i = scene->nodes[i].first_child)
                ++hidden;
            igTextDisabled("... %u more levels", hidden);
        }
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
    // While filtering, the way to a match is open, and parents that do not match are grey.
    b32 self = !filtering || (s->tree[index] & TREE_SELF);
    ++s->rows_now[SEARCH_SCENE];
    if (filtering) {
        if (self)
            ++s->tree_drawn;
        if (children)
            igSetNextItemOpen(true, ImGuiCond_Always);
        if (!self)
            igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){0.55f, 0.55f, 0.55f, 1.0f});
    }

    b32 open = children > 16 ? igTreeNodeEx_Ptr((void*)(umm)index, flags, "%s (%u)", node->name, children)
                             : igTreeNodeEx_Ptr((void*)(umm)index, flags, "%s", node->name);
    if (filtering && !self)
        igPopStyleColor(1);
    if (igIsItemClicked(ImGuiMouseButton_Left) && !igIsItemToggledOpen())
        tree_select(app, view, index);
    if (filtering && self) {
        ImVec2_c min = igGetItemRectMin();
        search_mark(app, node->name, (ImVec2_c){min.x + igGetTreeNodeToLabelSpacing(), min.y}, igGetFrameHeight());
    }
    if (open && children) {
        for (u32 child = node->first_child; child; child = scene->nodes[child].next_sibling)
            node_tree(app, view, child, depth + 1);
        igTreePop();
    }
}

void ui_scene_tab(App* app)
{
    Search* s = &app->search;
    SceneView* view = app_view(app);
    search_panel_begin(app, SEARCH_SCENE);
    s->tree_filtering = search_active(app);
    s->tree_drawn = 0;
    if (s->tree_filtering)
        tree_marks(app, view);
    for (u32 root = view->scene->first_root; root; root = view->scene->nodes[root].next_sibling)
        node_tree(app, view, root, 0);
    if (s->tree_filtering && s->tree_matches > s->tree_drawn)
        igTextDisabled("and %u more", s->tree_matches - s->tree_drawn);
    if (s->tree_filtering && !s->tree_matches)
        s->rows_now[SEARCH_SCENE] = 0;
    s->tree_filtering = 0;
    search_panel_end(app);
}

//
// Inspector tab
//

internal void animator_section(App* app, NvAnimator* animator)
{
    search_section(app, "Animator");

    // Clips of this skeleton, without the root-motion copies (app_play picks those).
    NvClipId current = animator->layers[0].clip.index ? app_regular_clip(app, animator->layers[0].clip) : (NvClipId){0};
    u32 clip_count = nv_anim_clip_count();
    for (u32 c = 1; c <= clip_count; ++c) {
        NvClipId clip = {c};
        if (nv_anim_clip_skeleton(clip).index != animator->skeleton.index || nv_anim_clip_has_root_motion(clip))
            continue;
        if (search_group(app, nv_anim_clip_name(clip), "clip animation play")) {
            if (igSelectable_Bool(nv_anim_clip_name(clip), clip.index == current.index, 0, (ImVec2_c){0, 0}))
                app_play(app, clip);
        }
    }
    igBeginDisabled(!app->playing); // a jump is something that happens while playing
    if (search_group(app, "Jump", "play")) {
        if (igButton("Jump", (ImVec2_c){-1.0f, 0.0f}))
            app_jump(app);
    }
    igEndDisabled();

    if (search_row(app, "Speed", "playback"))
        igSliderFloat("Speed", &animator->layers[0].speed, 0.0f, 2.0f, "%.2fx", 0);
    if (search_row(app, "Fade", "crossfade blend"))
        igSliderFloat("Fade", &app->fade_seconds, 0.0f, 1.0f, "%.2f s", 0);
    if (search_row(app, "Blend", "clip")) {
        const char* names[APP_MAX_CLIPS];
        for (u32 i = 0; i < app->clip_count; ++i)
            names[i] = nv_anim_clip_name(app->clips[i]);
        igCombo_Str_arr("Blend", &app->blend_clip, names, (int)app->clip_count, -1);
    }
    if (search_row(app, "Weight", "blend"))
        igSliderFloat("Weight", &app->blend_weight, 0.0f, 1.0f, "%.2f", 0);

    // What the layers play now is no setting: shown only without a search.
    if (search_plain(app)) {
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
    }

    if (search_row(app, "Root motion", "walk")) {
        if (igCheckbox("Root motion", &app->root_motion) && app->jump == JUMP_NONE)
            app_play(app, app_regular_clip(app, animator->layers[0].clip));
    }
    igBeginDisabled(!app->root_motion);
    if (search_row(app, "Turn", "root motion"))
        igSliderFloat("Turn", &app->turn_rate, -1.5f, 1.5f, "%.2f rad/s", 0);
    if (search_group(app, "Back to center", "root motion")) {
        if (igButton("Back to center", (ImVec2_c){-1.0f, 0.0f}))
            app_back_to_center(app);
    }
    igEndDisabled();
    if (search_row(app, "Look at target", "head ik aim"))
        igCheckbox("Look at target", &app->look_at);
}

internal void attach_section(App* app, NvNode* node)
{
    search_section(app, "Attach");
    NvAnimator* animator = nv_anim_get(node->attach.animator);
    const NvJointDesc* joints = nv_anim_joints(animator->skeleton);
    if (search_row(app, "Joint", "bone attach")) {
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
    }
    if (app->shown == SCENE_SHOWCASE && node == nv_scene_get(app->scene, app->sword)) {
        if (search_row(app, "Visible", "sword"))
            igCheckbox("Visible", &app->show_sword);
    }
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
    search_section(app, "Anti-aliasing");
    local_persist const char* names[] = {"Off", "MSAA 4x"};
    s32 index = app->renderer.msaa == 4 ? 1 : 0;
    if (search_row(app, "Edges", "msaa multisample samples smooth")) {
        if (igCombo_Str_arr("Edges", &index, names, 2, -1))
            app->renderer.msaa = index ? 4 : 1;
    }
}

internal u32 clamp_u32(u32 value, u32 lo, u32 hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

// The View tab's Resolution section (docs/specs/resolution.md).
internal void resolution_ui(App* app)
{
    Resolution* resolution = &app->resolution;
    search_section(app, "Resolution");
    local_persist const char* modes[] = {"Scale", "Fixed size"};
    s32 mode = (s32)resolution->mode;
    if (search_row(app, "Mode", "scale fixed size")) {
        if (igCombo_Str_arr("Mode", &mode, modes, 2, -1))
            resolution->mode = mode == 1 ? RESOLUTION_FIXED : RESOLUTION_SCALE;
    }

    if (resolution->mode == RESOLUTION_SCALE) {
        local_persist const char* divisors[] = {"1/1 (full)", "1/2", "1/3", "1/4"};
        s32 index = (s32)clamp_u32(resolution->divisor, 1, 4) - 1;
        if (search_row(app, "Scale", "divisor lower resolution pixel")) {
            if (igCombo_Str_arr("Scale", &index, divisors, 4, -1))
                resolution->divisor = (u32)index + 1;
        }
    } else {
        // Presets, and Custom for any other size (also while it is being typed).
        local_persist const u32 sizes[][2] = {{640, 360}, {1280, 720}, {1920, 1080}, {360, 640}, {720, 1280}};
        local_persist const char* names[] = {"640 x 360", "1280 x 720", "1920 x 1080", "360 x 640 (portrait)", "720 x 1280 (portrait)", "Custom"};
        local_persist b32 want_custom;
        s32 index = (s32)NV_ARRAY_COUNT(sizes); // Custom
        for (u32 i = 0; i < NV_ARRAY_COUNT(sizes); ++i) {
            if (sizes[i][0] == resolution->fixed_width && sizes[i][1] == resolution->fixed_height)
                index = (s32)i;
        }
        if (want_custom)
            index = (s32)NV_ARRAY_COUNT(sizes);
        if (search_row(app, "Size", "resolution fixed preset 720p 1080p portrait")) {
            if (igCombo_Str_arr("Size", &index, names, (int)NV_ARRAY_COUNT(names), -1)) {
                want_custom = index == (s32)NV_ARRAY_COUNT(sizes);
                if (!want_custom) {
                    resolution->fixed_width = sizes[index][0];
                    resolution->fixed_height = sizes[index][1];
                }
            }
        }
        local_persist const char* fits[] = {"Whole multiples", "Fit to viewport", "Stretch to viewport"};
        s32 fit = (s32)resolution->fixed_fit;
        if (search_row(app, "Fit", "whole multiples stretch letterbox bars")) {
            if (igCombo_Str_arr("Fit", &fit, fits, 3, -1))
                resolution->fixed_fit = (FixedFit)fit;
        }
        if (index == (s32)NV_ARRAY_COUNT(sizes)) {
            int width = (int)resolution->fixed_width, height = (int)resolution->fixed_height;
            if (search_row(app, "Width", "custom size")) {
                if (igInputInt("Width", &width, 16, 128, 0))
                    resolution->fixed_width = clamp_u32((u32)(width < 0 ? 0 : width), RESOLUTION_MIN, RESOLUTION_MAX);
            }
            if (search_row(app, "Height", "custom size")) {
                if (igInputInt("Height", &height, 16, 128, 0))
                    resolution->fixed_height = clamp_u32((u32)(height < 0 ? 0 : height), RESOLUTION_MIN, RESOLUTION_MAX);
            }
        }
    }
    // What the settings come to is no setting: shown only without a search.
    if (!search_plain(app))
        return;

    // What that comes to: the scene's pixels, and how big each one shows.
    const NvSceneOutput* scene = &app->layout.scene;
    f32 ratio = app->window.pixel_ratio > 0.0f ? app->window.pixel_ratio : 1.0f;
    f32 pw = scene->pixel_width, ph = scene->pixel_height;
    b32 whole = pw == ph && pw >= 1.0f && pw == (f32)(u32)pw;
    if (whole) {
        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){0.6f, 0.6f, 0.6f, 1.0f});
        igTextWrapped("Renders %u x %u; a pixel shows as %.0f x %.0f screen pixels (%.1f per CSS pixel)", scene->width, scene->height,
                      (f64)pw, (f64)ph, (f64)(pw / ratio));
        igPopStyleColor(1);
    } else if (pw >= 1.0f && ph >= 1.0f) {
        // Fitted or stretched: pixels are 1 or more screen pixels wide, unevenly.
        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){0.6f, 0.6f, 0.6f, 1.0f});
        if (pw == ph)
            igTextWrapped("Renders %u x %u, fitted to the viewport at %.2fx: pixels are uneven blocks of whole screen pixels",
                          scene->width, scene->height, (f64)pw);
        else
            igTextWrapped("Renders %u x %u, stretched to the viewport: %.2f x %.2f screen pixels per pixel, in uneven blocks",
                          scene->width, scene->height, (f64)pw, (f64)ph);
        igPopStyleColor(1);
    } else {
        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){1.0f, 0.75f, 0.35f, 1.0f});
        igTextWrapped("Renders %u x %u, larger than the viewport: shown at %.2fx x %.2fx, some pixels dropped", scene->width,
                      scene->height, (f64)pw, (f64)ph);
        igPopStyleColor(1);
    }
}

// The View tab's Shadows section (docs/specs/shadows.md).
internal void shadow_ui(App* app)
{
    NvShadowSettings* shadows = &app->renderer.shadows;
    search_section(app, "Shadows");
    local_persist const u32 sizes[] = {0, 512, 1024, 2048};
    local_persist const char* size_names[] = {"Off", "512", "1024", "2048"};
    s32 size_index = 0;
    for (u32 i = 0; i < NV_ARRAY_COUNT(sizes); ++i) {
        if (shadows->size == sizes[i])
            size_index = (s32)i;
    }
    if (search_row(app, "Map size", "shadow resolution off")) {
        if (igCombo_Str_arr("Map size", &size_index, size_names, (int)NV_ARRAY_COUNT(sizes), -1))
            shadows->size = sizes[size_index];
    }
    igBeginDisabled(!shadows->size);
    local_persist const char* format_names[] = {"32-bit float", "16-bit"};
    s32 format = (s32)shadows->format;
    if (search_row(app, "Format", "shadow depth")) {
        if (igCombo_Str_arr("Format", &format, format_names, 2, -1))
            shadows->format = (NvShadowFormat)format;
    }
    local_persist const char* filter_names[] = {"Low", "High"};
    s32 filter = (s32)shadows->filter;
    if (search_row(app, "Filter", "shadow soft pcf")) {
        if (igCombo_Str_arr("Filter", &filter, filter_names, 2, -1))
            shadows->filter = (NvShadowFilter)filter;
    }
    if (search_row(app, "Distance##shadow", "shadow fade"))
        igSliderFloat("Distance##shadow", &shadows->distance, 5.0f, 100.0f, "%.0f m", 0);
    bool show_box = shadows->show_box != 0;
    if (search_row(app, "Show light box", "shadow frustum debug")) {
        if (igCheckbox("Show light box", &show_box))
            shadows->show_box = show_box;
    }
    igEndDisabled();
}

void ui_inspector_tab(App* app)
{
    SceneView* view = app_view(app);
    search_panel_begin(app, SEARCH_INSPECTOR);
    if (!view->selected.index) {
        if (search_plain(app))
            igTextDisabled("Select a node in the Scene tab.");
        search_panel_end(app);
        return;
    }
    NvNode* node = nv_scene_get(view->scene, view->selected);
    if (search_row(app, "Name", "node rename"))
        igInputText("Name", node->name, sizeof(node->name), 0, NULL, NULL);
    if (view->selected.index != view->scene->active_camera.index) {
        // The gizmo in the viewport; W, E and R switch the operation there too.
        if (search_group(app, "Gizmo", "move rotate scale local snap transform")) {
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
    }
    if (search_row(app, "Position", "transform move translate"))
        igDragFloat3("Position", &node->position.x, 0.02f, 0.0f, 0.0f, "%.2f", 0);
    // Rotation as pitch (X), yaw (Y) and roll (Z) in degrees; the quaternion is only rewritten when
    // edited, so looking at a node never changes it.
    if (search_row(app, "Rotation", "transform euler pitch yaw roll")) {
        NvVec3 euler = nv_quat_to_euler(node->rotation);
        f32 degrees[3] = {euler.x * 180.0f / NV_PI, euler.y * 180.0f / NV_PI, euler.z * 180.0f / NV_PI};
        // Tiny negatives (float noise, -0) would show as "-0.0".
        for (u32 i = 0; i < 3; ++i) {
            if (fabsf(degrees[i]) < 0.05f)
                degrees[i] = 0.0f;
        }
        if (igDragFloat3("Rotation", degrees, 0.5f, 0.0f, 0.0f, "%.1f", 0))
            node->rotation = nv_quat_from_euler(nv_vec3(degrees[0] * NV_PI / 180.0f, degrees[1] * NV_PI / 180.0f, degrees[2] * NV_PI / 180.0f));
    }
    if (search_row(app, "Scale", "transform size"))
        igDragFloat3("Scale", &node->scale.x, 0.01f, 0.01f, 100.0f, "%.2f", 0);

    if (node->mesh.index) {
        search_section(app, "Mesh");
        if (search_row(app, "Color", "material base color")) {
            f32 color[4];
            for (u32 i = 0; i < 4; ++i)
                color[i] = app->renderer.materials[node->material.index].desc.base_color[i];
            if (igColorEdit4("Color", color, ImGuiColorEditFlags_Float))
                nv_renderer_set_material_color(&app->renderer, node->material, color);
        }
        NvTextureId texture = app->renderer.materials[node->material.index].desc.base_color_texture;
        if (texture.index && search_group(app, "Texture", "material base color image"))
            textures_inspector_thumbnail(app, texture);
    }
    if (node->camera.projection) {
        search_section(app, "Camera");
        if (search_row(app, "Field of view", "fov camera lens"))
            igSliderAngle("Field of view", &node->camera.fov_y, 20.0f, 100.0f, "%.0f deg", 0);
    }
    if (node->light.type) {
        search_section(app, "Light");
        if (search_row(app, "Color##light", "light"))
            igColorEdit3("Color##light", &node->light.color.x, ImGuiColorEditFlags_Float);
        if (search_row(app, "Intensity", "light brightness"))
            igSliderFloat("Intensity", &node->light.intensity, 0.0f, 3.0f, "%.2f", 0);
    }
    if (node->attach.animator.index)
        attach_section(app, node);
    // The full animator controls drive the showcase character; others show what they play.
    NvAnimatorId animator = app_node_animator(view->scene, view->selected);
    if (animator.index == app->animator.index) {
        animator_section(app, nv_anim_get(animator));
    } else if (animator.index) {
        search_section(app, "Animator");
        NvAnimLayer* layer = &nv_anim_get(animator)->layers[0];
        if (layer->clip.index && search_group(app, "Playing", "animation clip"))
            igText("%s  %.2f / %.2f s", nv_anim_clip_name(layer->clip), layer->time, nv_anim_clip_duration(layer->clip));
    }
    search_panel_end(app);
}

//
// View tab
//

void ui_view_tab(App* app)
{
    ImGuiIO* io = igGetIO_Nil();
    SceneView* view = app_view(app);
    search_panel_begin(app, SEARCH_VIEW);
    local_persist const char* scenes[SCENE_COUNT] = {"Showcase", "Stress"};
    int shown = (int)app->shown;
    if (search_row(app, "Scene", "showcase stress switch")) {
        if (igCombo_Str_arr("Scene", &shown, scenes, SCENE_COUNT, -1))
            app_show_scene(app, (SceneKind)shown);
    }
    if (search_plain(app)) {
        igText("%.0f FPS (%.2f ms)", io->Framerate, 1000.0f / io->Framerate);
        igTextDisabled("%s build, commit %s", NV_BUILD_NAME, NV_GIT_COMMIT);
        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){0.6f, 0.6f, 0.6f, 1.0f});
        igTextWrapped("%s", NV_GIT_SUBJECT);
        igPopStyleColor(1);
    }
    if (search_row(app, "Camera yaw", "orbit"))
        igSliderAngle("Camera yaw", &view->camera_yaw, -180.0f, 180.0f, "%.0f deg", 0);
    if (search_row(app, "Camera pitch", "orbit"))
        igSliderAngle("Camera pitch", &view->camera_pitch, -10.0f, 80.0f, "%.0f deg", 0);
    if (search_row(app, "Distance", "camera zoom orbit"))
        igSliderFloat("Distance", &view->camera_distance, 1.0f, 100.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
    if (search_row(app, "Camera follows selection", "orbit follow"))
        igCheckbox("Camera follows selection", &view->follow_selection);
    if (app->shown == SCENE_SHOWCASE) {
        if (search_row(app, "Show bones", "skeleton debug lines"))
            igCheckbox("Show bones", &app->show_bones);
        if (search_row(app, "Planet orbit", "speed moon spin"))
            igSliderFloat("Planet orbit", &app->orbit_speed, -3.0f, 3.0f, "%.2f rad/s", 0);
    }
    msaa_ui(app);
    resolution_ui(app);
    shadow_ui(app);
    save_ui(app);
    search_panel_end(app);
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
    // A popup (the command palette, the help window) is above everything but this foreground
    // drawing, so the label waits.
    if (igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        app->badge_box[0] = app->badge_box[1] = app->badge_box[2] = app->badge_box[3] = 0.0f;
        return;
    }
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

// The scene's resolution and where its image goes in the viewport (docs/specs/resolution.md).
internal NvSceneOutput scene_output(const Resolution* resolution, NvRect viewport)
{
    NvSceneOutput out = {.image = viewport, .pixel_width = 1.0f, .pixel_height = 1.0f};
    if (!viewport.width || !viewport.height) {
        out.width = out.height = 1;
        return out;
    }
    if (resolution->mode == RESOLUTION_FIXED) {
        u32 width = clamp_u32(resolution->fixed_width, RESOLUTION_MIN, RESOLUTION_MAX);
        u32 height = clamp_u32(resolution->fixed_height, RESOLUTION_MIN, RESOLUTION_MAX);
        out.width = width;
        out.height = height;
        f32 sx = (f32)viewport.width / (f32)width, sy = (f32)viewport.height / (f32)height;
        u32 image_width, image_height;
        u32 fit_x = viewport.width / width, fit_y = viewport.height / height;
        u32 multiple = fit_x < fit_y ? fit_x : fit_y;
        if (resolution->fixed_fit == FIT_STRETCH) {
            // The whole viewport, whatever the aspect ratio.
            out.pixel_width = sx;
            out.pixel_height = sy;
            image_width = viewport.width;
            image_height = viewport.height;
        } else if (resolution->fixed_fit == FIT_WHOLE && multiple >= 1) {
            // The largest whole multiple that fits, so every block is the same size.
            out.pixel_width = out.pixel_height = (f32)multiple;
            image_width = width * multiple;
            image_height = height * multiple;
        } else {
            // The largest scale that fits keeping the aspect ratio: what FIT_VIEWPORT always does, and
            // FIT_WHOLE does when even the size itself does not fit.
            out.pixel_width = out.pixel_height = sx < sy ? sx : sy;
            image_width = clamp_u32((u32)((f32)width * out.pixel_width + 0.5f), 1, viewport.width);
            image_height = clamp_u32((u32)((f32)height * out.pixel_width + 0.5f), 1, viewport.height);
        }
        out.image = (NvRect){viewport.x + (viewport.width - image_width) / 2, viewport.y + (viewport.height - image_height) / 2,
                             image_width, image_height};
        return out;
    }
    u32 divisor = clamp_u32(resolution->divisor, 1, 4);
    out.width = (viewport.width + divisor - 1) / divisor;
    out.height = (viewport.height + divisor - 1) / divisor;
    out.pixel_width = out.pixel_height = (f32)divisor;
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
    search_frame(app);
    ui_build_label(app);
    if (app->ui_mode == UI_PHONE)
        phone_build_ui(app);
    else
        desktop_build_ui(app);
}
