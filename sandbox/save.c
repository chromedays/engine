// The autosave's contents: the sandbox state as chunks (engine/chunk.h), and loading it back. What is
// saved, tag by tag, is listed in docs/specs/save.md; keep the two in step.

#include "sandbox.h"

#include <engine/chunk.h>

#include <math.h>
#include <string.h>

// Top level
#define TAG_EDIT NV_TAG('E', 'D', 'I', 'T')
#define TAG_SCNE NV_TAG('S', 'C', 'N', 'E')
// EDIT
#define TAG_AUTO NV_TAG('A', 'U', 'T', 'O')
#define TAG_GZOP NV_TAG('G', 'Z', 'O', 'P')
#define TAG_GZLC NV_TAG('G', 'Z', 'L', 'C')
#define TAG_GZSN NV_TAG('G', 'Z', 'S', 'N')
#define TAG_SHSZ NV_TAG('S', 'H', 'S', 'Z') // shadow map size, 0 = off
#define TAG_SHFM NV_TAG('S', 'H', 'F', 'M') // shadow map format
#define TAG_SHFL NV_TAG('S', 'H', 'F', 'L') // shadow filter
#define TAG_SHDS NV_TAG('S', 'H', 'D', 'S') // shadow distance
#define TAG_SHBX NV_TAG('S', 'H', 'B', 'X') // show the light box
#define TAG_MSAA NV_TAG('M', 'S', 'A', 'A') // anti-aliasing: samples per pixel, 1 or 4
#define TAG_RSMD NV_TAG('R', 'S', 'M', 'D') // resolution mode: 0 scale, 1 fixed
#define TAG_RSCL NV_TAG('R', 'S', 'C', 'L') // scale mode's divisor: 1 to 4
#define TAG_RSFT NV_TAG('R', 'S', 'F', 'T') // fixed mode's fit: 0 whole multiples, 1 viewport, 2 stretch
#define TAG_RSFW NV_TAG('R', 'S', 'F', 'W') // fixed mode's width
#define TAG_RSFH NV_TAG('R', 'S', 'F', 'H') // fixed mode's height
#define TAG_DKLW NV_TAG('D', 'K', 'L', 'W') // desktop: left dock width, CSS pixels
#define TAG_DKRW NV_TAG('D', 'K', 'R', 'W') // desktop: right dock width
#define TAG_DKBH NV_TAG('D', 'K', 'B', 'H') // desktop: bottom dock height
#define TAG_DKBO NV_TAG('D', 'K', 'B', 'O') // desktop: bottom dock open
#define TAG_TONE NV_TAG('T', 'O', 'N', 'E') // tone mapping: 0 Clamp, 1 PBR Neutral, 2 ACES (docs/specs/vfx.md)
#define TAG_EXPO NV_TAG('E', 'X', 'P', 'O') // exposure, 0.25 to 4
#define TAG_BLOM NV_TAG('B', 'L', 'O', 'M') // bloom on
#define TAG_BLMI NV_TAG('B', 'L', 'M', 'I') // bloom intensity, 0 to 0.2
#define TAG_LANG NV_TAG('L', 'A', 'N', 'G') // the UI's language: 0 English, 1 Korean (docs/specs/korean.md)
// SCNE
#define TAG_LAYT NV_TAG('L', 'A', 'Y', 'T')
#define TAG_VIEW NV_TAG('V', 'I', 'E', 'W')
#define TAG_PLNT NV_TAG('P', 'L', 'N', 'T')
#define TAG_ORBS NV_TAG('O', 'R', 'B', 'S') // undo only: the orbit speed without the angle
#define TAG_BONE NV_TAG('B', 'O', 'N', 'E')
#define TAG_CHAR NV_TAG('C', 'H', 'A', 'R')
#define TAG_NODE NV_TAG('N', 'O', 'D', 'E')
// VIEW
#define TAG_YAW  NV_TAG('Y', 'A', 'W', ' ')
#define TAG_PTCH NV_TAG('P', 'T', 'C', 'H')
#define TAG_DIST NV_TAG('D', 'I', 'S', 'T')
#define TAG_FOLW NV_TAG('F', 'O', 'L', 'W')
#define TAG_ORBT NV_TAG('O', 'R', 'B', 'T')
#define TAG_PAN  NV_TAG('P', 'A', 'N', ' ')
#define TAG_SELN NV_TAG('S', 'E', 'L', 'N')
#define TAG_SELO NV_TAG('S', 'E', 'L', 'O') // the other selected nodes' paths (docs/specs/selection.md)
// CHAR
#define TAG_CLIP NV_TAG('C', 'L', 'I', 'P')
#define TAG_CTIM NV_TAG('C', 'T', 'I', 'M')
#define TAG_SPED NV_TAG('S', 'P', 'E', 'D')
#define TAG_FADE NV_TAG('F', 'A', 'D', 'E')
#define TAG_BLND NV_TAG('B', 'L', 'N', 'D')
#define TAG_BLDW NV_TAG('B', 'L', 'D', 'W')
#define TAG_RMOT NV_TAG('R', 'M', 'O', 'T')
#define TAG_TURN NV_TAG('T', 'U', 'R', 'N')
#define TAG_LOOK NV_TAG('L', 'O', 'O', 'K')
#define TAG_SWRD NV_TAG('S', 'W', 'R', 'D')
// NODE
#define TAG_PATH NV_TAG('P', 'A', 'T', 'H')
#define TAG_NAME NV_TAG('N', 'A', 'M', 'E')
#define TAG_POS  NV_TAG('P', 'O', 'S', ' ')
#define TAG_ROT  NV_TAG('R', 'O', 'T', ' ')
#define TAG_SCL  NV_TAG('S', 'C', 'L', ' ')
#define TAG_COLR NV_TAG('C', 'O', 'L', 'R')
#define TAG_ATCH NV_TAG('A', 'T', 'C', 'H')
#define TAG_CFOV NV_TAG('C', 'F', 'O', 'V')
#define TAG_LCOL NV_TAG('L', 'C', 'O', 'L')
#define TAG_LINT NV_TAG('L', 'I', 'N', 'T')

#define SAVE_MAX_PATH 16
#define SAVE_MAX_CLIP_NAME 64

//
// Node paths: each node's index among its siblings, from the top level down.
//

// The node after `index` in pre-order, or 0 after the last one.
internal u32 next_in_tree(NvScene* scene, u32 index)
{
    if (scene->nodes[index].first_child)
        return scene->nodes[index].first_child;
    while (index && !scene->nodes[index].next_sibling)
        index = scene->nodes[index].parent;
    return index ? scene->nodes[index].next_sibling : 0;
}

internal u32 depth_of(NvScene* scene, u32 index)
{
    u32 depth = 0;
    for (u32 at = scene->nodes[index].parent; at; at = scene->nodes[at].parent)
        ++depth;
    return depth;
}

typedef struct NodePath {
    b32 ok;
    u32 length;
} NodePath;

// Writes the node's path into `path` (SAVE_MAX_PATH entries). Fails when it is deeper than that.
internal NodePath path_of(NvScene* scene, u32 index, u32* path)
{
    u32 length = depth_of(scene, index) + 1;
    if (length > SAVE_MAX_PATH)
        return (NodePath){0};
    u32 at = index;
    for (u32 level = length; level-- > 0;) {
        u32 parent = scene->nodes[at].parent;
        u32 sibling = parent ? scene->nodes[parent].first_child : scene->first_root;
        u32 position = 0;
        while (sibling != at) {
            sibling = scene->nodes[sibling].next_sibling;
            ++position;
        }
        path[level] = position;
        at = parent;
    }
    return (NodePath){.ok = 1, .length = length};
}

// The node at `path`, or 0.
internal u32 node_at(NvScene* scene, const u32* path, u32 length)
{
    u32 at = 0;
    for (u32 level = 0; level < length; ++level) {
        u32 child = at ? scene->nodes[at].first_child : scene->first_root;
        for (u32 i = 0; i < path[level] && child; ++i)
            child = scene->nodes[child].next_sibling;
        if (!child)
            return 0;
        at = child;
    }
    return at;
}

u32 save_scene_layout(NvScene* scene)
{
    // FNV-1a over every node's depth and name, in tree order.
    u32 hash = NV_FNV1A_SEED;
    for (u32 index = scene->first_root; index; index = next_in_tree(scene, index)) {
        // NOTE: The depth goes in as one word, not as a byte: a saved layout hash depends on it.
        hash = (hash ^ depth_of(scene, index)) * NV_FNV1A_PRIME;
        const char* name = scene->nodes[index].name;
        hash = nv_fnv1a(hash, name, strlen(name));
        hash = (hash ^ 0xFFu) * NV_FNV1A_PRIME; // ends the name
    }
    return hash;
}

//
// Writing
//

internal void write_view(NvChunkWriter* w, NvScene* scene, const SceneView* view)
{
    nv_chunk_begin(w, TAG_VIEW);
    nv_chunk_f32(w, TAG_YAW, view->orbit.yaw);
    nv_chunk_f32(w, TAG_PTCH, view->orbit.pitch);
    nv_chunk_f32(w, TAG_DIST, view->orbit.distance);
    nv_chunk_u32(w, TAG_FOLW, view->follow_selection);
    nv_chunk_f32s(w, TAG_ORBT, &view->orbit.target.x, 3);
    nv_chunk_f32s(w, TAG_PAN, &view->pan.x, 3);
    u32 path[SAVE_MAX_PATH];
    NodePath selected = view->selected.index ? path_of(scene, view->selected.index, path) : (NodePath){0};
    nv_chunk_u32s(w, TAG_SELN, path, selected.length);
    // The others: each path as its length, then its indices.
    u32 others[(SELECTION_MAX - 1) * (SAVE_MAX_PATH + 1)];
    u32 count = 0;
    for (u32 i = 0; i < view->other_count; ++i) {
        NodePath other = path_of(scene, view->others[i].index, &others[count + 1]);
        if (!other.ok)
            continue;
        others[count] = other.length;
        count += 1 + other.length;
    }
    if (count)
        nv_chunk_u32s(w, TAG_SELO, others, count);
    nv_chunk_end(w);
}

// The character's fields. The clip's time (CTIM) is no longer written: the saved state is the edit
// state, which has no running time, and a run starts every clip from its start
// (docs/specs/play.md). Older saves that have it still load.
internal void write_character_fields(NvChunkWriter* w, Sandbox* sandbox, b32 undo)
{
    (void)undo;
    NvAnimator* animator = nv_anim_get(sandbox->animator);
    NvAnimLayer* layer = &animator->layers[0];
    // Mid-jump, the clip the jump lands back into stands for it.
    NvClipId clip = sandbox->jump != JUMP_NONE ? sandbox->jump_return : layer->clip;

    if (clip.index)
        nv_chunk_string(w, TAG_CLIP, nv_anim_clip_name(sandbox_regular_clip(sandbox, clip)));
    nv_chunk_f32(w, TAG_SPED, layer->speed);
    nv_chunk_f32(w, TAG_FADE, sandbox->fade_seconds);
    if (sandbox->blend_clip >= 0 && (u32)sandbox->blend_clip < sandbox->clip_count)
        nv_chunk_string(w, TAG_BLND, nv_anim_clip_name(sandbox->clips[sandbox->blend_clip]));
    nv_chunk_f32(w, TAG_BLDW, sandbox->blend_weight);
    nv_chunk_u32(w, TAG_RMOT, sandbox->root_motion);
    nv_chunk_f32(w, TAG_TURN, sandbox->turn_rate);
    nv_chunk_u32(w, TAG_LOOK, sandbox->look_at);
    nv_chunk_u32(w, TAG_SWRD, sandbox->show_sword);
}

internal void write_character(NvChunkWriter* w, Sandbox* sandbox)
{
    nv_chunk_begin(w, TAG_CHAR);
    write_character_fields(w, sandbox, 0);
    nv_chunk_end(w);
}

#define DRIVEN_POSITION (1u << 0)
#define DRIVEN_ROTATION (1u << 1)

// Which of a showcase node's transform the sandbox rewrites every frame in Edit mode. Undo leaves these
// out, or every frame would look like an edit. Only the orbit camera is left: everything else that
// moves by itself (spins, the walk, the look target's sweep) only moves while playing
// (docs/specs/play.md), and undo is off then.
internal u32 driven_fields(Sandbox* sandbox, u32 index)
{
    if (index == sandbox->views[SCENE_SHOWCASE].camera.index)
        return DRIVEN_POSITION | DRIVEN_ROTATION;
    return 0;
}

// A node's fields. `undo` leaves out what the sandbox drives.
internal void write_node_fields(NvChunkWriter* w, Sandbox* sandbox, u32 index, b32 undo)
{
    NvNode* node = &sandbox->scene->nodes[index];
    u32 driven = undo ? driven_fields(sandbox, index) : 0;
    nv_chunk_string(w, TAG_NAME, node->name);
    if (!(driven & DRIVEN_POSITION))
        nv_chunk_f32s(w, TAG_POS, &node->position.x, 3);
    if (!(driven & DRIVEN_ROTATION))
        nv_chunk_f32s(w, TAG_ROT, &node->rotation.x, 4);
    nv_chunk_f32s(w, TAG_SCL, &node->scale.x, 3);
    if (node->material.index)
        nv_chunk_f32s(w, TAG_COLR, sandbox->renderer.materials[node->material.index].desc.base_color, 4);
    if (node->attach.animator.index) {
        NvAnimator* animator = nv_anim_get(node->attach.animator);
        nv_chunk_string(w, TAG_ATCH, nv_anim_joints(animator->skeleton)[node->attach.joint].name);
    }
    if (node->camera.projection)
        nv_chunk_f32(w, TAG_CFOV, node->camera.fov_y);
    if (node->light.type) {
        nv_chunk_f32s(w, TAG_LCOL, &node->light.color.x, 3);
        nv_chunk_f32(w, TAG_LINT, node->light.intensity);
    }
}

internal void write_node(NvChunkWriter* w, Sandbox* sandbox, u32 index)
{
    u32 path[SAVE_MAX_PATH];
    NodePath node_path = path_of(sandbox->scene, index, path);
    if (!node_path.ok)
        return;
    nv_chunk_begin(w, TAG_NODE);
    nv_chunk_u32s(w, TAG_PATH, path, node_path.length);
    write_node_fields(w, sandbox, index, 0);
    nv_chunk_end(w);
}

NvChunkWritten save_write(Sandbox* sandbox, void* buffer, u32 capacity)
{
    NvChunkWriter w;
    nv_chunk_writer_init(&w, buffer, capacity);
    nv_chunk_file_begin(&w, SAVE_MAGIC, SAVE_VERSION);

    nv_chunk_begin(&w, TAG_EDIT);
    nv_chunk_u32(&w, TAG_AUTO, sandbox->autosave);
    nv_chunk_u32(&w, TAG_GZOP, (u32)sandbox->gizmo_operation);
    nv_chunk_u32(&w, TAG_GZLC, sandbox->gizmo_local);
    nv_chunk_u32(&w, TAG_GZSN, sandbox->gizmo_snap);
    const NvShadowSettings* shadows = &sandbox->renderer.shadows;
    nv_chunk_u32(&w, TAG_SHSZ, shadows->size);
    nv_chunk_u32(&w, TAG_SHFM, (u32)shadows->format);
    nv_chunk_u32(&w, TAG_SHFL, (u32)shadows->filter);
    nv_chunk_f32(&w, TAG_SHDS, shadows->distance);
    nv_chunk_u32(&w, TAG_SHBX, shadows->show_box);
    nv_chunk_u32(&w, TAG_MSAA, sandbox->renderer.msaa);
    nv_chunk_u32(&w, TAG_TONE, (u32)sandbox->renderer.post.tone);
    nv_chunk_f32(&w, TAG_EXPO, sandbox->renderer.post.exposure);
    nv_chunk_u32(&w, TAG_BLOM, sandbox->renderer.post.bloom);
    nv_chunk_f32(&w, TAG_BLMI, sandbox->renderer.post.bloom_intensity);
    nv_chunk_u32(&w, TAG_RSMD, (u32)sandbox->resolution.mode);
    nv_chunk_u32(&w, TAG_RSCL, sandbox->resolution.divisor);
    nv_chunk_u32(&w, TAG_RSFT, (u32)sandbox->resolution.fixed_fit);
    nv_chunk_u32(&w, TAG_RSFW, sandbox->resolution.fixed_width);
    nv_chunk_u32(&w, TAG_RSFH, sandbox->resolution.fixed_height);
    nv_chunk_u32(&w, TAG_DKLW, (u32)(sandbox->docks.left_width + 0.5f));
    nv_chunk_u32(&w, TAG_DKRW, (u32)(sandbox->docks.right_width + 0.5f));
    nv_chunk_u32(&w, TAG_DKBH, (u32)(sandbox->docks.bottom_height + 0.5f));
    nv_chunk_u32(&w, TAG_DKBO, sandbox->docks.bottom_open);
    nv_chunk_u32(&w, TAG_LANG, (u32)nv_strings_language());
    nv_chunk_end(&w);

    nv_chunk_begin(&w, TAG_SCNE);
    nv_chunk_u32(&w, TAG_LAYT, sandbox->scene_layout);
    write_view(&w, sandbox->scene, &sandbox->views[SCENE_SHOWCASE]);
    // The orbit angle is written as 0: like the clip time, it only runs while playing.
    f32 planet[2] = {sandbox->orbit_speed, 0.0f};
    nv_chunk_f32s(&w, TAG_PLNT, planet, 2);
    nv_chunk_u32(&w, TAG_BONE, sandbox->show_bones);
    write_character(&w, sandbox);
    for (u32 index = sandbox->scene->first_root; index; index = next_in_tree(sandbox->scene, index))
        write_node(&w, sandbox, index);
    nv_chunk_end(&w);

    return nv_chunk_file_end(&w);
}

//
// Reading
//
// One function reads the save twice: a dry run with `apply` off that only checks it, then, if
// that passed, again with `apply` on. Both passes read the same fields, so a save that passes the
// dry run cannot fail halfway through changing the sandbox.

internal b32 read_bool(NvChunkReader* r, NvChunk parent, u32 tag, bool* out)
{
    u32 value;
    if (!nv_chunk_read_u32s(r, parent, tag, &value, 1))
        return 0;
    *out = value != 0;
    return 1;
}

internal void read_edit(NvChunkReader* r, NvChunk edit, Sandbox* sandbox, b32 apply)
{
    bool autosave = sandbox->autosave, local = sandbox->gizmo_local, snap = sandbox->gizmo_snap;
    u32 operation = (u32)sandbox->gizmo_operation;
    read_bool(r, edit, TAG_AUTO, &autosave);
    nv_chunk_read_u32s(r, edit, TAG_GZOP, &operation, 1);
    read_bool(r, edit, TAG_GZLC, &local);
    read_bool(r, edit, TAG_GZSN, &snap);
    NvShadowSettings shadows = sandbox->renderer.shadows;
    u32 format = (u32)shadows.format, filter = (u32)shadows.filter;
    nv_chunk_read_u32s(r, edit, TAG_SHSZ, &shadows.size, 1);
    nv_chunk_read_u32s(r, edit, TAG_SHFM, &format, 1);
    nv_chunk_read_u32s(r, edit, TAG_SHFL, &filter, 1);
    nv_chunk_read_f32s(r, edit, TAG_SHDS, &shadows.distance, 1);
    nv_chunk_read_u32s(r, edit, TAG_SHBX, (u32*)&shadows.show_box, 1);
    u32 msaa = sandbox->renderer.msaa;
    nv_chunk_read_u32s(r, edit, TAG_MSAA, &msaa, 1);
    NvPostSettings post = sandbox->renderer.post;
    u32 tone = (u32)post.tone;
    nv_chunk_read_u32s(r, edit, TAG_TONE, &tone, 1);
    nv_chunk_read_f32s(r, edit, TAG_EXPO, &post.exposure, 1);
    nv_chunk_read_u32s(r, edit, TAG_BLOM, (u32*)&post.bloom, 1);
    nv_chunk_read_f32s(r, edit, TAG_BLMI, &post.bloom_intensity, 1);
    NvResolution resolution = sandbox->resolution;
    u32 mode = (u32)resolution.mode;
    nv_chunk_read_u32s(r, edit, TAG_RSMD, &mode, 1);
    nv_chunk_read_u32s(r, edit, TAG_RSCL, &resolution.divisor, 1);
    u32 fit = (u32)resolution.fixed_fit;
    nv_chunk_read_u32s(r, edit, TAG_RSFT, &fit, 1);
    nv_chunk_read_u32s(r, edit, TAG_RSFW, &resolution.fixed_width, 1);
    nv_chunk_read_u32s(r, edit, TAG_RSFH, &resolution.fixed_height, 1);
    u32 dock_left = (u32)(sandbox->docks.left_width + 0.5f), dock_right = (u32)(sandbox->docks.right_width + 0.5f);
    u32 dock_bottom = (u32)(sandbox->docks.bottom_height + 0.5f);
    bool bottom_open = sandbox->docks.bottom_open;
    nv_chunk_read_u32s(r, edit, TAG_DKLW, &dock_left, 1);
    nv_chunk_read_u32s(r, edit, TAG_DKRW, &dock_right, 1);
    nv_chunk_read_u32s(r, edit, TAG_DKBH, &dock_bottom, 1);
    read_bool(r, edit, TAG_DKBO, &bottom_open);
    u32 language = (u32)nv_strings_language(); // a save without the tag keeps the browser's language
    nv_chunk_read_u32s(r, edit, TAG_LANG, &language, 1);
    if (!apply)
        return;
    sandbox->autosave = autosave;
    sandbox->gizmo_operation = operation <= GIZMO_SCALE ? (GizmoOperation)operation : GIZMO_MOVE;
    sandbox->gizmo_local = local;
    sandbox->gizmo_snap = snap;
    // Only the sizes the View tab offers; anything else is off.
    if (shadows.size != 512 && shadows.size != 1024 && shadows.size != 2048)
        shadows.size = 0;
    shadows.format = format == NV_SHADOW_FORMAT_DEPTH16 ? NV_SHADOW_FORMAT_DEPTH16 : NV_SHADOW_FORMAT_DEPTH32F;
    shadows.filter = filter == NV_SHADOW_FILTER_LOW ? NV_SHADOW_FILTER_LOW : NV_SHADOW_FILTER_HIGH;
    shadows.distance = nv_clamp_f32(shadows.distance, 5.0f, 100.0f);
    shadows.show_box = shadows.show_box != 0;
    sandbox->renderer.shadows = shadows;
    // The two counts the View tab offers; anything else is the default.
    sandbox->renderer.msaa = msaa == 1 ? 1 : 4;
    // The tone mappers the View tab offers (anything else is PBR Neutral), exposure 0.25 to 4, bloom 0 to 0.2.
    post.tone = tone < NV_TONE_COUNT ? (NvToneMap)tone : NV_TONE_PBR_NEUTRAL;
    post.exposure = nv_clamp_f32(post.exposure, 0.25f, 4.0f);
    post.bloom = post.bloom != 0;
    post.bloom_intensity = nv_clamp_f32(post.bloom_intensity, 0.0f, 0.2f);
    sandbox->renderer.post = post;
    // The modes and counts the View tab offers; anything else is the device's default (the phone
    // shows a quarter of the pixels by default, the desktop all of them) or 1280 x 720.
    resolution.mode = mode == NV_RESOLUTION_FIXED ? NV_RESOLUTION_FIXED : NV_RESOLUTION_SCALE;
    resolution.fixed_fit = fit == NV_FIT_VIEWPORT ? NV_FIT_VIEWPORT : fit == NV_FIT_STRETCH ? NV_FIT_STRETCH : NV_FIT_WHOLE;
    if (resolution.divisor < 1 || resolution.divisor > 4)
        resolution.divisor = sandbox->ui_mode == UI_PHONE ? 2 : 1;
    if (resolution.fixed_width < NV_RESOLUTION_MIN || resolution.fixed_width > NV_RESOLUTION_MAX)
        resolution.fixed_width = 1280;
    if (resolution.fixed_height < NV_RESOLUTION_MIN || resolution.fixed_height > NV_RESOLUTION_MAX)
        resolution.fixed_height = 720;
    sandbox->resolution = resolution;
    // Docks: within what the splitters allow (ui_desktop.c clamps again to the window).
    sandbox->docks.left_width = nv_clamp_f32((f32)dock_left, DOCK_LEFT_MIN, DOCK_SIDE_MAX);
    sandbox->docks.right_width = nv_clamp_f32((f32)dock_right, DOCK_RIGHT_MIN, DOCK_SIDE_MAX);
    sandbox->docks.bottom_height = nv_clamp_f32((f32)dock_bottom, DOCK_BOTTOM_MIN, DOCK_BOTTOM_MAX);
    sandbox->docks.bottom_open = bottom_open;
    // The two languages the View tab offers; anything else is English.
    nv_strings_set_language(language == NV_LANGUAGE_KO ? NV_LANGUAGE_KO : NV_LANGUAGE_EN);
}

internal void read_view(NvChunkReader* r, NvChunk parent, NvScene* scene, SceneView* view, b32 apply, b32 nodes_match)
{
    NvChunk chunk = nv_chunk_find(r, parent, TAG_VIEW);
    if (!chunk.data)
        return;
    SceneView v = *view;
    nv_chunk_read_f32s(r, chunk, TAG_YAW, &v.orbit.yaw, 1);
    nv_chunk_read_f32s(r, chunk, TAG_PTCH, &v.orbit.pitch, 1);
    nv_chunk_read_f32s(r, chunk, TAG_DIST, &v.orbit.distance, 1);
    read_bool(r, chunk, TAG_FOLW, &v.follow_selection);
    nv_chunk_read_f32s(r, chunk, TAG_ORBT, &v.orbit.target.x, 3);
    nv_chunk_read_f32s(r, chunk, TAG_PAN, &v.pan.x, 3);
    u32 path[SAVE_MAX_PATH];
    NvChunkList selection = nv_chunk_read_u32_list(r, chunk, TAG_SELN, path, SAVE_MAX_PATH);
    u32 others[(SELECTION_MAX - 1) * (SAVE_MAX_PATH + 1)];
    u32 others_size = nv_chunk_read_u32_list(r, chunk, TAG_SELO, others, NV_ARRAY_COUNT(others)).count; // 0 when missing
    if (!apply)
        return;
    v.orbit.pitch = nv_clamp_f32(v.orbit.pitch, v.orbit.min_pitch, v.orbit.max_pitch);
    v.orbit.distance = nv_clamp_f32(v.orbit.distance, v.orbit.min_distance, v.orbit.max_distance);
    // A selection path only means something while the tree is the one it was saved from.
    if (selection.ok && nodes_match) {
        u32 index = selection.count ? node_at(scene, path, selection.count) : 0;
        v.selected = index ? (NvNodeId){index, scene->nodes[index].gen} : (NvNodeId){0};
        v.other_count = 0;
        v.range_anchor = v.selected;
        // The others' paths, each its length and then its indices; a broken list gives none.
        b32 broken = 0;
        for (u32 at = 0; at < others_size && !broken; at += 1 + others[at])
            broken = others[at] == 0 || others[at] > SAVE_MAX_PATH || at + 1 + others[at] > others_size;
        for (u32 at = 0; v.selected.index && !broken && at < others_size; at += 1 + others[at]) {
            u32 other = node_at(scene, &others[at + 1], others[at]);
            NvNodeId id = other ? (NvNodeId){other, scene->nodes[other].gen} : (NvNodeId){0};
            if (id.index && !selection_has(&v, id) && v.other_count < SELECTION_MAX - 1)
                v.others[v.other_count++] = id;
        }
    }
    v.panned_for = v.selected;
    *view = v;
}

internal NvClipId clip_named(Sandbox* sandbox, const char* name)
{
    for (u32 i = 0; i < sandbox->clip_count; ++i) {
        if (strcmp(nv_anim_clip_name(sandbox->clips[i]), name) == 0)
            return sandbox->clips[i];
    }
    return (NvClipId){0};
}

// Applies only the fields present, so it also restores an undo snapshot (which has no time).
internal void read_character_fields(NvChunkReader* r, NvChunk chunk, Sandbox* sandbox, b32 apply)
{
    NvAnimator* animator = nv_anim_get(sandbox->animator);
    char clip_name[SAVE_MAX_CLIP_NAME] = "Idle_Loop";
    char blend_name[SAVE_MAX_CLIP_NAME] = "";
    f32 time = animator->layers[0].time;
    f32 speed = animator->layers[0].speed;
    f32 fade = sandbox->fade_seconds, blend_weight = sandbox->blend_weight, turn = sandbox->turn_rate;
    bool root_motion = sandbox->root_motion, look_at = sandbox->look_at, sword = sandbox->show_sword;
    b32 has_clip = nv_chunk_read_string(r, chunk, TAG_CLIP, clip_name, sizeof(clip_name));
    b32 has_time = nv_chunk_read_f32s(r, chunk, TAG_CTIM, &time, 1);
    nv_chunk_read_f32s(r, chunk, TAG_SPED, &speed, 1);
    nv_chunk_read_f32s(r, chunk, TAG_FADE, &fade, 1);
    nv_chunk_read_string(r, chunk, TAG_BLND, blend_name, sizeof(blend_name));
    nv_chunk_read_f32s(r, chunk, TAG_BLDW, &blend_weight, 1);
    read_bool(r, chunk, TAG_RMOT, &root_motion);
    nv_chunk_read_f32s(r, chunk, TAG_TURN, &turn, 1);
    read_bool(r, chunk, TAG_LOOK, &look_at);
    read_bool(r, chunk, TAG_SWRD, &sword);
    if (!apply)
        return;

    sandbox->root_motion = root_motion;
    sandbox->turn_rate = turn;
    sandbox->look_at = look_at;
    sandbox->show_sword = sword;
    sandbox->blend_weight = nv_clamp_f32(blend_weight, 0.0f, 1.0f);
    for (u32 i = 0; i < sandbox->clip_count; ++i) {
        if (strcmp(nv_anim_clip_name(sandbox->clips[i]), blend_name) == 0)
            sandbox->blend_clip = (s32)i;
    }
    if (has_clip) {
        NvClipId clip = clip_named(sandbox, clip_name);
        if (!clip.index)
            clip = clip_named(sandbox, "Idle_Loop");
        // Straight into the clip, without a crossfade from whatever played before. The same clip
        // keeps playing where it is; root motion on or off picks its copy.
        sandbox->fade_seconds = 0.0f;
        sandbox_play(sandbox, clip);
    }
    sandbox->fade_seconds = nv_clamp_f32(fade, 0.0f, 1.0f);
    if (has_time)
        animator->layers[0].time = time;
    animator->layers[0].speed = speed;
}

internal void read_character(NvChunkReader* r, NvChunk parent, Sandbox* sandbox, b32 apply)
{
    NvChunk chunk = nv_chunk_find(r, parent, TAG_CHAR);
    if (chunk.data)
        read_character_fields(r, chunk, sandbox, apply);
}

// Applies only the fields present to node `index` (0: only checks them).
internal void read_node_fields(NvChunkReader* r, NvChunk chunk, Sandbox* sandbox, u32 index, b32 apply)
{
    NvScene* scene = sandbox->scene;
    NvNode none = {.rotation = nv_quat_identity(), .scale = nv_vec3(1, 1, 1)};
    NvNode* node = index ? &scene->nodes[index] : &none; // a missing node is still read, to check it
    NvNode n = *node;
    f32 color[4] = {0};
    char joint[NV_NODE_NAME_MAX];
    nv_chunk_read_string(r, chunk, TAG_NAME, n.name, sizeof(n.name));
    nv_chunk_read_f32s(r, chunk, TAG_POS, &n.position.x, 3);
    nv_chunk_read_f32s(r, chunk, TAG_ROT, &n.rotation.x, 4);
    nv_chunk_read_f32s(r, chunk, TAG_SCL, &n.scale.x, 3);
    b32 has_color = nv_chunk_read_f32s(r, chunk, TAG_COLR, color, 4);
    b32 has_joint = nv_chunk_read_string(r, chunk, TAG_ATCH, joint, sizeof(joint));
    nv_chunk_read_f32s(r, chunk, TAG_CFOV, &n.camera.fov_y, 1);
    nv_chunk_read_f32s(r, chunk, TAG_LCOL, &n.light.color.x, 3);
    nv_chunk_read_f32s(r, chunk, TAG_LINT, &n.light.intensity, 1);
    if (!apply || !index)
        return;

    // Only a rotation that is clearly off unit length is normalized: renormalizing a good one
    // would change its last bits, and the next save would differ from this one.
    f32 length_squared = n.rotation.x * n.rotation.x + n.rotation.y * n.rotation.y + n.rotation.z * n.rotation.z +
                         n.rotation.w * n.rotation.w;
    if (fabsf(length_squared - 1.0f) > 1.0e-3f) {
        f32 inverse = length_squared > 1.0e-12f ? 1.0f / sqrtf(length_squared) : 0.0f;
        n.rotation = inverse ? (NvQuat){n.rotation.x * inverse, n.rotation.y * inverse, n.rotation.z * inverse, n.rotation.w * inverse}
                             : nv_quat_identity();
    }
    memcpy(node->name, n.name, sizeof(node->name));
    node->position = n.position;
    node->rotation = n.rotation;
    node->scale = n.scale;
    if (has_color && node->material.index)
        nv_renderer_set_material_color(&sandbox->renderer, node->material, color);
    if (has_joint && node->attach.animator.index) {
        s32 found = nv_anim_find_joint(nv_anim_get(node->attach.animator)->skeleton, joint);
        if (found >= 0)
            node->attach.joint = (u32)found;
    }
    if (node->camera.projection)
        node->camera.fov_y = nv_clamp_f32(n.camera.fov_y, 1.0f * NV_PI / 180.0f, 179.0f * NV_PI / 180.0f);
    if (node->light.type) {
        node->light.color = n.light.color;
        node->light.intensity = n.light.intensity;
    }
}

internal void read_node(NvChunkReader* r, NvChunk chunk, Sandbox* sandbox, b32 apply)
{
    u32 path[SAVE_MAX_PATH];
    NvChunkList node_path = nv_chunk_read_u32_list(r, chunk, TAG_PATH, path, SAVE_MAX_PATH);
    if (!node_path.ok || !node_path.count)
        return;
    read_node_fields(r, chunk, sandbox, node_at(sandbox->scene, path, node_path.count), apply);
}

internal void read_scene(NvChunkReader* r, NvChunk scene, Sandbox* sandbox, b32 apply, u32 parts)
{
    b32 apply_scene = apply && (parts & SAVE_PART_SCENE);
    u32 layout = 0;
    nv_chunk_read_u32s(r, scene, TAG_LAYT, &layout, 1);
    b32 nodes_match = layout == sandbox->scene_layout;

    f32 planet[2] = {sandbox->orbit_speed, sandbox->orbit_angle};
    bool bones = sandbox->show_bones;
    nv_chunk_read_f32s(r, scene, TAG_PLNT, planet, 2);
    read_bool(r, scene, TAG_BONE, &bones);
    if (apply_scene) {
        sandbox->orbit_speed = planet[0];
        sandbox->orbit_angle = planet[1];
        sandbox->show_bones = bones;
    }
    read_character(r, scene, sandbox, apply_scene);
    // Nodes are read even when they will not be applied, so a damaged one still fails the load.
    NvChunk child = {0};
    while (nv_chunk_next(r, scene, &child)) {
        if (child.tag == TAG_NODE)
            read_node(r, child, sandbox, apply_scene && nodes_match);
    }
    // After the nodes, so a selection's path finds the tree as saved.
    read_view(r, scene, sandbox->scene, &sandbox->views[SCENE_SHOWCASE], apply && (parts & SAVE_PART_VIEW), nodes_match);
}

internal b32 read_state(NvChunk root, Sandbox* sandbox, b32 apply, u32 parts)
{
    NvChunkReader r = {0};
    NvChunk edit = nv_chunk_find(&r, root, TAG_EDIT);
    if (edit.data)
        read_edit(&r, edit, sandbox, apply && (parts & SAVE_PART_EDITOR));
    NvChunk scene = nv_chunk_find(&r, root, TAG_SCNE);
    if (scene.data)
        read_scene(&r, scene, sandbox, apply, parts);
    // Walk the whole top level too, so a malformed chunk after the known ones is caught.
    NvChunk child = {0};
    while (nv_chunk_next(&r, root, &child)) {
    }
    return !r.failed;
}

SaveLoad save_load_parts(Sandbox* sandbox, const void* bytes, u32 size, u32 parts)
{
    NvChunkFile file = nv_chunk_file_open(bytes, size, SAVE_MAGIC, SAVE_VERSION);
    if (!file.ok)
        return (SaveLoad){.error = nv_chunk_file_status_name(file.status)};
    if (!read_state(file.root, sandbox, 0, parts))
        return (SaveLoad){.error = "malformed"};
    read_state(file.root, sandbox, 1, parts);
    return (SaveLoad){.ok = 1};
}

SaveLoad save_load(Sandbox* sandbox, const void* bytes, u32 size)
{
    return save_load_parts(sandbox, bytes, size, SAVE_PART_ALL);
}

b32 save_round_trip_matches(Sandbox* sandbox)
{
    NvArena* scratch = &sandbox->scratch;
    umm mark = scratch->used;
    u8* first = NV_PUSH_ARRAY(scratch, SAVE_MAX_SIZE, u8);
    u8* second = NV_PUSH_ARRAY(scratch, SAVE_MAX_SIZE, u8);
    NvChunkWritten first_written = save_write(sandbox, first, SAVE_MAX_SIZE);
    b32 matches = first_written.ok && save_load(sandbox, first, first_written.size).ok;
    NvChunkWritten second_written = matches ? save_write(sandbox, second, SAVE_MAX_SIZE) : (NvChunkWritten){0};
    matches = matches && second_written.ok && second_written.size == first_written.size &&
              memcmp(first, second, first_written.size) == 0;
    scratch->used = mark;
    return matches;
}

//
// Undo scopes (docs/specs/undo.md): the undoable fields of one part of the showcase, as bare
// chunks without a file header, written and read with the same code as the save.
//

u32 save_driven_fields(Sandbox* sandbox, u32 node)
{
    return driven_fields(sandbox, node);
}

NvChunkWritten save_write_scope(Sandbox* sandbox, SaveScope scope, const NvNodeId* nodes, u32 node_count, void* buffer, u32 capacity)
{
    NvChunkWriter w;
    nv_chunk_writer_init(&w, buffer, capacity);
    switch (scope) {
    case SAVE_SCOPE_NODE:
        // Each node's fields in a container of its own, in the order given.
        for (u32 i = 0; i < node_count; ++i) {
            nv_chunk_begin(&w, TAG_NODE);
            write_node_fields(&w, sandbox, nodes[i].index, 1);
            nv_chunk_end(&w);
        }
        break;
    case SAVE_SCOPE_CHARACTER:
        write_character_fields(&w, sandbox, 1);
        break;
    case SAVE_SCOPE_SCENE:
        nv_chunk_f32(&w, TAG_ORBS, sandbox->orbit_speed);
        nv_chunk_u32(&w, TAG_BONE, sandbox->show_bones);
        break;
    case SAVE_SCOPE_COUNT:
        NV_INVALID_CODE_PATH;
        break;
    }
    return w.overflow ? (NvChunkWritten){0} : (NvChunkWritten){.ok = 1, .size = w.size};
}

internal b32 read_scope(NvChunk chunk, Sandbox* sandbox, SaveScope scope, const NvNodeId* nodes, u32 node_count, b32 apply)
{
    NvChunkReader r = {0};
    switch (scope) {
    case SAVE_SCOPE_NODE: {
        // One container per node, in the order the nodes are given.
        NvChunk child = {0};
        u32 read = 0;
        while (nv_chunk_next(&r, chunk, &child)) {
            if (child.tag != TAG_NODE || read == node_count)
                return 0;
            read_node_fields(&r, child, sandbox, nodes[read++].index, apply);
        }
        if (read != node_count)
            return 0;
    } break;
    case SAVE_SCOPE_CHARACTER:
        read_character_fields(&r, chunk, sandbox, apply);
        break;
    case SAVE_SCOPE_SCENE: {
        f32 speed = sandbox->orbit_speed;
        bool bones = sandbox->show_bones;
        nv_chunk_read_f32s(&r, chunk, TAG_ORBS, &speed, 1);
        read_bool(&r, chunk, TAG_BONE, &bones);
        if (apply) {
            sandbox->orbit_speed = speed;
            sandbox->show_bones = bones;
        }
    } break;
    case SAVE_SCOPE_COUNT:
        NV_INVALID_CODE_PATH;
        break;
    }
    // Every chunk must be whole, not only the ones read.
    NvChunk child = {0};
    while (nv_chunk_next(&r, chunk, &child)) {
    }
    return !r.failed;
}

b32 save_apply_scope(Sandbox* sandbox, SaveScope scope, const NvNodeId* nodes, u32 node_count, const void* bytes, u32 size)
{
    NvChunk chunk = {.size = size, .data = bytes};
    if (!read_scope(chunk, sandbox, scope, nodes, node_count, 0))
        return 0;
    read_scope(chunk, sandbox, scope, nodes, node_count, 1);
    return 1;
}

const char* save_field_label(u32 tag)
{
    switch (tag) {
    case TAG_NAME: return "Name";
    case TAG_POS: return "Position";
    case TAG_ROT: return "Rotation";
    case TAG_SCL: return "Scale";
    case TAG_COLR: return "Color";
    case TAG_ATCH: return "Joint";
    case TAG_CFOV: return "Field of view";
    case TAG_LCOL: return "Light color";
    case TAG_LINT: return "Intensity";
    case TAG_CLIP: return "Clip";
    case TAG_SPED: return "Speed";
    case TAG_FADE: return "Fade";
    case TAG_BLND: return "Blend";
    case TAG_BLDW: return "Blend weight";
    case TAG_RMOT: return "Root motion";
    case TAG_TURN: return "Turn";
    case TAG_LOOK: return "Look at";
    case TAG_SWRD: return "Sword";
    case TAG_ORBS: return "Planet orbit";
    case TAG_BONE: return "Show bones";
    }
    return "Edit";
}

//
// Autosave
//

#include <stdio.h>

void save_now(Sandbox* sandbox, b32 force)
{
    if (!sandbox->storage.available || sandbox->save_stopped)
        return;
    // While playing, the edit state is the snapshot taken at Play; the running scene is never saved.
    NvChunkWritten written;
    if (sandbox->playing) {
        written = (NvChunkWritten){.ok = 1, .size = sandbox->play_snapshot_size};
        memcpy(sandbox->next_save, sandbox->play_snapshot, written.size);
    } else {
        written = save_write(sandbox, sandbox->next_save, SAVE_MAX_SIZE);
    }
    u32 size = written.size;
    if (!written.ok) {
        snprintf(sandbox->save_notice, sizeof(sandbox->save_notice), T("Not saved: the state is larger than %u KB."),
                 (u32)(SAVE_MAX_SIZE / 1024));
        nv_log(NV_LOG_WARNING, "sandbox", "%s", sandbox->save_notice);
        return;
    }
    if (!force && size == sandbox->saved_size && memcmp(sandbox->next_save, sandbox->saved, size) == 0)
        return;
    if (!nv_storage_write(&sandbox->storage, SAVE_FILE, sandbox->next_save, size)) {
        snprintf(sandbox->save_notice, sizeof(sandbox->save_notice), T("Not saved: writing %s failed."), SAVE_FILE);
        nv_log(NV_LOG_WARNING, "sandbox", "%s", sandbox->save_notice);
        return;
    }
    nv_storage_flush(&sandbox->storage);
    u8* swap = sandbox->saved;
    sandbox->saved = sandbox->next_save;
    sandbox->next_save = swap;
    sandbox->saved_size = size;
    sandbox->saved_at = nv_time_seconds();
}

// Frames stop while the page is hidden, and it may be closing: save now.
internal void save_on_hidden(void* userdata)
{
    Sandbox* sandbox = userdata;
    if (sandbox->autosave)
        save_now(sandbox, 0);
}

// Starts over as on a first visit: deletes the save and reloads once IndexedDB has caught up.
// Animators cannot be removed, so the showcase cannot be rebuilt in place.
internal void save_reset(Sandbox* sandbox)
{
    // IMPORTANT: The reload hides the page, which would otherwise save the state just deleted.
    sandbox->save_stopped = 1;
    nv_storage_remove(&sandbox->storage, SAVE_FILE);
    nv_storage_remove(&sandbox->storage, SAVE_BAD_FILE);
    nv_storage_flush_then_reload(&sandbox->storage);
}

//
// Save viewer: the save's chunks as a tree, read the way the loader reads them.
//

typedef enum TagKind {
    TAG_KIND_UNKNOWN,
    TAG_KIND_CONTAINER,
    TAG_KIND_U32,
    TAG_KIND_F32,
    TAG_KIND_STRING,
} TagKind;

internal TagKind tag_kind(u32 container, u32 tag)
{
    switch (tag) {
    case TAG_EDIT: case TAG_SCNE: case TAG_VIEW: case TAG_CHAR: case TAG_NODE:
        return TAG_KIND_CONTAINER;
    case TAG_MSAA: case TAG_TONE: case TAG_BLOM: case TAG_RSMD: case TAG_RSCL: case TAG_RSFT: case TAG_RSFW: case TAG_RSFH:
    case TAG_DKLW: case TAG_DKRW: case TAG_DKBH: case TAG_DKBO: case TAG_LANG:
    case TAG_AUTO: case TAG_GZOP: case TAG_GZLC: case TAG_GZSN: case TAG_LAYT: case TAG_FOLW: case TAG_SELN: case TAG_SELO:
    case TAG_RMOT: case TAG_LOOK: case TAG_SWRD: case TAG_PATH:
        return TAG_KIND_U32;
    case TAG_BONE: // a u32 in SCNE (show bones)
        return TAG_KIND_U32;
    case TAG_YAW: case TAG_PTCH: case TAG_DIST: case TAG_ORBT: case TAG_PAN: case TAG_PLNT: case TAG_CTIM:
    case TAG_SPED: case TAG_FADE: case TAG_BLDW: case TAG_TURN: case TAG_POS: case TAG_ROT: case TAG_SCL:
    case TAG_COLR: case TAG_CFOV: case TAG_LCOL: case TAG_LINT: case TAG_EXPO: case TAG_BLMI:
        return TAG_KIND_F32;
    case TAG_CLIP: case TAG_BLND: case TAG_NAME: case TAG_ATCH:
        return TAG_KIND_STRING;
    }
    (void)container;
    return TAG_KIND_UNKNOWN;
}

internal void tag_text(u32 tag, char* out)
{
    for (u32 i = 0; i < 4; ++i) {
        u8 c = (u8)(tag >> (i * 8));
        out[i] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    out[4] = 0;
}

// One line for a field: its tag and its value, formatted by what the tag holds.
internal void field_text(NvChunk chunk, TagKind kind, char* out, umm capacity)
{
    char tag[5];
    tag_text(chunk.tag, tag);
    int at = snprintf(out, capacity, "%s  ", tag);
    for (u32 i = 0; i + 4 <= chunk.size && kind != TAG_KIND_STRING && kind != TAG_KIND_UNKNOWN && at < (int)capacity; i += 4) {
        u32 bits = nv_chunk_load_u32(chunk.data + i);
        if (kind == TAG_KIND_U32) {
            at += snprintf(out + at, capacity - (umm)at, "%u ", bits);
        } else {
            f32 value;
            memcpy(&value, &bits, 4);
            at += snprintf(out + at, capacity - (umm)at, "%g ", (f64)value);
        }
    }
    if (kind == TAG_KIND_STRING) {
        // At most 64 bytes, ending between characters.
        u32 shown = chunk.size < 64 ? chunk.size : 64;
        while (shown > 0 && shown < chunk.size && (chunk.data[shown] & 0xC0u) == 0x80u)
            --shown;
        at += snprintf(out + at, capacity - (umm)at, "\"%.*s\"", (int)shown, chunk.data);
    }
    if (kind == TAG_KIND_UNKNOWN) {
        for (u32 i = 0; i < chunk.size && i < 24 && at < (int)capacity; ++i)
            at += snprintf(out + at, capacity - (umm)at, "%02x ", chunk.data[i]);
        if (chunk.size > 24 && at < (int)capacity)
            snprintf(out + at, capacity - (umm)at, "... (%u bytes, unknown tag)", chunk.size);
        else if (at < (int)capacity)
            snprintf(out + at, capacity - (umm)at, "(unknown tag)");
    }
    if ((kind == TAG_KIND_U32 || kind == TAG_KIND_F32) && chunk.size % 4 && at < (int)capacity)
        snprintf(out + at, capacity - (umm)at, "(%u bytes: not whole numbers)", chunk.size);
}

internal void view_chunks(NvChunkReader* r, NvChunk parent, u32 parent_tag)
{
    NvChunk child = {0};
    u32 count = 0;
    while (nv_chunk_next(r, parent, &child)) {
        TagKind kind = tag_kind(parent_tag, child.tag);
        igPushID_Int((int)count++);
        if (kind == TAG_KIND_CONTAINER) {
            char tag[5];
            tag_text(child.tag, tag);
            // A node is easier to find by its name.
            char name[NV_NODE_NAME_MAX] = "";
            NvChunkReader peek = {0};
            if (child.tag == TAG_NODE)
                nv_chunk_read_string(&peek, child, TAG_NAME, name, sizeof(name));
            if (igTreeNode_Ptr((void*)(umm)child.data, "%s  %u bytes  %s", tag, child.size, name)) {
                view_chunks(r, child, child.tag);
                igTreePop();
            }
        } else {
            char line[256];
            field_text(child, kind, line, sizeof(line));
            igBulletText("%s", line);
        }
        igPopID();
    }
    if (r->failed)
        igTextColored((ImVec4_c){1.0f, 0.45f, 0.35f, 1.0f}, "Malformed from here: a chunk runs past its parent.");
}

internal void load_viewed(Sandbox* sandbox, b32 bad)
{
    umm mark = sandbox->scratch.used;
    NvFileData file = nv_storage_read(&sandbox->storage, bad ? SAVE_BAD_FILE : SAVE_FILE, &sandbox->scratch, SAVE_MAX_SIZE);
    if (file.ok)
        memcpy(sandbox->viewed, file.bytes, file.size);
    sandbox->viewed_size = (u32)file.size;
    sandbox->viewed_bad = bad;
    sandbox->scratch.used = mark;
}

internal void save_viewer(Sandbox* sandbox)
{
    b32 has_bad = nv_storage_exists(&sandbox->storage, SAVE_BAD_FILE);
    if (igButton(TL("Reload"), (ImVec2_c){0.0f, 0.0f}))
        load_viewed(sandbox, sandbox->viewed_bad);
    if (has_bad) {
        igSameLine(0.0f, -1.0f);
        if (igButton(sandbox->viewed_bad ? "Show " SAVE_FILE : "Show " SAVE_BAD_FILE, (ImVec2_c){0.0f, 0.0f}))
            load_viewed(sandbox, !sandbox->viewed_bad);
    }
    const char* file = sandbox->viewed_bad ? SAVE_BAD_FILE : SAVE_FILE;
    if (!sandbox->viewed_size) {
        igTextDisabled("%s is missing or empty.", file);
        return;
    }
    NvChunkFile opened = nv_chunk_file_open(sandbox->viewed, sandbox->viewed_size, SAVE_MAGIC, SAVE_VERSION);
    igText("%s: %u bytes, %s", file, sandbox->viewed_size, nv_chunk_file_status_name(opened.status));
    if (sandbox->viewed_size < NV_CHUNK_FILE_HEADER_SIZE)
        return;
    u32 magic = nv_chunk_load_u32(sandbox->viewed);
    char magic_text[5];
    tag_text(magic, magic_text);
    igText("Header: %s, version %u, %u bytes after it, checksum %08x", magic_text, nv_chunk_load_u32(sandbox->viewed + 4),
           nv_chunk_load_u32(sandbox->viewed + 8), nv_chunk_load_u32(sandbox->viewed + 12));
    // A damaged file is still walked as far as it goes, to show where it breaks.
    NvChunk root = opened.root;
    if (!opened.ok)
        root = (NvChunk){.size = sandbox->viewed_size - NV_CHUNK_FILE_HEADER_SIZE, .data = sandbox->viewed + NV_CHUNK_FILE_HEADER_SIZE};
    NvChunkReader r = {0};
    view_chunks(&r, root, 0);
}

void save_init(Sandbox* sandbox)
{
    sandbox->saved = NV_PUSH_ARRAY(&sandbox->permanent, SAVE_MAX_SIZE, u8);
    sandbox->next_save = NV_PUSH_ARRAY(&sandbox->permanent, SAVE_MAX_SIZE, u8);
    nv_storage_init(&sandbox->storage, SAVE_DIR);
    if (!sandbox->storage.available)
        return;

    umm mark = sandbox->scratch.used;
    NvFileData file = nv_storage_read(&sandbox->storage, SAVE_FILE, &sandbox->scratch, SAVE_MAX_SIZE);
    const char* problem = NULL;
    if (file.ok) {
        SaveLoad load = save_load(sandbox, file.bytes, (u32)file.size);
        problem = load.error;
    } else if (nv_storage_exists(&sandbox->storage, SAVE_FILE)) {
        problem = "empty, unreadable or too large";
    }
    sandbox->scratch.used = mark;
    if (problem) {
        // The sandbox starts as on a first visit. The file is set aside rather than deleted, so it can
        // be looked at (Show save), and the next autosave does not overwrite it.
        nv_storage_rename(&sandbox->storage, SAVE_FILE, SAVE_BAD_FILE);
        nv_storage_flush(&sandbox->storage);
        snprintf(sandbox->save_notice, sizeof(sandbox->save_notice),
                 T("The save could not be loaded: %s. The sandbox started fresh and kept it as %s."), problem,
                 SAVE_BAD_FILE);
        nv_log(NV_LOG_WARNING, "sandbox", "%s", sandbox->save_notice);
    }
    sandbox->viewed = NV_PUSH_ARRAY(&sandbox->permanent, SAVE_MAX_SIZE, u8);

    // What is on screen now counts as saved, so an unchanged state is not written again.
    sandbox->saved_size = save_write(sandbox, sandbox->saved, SAVE_MAX_SIZE).size;
    sandbox->last_save_check = nv_time_seconds();
    nv_window_on_hidden(&sandbox->window, save_on_hidden, sandbox);
}

void save_update(Sandbox* sandbox)
{
    f64 now = nv_time_seconds();
    if (!sandbox->autosave || now - sandbox->last_save_check < AUTOSAVE_SECONDS)
        return;
    sandbox->last_save_check = now;
    save_now(sandbox, 0);
}

void save_reset_popup(Sandbox* sandbox)
{
    if (igBeginPopupModal(TL("Reset everything?"), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        igText(T("Delete the save and start over as on a first visit?"));
        if (igButton(TL("Reset"), (ImVec2_c){0.0f, 0.0f})) {
            save_reset(sandbox);
            igCloseCurrentPopup();
        }
        igSameLine(0.0f, -1.0f);
        if (igButton(TL("Cancel"), (ImVec2_c){0.0f, 0.0f}))
            igCloseCurrentPopup();
        igEndPopup();
    }
}

void save_show_viewer(Sandbox* sandbox)
{
    sandbox->show_save = true;
    if (sandbox->autosave)
        save_now(sandbox, 0); // so the viewer shows the current state
    load_viewed(sandbox, 0);
}

void save_ui(Sandbox* sandbox)
{
    search_section(sandbox, "Autosave");
    if (!sandbox->storage.available) {
        if (search_group(sandbox, "Autosave", "save storage unavailable"))
            igTextWrapped(T("Browser storage is unavailable here (a private window may refuse it), so nothing is saved."));
        return;
    }
    // The setting is part of the save, so turning autosave off is saved too.
    if (search_group(sandbox, "Autosave", "save now storage")) {
        if (igCheckbox(TL("Autosave"), &sandbox->autosave))
            save_now(sandbox, 1);
        igSameLine(0.0f, -1.0f);
        if (igButton(TL("Save now"), (ImVec2_c){0.0f, 0.0f}))
            save_now(sandbox, 1);
    }
    // What the save did is no setting: shown only without a search.
    if (search_plain(sandbox)) {
        if (sandbox->saved_at > 0.0)
            igText(T("Saved %.0f s ago (%u bytes)"), nv_time_seconds() - sandbox->saved_at, sandbox->saved_size);
        else
            igTextDisabled(T("Not saved yet this visit."));
        const char* error = nv_storage_error(&sandbox->storage);
        if (error[0])
            igTextWrapped(T("Browser storage: %s"), error);
        if (sandbox->save_notice[0])
            igTextColored((ImVec4_c){1.0f, 0.75f, 0.35f, 1.0f}, "%s", sandbox->save_notice);
    }

    if (search_group(sandbox, "Reset", "show save viewer delete start over")) {
        if (igButton(TL("Reset"), (ImVec2_c){0.0f, 0.0f}))
            igOpenPopup_Str(TL("Reset everything?"), 0);
        save_reset_popup(sandbox);
        igSameLine(0.0f, -1.0f);
        if (igCheckbox(TL("Show save"), &sandbox->show_save) && sandbox->show_save)
            save_show_viewer(sandbox);
        if (sandbox->show_save)
            save_viewer(sandbox);
    }
}
