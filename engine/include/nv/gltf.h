#pragma once

// glTF loading through cgltf. Files are read with fopen; on the web they come from the assets
// preloaded with nv_setup_executable(... ASSETS ...).

#include "nv/anim.h"
#include "nv/renderer.h"
#include "nv/scene.h"

#define NV_GLTF_MAX_MESH_NODES 32

typedef struct NvGltfModel {
    NvNodeId root;                               // parent of every node the model created
    NvNodeId mesh_nodes[NV_GLTF_MAX_MESH_NODES]; // one per mesh primitive
    u32 mesh_node_count;

    // The first skin, if any. Joint order is the skin's joint order, which is also the joint order
    // the meshes' JOINTS_0 attributes index.
    u32 joint_count;
    NvJointDesc* joints; // [joint_count], from the permanent arena
    NvMat4* inverse_bind; // [joint_count], from the permanent arena
} NvGltfModel;

// Loads meshes, base color materials and textures into `renderer`, and adds a node tree under a
// new root node in `scene`. Loading uses `scratch` and leaves it as it was.
b32 nv_gltf_load_model(const char* path, NvScene* scene, NvRenderer* renderer, NvArena* permanent,
                       NvArena* scratch, NvGltfModel* out);

// Creates a clip on `skeleton` for every animation in the file, matching animated nodes to joints
// by name. Joints named `root_motion_joint` (NULL for none) have their horizontal motion taken
// out. Returns the number of clips written to `clips`.
u32 nv_gltf_load_clips(const char* path, NvSkeletonId skeleton, const char* root_motion_joint,
                       NvArena* scratch, NvClipId* clips, u32 max_clips);
