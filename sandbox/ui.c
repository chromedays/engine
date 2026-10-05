#include "sandbox.h"

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
internal void tree_marks(Sandbox* sandbox, SceneView* view)
{
    Search* s = &sandbox->search;
    NvScene* scene = view->scene;
    // Only a change detector, so the words are hashed by their bytes.
    u32 shown = (u32)sandbox->shown;
    u32 key = nv_fnv1a(NV_FNV1A_SEED, s->queries[SEARCH_SCENE], strlen(s->queries[SEARCH_SCENE]));
    key = nv_fnv1a(key, &scene->node_count, sizeof(scene->node_count));
    key = nv_fnv1a(key, &shown, sizeof(shown));
    s32 frame = igGetFrameCount();
    if (key == s->tree_key && frame - s->tree_frame < 30)
        return;
    s->tree_key = key;
    s->tree_frame = frame;
    s->tree_matches = 0;
    memset(s->tree, 0, scene->node_count + 1);
    for (u32 i = 1; i <= scene->node_count; ++i) {
        if (!(scene->nodes[i].gen & 1) || !search_match(sandbox, scene->nodes[i].name))
            continue;
        s->tree[i] |= TREE_SELF;
        ++s->tree_matches;
        for (u32 p = i; p && !(s->tree[p] & TREE_PATH); p = scene->nodes[p].parent)
            s->tree[p] |= TREE_PATH;
    }
}

// Notes a row in the order the tree draws them; a Shift+click range runs over last frame's rows.
internal void tree_row(Sandbox* sandbox, u32 index)
{
#if !defined(NDEBUG)
    if (sandbox->tree_row_count[0] < NV_ARRAY_COUNT(sandbox->tree_row_rects)) {
        ImVec2_c min = igGetItemRectMin(), max = igGetItemRectMax();
        f32* r = sandbox->tree_row_rects[sandbox->tree_row_count[0]];
        r[0] = min.x, r[1] = min.y, r[2] = max.x, r[3] = max.y;
    }
#endif
    if (sandbox->tree_row_count[0] < TREE_ROWS_MAX)
        sandbox->tree_rows[0][sandbox->tree_row_count[0]++] = index;
}

internal s32 find_row(Sandbox* sandbox, u32 index)
{
    for (u32 i = 0; i < sandbox->tree_row_count[1]; ++i) {
        if (sandbox->tree_rows[1][i] == index)
            return (s32)i;
    }
    return -1;
}

// A click on a row (docs/specs/selection.md): only that node; with Ctrl, or the phone's Multi
// toggle, added or removed; with Shift, the rows from the range's anchor to it.
internal void tree_select(Sandbox* sandbox, SceneView* view, u32 index)
{
    NvScene* scene = view->scene;
    NvNodeId id = {index, scene->nodes[index].gen};
    ImGuiIO* io = igGetIO_Nil();
    sandbox->open_inspector = 1;
    if (sandbox->multi_select || (io->KeyCtrl && !io->KeyShift)) {
        selection_toggle(sandbox, view, id);
        return;
    }
    s32 from = io->KeyShift ? find_row(sandbox, view->range_anchor.index) : -1;
    s32 to = find_row(sandbox, index);
    if (from < 0 || to < 0) {
        selection_set(view, id);
        return;
    }
    // From the clicked row towards the anchor, so a range longer than the selection holds keeps the
    // clicked end; the clicked node ends up the primary.
    NvNodeId anchor = view->range_anchor;
    selection_set(view, (NvNodeId){0});
    s32 step = from < to ? -1 : 1;
    for (s32 i = to;; i += step) {
        u32 row = sandbox->tree_rows[1][i];
        selection_add(sandbox, view, (NvNodeId){row, scene->nodes[row].gen});
        if (i == from)
            break;
    }
    selection_add(sandbox, view, id);
    view->range_anchor = anchor;
}

// Below the depth the tree draws: the matches under `index`, flat, so a match at the end of the
// 1000-deep chain can still be picked.
internal void tree_deep_matches(Sandbox* sandbox, SceneView* view, u32 index)
{
    Search* s = &sandbox->search;
    NvScene* scene = view->scene;
    u32 hidden = 0;
    for (u32 i = index; i; i = scene->nodes[i].first_child)
        ++hidden;
    igTextDisabled(T("... %u more levels"), hidden);
    igIndent(igGetTreeNodeToLabelSpacing());
    u32 listed = 0;
    u32 n = index;
    for (;;) {
        if ((s->tree[n] & TREE_SELF) && listed < TREE_DEEP_MAX) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selection_has(view, (NvNodeId){n, scene->nodes[n].gen}))
                flags |= ImGuiTreeNodeFlags_Selected;
            igTreeNodeEx_Ptr((void*)(umm)n, flags, "%s", scene->nodes[n].name);
            tree_row(sandbox, n);
            if (igIsItemClicked(ImGuiMouseButton_Left))
                tree_select(sandbox, view, n);
            ImVec2_c min = igGetItemRectMin();
            search_mark(sandbox, scene->nodes[n].name, (ImVec2_c){min.x + igGetTreeNodeToLabelSpacing(), min.y}, igGetFrameHeight());
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

internal void node_tree(Sandbox* sandbox, SceneView* view, u32 index, u32 depth)
{
    Search* s = &sandbox->search;
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
            tree_deep_matches(sandbox, view, index);
        } else {
            u32 hidden = 0;
            for (u32 i = index; i; i = scene->nodes[i].first_child)
                ++hidden;
            igTextDisabled(T("... %u more levels"), hidden);
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
    if (selection_has(view, id))
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
    tree_row(sandbox, index);
    if (igIsItemClicked(ImGuiMouseButton_Left) && !igIsItemToggledOpen())
        tree_select(sandbox, view, index);
    if (filtering && self) {
        ImVec2_c min = igGetItemRectMin();
        search_mark(sandbox, node->name, (ImVec2_c){min.x + igGetTreeNodeToLabelSpacing(), min.y}, igGetFrameHeight());
    }
    if (open && children) {
        for (u32 child = node->first_child; child; child = scene->nodes[child].next_sibling)
            node_tree(sandbox, view, child, depth + 1);
        igTreePop();
    }
}

void ui_scene_tab(Sandbox* sandbox)
{
    Search* s = &sandbox->search;
    SceneView* view = sandbox_view(sandbox);
    // Last frame's rows are the ones a click this frame can range over.
    memcpy(sandbox->tree_rows[1], sandbox->tree_rows[0], sandbox->tree_row_count[0] * sizeof(u32));
    sandbox->tree_row_count[1] = sandbox->tree_row_count[0];
    sandbox->tree_row_count[0] = 0;
    search_panel_begin(sandbox, SEARCH_SCENE);
    s->tree_filtering = search_active(sandbox);
    s->tree_drawn = 0;
    if (s->tree_filtering)
        tree_marks(sandbox, view);
    for (u32 root = view->scene->first_root; root; root = view->scene->nodes[root].next_sibling)
        node_tree(sandbox, view, root, 0);
    if (s->tree_filtering && s->tree_matches > s->tree_drawn)
        igTextDisabled(T("and %u more"), s->tree_matches - s->tree_drawn);
    if (s->tree_filtering && !s->tree_matches)
        s->rows_now[SEARCH_SCENE] = 0;
    s->tree_filtering = 0;
    search_panel_end(sandbox);
}

//
// Inspector tab
//

internal void animator_section(Sandbox* sandbox, NvAnimator* animator)
{
    search_section(sandbox, "Animator");

    // Clips of this skeleton, without the root-motion copies (sandbox_play picks those).
    NvClipId current = animator->layers[0].clip.index ? sandbox_regular_clip(sandbox, animator->layers[0].clip) : (NvClipId){0};
    u32 clip_count = nv_anim_clip_count();
    for (u32 c = 1; c <= clip_count; ++c) {
        NvClipId clip = {c};
        if (nv_anim_clip_skeleton(clip).index != animator->skeleton.index || nv_anim_clip_has_root_motion(clip))
            continue;
        if (search_group(sandbox, nv_anim_clip_name(clip), "clip animation play")) {
            if (igSelectable_Bool(nv_anim_clip_name(clip), clip.index == current.index, 0, (ImVec2_c){0, 0}))
                sandbox_play(sandbox, clip);
        }
    }
    igBeginDisabled(!sandbox->playing); // a jump is something that happens while playing
    if (search_group(sandbox, "Jump", "play")) {
        if (igButton(TL("Jump"), (ImVec2_c){-1.0f, 0.0f}))
            sandbox_jump(sandbox);
    }
    igEndDisabled();

    if (search_row(sandbox, "Speed", "playback"))
        igSliderFloat(TL("Speed"), &animator->layers[0].speed, 0.0f, 2.0f, "%.2fx", 0);
    if (search_row(sandbox, "Fade", "crossfade blend"))
        igSliderFloat(TL("Fade"), &sandbox->fade_seconds, 0.0f, 1.0f, "%.2f s", 0);
    if (search_row(sandbox, "Blend", "clip")) {
        const char* names[SANDBOX_MAX_CLIPS];
        for (u32 i = 0; i < sandbox->clip_count; ++i)
            names[i] = nv_anim_clip_name(sandbox->clips[i]);
        igCombo_Str_arr(TL("Blend"), &sandbox->blend_clip, names, (int)sandbox->clip_count, -1);
    }
    if (search_row(sandbox, "Weight", "blend"))
        igSliderFloat(TL("Weight"), &sandbox->blend_weight, 0.0f, 1.0f, "%.2f", 0);

    // What the layers play now is no setting: shown only without a search.
    if (search_plain(sandbox)) {
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

    if (search_row(sandbox, "Root motion", "walk")) {
        if (igCheckbox(TL("Root motion"), &sandbox->root_motion) && sandbox->jump == JUMP_NONE)
            sandbox_play(sandbox, sandbox_regular_clip(sandbox, animator->layers[0].clip));
    }
    igBeginDisabled(!sandbox->root_motion);
    if (search_row(sandbox, "Turn", "root motion"))
        igSliderFloat(TL("Turn"), &sandbox->turn_rate, -1.5f, 1.5f, "%.2f rad/s", 0);
    if (search_group(sandbox, "Back to center", "root motion")) {
        if (igButton(TL("Back to center"), (ImVec2_c){-1.0f, 0.0f}))
            sandbox_back_to_center(sandbox);
    }
    igEndDisabled();
    if (search_row(sandbox, "Look at target", "head ik aim"))
        igCheckbox(TL("Look at target"), &sandbox->look_at);
}

internal void attach_section(Sandbox* sandbox, NvNode* node)
{
    search_section(sandbox, "Attach");
    NvAnimator* animator = nv_anim_get(node->attach.animator);
    const NvJointDesc* joints = nv_anim_joints(animator->skeleton);
    if (search_row(sandbox, "Joint", "bone attach")) {
        if (igBeginCombo(TL("Joint"), joints[node->attach.joint].name, 0)) {
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
    if (sandbox->shown == SCENE_SHOWCASE && node == nv_scene_get(sandbox->scene, sandbox->sword)) {
        if (search_row(sandbox, "Visible", "sword"))
            igCheckbox(TL("Visible"), &sandbox->show_sword);
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
internal void msaa_ui(Sandbox* sandbox)
{
    search_section(sandbox, "Anti-aliasing");
    const char* names[] = {T("Off"), T("MSAA 4x")};
    s32 index = sandbox->renderer.msaa == 4 ? 1 : 0;
    if (search_row(sandbox, "Edges", "msaa multisample samples smooth")) {
        if (igCombo_Str_arr(TL("Edges"), &index, names, 2, -1))
            sandbox->renderer.msaa = index ? 4 : 1;
    }
}

// The View tab's Post-processing section (docs/specs/vfx.md): tone mapping and exposure, then bloom.
internal void post_ui(Sandbox* sandbox)
{
    NvPostSettings* post = &sandbox->renderer.post;
    search_section(sandbox, "Post-processing");
    const char* tones[NV_TONE_COUNT] = {T("Clamp"), T("PBR Neutral"), T("ACES")};
    s32 tone = (s32)post->tone;
    if (search_row(sandbox, "Tone mapping", "hdr clamp pbr neutral aces filmic")) {
        if (igCombo_Str_arr(TL("Tone mapping"), &tone, tones, NV_TONE_COUNT, -1))
            post->tone = (NvToneMap)tone;
    }
    if (search_row(sandbox, "Exposure", "brightness hdr"))
        igSliderFloat(TL("Exposure"), &post->exposure, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    bool bloom = post->bloom;
    if (search_row(sandbox, "Bloom", "glow hdr bright")) {
        if (igCheckbox(TL("Bloom"), &bloom))
            post->bloom = bloom;
    }
    if (search_row(sandbox, "Bloom intensity", "glow hdr bright strength"))
        igSliderFloat(TL("Bloom intensity"), &post->bloom_intensity, 0.0f, 0.2f, "%.3f", 0);
}

// The View tab's Resolution section (docs/specs/resolution.md).
internal void resolution_ui(Sandbox* sandbox)
{
    NvResolution* resolution = &sandbox->resolution;
    search_section(sandbox, "Resolution");
    const char* modes[] = {T("Scale"), T("Fixed size")};
    s32 mode = (s32)resolution->mode;
    if (search_row(sandbox, "Mode", "scale fixed size")) {
        if (igCombo_Str_arr(TL("Mode"), &mode, modes, 2, -1))
            resolution->mode = mode == 1 ? NV_RESOLUTION_FIXED : NV_RESOLUTION_SCALE;
    }

    if (resolution->mode == NV_RESOLUTION_SCALE) {
        const char* divisors[] = {T("1/1 (full)"), "1/2", "1/3", "1/4"};
        s32 index = (s32)nv_clamp_u32(resolution->divisor, 1, 4) - 1;
        if (search_row(sandbox, "Scale", "divisor lower resolution pixel")) {
            if (igCombo_Str_arr(TL("Scale"), &index, divisors, 4, -1))
                resolution->divisor = (u32)index + 1;
        }
    } else {
        // Presets, and Custom for any other size (also while it is being typed).
        local_persist const u32 sizes[][2] = {{640, 360}, {1280, 720}, {1920, 1080}, {360, 640}, {720, 1280}};
        const char* names[] = {"640 x 360", "1280 x 720", "1920 x 1080", T("360 x 640 (portrait)"), T("720 x 1280 (portrait)"), T("Custom")};
        local_persist b32 want_custom;
        s32 index = (s32)NV_ARRAY_COUNT(sizes); // Custom
        for (u32 i = 0; i < NV_ARRAY_COUNT(sizes); ++i) {
            if (sizes[i][0] == resolution->fixed_width && sizes[i][1] == resolution->fixed_height)
                index = (s32)i;
        }
        if (want_custom)
            index = (s32)NV_ARRAY_COUNT(sizes);
        if (search_row(sandbox, "Size", "resolution fixed preset 720p 1080p portrait")) {
            if (igCombo_Str_arr(TL("Size"), &index, names, (int)NV_ARRAY_COUNT(names), -1)) {
                want_custom = index == (s32)NV_ARRAY_COUNT(sizes);
                if (!want_custom) {
                    resolution->fixed_width = sizes[index][0];
                    resolution->fixed_height = sizes[index][1];
                }
            }
        }
        const char* fits[] = {T("Whole multiples"), T("Fit to viewport"), T("Stretch to viewport")};
        s32 fit = (s32)resolution->fixed_fit;
        if (search_row(sandbox, "Fit", "whole multiples stretch letterbox bars")) {
            if (igCombo_Str_arr(TL("Fit"), &fit, fits, 3, -1))
                resolution->fixed_fit = (NvFixedFit)fit;
        }
        if (index == (s32)NV_ARRAY_COUNT(sizes)) {
            int width = (int)resolution->fixed_width, height = (int)resolution->fixed_height;
            if (search_row(sandbox, "Width", "custom size")) {
                if (igInputInt(TL("Width"), &width, 16, 128, 0))
                    resolution->fixed_width = nv_clamp_u32((u32)(width < 0 ? 0 : width), NV_RESOLUTION_MIN, NV_RESOLUTION_MAX);
            }
            if (search_row(sandbox, "Height", "custom size")) {
                if (igInputInt(TL("Height"), &height, 16, 128, 0))
                    resolution->fixed_height = nv_clamp_u32((u32)(height < 0 ? 0 : height), NV_RESOLUTION_MIN, NV_RESOLUTION_MAX);
            }
        }
    }
    // What the settings come to is no setting: shown only without a search.
    if (!search_plain(sandbox))
        return;

    // What that comes to: the scene's pixels, and how big each one shows.
    const NvSceneOutput* scene = &sandbox->layout.scene;
    f32 ratio = nv_window_pixel_ratio(&sandbox->window);
    f32 pw = scene->pixel_width, ph = scene->pixel_height;
    b32 whole = pw == ph && pw >= 1.0f && pw == (f32)(u32)pw;
    if (whole) {
        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){0.6f, 0.6f, 0.6f, 1.0f});
        igTextWrapped(T("Renders %u x %u; a pixel shows as %.0f x %.0f screen pixels (%.1f per CSS pixel)"), scene->width, scene->height,
                      (f64)pw, (f64)ph, (f64)(pw / ratio));
        igPopStyleColor(1);
    } else if (pw >= 1.0f && ph >= 1.0f) {
        // Fitted or stretched: pixels are 1 or more screen pixels wide, unevenly.
        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){0.6f, 0.6f, 0.6f, 1.0f});
        if (pw == ph)
            igTextWrapped(T("Renders %u x %u, fitted to the viewport at %.2fx: pixels are uneven blocks of whole screen pixels"),
                          scene->width, scene->height, (f64)pw);
        else
            igTextWrapped(T("Renders %u x %u, stretched to the viewport: %.2f x %.2f screen pixels per pixel, in uneven blocks"),
                          scene->width, scene->height, (f64)pw, (f64)ph);
        igPopStyleColor(1);
    } else {
        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){1.0f, 0.75f, 0.35f, 1.0f});
        igTextWrapped(T("Renders %u x %u, larger than the viewport: shown at %.2fx x %.2fx, some pixels dropped"), scene->width,
                      scene->height, (f64)pw, (f64)ph);
        igPopStyleColor(1);
    }
}

// The View tab's Shadows section (docs/specs/shadows.md).
internal void shadow_ui(Sandbox* sandbox)
{
    NvShadowSettings* shadows = &sandbox->renderer.shadows;
    search_section(sandbox, "Shadows");
    local_persist const u32 sizes[] = {0, 512, 1024, 2048};
    const char* size_names[] = {T("Off"), "512", "1024", "2048"};
    s32 size_index = 0;
    for (u32 i = 0; i < NV_ARRAY_COUNT(sizes); ++i) {
        if (shadows->size == sizes[i])
            size_index = (s32)i;
    }
    if (search_row(sandbox, "Map size", "shadow resolution off")) {
        if (igCombo_Str_arr(TL("Map size"), &size_index, size_names, (int)NV_ARRAY_COUNT(sizes), -1))
            shadows->size = sizes[size_index];
    }
    igBeginDisabled(!shadows->size);
    const char* format_names[] = {T("32-bit float"), T("16-bit")};
    s32 format = (s32)shadows->format;
    if (search_row(sandbox, "Format", "shadow depth")) {
        if (igCombo_Str_arr(TL("Format"), &format, format_names, 2, -1))
            shadows->format = (NvShadowFormat)format;
    }
    const char* filter_names[] = {T("Low"), T("High")};
    s32 filter = (s32)shadows->filter;
    if (search_row(sandbox, "Filter", "shadow soft pcf")) {
        if (igCombo_Str_arr(TL("Filter"), &filter, filter_names, 2, -1))
            shadows->filter = (NvShadowFilter)filter;
    }
    if (search_row(sandbox, "Distance##shadow", "shadow fade"))
        igSliderFloat(TL("Distance##shadow"), &shadows->distance, 5.0f, 100.0f, "%.0f m", 0);
    bool show_box = shadows->show_box != 0;
    if (search_row(sandbox, "Show light box", "shadow frustum debug")) {
        if (igCheckbox(TL("Show light box"), &show_box))
            shadows->show_box = show_box;
    }
    igEndDisabled();
}

void ui_inspector_tab(Sandbox* sandbox)
{
    SceneView* view = sandbox_view(sandbox);
    search_panel_begin(sandbox, SEARCH_INSPECTOR);
    if (!view->selected.index) {
        if (search_plain(sandbox))
            igTextDisabled(T("Select a node in the Scene tab."));
        search_panel_end(sandbox);
        return;
    }
    // Several nodes selected: the Inspector edits the primary (docs/specs/selection.md).
    u32 selected = selection_count(view);
    if (selected > 1 && search_plain(sandbox)) {
        igTextDisabled(T("%u selected"), selected);
        igSameLine(0.0f, -1.0f);
        if (igSmallButton(TL("Keep one")))
            selection_keep_primary(view);
        igSetItemTooltip("%s", T("Keep only the node shown here selected"));
    }
    NvNode* node = nv_scene_get(view->scene, view->selected);
    if (search_row(sandbox, "Name", "node rename"))
        igInputText(TL("Name"), node->name, sizeof(node->name), 0, NULL, NULL);
    if (view->selected.index != view->scene->active_camera.index) {
        // The gizmo in the viewport; W, E and R switch the operation there too.
        if (search_group(sandbox, "Gizmo", "move rotate scale local snap transform")) {
            s32* operation = (s32*)&sandbox->gizmo_operation;
            igRadioButton_IntPtr(TL("Move"), operation, GIZMO_MOVE);
            same_line_if_fits(T("Rotate"));
            igRadioButton_IntPtr(TL("Rotate"), operation, GIZMO_ROTATE);
            same_line_if_fits(T("Scale"));
            igRadioButton_IntPtr(TL("Scale"), operation, GIZMO_SCALE);
            same_line_if_fits(T("Local"));
            igBeginDisabled(sandbox->gizmo_operation == GIZMO_SCALE);
            igCheckbox(TL("Local"), &sandbox->gizmo_local);
            igEndDisabled();
            same_line_if_fits(T("Snap"));
            igCheckbox(TL("Snap"), &sandbox->gizmo_snap);
        }
    }
    if (search_row(sandbox, "Position", "transform move translate"))
        igDragFloat3(TL("Position"), &node->position.x, 0.02f, 0.0f, 0.0f, "%.2f", 0);
    // Rotation as pitch (X), yaw (Y) and roll (Z) in degrees; the quaternion is only rewritten when
    // edited, so looking at a node never changes it.
    if (search_row(sandbox, "Rotation", "transform euler pitch yaw roll")) {
        NvVec3 euler = nv_quat_to_euler(node->rotation);
        f32 degrees[3] = {euler.x * 180.0f / NV_PI, euler.y * 180.0f / NV_PI, euler.z * 180.0f / NV_PI};
        // Tiny negatives (float noise, -0) would show as "-0.0".
        for (u32 i = 0; i < 3; ++i) {
            if (fabsf(degrees[i]) < 0.05f)
                degrees[i] = 0.0f;
        }
        if (igDragFloat3(TL("Rotation"), degrees, 0.5f, 0.0f, 0.0f, "%.1f", 0))
            node->rotation = nv_quat_from_euler(nv_vec3(degrees[0] * NV_PI / 180.0f, degrees[1] * NV_PI / 180.0f, degrees[2] * NV_PI / 180.0f));
    }
    if (search_row(sandbox, "Scale", "transform size"))
        igDragFloat3(TL("Scale"), &node->scale.x, 0.01f, 0.01f, 100.0f, "%.2f", 0);

    if (node->mesh.index) {
        search_section(sandbox, "Mesh");
        if (search_row(sandbox, "Color", "material base color")) {
            f32 color[4];
            for (u32 i = 0; i < 4; ++i)
                color[i] = sandbox->renderer.materials[node->material.index].desc.base_color[i];
            if (igColorEdit4(TL("Color"), color, ImGuiColorEditFlags_Float))
                nv_renderer_set_material_color(&sandbox->renderer, node->material, color);
        }
        NvTextureId texture = sandbox->renderer.materials[node->material.index].desc.base_color_texture;
        if (texture.index && search_group(sandbox, "Texture", "material base color image"))
            textures_inspector_thumbnail(sandbox, texture);
    }
    if (node->camera.projection) {
        search_section(sandbox, "Camera");
        if (search_row(sandbox, "Field of view", "fov camera lens"))
            igSliderAngle(TL("Field of view"), &node->camera.fov_y, 20.0f, 100.0f, "%.0f deg", 0);
    }
    if (node->light.type) {
        search_section(sandbox, "Light");
        if (search_row(sandbox, "Color##light", "light"))
            igColorEdit3(TL("Color##light"), &node->light.color.x, ImGuiColorEditFlags_Float);
        if (search_row(sandbox, "Intensity", "light brightness"))
            igSliderFloat(TL("Intensity"), &node->light.intensity, 0.0f, 3.0f, "%.2f", 0);
    }
    if (node->attach.animator.index)
        attach_section(sandbox, node);
    // The full animator controls drive the showcase character; others show what they play.
    NvAnimatorId animator = sandbox_node_animator(view->scene, view->selected);
    if (animator.index == sandbox->animator.index) {
        animator_section(sandbox, nv_anim_get(animator));
    } else if (animator.index) {
        search_section(sandbox, "Animator");
        NvAnimLayer* layer = &nv_anim_get(animator)->layers[0];
        if (layer->clip.index && search_group(sandbox, "Playing", "animation clip"))
            igText("%s  %.2f / %.2f s", nv_anim_clip_name(layer->clip), layer->time, nv_anim_clip_duration(layer->clip));
    }
    search_panel_end(sandbox);
}

//
// View tab
//

void ui_view_tab(Sandbox* sandbox)
{
    ImGuiIO* io = igGetIO_Nil();
    SceneView* view = sandbox_view(sandbox);
    search_panel_begin(sandbox, SEARCH_VIEW);
    const char* scenes[SCENE_COUNT] = {T("Showcase"), T("Stress")};
    int shown = (int)sandbox->shown;
    if (search_row(sandbox, "Scene", "showcase stress switch")) {
        if (igCombo_Str_arr(TL("Scene"), &shown, scenes, SCENE_COUNT, -1))
            sandbox_show_scene(sandbox, (SceneKind)shown);
    }
    if (search_row(sandbox, "Language", "english korean 한국어 언어")) {
        const char* languages[NV_LANGUAGE_COUNT] = {"English", "한국어"};
        s32 language = (s32)nv_strings_language();
        if (igCombo_Str_arr(TL("Language"), &language, languages, NV_LANGUAGE_COUNT, -1))
            nv_strings_set_language((NvLanguage)language);
    }
    if (search_plain(sandbox)) {
        igText(T("%.0f FPS (%.2f ms)"), io->Framerate, 1000.0f / io->Framerate);
        igTextDisabled("%s build, commit %s", NV_BUILD_NAME, NV_GIT_COMMIT);
        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){0.6f, 0.6f, 0.6f, 1.0f});
        igTextWrapped(T("Commit: %s"), NV_GIT_SUBJECT);
        igPopStyleColor(1);
    }
    if (search_row(sandbox, "Camera yaw", "orbit"))
        igSliderAngle(TL("Camera yaw"), &view->orbit.yaw, -180.0f, 180.0f, "%.0f deg", 0);
    if (search_row(sandbox, "Camera pitch", "orbit"))
        igSliderAngle(TL("Camera pitch"), &view->orbit.pitch, -10.0f, 80.0f, "%.0f deg", 0);
    if (search_row(sandbox, "Distance", "camera zoom orbit"))
        igSliderFloat(TL("Distance"), &view->orbit.distance, 1.0f, 100.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
    if (search_row(sandbox, "Camera follows selection", "orbit follow"))
        igCheckbox(TL("Camera follows selection"), &view->follow_selection);
    if (sandbox->shown == SCENE_SHOWCASE) {
        if (search_row(sandbox, "Show bones", "skeleton debug lines"))
            igCheckbox(TL("Show bones"), &sandbox->show_bones);
        if (search_row(sandbox, "Planet orbit", "speed moon spin"))
            igSliderFloat(TL("Planet orbit"), &sandbox->orbit_speed, -3.0f, 3.0f, "%.2f rad/s", 0);
    }
    msaa_ui(sandbox);
    post_ui(sandbox);
    effects_ui(sandbox);
    resolution_ui(sandbox);
    shadow_ui(sandbox);
    save_ui(sandbox);
    search_panel_end(sandbox);
}

// The number of warnings and errors not yet seen, for the Console tab's label and the badge.
internal void format_unseen(u32 unseen, char* out, umm capacity)
{
    if (unseen > 99)
        snprintf(out, capacity, "99+");
    else
        snprintf(out, capacity, "%u", unseen);
}

// The build type in the viewport's top-left corner, so a Debug page is never mistaken for Release.
// Warnings and errors that arrived while the Console tab was not shown add a badge, a dot and a
// count; a tap on the label then opens the Console tab (pick in main.c, through `badge_box`).
void ui_draw_build_label(Sandbox* sandbox)
{
    // A popup (the command palette, the help window) is above everything but this foreground
    // drawing, so the label waits.
    if (igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        sandbox->badge_box[0] = sandbox->badge_box[1] = sandbox->badge_box[2] = sandbox->badge_box[3] = 0.0f;
        return;
    }
    char text[160];
    snprintf(text, sizeof(text), "%s build %s%s%s%s", NV_BUILD_NAME, NV_GIT_COMMIT, sandbox->download_text[0] ? " \xC2\xB7 " : "",
             sandbox->download_text, sandbox->playing && sandbox->shown == SCENE_SHOWCASE ? " \xC2\xB7 Playing" : "");
    NvLogLevel worst;
    u32 unseen = console_unseen(sandbox, &worst);
    char count[8];
    format_unseen(unseen, count, sizeof(count));

    // The badge goes after the text, inside the label's backdrop (the engine draws the label and the commit's subject line).
    f32 radius = igGetFontSize() * 0.3f;
    f32 badge_width = 0.0f;
    if (unseen)
        badge_width = 8.0f + radius * 2.0f + 4.0f + igCalcTextSize(count, NULL, false, -1.0f).x;
    NvBuildLabel label = nv_imgui_draw_build_label(&sandbox->imgui, sandbox->layout.viewport, text, badge_width, "Commit: " NV_GIT_SUBJECT);
    f32* box = sandbox->badge_box;
    memcpy(box, label.box, sizeof(label.box));
    if (unseen) {
        ImDrawList* draw = igGetForegroundDrawList_ViewportPtr(NULL);
        ImU32 color = console_level_color(worst);
        f32 x = label.text_end + 8.0f;
        ImDrawList_AddCircleFilled(draw, (ImVec2_c){x + radius, label.text_top + label.text_height * 0.5f}, radius, color, 12);
        ImDrawList_AddText_Vec2(draw, (ImVec2_c){x + radius * 2.0f + 4.0f, label.text_top}, color, count, NULL);
    }

    // The box a tap counts in: the label (not the subject line), grown to a size a finger can hit.
    if (!unseen) {
        box[0] = box[1] = box[2] = box[3] = 0.0f;
        return;
    }
    f32 minimum = 32.0f * sandbox->imgui.ui_scale;
    for (u32 axis = 0; axis < 2; ++axis) {
        f32 extra = minimum - (box[axis + 2] - box[axis]);
        if (extra > 0.0f) {
            box[axis] -= extra * 0.5f;
            box[axis + 2] += extra * 0.5f;
        }
    }
}

void ui_play_button(Sandbox* sandbox, ImVec2_c size)
{
    f32* box = sandbox->play_box;
    box[0] = box[1] = box[2] = box[3] = 0.0f;
    // Play and Stop belong to the showcase; the stress scene always runs.
    if (sandbox->shown != SCENE_SHOWCASE)
        return;
    char play_label[64];
    snprintf(play_label, sizeof(play_label), "%s###play", T(sandbox->playing ? "Stop" : "Play"));
    if (igButton(play_label, size)) {
        if (sandbox->playing)
            sandbox_stop_playing(sandbox);
        else
            sandbox_start_playing(sandbox);
    }
    ImVec2_c min = igGetItemRectMin(), max = igGetItemRectMax();
    box[0] = min.x;
    box[1] = min.y;
    box[2] = max.x;
    box[3] = max.y;
}

void ui_playing_note(Sandbox* sandbox)
{
    if (!sandbox->playing || sandbox->shown != SCENE_SHOWCASE)
        return;
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){0.55f, 0.85f, 1.0f, 1.0f});
    igTextWrapped(T("Playing: edits are lost on Stop."));
    igPopStyleColor(1);
}

b32 ui_push_play_tint(Sandbox* sandbox)
{
    // A tinted panel while the showcase plays, so edits that will be lost are not mistaken for
    // edits that stay.
    if (!sandbox->playing || sandbox->shown != SCENE_SHOWCASE)
        return 0;
    igPushStyleColor_Vec4(ImGuiCol_WindowBg, (ImVec4_c){0.05f, 0.12f, 0.20f, 1.0f});
    return 1;
}

void ui_pop_play_tint(b32 pushed)
{
    if (pushed)
        igPopStyleColor(1);
}

b32 ui_begin_console_tab(Sandbox* sandbox)
{
    // NOTE: The label counts warnings and errors that arrived while the tab was not shown. Its id
    // (###console) stays the same, so the tab keeps its place as the count changes.
    NvLogLevel worst;
    u32 unseen = console_unseen(sandbox, &worst);
    char label[64];
    snprintf(label, sizeof(label), "%s###console", T("Console"));
    if (unseen) {
        // A phone's tab bar has no room for the number (the tabs would scroll): the color says
        // it, and the badge on the build label counts.
        if (!console_is_compact()) {
            char count[8];
            format_unseen(unseen, count, sizeof(count));
            snprintf(label, sizeof(label), "%s (%s)###console", T("Console"), count);
        }
        igPushStyleColor_U32(ImGuiCol_Text, console_level_color(worst));
    }
    ImGuiTabItemFlags flags = sandbox->open_console ? ImGuiTabItemFlags_SetSelected : 0;
    sandbox->open_console = 0;
    b32 open = igBeginTabItem(label, NULL, flags);
    if (unseen)
        igPopStyleColor(1);
    console_record(&sandbox->console, CONSOLE_RECT_TAB);
    return open;
}

b32 ui_begin_textures_tab(Sandbox* sandbox)
{
    ImGuiTabItemFlags flags = sandbox->open_textures ? ImGuiTabItemFlags_SetSelected : 0;
    sandbox->open_textures = 0;
    b32 open = igBeginTabItem(TL("Textures"), NULL, flags);
    textures_record(&sandbox->textures, TEXTURES_RECT_TAB);
    return open;
}

void sandbox_layout(Sandbox* sandbox)
{
    f32 ratio = nv_window_pixel_ratio(&sandbox->window);
    f32 width_css = (f32)sandbox->gpu.width / ratio;
    f32 height_css = (f32)sandbox->gpu.height / ratio;
    if (sandbox->ui_mode == UI_PHONE)
        phone_layout(sandbox, width_css, height_css, ratio);
    else
        desktop_layout(sandbox, width_css, height_css, ratio);
    sandbox->layout.scene = nv_renderer_scene_output(&sandbox->resolution, sandbox->layout.viewport);
}

void sandbox_build_ui(Sandbox* sandbox)
{
    // The Console tab counts what arrived while it was not shown: it was shown last frame or not.
    sandbox->console.shown_last = sandbox->console.shown_now;
    sandbox->console.shown_now = 0;
    sandbox->textures.shown_last = sandbox->textures.shown_now;
    sandbox->textures.shown_now = 0;
    search_frame(sandbox);
    ui_draw_build_label(sandbox);
    if (sandbox->ui_mode == UI_PHONE)
        phone_build_ui(sandbox);
    else
        desktop_build_ui(sandbox);
}
