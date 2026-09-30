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

// The node the Node scope follows: the showcase's selection.
internal NvNodeId selected_node(App* app)
{
    return app->views[SCENE_SHOWCASE].selected;
}

internal u32 write_scope(App* app, SaveScope scope, NvNodeId node, u8* out)
{
    if (scope == SAVE_SCOPE_NODE && !node.index)
        return 0;
    u32 size = save_write_scope(app, scope, node.index, out, UNDO_MAX_BYTES);
    NV_ASSERT(size); // a scope that does not fit in UNDO_MAX_BYTES is not undoable
    return size;
}

internal void commit(App* app, SaveScope scope)
{
    Undo* undo = &app->undo;
    if (scope == SAVE_SCOPE_NODE) {
        undo->committed_node = selected_node(app);
        undo->committed_driven = undo->committed_node.index ? save_driven_fields(app, undo->committed_node.index) : 0;
    }
    undo->committed_size[scope] = write_scope(app, scope, undo->committed_node, undo->committed[scope]);
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

internal void push_step(App* app, SaveScope scope, NvNodeId node, const u8* before, u32 before_size, const u8* after,
                        u32 after_size)
{
    Undo* undo = &app->undo;
    undo->count = undo->done; // a new edit drops what could have been redone
    if (undo->count == UNDO_MAX_STEPS) {
        undo->first = (undo->first + 1) % UNDO_MAX_STEPS;
        --undo->count;
    }
    UndoStep* step = step_at(undo, undo->count);
    step->scope = scope;
    step->node = node;
    memcpy(step->before, before, before_size);
    step->before_size = before_size;
    memcpy(step->after, after, after_size);
    step->after_size = after_size;
    const char* field = save_field_label(first_difference(before, before_size, after, after_size));
    if (scope == SAVE_SCOPE_NODE)
        snprintf(step->label, sizeof(step->label), "%s %s", nv_scene_get(app->scene, node)->name, field);
    else if (scope == SAVE_SCOPE_CHARACTER)
        snprintf(step->label, sizeof(step->label), "Character %s", field);
    else
        snprintf(step->label, sizeof(step->label), "%s", field);
    ++undo->count;
    undo->done = undo->count;
}

// Takes a step for every scope that changed since its last commit.
internal void take_steps(App* app)
{
    Undo* undo = &app->undo;
    // Another node selected, or its driven fields changed (root motion turned on or off): its bytes
    // as they are become the starting point, without a step.
    NvNodeId selected = selected_node(app);
    if (selected.index != undo->committed_node.index || selected.gen != undo->committed_node.gen ||
        (selected.index && save_driven_fields(app, selected.index) != undo->committed_driven))
        commit(app, SAVE_SCOPE_NODE);

    for (u32 s = 0; s < SAVE_SCOPE_COUNT; ++s) {
        SaveScope scope = (SaveScope)s;
        u32 size = write_scope(app, scope, undo->committed_node, undo->current);
        if (size == undo->committed_size[scope] && memcmp(undo->current, undo->committed[scope], size) == 0)
            continue;
        push_step(app, scope, undo->committed_node, undo->committed[scope], undo->committed_size[scope], undo->current, size);
        memcpy(undo->committed[scope], undo->current, size);
        undo->committed_size[scope] = size;
    }
}

internal void apply(App* app, UndoStep* step, b32 redo)
{
    const u8* bytes = redo ? step->after : step->before;
    u32 size = redo ? step->after_size : step->before_size;
    if (step->scope == SAVE_SCOPE_NODE) {
        NvNode* node = &app->scene->nodes[step->node.index];
        if (node->gen != step->node.gen)
            return;
        // Show what changed.
        app->views[SCENE_SHOWCASE].selected = step->node;
    }
    b32 applied = save_apply_scope(app, step->scope, step->node.index, bytes, size);
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
    snprintf(redo_label, capacity, "%s", *can_redo ? step_at(undo, undo->done)->label : "");
}

void undo_button(App* app, s32 direction, b32 labels, ImVec2_c size)
{
    Undo* undo = &app->undo;
    b32 can_undo, can_redo;
    char undo_label[UNDO_LABEL_MAX], redo_label[UNDO_LABEL_MAX];
    undo_state(app, &can_undo, &can_redo, undo_label, redo_label, sizeof(undo_label));
    char label[UNDO_LABEL_MAX + 16];
    b32 can = direction < 0 ? can_undo : can_redo;
    const char* name = direction < 0 ? "Undo" : "Redo";
    const char* step = direction < 0 ? undo_label : redo_label;
    if (labels && can)
        snprintf(label, sizeof(label), "%s: %s###%s", name, step, name);
    else
        snprintf(label, sizeof(label), "%s###%s", name, name);
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
        snprintf(label, sizeof(label), "Undo: %s", undo_label);
    else
        snprintf(label, sizeof(label), "Undo");
    if (igMenuItem_Bool(label, shortcut_label(SC_UNDO), false, can_undo))
        undo->request = -1;
    if (can_redo)
        snprintf(label, sizeof(label), "Redo: %s", redo_label);
    else
        snprintf(label, sizeof(label), "Redo");
    if (igMenuItem_Bool(label, shortcut_label(SC_REDO), false, can_redo))
        undo->request = 1;
}
