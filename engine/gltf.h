#pragma once

// glTF loading through cgltf. Files are read with fopen; on the web they come from the assets
// preloaded with nv_setup_executable(... ASSETS ...).

#include "engine/anim.h"
#include "engine/renderer.h"
#include "engine/scene.h"

#define NV_GLTF_MAX_MESH_NODES 64 // mesh nodes a model may have (a static model has one per primitive)
#define NV_GLTF_MAX_NODES      64 // nodes of a static model's tree

typedef struct NvGltfModel {
    NvScene* scene; // the scene the nodes below live in
    NvNodeId root;                               // parent of every node the model created
    NvNodeId mesh_nodes[NV_GLTF_MAX_MESH_NODES]; // one per mesh primitive
    u32 mesh_node_count;

    // A static model (no skin) keeps the file's node tree: node i is nodes[i], under nodes[parents[i]] (-1: under `root`), parents
    // first. Its local transforms are the file's and are not baked into the vertices. local_* are kept apart from the scene's nodes,
    // which an edit of this instance may change; the copies start from them. A skinned model has node_count 0: its meshes stay flat
    // under `root`.
    u32 node_count;
    NvNodeId nodes[NV_GLTF_MAX_NODES];
    s32 parents[NV_GLTF_MAX_NODES];
    NvVec3 local_position[NV_GLTF_MAX_NODES];
    NvQuat local_rotation[NV_GLTF_MAX_NODES];
    NvVec3 local_scale[NV_GLTF_MAX_NODES];

    // The first skin, if any. Joint order is the skin's joint order, which is also the joint order
    // the meshes' JOINTS_0 attributes index.
    u32 joint_count;
    NvJointDesc* joints; // [joint_count], from the permanent arena
    NvMat4* inverse_bind; // [joint_count], from the permanent arena

    // Created for the first skin (0 without one): the animator's owner is `root`, and it poses
    // every skinned mesh node. Needs nv_anim_init.
    NvSkeletonId skeleton;
    NvAnimatorId animator;
} NvGltfModel;

// Loads meshes, base color materials and textures into `renderer`, and adds a node tree under a
// new root node in `scene`, a child of `parent` (a zeroed id adds it at the top level). Loading uses
// `scratch` and leaves it as it was. Fails (zeroed, having logged why) when the file is missing or
// not valid glTF.
typedef struct NvGltfLoad {
    b32 ok;
    NvGltfModel model;
} NvGltfLoad;
NvGltfLoad nv_gltf_load_model(const char* path, NvScene* scene, NvNodeId parent, NvRenderer* renderer, NvArena* permanent,
                              NvArena* scratch);

// Adds another copy of a loaded model to `scene` (any scene), under `parent` (a zeroed id adds it
// at the top level): a new root with the same node names, meshes and materials, the file's node tree
// (static models) and a new animator on the same skeleton. Nothing is loaded again.
void nv_gltf_instantiate(const NvGltfModel* model, NvScene* scene, NvNodeId parent, NvGltfModel* out);

// Creates a clip on `skeleton` for every animation in the file, matching animated nodes to joints
// by name. Joints named `root_motion_joint` (NULL for none) have their horizontal motion taken
// out. `count` is the number of clips written to `clips`. Fails (zeroed) like nv_gltf_load_model.
typedef struct NvGltfClips {
    b32 ok;
    u32 count;
} NvGltfClips;
NvGltfClips nv_gltf_load_clips(const char* path, NvSkeletonId skeleton, const char* root_motion_joint,
                               NvArena* scratch, NvClipId* clips, u32 max_clips);
