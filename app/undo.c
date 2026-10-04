// Undo and redo for the showcase (docs/specs/undo.md). Nothing records edits as they happen:
// whenever the editor is idle, each scope's undoable fields are written with the save code and
// compared with what was last committed. A difference is one step, however many frames the edit
// took (a whole gizmo drag, a slider drag, a name typed).

#include "app.h"

#include <cimguizmo.h>

#include <stdio.h>
#include <string.h>

internal UndoStep* step_at(Undo* undo, u32 i)
{
    return &undo->steps[(undo->first + i) % UNDO_MAX_STEPS];
}

// The nodes the Node scope follows: the showcase's selection, the primary first
// (docs/specs/selection.md).
internal u32 selected_nodes(App* app, NvNodeId* out)
{
    SceneView* view = &app->views[SCENE_SHOWCASE];
    u32 count = selection_count(view);
    for (u32 i = 0; i < count; ++i)
        out[i] = selection_get(view, i);
    return count;
}

internal b32 same_nodes(const NvNodeId* a, u32 a_count, const NvNodeId* b, u32 b_count)
{
    if (a_count != b_count)
        return 0;
    for (u32 i = 0; i < a_count; ++i) {
        if (a[i].index != b[i].index || a[i].gen != b[i].gen)
            return 0;
    }
    return 1;
}

internal u32 write_scope(App* app, SaveScope scope, const NvNodeId* nodes, u32 node_count, u8* out)
{
    if (scope == SAVE_SCOPE_NODE && !node_count)
        return 0;
    NvChunkWritten written = save_write_scope(app, scope, nodes, node_count, out, UNDO_MAX_BYTES);
    NV_ASSERT(written.ok); // a scope that does not fit in UNDO_MAX_BYTES is not undoable
    return written.size;
}

internal void commit(App* app, SaveScope scope)
{
    Undo* undo = &app->undo;
    if (scope == SAVE_SCOPE_NODE) {
        undo->committed_node_count = selected_nodes(app, undo->committed_nodes);
        for (u32 i = 0; i < undo->committed_node_count; ++i)
            undo->committed_driven[i] = save_driven_fields(app, undo->committed_nodes[i].index);
    }
    undo->committed_size[scope] =
        write_scope(app, scope, undo->committed_nodes, undo->committed_node_count, undo->committed[scope]);
}

internal void commit_all(App* app)
{
    for (u32 scope = 0; scope < SAVE_SCOPE_COUNT; ++scope)
        commit(app, (SaveScope)scope);
}

// The first field whose bytes differ between two snapshots of a scope, as a tag.
internal u32 first_difference(const u8* before, u32 before_size, const u8* after, u32 after_size)
{
    NvChunk a = {.size = before_size, .data = before};
    NvChunk b = {.size = after_size, .data = after};
    NvChunkReader r = {0};
    NvChunk field = {0};
    while (nv_chunk_next(&r, b, &field)) {
        NvChunk old = nv_chunk_find(&r, a, field.tag);
        if (!old.data || old.size != field.size || memcmp(old.data, field.data, field.size) != 0)
            return field.tag;
    }
    field = (NvChunk){0};
    while (nv_chunk_next(&r, a, &field)) {
        if (!nv_chunk_find(&r, b, field.tag).data)
            return field.tag;
    }
    return 0;
}

// The label of a Node step: "moon Position" when one node changed, "3 nodes Position" for more.
internal void node_step_label(App* app, UndoStep* step)
{
    NvChunk before = {.size = step->before_size, .data = step->before};
    NvChunk after = {.size = step->after_size, .data = step->after};
    NvChunkReader rb = {0}, ra = {0};
    NvChunk b = {0}, a = {0};
    u32 changed = 0, first = 0, field = 0;
    for (u32 i = 0; nv_chunk_next(&rb, before, &b) && nv_chunk_next(&ra, after, &a); ++i) {
        u32 tag = first_difference(b.data, b.size, a.data, a.size);
        if (!tag)
            continue;
        if (!changed++) {
            first = i;
            field = tag;
        }
    }
    const char* label = save_field_label(field);
    if (changed > 1)
        snprintf(step->label, sizeof(step->label), "%u nodes %s", changed, label);
    else
        snprintf(step->label, sizeof(step->label), "%s %s", app->scene->nodes[step->nodes[first].index].name, label);
}

internal void push_step(App* app, SaveScope scope, const u8* before, u32 before_size, const u8* after, u32 after_size)
{
    Undo* undo = &app->undo;
    undo->count = undo->done; // a new edit drops what could have been redone
    if (undo->count == UNDO_MAX_STEPS) {
        undo->first = (undo->first + 1) % UNDO_MAX_STEPS;
        --undo->count;
    }
    UndoStep* step = step_at(undo, undo->count);
    step->scope = scope;
    step->node_count = scope == SAVE_SCOPE_NODE ? undo->committed_node_count : 0;
    memcpy(step->nodes, undo->committed_nodes, step->node_count * sizeof(NvNodeId));
    memcpy(step->before, before, before_size);
    step->before_size = before_size;
    memcpy(step->after, after, after_size);
    step->after_size = after_size;
    const char* field = save_field_label(first_difference(before, before_size, after, after_size));
    if (scope == SAVE_SCOPE_NODE)
        node_step_label(app, step);
    else if (scope == SAVE_SCOPE_CHARACTER)
        snprintf(step->label, sizeof(step->label), "Character %s", field);
    else
        snprintf(step->label, sizeof(step->label), "%s", field);
    nv_utf8_trim(step->label); // a long Hangul name may have been cut
    ++undo->count;
    undo->done = undo->count;
}

// Takes a step for every scope that changed since its last commit.
internal void take_steps(App* app)
{
    Undo* undo = &app->undo;
    // Other nodes selected, or their driven fields changed (root motion turned on or off): their
    // bytes as they are become the starting point, without a step.
    NvNodeId selected[SELECTION_MAX];
    u32 selected_count = selected_nodes(app, selected);
    b32 recommit = !same_nodes(selected, selected_count, undo->committed_nodes, undo->committed_node_count);
    for (u32 i = 0; i < selected_count && !recommit; ++i)
        recommit = save_driven_fields(app, selected[i].index) != undo->committed_driven[i];
    if (recommit)
        commit(app, SAVE_SCOPE_NODE);

    for (u32 s = 0; s < SAVE_SCOPE_COUNT; ++s) {
        SaveScope scope = (SaveScope)s;
        u32 size = write_scope(app, scope, undo->committed_nodes, undo->committed_node_count, undo->current);
        if (size == undo->committed_size[scope] && memcmp(undo->current, undo->committed[scope], size) == 0)
            continue;
        push_step(app, scope, undo->committed[scope], undo->committed_size[scope], undo->current, size);
        memcpy(undo->committed[scope], undo->current, size);
        undo->committed_size[scope] = size;
    }
}

internal void apply(App* app, UndoStep* step, b32 redo)
{
    const u8* bytes = redo ? step->after : step->before;
    u32 size = redo ? step->after_size : step->before_size;
    if (step->scope == SAVE_SCOPE_NODE) {
        for (u32 i = 0; i < step->node_count; ++i) {
            if (!nv_scene_alive(app->scene, step->nodes[i]))
                return;
        }
        // Show what changed: the step's nodes become the selection, the first the primary.
        SceneView* view = &app->views[SCENE_SHOWCASE];
        selection_set(view, step->nodes[0]);
        view->other_count = step->node_count - 1;
        memcpy(view->others, &step->nodes[1], view->other_count * sizeof(NvNodeId));
    }
    b32 applied = save_apply_scope(app, step->scope, step->nodes, step->node_count, bytes, size);
    NV_ASSERT(applied); // the bytes were written by save_write_scope
    (void)applied;
}

internal void undo_step(App* app)
{
    Undo* undo = &app->undo;
    if (!undo->done)
        return;
    --undo->done;
    apply(app, step_at(undo, undo->done), 0);
    commit_all(app); // what was applied is the new starting point, not an edit
}

internal void redo_step(App* app)
{
    Undo* undo = &app->undo;
    if (undo->done == undo->count)
        return;
    apply(app, step_at(undo, undo->done), 1);
    ++undo->done;
    commit_all(app);
}

void undo_init(App* app)
{
    app->undo.steps = NV_PUSH_ARRAY(&app->permanent, UNDO_MAX_STEPS, UndoStep);
    commit_all(app);
}

void undo_update(App* app)
{
    Undo* undo = &app->undo;
    s32 request = undo->request;
    undo->request = 0;
    // Undo's history is the edit state's; while playing, edits are lost on Stop anyway.
    if (app->shown != SCENE_SHOWCASE || app->playing)
        return;
    // Idle: nothing is being dragged, typed or picked from a popup, so an edit in progress is done.
    b32 idle = !igIsAnyItemActive() && !ImGuizmo_IsUsingAny() &&
               !igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!idle)
        return;
    take_steps(app);

    // The keys (Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y) are the desktop UI's shortcuts; they ask through `request`.
    if (request < 0)
        undo_step(app);
    else if (request > 0)
        redo_step(app);
}

// Whether undo and redo are available now, and the labels of the steps they would take.
internal void undo_state(App* app, b32* can_undo, b32* can_redo, char* undo_label, char* redo_label, umm capacity)
{
    Undo* undo = &app->undo;
    // Undo's history is the showcase's edit state: not in the stress scene, not while playing.
    b32 on = app->shown == SCENE_SHOWCASE && !app->playing;
    *can_undo = on && undo->done > 0;
    *can_redo = on && undo->done < undo->count;
    snprintf(undo_label, capacity, "%s", *can_undo ? step_at(undo, undo->done - 1)->label : "");
    nv_utf8_trim(undo_label);
    snprintf(redo_label, capacity, "%s", *can_redo ? step_at(undo, undo->done)->label : "");
    nv_utf8_trim(redo_label);
}

void undo_button(App* app, s32 direction, b32 labels, ImVec2_c size)
{
    Undo* undo = &app->undo;
    b32 can_undo, can_redo;
    char undo_label[UNDO_LABEL_MAX], redo_label[UNDO_LABEL_MAX];
    undo_state(app, &can_undo, &can_redo, undo_label, redo_label, sizeof(undo_label));
    char label[UNDO_LABEL_MAX + 16];
    b32 can = direction < 0 ? can_undo : can_redo;
    const char* name = direction < 0 ? "Undo" : "Redo"; // the id; the text is translated
    const char* step = direction < 0 ? undo_label : redo_label;
    if (labels && can)
        snprintf(label, sizeof(label), "%s: %s###%s", T(name), step, name);
    else
        snprintf(label, sizeof(label), "%s###%s", T(name), name);
    igBeginDisabled(!can);
    if (igButton(label, size))
        undo->request = direction;
    igEndDisabled();
}

void undo_menu_items(App* app)
{
    Undo* undo = &app->undo;
    b32 can_undo, can_redo;
    char undo_label[UNDO_LABEL_MAX], redo_label[UNDO_LABEL_MAX];
    undo_state(app, &can_undo, &can_redo, undo_label, redo_label, sizeof(undo_label));
    char label[UNDO_LABEL_MAX + 16];

    if (can_undo)
        snprintf(label, sizeof(label), "%s: %s", T("Undo"), undo_label);
    else
        snprintf(label, sizeof(label), "%s", T("Undo"));
    if (igMenuItem_Bool(label, shortcut_label(SC_UNDO), false, can_undo))
        undo->request = -1;
    if (can_redo)
        snprintf(label, sizeof(label), "%s: %s", T("Redo"), redo_label);
    else
        snprintf(label, sizeof(label), "%s", T("Redo"));
    if (igMenuItem_Bool(label, shortcut_label(SC_REDO), false, can_redo))
        undo->request = 1;
}
