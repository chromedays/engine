#include "sandbox.h"

// The selection (docs/specs/selection.md): a primary node, the one picked last, and up to
// SELECTION_MAX - 1 others, oldest first.

internal b32 same_node(NvNodeId a, NvNodeId b)
{
    return a.index == b.index && a.gen == b.gen;
}

u32 selection_count(SceneView* view)
{
    return view->selected.index ? 1 + view->other_count : 0;
}

NvNodeId selection_get(SceneView* view, u32 i)
{
    NV_ASSERT(i < selection_count(view));
    return i ? view->others[i - 1] : view->selected;
}

b32 selection_has(SceneView* view, NvNodeId id)
{
    if (!id.index)
        return 0;
    if (same_node(view->selected, id))
        return 1;
    for (u32 i = 0; i < view->other_count; ++i) {
        if (same_node(view->others[i], id))
            return 1;
    }
    return 0;
}

void selection_set(SceneView* view, NvNodeId id)
{
    view->selected = id;
    view->other_count = 0;
    view->range_anchor = id;
}

// Takes `id` out of the others, keeping their order.
internal void remove_other(SceneView* view, NvNodeId id)
{
    for (u32 i = 0; i < view->other_count; ++i) {
        if (!same_node(view->others[i], id))
            continue;
        for (u32 j = i + 1; j < view->other_count; ++j)
            view->others[j - 1] = view->others[j];
        --view->other_count;
        return;
    }
}

// The most recently added other becomes the primary.
internal void promote_last_other(SceneView* view)
{
    view->selected = view->other_count ? view->others[--view->other_count] : (NvNodeId){0};
}

void selection_add(Sandbox* sandbox, SceneView* view, NvNodeId id)
{
    if (!id.index || same_node(view->selected, id))
        return;
    if (selection_has(view, id)) {
        // Already among the others: it becomes the primary.
        remove_other(view, id);
    } else if (selection_count(view) == SELECTION_MAX) {
        if (!sandbox->selection_full_warned)
            nv_log(NV_LOG_WARNING, "sandbox", "The selection holds at most %u nodes.", SELECTION_MAX);
        sandbox->selection_full_warned = 1;
        return;
    }
    if (view->selected.index)
        view->others[view->other_count++] = view->selected;
    view->selected = id;
}

void selection_toggle(Sandbox* sandbox, SceneView* view, NvNodeId id)
{
    if (!id.index)
        return;
    view->range_anchor = id;
    if (same_node(view->selected, id)) {
        promote_last_other(view);
        return;
    }
    if (selection_has(view, id)) {
        remove_other(view, id);
        return;
    }
    selection_add(sandbox, view, id);
}

void selection_keep_primary(SceneView* view)
{
    view->other_count = 0;
}

void selection_prune(SceneView* view)
{
    NvScene* scene = view->scene;
    u32 kept = 0;
    for (u32 i = 0; i < view->other_count; ++i) {
        if (nv_scene_alive(scene, view->others[i]))
            view->others[kept++] = view->others[i];
    }
    view->other_count = kept;
    if (view->selected.index && !nv_scene_alive(scene, view->selected))
        promote_last_other(view);
    if (view->range_anchor.index && !nv_scene_alive(scene, view->range_anchor))
        view->range_anchor = view->selected;
}
