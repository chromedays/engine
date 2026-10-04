#include "engine/scene.h"

internal NvNode* get_slot(NvScene* scene, u32 index)
{
    NV_ASSERT(index > 0 && index <= scene->node_count);
    return &scene->nodes[index];
}

// Unlinks `index` from its parent's child list, or from the root list.
internal void unlink(NvScene* scene, u32 index)
{
    NvNode* node = get_slot(scene, index);
    u32* link = node->parent ? &get_slot(scene, node->parent)->first_child : &scene->first_root;
    while (*link != index) {
        NV_ASSERT(*link);
        link = &get_slot(scene, *link)->next_sibling;
    }
    *link = node->next_sibling;
    node->next_sibling = 0;
}

internal void free_slot(NvScene* scene, u32 index)
{
    NvNode* node = get_slot(scene, index);
    u32 gen = node->gen + 1; // odd -> even: free
    *node = (NvNode){0};
    node->gen = gen;
    node->next_sibling = scene->first_free;
    scene->first_free = index;
}

NvNodeId nv_scene_add_node(NvScene* scene, NvNodeId parent, const char* name)
{
    u32 index = scene->first_free;
    if (index) {
        scene->first_free = scene->nodes[index].next_sibling;
    } else {
        NV_ASSERT(scene->node_count + 1 < NV_MAX_NODES);
        index = ++scene->node_count;
    }

    NvNode* node = &scene->nodes[index];
    u32 gen = node->gen + 1; // even -> odd: in use
    *node = (NvNode){0};
    node->gen = gen;
    node->rotation = nv_quat_identity();
    node->scale = nv_vec3(1.0f, 1.0f, 1.0f);
    node->world = nv_mat4_identity();

    if (name) {
        u32 length = nv_utf8_fit(name, NV_NODE_NAME_MAX - 1);
        for (u32 i = 0; i < length; ++i)
            node->name[i] = name[i];
        node->name[length] = 0;
    }

    // Append so siblings keep their creation order.
    u32* link = &scene->first_root;
    if (parent.index) {
        nv_scene_get(scene, parent);
        node->parent = parent.index;
        link = &scene->nodes[parent.index].first_child;
    }
    while (*link)
        link = &scene->nodes[*link].next_sibling;
    *link = index;

    return (NvNodeId){index, gen};
}

void nv_scene_remove_node(NvScene* scene, NvNodeId id)
{
    nv_scene_get(scene, id);
    unlink(scene, id.index);

    // NOTE: Post-order walk that frees each node after all of its children, so the links it
    // follows are never read from a freed slot. When a node has no next sibling, all of its
    // parent's children are gone and the parent is next.
    u32 index = id.index;
    for (;;) {
        while (scene->nodes[index].first_child)
            index = scene->nodes[index].first_child;

        NvNode* node = &scene->nodes[index];
        u32 next = node->next_sibling;
        u32 parent = node->parent;
        b32 was_root = (index == id.index);
        free_slot(scene, index);
        if (was_root)
            break;

        if (next) {
            index = next;
        } else {
            scene->nodes[parent].first_child = 0;
            index = parent;
        }
    }

    // The active camera may have been anywhere in the removed subtree.
    NvNodeId camera = scene->active_camera;
    if (camera.index && scene->nodes[camera.index].gen != camera.gen)
        scene->active_camera = (NvNodeId){0};
}

NvNode* nv_scene_get(NvScene* scene, NvNodeId id)
{
    NvNode* node = get_slot(scene, id.index);
    NV_ASSERT(node->gen == id.gen && (node->gen & 1));
    return node;
}

b32 nv_scene_alive(NvScene* scene, NvNodeId id)
{
    return id.index && id.index <= scene->node_count && scene->nodes[id.index].gen == id.gen && (id.gen & 1);
}

void nv_scene_update(NvScene* scene)
{
    // NOTE: Pre-order walk using the parent links instead of a stack, so a parent's world matrix
    // is always ready before its children need it.
    u32 index = scene->first_root;
    while (index) {
        NvNode* node = &scene->nodes[index];
        NvMat4 local = nv_mat4_trs(node->position, node->rotation, node->scale);
        if (node->attach.animator.index)
            local = nv_mat4_mul(node->attach.joint_model, local);
        node->world = node->parent ? nv_mat4_mul(scene->nodes[node->parent].world, local) : local;

        if (node->first_child) {
            index = node->first_child;
        } else {
            while (index && !scene->nodes[index].next_sibling)
                index = scene->nodes[index].parent;
            if (index)
                index = scene->nodes[index].next_sibling;
        }
    }
}
