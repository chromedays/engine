#pragma once

// Skeletal animation: skeletons, clips and animators, backed by ozz-animation. The C++ side lives in
// engine/src/anim.cpp; everything here is plain C.

#include "nv/renderer.h"
#include "nv/scene.h"

#define NV_MAX_SKELETONS   8
#define NV_MAX_CLIPS       64
#define NV_MAX_ANIMATORS   64
#define NV_MAX_JOINTS      128 // the Quaternius rig has 65
#define NV_MAX_ANIM_LAYERS 4

typedef struct NvSkeletonId { u32 index; } NvSkeletonId; // 0 = none
typedef struct NvClipId     { u32 index; } NvClipId;     // 0 = none

// One joint of a skeleton, in the order the caller uses for joint indices everywhere (skin
// matrices, joint lookups). Parents come before their children.
typedef struct NvJointDesc {
    char name[NV_NODE_NAME_MAX];
    s32 parent; // -1 for a root
    NvVec3 position; // rest pose, relative to the parent
    NvQuat rotation;
    NvVec3 scale;
} NvJointDesc;

// Linearly interpolated keyframes for one joint. Channels with no keys hold the rest pose.
typedef struct NvTrackDesc {
    u32 joint;
    u32 translation_count;
    u32 rotation_count;
    u32 scale_count;
    const f32* translation_times; // seconds
    const f32* rotation_times;
    const f32* scale_times;
    const NvVec3* translations;
    const NvQuat* rotations;
    const NvVec3* scales;
} NvTrackDesc;

// Horizontal motion taken out of a clip's root joint, so a character can move by it instead of
// sliding back to where the loop started (see nv_anim_create_clip).
typedef struct NvRootMotionDesc {
    u32 joint;    // the joint whose translation carries the motion
    b32 enabled;
} NvRootMotionDesc;

typedef struct NvAnimLayer {
    NvClipId clip; // 0 = empty layer
    f32 time;      // seconds into the clip
    f32 speed;     // playback rate, 1 = as authored
    f32 weight;    // blend weight; layers are normalized by their total weight
    b32 loop;
} NvAnimLayer;

// Aim IK: turns `joint` so its `forward` axis points at `target` (model space).
typedef struct NvLookAt {
    b32 enabled;
    s32 joint;
    NvVec3 forward; // joint-space axis that should face the target
    NvVec3 up;      // joint-space axis kept upward
    NvVec3 target;  // model space
    f32 weight;
} NvLookAt;

typedef struct NvAnimator {
    NvSkeletonId skeleton;
    NvAnimLayer layers[NV_MAX_ANIM_LAYERS];

    // Crossfade in progress: layer 1 fades out while layer 0 fades in.
    f32 fade_duration;
    f32 fade_elapsed;

    NvLookAt look_at;

    // Root motion: horizontal model-space movement accumulated by nv_anim_update since the caller
    // last cleared it.
    NvVec3 root_motion;

    // Outputs of nv_anim_update, in the joint order of the skeleton's NvJointDesc array.
    u32 joint_count;
    NvMat4* joint_model; // model-space joint matrices
} NvAnimator;

// Routes ozz's allocations to `permanent`; call once before creating anything.
void nv_anim_init(NvArena* permanent);

// `inverse_bind` (one per joint) turns model-space joint matrices into skinning matrices.
NvSkeletonId nv_anim_create_skeleton(const NvJointDesc* joints, u32 joint_count, const NvMat4* inverse_bind);
u32 nv_anim_joint_count(NvSkeletonId skeleton);
s32 nv_anim_find_joint(NvSkeletonId skeleton, const char* name); // -1 if missing
const NvJointDesc* nv_anim_joints(NvSkeletonId skeleton);

NvClipId nv_anim_create_clip(NvSkeletonId skeleton, const char* name, f32 duration,
                             const NvTrackDesc* tracks, u32 track_count, NvRootMotionDesc root_motion);
const char* nv_anim_clip_name(NvClipId clip);
f32 nv_anim_clip_duration(NvClipId clip);
b32 nv_anim_clip_has_root_motion(NvClipId clip);

NvAnimatorId nv_anim_create_animator(NvSkeletonId skeleton);
NvAnimator* nv_anim_get(NvAnimatorId id);

// Plays `clip` on layer 0, crossfading from what layer 0 played over `fade_seconds` (0 = cut).
void nv_anim_play(NvAnimator* animator, NvClipId clip, f32 fade_seconds, b32 loop);

// Advances every layer by dt, samples and blends them, applies IK and updates joint_model.
void nv_anim_update(NvAnimator* animator, f32 dt);

// Skinning matrices of every animator, indexed by NvAnimatorId, for nv_renderer_draw.
const NvSkin* nv_anim_skins(void);
