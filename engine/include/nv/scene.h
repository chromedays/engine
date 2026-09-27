#pragma once

#include "nv/math.h"

#define NV_MAX_NODES     4096
#define NV_NODE_NAME_MAX 32

// NOTE: Slot 0 is never used, so a zeroed id means "no node". `gen` catches ids that outlived
// their node.
typedef struct NvNodeId     { u32 index, gen; } NvNodeId;
typedef struct NvMeshId     { u32 index; } NvMeshId;     // 0 = no mesh
typedef struct NvMaterialId { u32 index; } NvMaterialId; // 0 = default material
typedef struct NvAnimatorId { u32 index; } NvAnimatorId; // 0 = not animated (see nv/anim.h)

typedef enum NvProjection {
    NV_PROJECTION_NONE, // node has no camera
    NV_PROJECTION_PERSPECTIVE,
    NV_PROJECTION_ORTHOGRAPHIC,
} NvProjection;

typedef struct NvCamera {
    NvProjection projection;
    f32 fov_y;        // radians (perspective)
    f32 ortho_height; // world units (orthographic)
    f32 near_z;
    f32 far_z;
} NvCamera;

typedef enum NvLightType {
    NV_LIGHT_NONE, // node has no light
    NV_LIGHT_DIRECTIONAL,
    NV_LIGHT_POINT,
    NV_LIGHT_SPOT,
} NvLightType;

// Lights shine along the node's -Z axis.
typedef struct NvLight {
    NvLightType type;
    NvVec3 color;   // linear RGB
    f32 intensity;
    f32 range;      // point / spot
    f32 inner_cone; // spot, radians
    f32 outer_cone; // spot, radians
} NvLight;

// Makes a node follow a joint of an animator's skeleton. The node's parent should be the
// animator's owner. nv_anim_update_scene writes `joint_model`; the scene only multiplies by it.
typedef struct NvJointAttach {
    NvAnimatorId animator; // 0 = not attached
    u32 joint;
    NvMat4 joint_model; // the joint in the owner's model space
} NvJointAttach;

typedef struct NvNode {
    u32 gen; // odd while the slot is in use, even while it is free
    char name[NV_NODE_NAME_MAX];

    // Hierarchy as intrusive lists of slot indices; 0 = none.
    u32 parent;
    u32 first_child;
    u32 next_sibling; // also links the free list while the slot is free

    // IMPORTANT: Zero scale or rotation would make a node vanish, so nv_scene_add_node sets
    // scale (1,1,1) and the identity rotation. This is the one place zero is not the default.
    NvVec3 position;
    NvQuat rotation;
    NvVec3 scale;
    NvMat4 world; // written by nv_scene_update

    // Components: each one is absent while zero.
    NvMeshId mesh;
    NvMaterialId material;
    NvAnimatorId animator; // poses a skinned mesh; unused by static meshes
    NvJointAttach attach;
    NvCamera camera;
    NvLight light;
} NvNode;

// NOTE: A zeroed NvScene is an empty scene. It is large (about 1 MB), so push it from an arena
// instead of putting it on the stack.
typedef struct NvScene {
    NvNode nodes[NV_MAX_NODES]; // [0] is the unused null slot
    u32 node_count;             // highest slot index ever used
    u32 first_free;             // free list through next_sibling; 0 = empty
    u32 first_root;             // top-level nodes, linked through next_sibling
    NvNodeId active_camera;
} NvScene;

// Adds a node under `parent` (a zeroed id adds a top-level node). `name` is copied and truncated
// to NV_NODE_NAME_MAX - 1 characters.
NvNodeId nv_scene_add_node(NvScene* scene, NvNodeId parent, const char* name);

// Removes the node and everything below it. Ids to removed nodes become stale.
void nv_scene_remove_node(NvScene* scene, NvNodeId id);

// Asserts that `id` refers to a live node.
NvNode* nv_scene_get(NvScene* scene, NvNodeId id);

// Recomputes every node's world matrix from its local transform and its parents.
void nv_scene_update(NvScene* scene);
