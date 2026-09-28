// The autosave's contents: the app state as chunks (nv/chunk.h), and loading it back. What is
// saved, tag by tag, is listed in docs/specs/save.md; keep the two in step.

#include "app.h"

#include <nv/chunk.h>

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

// Returns the path's length, or 0 if it is deeper than SAVE_MAX_PATH.
internal u32 path_of(NvScene* scene, u32 index, u32* path)
{
    u32 length = depth_of(scene, index) + 1;
    if (length > SAVE_MAX_PATH)
        return 0;
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
    return length;
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
    u32 hash = 2166136261u;
    for (u32 index = scene->first_root; index; index = next_in_tree(scene, index)) {
        hash = (hash ^ depth_of(scene, index)) * 16777619u;
        for (const char* c = scene->nodes[index].name; *c; ++c)
            hash = (hash ^ (u8)*c) * 16777619u;
        hash = (hash ^ 0xFFu) * 16777619u; // ends the name
    }
    return hash;
}

//
// Writing
//

internal void write_view(NvChunkWriter* w, NvScene* scene, const SceneView* view)
{
    nv_chunk_begin(w, TAG_VIEW);
    nv_chunk_f32(w, TAG_YAW, view->camera_yaw);
    nv_chunk_f32(w, TAG_PTCH, view->camera_pitch);
    nv_chunk_f32(w, TAG_DIST, view->camera_distance);
    nv_chunk_u32(w, TAG_FOLW, view->follow_selection);
    nv_chunk_f32s(w, TAG_ORBT, &view->orbit_point.x, 3);
    nv_chunk_f32s(w, TAG_PAN, &view->pan.x, 3);
    u32 path[SAVE_MAX_PATH];
    u32 length = view->selected.index ? path_of(scene, view->selected.index, path) : 0;
    nv_chunk_u32s(w, TAG_SELN, path, length);
    nv_chunk_end(w);
}

// The character's fields. The clip's time (CTIM) is no longer written: the saved state is the edit
// state, which has no running time, and a run starts every clip from its start
// (docs/specs/play.md). Older saves that have it still load.
internal void write_character_fields(NvChunkWriter* w, App* app, b32 undo)
{
    (void)undo;
    NvAnimator* animator = nv_anim_get(app->animator);
    NvAnimLayer* layer = &animator->layers[0];
    // Mid-jump, the clip the jump lands back into stands for it.
    NvClipId clip = app->jump != JUMP_NONE ? app->jump_return : layer->clip;

    if (clip.index)
        nv_chunk_string(w, TAG_CLIP, nv_anim_clip_name(app_regular_clip(app, clip)));
    nv_chunk_f32(w, TAG_SPED, layer->speed);
    nv_chunk_f32(w, TAG_FADE, app->fade_seconds);
    if (app->blend_clip >= 0 && (u32)app->blend_clip < app->clip_count)
        nv_chunk_string(w, TAG_BLND, nv_anim_clip_name(app->clips[app->blend_clip]));
    nv_chunk_f32(w, TAG_BLDW, app->blend_weight);
    nv_chunk_u32(w, TAG_RMOT, app->root_motion);
    nv_chunk_f32(w, TAG_TURN, app->turn_rate);
    nv_chunk_u32(w, TAG_LOOK, app->look_at);
    nv_chunk_u32(w, TAG_SWRD, app->show_sword);
}

internal void write_character(NvChunkWriter* w, App* app)
{
    nv_chunk_begin(w, TAG_CHAR);
    write_character_fields(w, app, 0);
    nv_chunk_end(w);
}

#define DRIVEN_POSITION (1u << 0)
#define DRIVEN_ROTATION (1u << 1)

// Which of a showcase node's transform the app rewrites every frame. Undo leaves these out, or
// every frame would look like an edit.
internal u32 driven_fields(App* app, u32 index)
{
    if (index == app->views[SCENE_SHOWCASE].camera.index || index == app->target.index)
        return DRIVEN_POSITION | DRIVEN_ROTATION;
    if (index == app->planet.index || index == app->moon.index) // they spin
        return DRIVEN_ROTATION;
    if (index == app->character.root.index && app->root_motion) // it walks
        return DRIVEN_POSITION | DRIVEN_ROTATION;
    return 0;
}

// A node's fields. `undo` leaves out what the app drives.
internal void write_node_fields(NvChunkWriter* w, App* app, u32 index, b32 undo)
{
    NvNode* node = &app->scene->nodes[index];
    u32 driven = undo ? driven_fields(app, index) : 0;
    nv_chunk_string(w, TAG_NAME, node->name);
    if (!(driven & DRIVEN_POSITION))
        nv_chunk_f32s(w, TAG_POS, &node->position.x, 3);
    if (!(driven & DRIVEN_ROTATION))
        nv_chunk_f32s(w, TAG_ROT, &node->rotation.x, 4);
    nv_chunk_f32s(w, TAG_SCL, &node->scale.x, 3);
    if (node->material.index)
        nv_chunk_f32s(w, TAG_COLR, app->renderer.materials[node->material.index].desc.base_color, 4);
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

internal void write_node(NvChunkWriter* w, App* app, u32 index)
{
    u32 path[SAVE_MAX_PATH];
    u32 length = path_of(app->scene, index, path);
    if (!length)
        return;
    nv_chunk_begin(w, TAG_NODE);
    nv_chunk_u32s(w, TAG_PATH, path, length);
    write_node_fields(w, app, index, 0);
    nv_chunk_end(w);
}

u32 save_write(App* app, void* buffer, u32 capacity)
{
    NvChunkWriter w;
    nv_chunk_writer_init(&w, buffer, capacity);
    nv_chunk_file_begin(&w, SAVE_MAGIC, SAVE_VERSION);

    nv_chunk_begin(&w, TAG_EDIT);
    nv_chunk_u32(&w, TAG_AUTO, app->autosave);
    nv_chunk_u32(&w, TAG_GZOP, (u32)app->gizmo_operation);
    nv_chunk_u32(&w, TAG_GZLC, app->gizmo_local);
    nv_chunk_u32(&w, TAG_GZSN, app->gizmo_snap);
    nv_chunk_end(&w);

    nv_chunk_begin(&w, TAG_SCNE);
    nv_chunk_u32(&w, TAG_LAYT, app->scene_layout);
    write_view(&w, app->scene, &app->views[SCENE_SHOWCASE]);
    // The orbit angle is written as 0: like the clip time, it only runs while playing.
    f32 planet[2] = {app->orbit_speed, 0.0f};
    nv_chunk_f32s(&w, TAG_PLNT, planet, 2);
    nv_chunk_u32(&w, TAG_BONE, app->show_bones);
    write_character(&w, app);
    for (u32 index = app->scene->first_root; index; index = next_in_tree(app->scene, index))
        write_node(&w, app, index);
    nv_chunk_end(&w);

    return nv_chunk_file_end(&w);
}

//
// Reading
//
// One function reads the save twice: a dry run with `apply` off that only checks it, then, if
// that passed, again with `apply` on. Both passes read the same fields, so a save that passes the
// dry run cannot fail halfway through changing the app.

internal b32 read_bool(NvChunkReader* r, NvChunk parent, u32 tag, bool* out)
{
    u32 value;
    if (!nv_chunk_read_u32s(r, parent, tag, &value, 1))
        return 0;
    *out = value != 0;
    return 1;
}

internal f32 clamp(f32 v, f32 lo, f32 hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

internal void read_edit(NvChunkReader* r, NvChunk edit, App* app, b32 apply)
{
    bool autosave = app->autosave, local = app->gizmo_local, snap = app->gizmo_snap;
    u32 operation = (u32)app->gizmo_operation;
    read_bool(r, edit, TAG_AUTO, &autosave);
    nv_chunk_read_u32s(r, edit, TAG_GZOP, &operation, 1);
    read_bool(r, edit, TAG_GZLC, &local);
    read_bool(r, edit, TAG_GZSN, &snap);
    if (!apply)
        return;
    app->autosave = autosave;
    app->gizmo_operation = operation <= GIZMO_SCALE ? (GizmoOperation)operation : GIZMO_MOVE;
    app->gizmo_local = local;
    app->gizmo_snap = snap;
}

internal void read_view(NvChunkReader* r, NvChunk parent, NvScene* scene, SceneView* view, b32 apply, b32 nodes_match)
{
    NvChunk chunk = nv_chunk_find(r, parent, TAG_VIEW);
    if (!chunk.data)
        return;
    SceneView v = *view;
    nv_chunk_read_f32s(r, chunk, TAG_YAW, &v.camera_yaw, 1);
    nv_chunk_read_f32s(r, chunk, TAG_PTCH, &v.camera_pitch, 1);
    nv_chunk_read_f32s(r, chunk, TAG_DIST, &v.camera_distance, 1);
    read_bool(r, chunk, TAG_FOLW, &v.follow_selection);
    nv_chunk_read_f32s(r, chunk, TAG_ORBT, &v.orbit_point.x, 3);
    nv_chunk_read_f32s(r, chunk, TAG_PAN, &v.pan.x, 3);
    u32 path[SAVE_MAX_PATH];
    u32 length = 0;
    b32 has_selection = nv_chunk_read_u32_list(r, chunk, TAG_SELN, path, SAVE_MAX_PATH, &length);
    if (!apply)
        return;
    v.camera_pitch = clamp(v.camera_pitch, CAMERA_MIN_PITCH, CAMERA_MAX_PITCH);
    v.camera_distance = clamp(v.camera_distance, CAMERA_MIN_DISTANCE, CAMERA_MAX_DISTANCE);
    // A selection path only means something while the tree is the one it was saved from.
    if (has_selection && nodes_match) {
        u32 index = length ? node_at(scene, path, length) : 0;
        v.selected = index ? (NvNodeId){index, scene->nodes[index].gen} : (NvNodeId){0};
    }
    v.panned_for = v.selected;
    *view = v;
}

internal NvClipId clip_named(App* app, const char* name)
{
    for (u32 i = 0; i < app->clip_count; ++i) {
        if (strcmp(nv_anim_clip_name(app->clips[i]), name) == 0)
            return app->clips[i];
    }
    return (NvClipId){0};
}

// Applies only the fields present, so it also restores an undo snapshot (which has no time).
internal void read_character_fields(NvChunkReader* r, NvChunk chunk, App* app, b32 apply)
{
    NvAnimator* animator = nv_anim_get(app->animator);
    char clip_name[SAVE_MAX_CLIP_NAME] = "Idle_Loop";
    char blend_name[SAVE_MAX_CLIP_NAME] = "";
    f32 time = animator->layers[0].time;
    f32 speed = animator->layers[0].speed;
    f32 fade = app->fade_seconds, blend_weight = app->blend_weight, turn = app->turn_rate;
    bool root_motion = app->root_motion, look_at = app->look_at, sword = app->show_sword;
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

    app->root_motion = root_motion;
    app->turn_rate = turn;
    app->look_at = look_at;
    app->show_sword = sword;
    app->blend_weight = clamp(blend_weight, 0.0f, 1.0f);
    for (u32 i = 0; i < app->clip_count; ++i) {
        if (strcmp(nv_anim_clip_name(app->clips[i]), blend_name) == 0)
            app->blend_clip = (s32)i;
    }
    if (has_clip) {
        NvClipId clip = clip_named(app, clip_name);
        if (!clip.index)
            clip = clip_named(app, "Idle_Loop");
        // Straight into the clip, without a crossfade from whatever played before. The same clip
        // keeps playing where it is; root motion on or off picks its copy.
        app->fade_seconds = 0.0f;
        app_play(app, clip);
    }
    app->fade_seconds = clamp(fade, 0.0f, 1.0f);
    if (has_time)
        animator->layers[0].time = time;
    animator->layers[0].speed = speed;
}

internal void read_character(NvChunkReader* r, NvChunk parent, App* app, b32 apply)
{
    NvChunk chunk = nv_chunk_find(r, parent, TAG_CHAR);
    if (chunk.data)
        read_character_fields(r, chunk, app, apply);
}

// Applies only the fields present to node `index` (0: only checks them).
internal void read_node_fields(NvChunkReader* r, NvChunk chunk, App* app, u32 index, b32 apply)
{
    NvScene* scene = app->scene;
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
        nv_renderer_set_material_color(&app->renderer, node->material, color);
    if (has_joint && node->attach.animator.index) {
        s32 found = nv_anim_find_joint(nv_anim_get(node->attach.animator)->skeleton, joint);
        if (found >= 0)
            node->attach.joint = (u32)found;
    }
    if (node->camera.projection)
        node->camera.fov_y = clamp(n.camera.fov_y, 1.0f * NV_PI / 180.0f, 179.0f * NV_PI / 180.0f);
    if (node->light.type) {
        node->light.color = n.light.color;
        node->light.intensity = n.light.intensity;
    }
}

internal void read_node(NvChunkReader* r, NvChunk chunk, App* app, b32 apply)
{
    u32 path[SAVE_MAX_PATH];
    u32 length = 0;
    if (!nv_chunk_read_u32_list(r, chunk, TAG_PATH, path, SAVE_MAX_PATH, &length) || !length)
        return;
    read_node_fields(r, chunk, app, node_at(app->scene, path, length), apply);
}

internal void read_scene(NvChunkReader* r, NvChunk scene, App* app, b32 apply, u32 parts)
{
    b32 apply_scene = apply && (parts & SAVE_PART_SCENE);
    u32 layout = 0;
    nv_chunk_read_u32s(r, scene, TAG_LAYT, &layout, 1);
    b32 nodes_match = layout == app->scene_layout;

    f32 planet[2] = {app->orbit_speed, app->orbit_angle};
    bool bones = app->show_bones;
    nv_chunk_read_f32s(r, scene, TAG_PLNT, planet, 2);
    read_bool(r, scene, TAG_BONE, &bones);
    if (apply_scene) {
        app->orbit_speed = planet[0];
        app->orbit_angle = planet[1];
        app->show_bones = bones;
    }
    read_character(r, scene, app, apply_scene);
    // Nodes are read even when they will not be applied, so a damaged one still fails the load.
    NvChunk child = {0};
    while (nv_chunk_next(r, scene, &child)) {
        if (child.tag == TAG_NODE)
            read_node(r, child, app, apply_scene && nodes_match);
    }
    // After the nodes, so a selection's path finds the tree as saved.
    read_view(r, scene, app->scene, &app->views[SCENE_SHOWCASE], apply && (parts & SAVE_PART_VIEW), nodes_match);
}

internal b32 read_state(NvChunk root, App* app, b32 apply, u32 parts)
{
    NvChunkReader r = {0};
    NvChunk edit = nv_chunk_find(&r, root, TAG_EDIT);
    if (edit.data)
        read_edit(&r, edit, app, apply && (parts & SAVE_PART_EDITOR));
    NvChunk scene = nv_chunk_find(&r, root, TAG_SCNE);
    if (scene.data)
        read_scene(&r, scene, app, apply, parts);
    // Walk the whole top level too, so a malformed chunk after the known ones is caught.
    NvChunk child = {0};
    while (nv_chunk_next(&r, root, &child)) {
    }
    return !r.failed;
}

const char* save_load_parts(App* app, const void* bytes, u32 size, u32 parts)
{
    u32 version = 0;
    NvChunk root;
    NvChunkFileStatus status = nv_chunk_file_open(bytes, size, SAVE_MAGIC, SAVE_VERSION, &version, &root);
    if (status != NV_CHUNK_FILE_OK)
        return nv_chunk_file_status_name(status);
    if (!read_state(root, app, 0, parts))
        return "malformed";
    read_state(root, app, 1, parts);
    return NULL;
}

const char* save_load(App* app, const void* bytes, u32 size)
{
    return save_load_parts(app, bytes, size, SAVE_PART_ALL);
}

b32 save_round_trip_matches(App* app)
{
    NvArena* scratch = &app->scratch;
    umm mark = scratch->used;
    u8* first = NV_PUSH_ARRAY(scratch, SAVE_MAX_SIZE, u8);
    u8* second = NV_PUSH_ARRAY(scratch, SAVE_MAX_SIZE, u8);
    u32 first_size = save_write(app, first, SAVE_MAX_SIZE);
    b32 matches = first_size && !save_load(app, first, first_size);
    u32 second_size = matches ? save_write(app, second, SAVE_MAX_SIZE) : 0;
    matches = matches && second_size == first_size && memcmp(first, second, first_size) == 0;
    scratch->used = mark;
    return matches;
}

//
// Undo scopes (docs/specs/undo.md): the undoable fields of one part of the showcase, as bare
// chunks without a file header, written and read with the same code as the save.
//

u32 save_driven_fields(App* app, u32 node)
{
    return driven_fields(app, node);
}

u32 save_write_scope(App* app, SaveScope scope, u32 node, void* buffer, u32 capacity)
{
    NvChunkWriter w;
    nv_chunk_writer_init(&w, buffer, capacity);
    switch (scope) {
    case SAVE_SCOPE_NODE:
        write_node_fields(&w, app, node, 1);
        break;
    case SAVE_SCOPE_CHARACTER:
        write_character_fields(&w, app, 1);
        break;
    case SAVE_SCOPE_SCENE:
        nv_chunk_f32(&w, TAG_ORBS, app->orbit_speed);
        nv_chunk_u32(&w, TAG_BONE, app->show_bones);
        break;
    case SAVE_SCOPE_COUNT:
        NV_INVALID_CODE_PATH;
        break;
    }
    return w.overflow ? 0 : w.size;
}

internal b32 read_scope(NvChunk chunk, App* app, SaveScope scope, u32 node, b32 apply)
{
    NvChunkReader r = {0};
    switch (scope) {
    case SAVE_SCOPE_NODE:
        read_node_fields(&r, chunk, app, node, apply);
        break;
    case SAVE_SCOPE_CHARACTER:
        read_character_fields(&r, chunk, app, apply);
        break;
    case SAVE_SCOPE_SCENE: {
        f32 speed = app->orbit_speed;
        bool bones = app->show_bones;
        nv_chunk_read_f32s(&r, chunk, TAG_ORBS, &speed, 1);
        read_bool(&r, chunk, TAG_BONE, &bones);
        if (apply) {
            app->orbit_speed = speed;
            app->show_bones = bones;
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

b32 save_apply_scope(App* app, SaveScope scope, u32 node, const void* bytes, u32 size)
{
    NvChunk chunk = {.size = size, .data = bytes};
    if (!read_scope(chunk, app, scope, node, 0))
        return 0;
    read_scope(chunk, app, scope, node, 1);
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

void save_now(App* app, b32 force)
{
    if (!app->storage.available || app->save_stopped)
        return;
    u32 size = save_write(app, app->next_save, SAVE_MAX_SIZE);
    if (!size) {
        snprintf(app->save_notice, sizeof(app->save_notice), "Not saved: the state is larger than %u KB.",
                 (u32)(SAVE_MAX_SIZE / 1024));
        return;
    }
    if (!force && size == app->saved_size && memcmp(app->next_save, app->saved, size) == 0)
        return;
    if (!nv_storage_write(&app->storage, SAVE_FILE, app->next_save, size)) {
        snprintf(app->save_notice, sizeof(app->save_notice), "Not saved: writing %s failed.", SAVE_FILE);
        return;
    }
    nv_storage_flush(&app->storage);
    u8* swap = app->saved;
    app->saved = app->next_save;
    app->next_save = swap;
    app->saved_size = size;
    app->saved_at = nv_time_seconds();
}

// Frames stop while the page is hidden, and it may be closing: save now.
internal void save_on_hidden(void* userdata)
{
    App* app = userdata;
    if (app->autosave)
        save_now(app, 0);
}

// Starts over as on a first visit: deletes the save and reloads once IndexedDB has caught up.
// Animators cannot be removed, so the showcase cannot be rebuilt in place.
internal void save_reset(App* app)
{
    // IMPORTANT: The reload hides the page, which would otherwise save the state just deleted.
    app->save_stopped = 1;
    nv_storage_remove(&app->storage, SAVE_FILE);
    nv_storage_remove(&app->storage, SAVE_BAD_FILE);
    nv_storage_flush_then_reload(&app->storage);
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
    case TAG_AUTO: case TAG_GZOP: case TAG_GZLC: case TAG_GZSN: case TAG_LAYT: case TAG_FOLW: case TAG_SELN:
    case TAG_RMOT: case TAG_LOOK: case TAG_SWRD: case TAG_PATH:
        return TAG_KIND_U32;
    case TAG_BONE: // a u32 in SCNE (show bones)
        return TAG_KIND_U32;
    case TAG_YAW: case TAG_PTCH: case TAG_DIST: case TAG_ORBT: case TAG_PAN: case TAG_PLNT: case TAG_CTIM:
    case TAG_SPED: case TAG_FADE: case TAG_BLDW: case TAG_TURN: case TAG_POS: case TAG_ROT: case TAG_SCL:
    case TAG_COLR: case TAG_CFOV: case TAG_LCOL: case TAG_LINT:
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
    if (kind == TAG_KIND_STRING)
        at += snprintf(out + at, capacity - (umm)at, "\"%.*s\"", (int)(chunk.size < 64 ? chunk.size : 64), chunk.data);
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

internal void load_viewed(App* app, b32 bad)
{
    umm mark = app->scratch.used;
    u8* bytes = NULL;
    u32 size = nv_storage_read(&app->storage, bad ? SAVE_BAD_FILE : SAVE_FILE, &app->scratch, SAVE_MAX_SIZE, &bytes);
    if (size)
        memcpy(app->viewed, bytes, size);
    app->viewed_size = size;
    app->viewed_bad = bad;
    app->scratch.used = mark;
}

internal void save_viewer(App* app)
{
    b32 has_bad = nv_storage_exists(&app->storage, SAVE_BAD_FILE);
    if (igButton("Reload", (ImVec2_c){0.0f, 0.0f}))
        load_viewed(app, app->viewed_bad);
    if (has_bad) {
        igSameLine(0.0f, -1.0f);
        if (igButton(app->viewed_bad ? "Show " SAVE_FILE : "Show " SAVE_BAD_FILE, (ImVec2_c){0.0f, 0.0f}))
            load_viewed(app, !app->viewed_bad);
    }
    const char* file = app->viewed_bad ? SAVE_BAD_FILE : SAVE_FILE;
    if (!app->viewed_size) {
        igTextDisabled("%s is missing or empty.", file);
        return;
    }
    u32 version = 0;
    NvChunk root;
    NvChunkFileStatus status = nv_chunk_file_open(app->viewed, app->viewed_size, SAVE_MAGIC, SAVE_VERSION, &version, &root);
    igText("%s: %u bytes, %s", file, app->viewed_size, nv_chunk_file_status_name(status));
    if (app->viewed_size < NV_CHUNK_FILE_HEADER_SIZE)
        return;
    u32 magic = nv_chunk_load_u32(app->viewed);
    char magic_text[5];
    tag_text(magic, magic_text);
    igText("Header: %s, version %u, %u bytes after it, checksum %08x", magic_text, nv_chunk_load_u32(app->viewed + 4),
           nv_chunk_load_u32(app->viewed + 8), nv_chunk_load_u32(app->viewed + 12));
    // A damaged file is still walked as far as it goes, to show where it breaks.
    if (status != NV_CHUNK_FILE_OK)
        root = (NvChunk){.size = app->viewed_size - NV_CHUNK_FILE_HEADER_SIZE, .data = app->viewed + NV_CHUNK_FILE_HEADER_SIZE};
    NvChunkReader r = {0};
    view_chunks(&r, root, 0);
}

void save_init(App* app)
{
    app->saved = NV_PUSH_ARRAY(&app->permanent, SAVE_MAX_SIZE, u8);
    app->next_save = NV_PUSH_ARRAY(&app->permanent, SAVE_MAX_SIZE, u8);
    nv_storage_init(&app->storage, SAVE_DIR);
    if (!app->storage.available)
        return;

    umm mark = app->scratch.used;
    u8* bytes = NULL;
    u32 size = nv_storage_read(&app->storage, SAVE_FILE, &app->scratch, SAVE_MAX_SIZE, &bytes);
    const char* problem = NULL;
    if (size)
        problem = save_load(app, bytes, size);
    else if (nv_storage_exists(&app->storage, SAVE_FILE))
        problem = "empty, unreadable or too large";
    app->scratch.used = mark;
    if (problem) {
        // The app starts as on a first visit. The file is set aside rather than deleted, so it can
        // be looked at (Show save), and the next autosave does not overwrite it.
        nv_storage_rename(&app->storage, SAVE_FILE, SAVE_BAD_FILE);
        nv_storage_flush(&app->storage);
        snprintf(app->save_notice, sizeof(app->save_notice),
                 "The save could not be loaded: %s. The app started fresh and kept it as %s.", problem,
                 SAVE_BAD_FILE);
    }
    app->viewed = NV_PUSH_ARRAY(&app->permanent, SAVE_MAX_SIZE, u8);

    // What is on screen now counts as saved, so an unchanged state is not written again.
    app->saved_size = save_write(app, app->saved, SAVE_MAX_SIZE);
    app->last_save_check = nv_time_seconds();
    nv_window_on_hidden(&app->window, save_on_hidden, app);
}

void save_update(App* app)
{
    f64 now = nv_time_seconds();
    if (!app->autosave || now - app->last_save_check < AUTOSAVE_SECONDS)
        return;
    app->last_save_check = now;
    save_now(app, 0);
}

void save_ui(App* app)
{
    igSeparatorText("Autosave");
    if (!app->storage.available) {
        igTextWrapped("Browser storage is unavailable here (a private window may refuse it), so nothing is saved.");
        return;
    }
    // The setting is part of the save, so turning autosave off is saved too.
    if (igCheckbox("Autosave", &app->autosave))
        save_now(app, 1);
    igSameLine(0.0f, -1.0f);
    if (igButton("Save now", (ImVec2_c){0.0f, 0.0f}))
        save_now(app, 1);
    if (app->saved_at > 0.0)
        igText("Saved %.0f s ago (%u bytes)", nv_time_seconds() - app->saved_at, app->saved_size);
    else
        igTextDisabled("Not saved yet this visit.");
    const char* error = nv_storage_error(&app->storage);
    if (error[0])
        igTextWrapped("Browser storage: %s", error);
    if (app->save_notice[0])
        igTextColored((ImVec4_c){1.0f, 0.75f, 0.35f, 1.0f}, "%s", app->save_notice);

    if (igButton("Reset", (ImVec2_c){0.0f, 0.0f}))
        igOpenPopup_Str("Reset everything?", 0);
    if (igBeginPopupModal("Reset everything?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        igText("Delete the save and start over as on a first visit?");
        if (igButton("Reset", (ImVec2_c){0.0f, 0.0f})) {
            save_reset(app);
            igCloseCurrentPopup();
        }
        igSameLine(0.0f, -1.0f);
        if (igButton("Cancel", (ImVec2_c){0.0f, 0.0f}))
            igCloseCurrentPopup();
        igEndPopup();
    }
    igSameLine(0.0f, -1.0f);
    if (igCheckbox("Show save", &app->show_save) && app->show_save) {
        if (app->autosave)
            save_now(app, 0); // so the viewer shows the current state
        load_viewed(app, 0);
    }
    if (app->show_save)
        save_viewer(app);
}
