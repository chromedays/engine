// C API over ozz-animation (see engine/anim.h). This is the one C++ file of ours: it stays a thin
// translation layer, and nothing C++ crosses the header.

// IMPORTANT: ozz headers come first. engine/base.h defines `internal` as a macro, which would break
// ozz's `internal::` namespace.
#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/motion_extractor.h"
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/raw_track.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/offline/track_builder.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/blending_job.h"
#include "ozz/animation/runtime/ik_aim_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/track.h"
#include "ozz/animation/runtime/track_sampling_job.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/simd_quaternion.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/memory/allocator.h"

extern "C" {
#include "engine/anim.h"
#include "engine/log.h"
}

#include <cmath>
#include <cstdio>
#include <cstring>

namespace oa = ozz::animation;
namespace oo = ozz::animation::offline;
namespace om = ozz::math;

namespace {

// ozz allocates through this; everything lives until the page closes, so frees are ignored.
class ArenaAllocator : public ozz::memory::Allocator {
public:
    NvArena* arena = nullptr;
    void* Allocate(size_t size, size_t alignment) override
    {
        return nv_arena_push(arena, size, alignment < 16 ? 16 : alignment);
    }
    void Deallocate(void*) override {}
};

struct Skeleton {
    oa::Skeleton* skeleton;
    u32 joint_count;
    NvJointDesc* joints;  // caller's joint order
    NvMat4* inverse_bind; // caller's joint order
    s32* to_ozz;          // caller joint -> ozz joint
};

struct Clip {
    NvSkeletonId skeleton;
    oa::Animation* animation;
    oa::Float3Track* motion; // root motion, model space; null without root motion
    char name[NV_NODE_NAME_MAX];
    f32 duration;
};

struct AnimatorState {
    NvAnimator pub;
    oa::SamplingJob::Context* contexts[NV_MAX_ANIM_LAYERS];
    om::SoaTransform* layer_locals[NV_MAX_ANIM_LAYERS];
    om::SoaTransform* locals;
    om::Float4x4* models; // ozz joint order
    NvMat4* skinning;     // caller joint order
};

ArenaAllocator g_allocator;
NvArena* g_arena = nullptr;
Skeleton g_skeletons[NV_MAX_SKELETONS];
u32 g_skeleton_count = 1; // slot 0 is "none"
Clip g_clips[NV_MAX_CLIPS];
u32 g_clip_count = 1;
AnimatorState g_animators[NV_MAX_ANIMATORS];
u32 g_animator_count = 1;
NvSkin g_skins[NV_MAX_ANIMATORS];

template <typename T>
T* push_array(u32 count)
{
    return static_cast<T*>(nv_arena_push(g_arena, sizeof(T) * count, alignof(T) < 16 ? 16 : alignof(T)));
}

Skeleton* get_skeleton(NvSkeletonId id)
{
    NV_ASSERT(id.index > 0 && id.index < g_skeleton_count);
    return &g_skeletons[id.index];
}

Clip* get_clip(NvClipId id)
{
    NV_ASSERT(id.index > 0 && id.index < g_clip_count);
    return &g_clips[id.index];
}

AnimatorState* get_state(NvAnimator* animator)
{
    return reinterpret_cast<AnimatorState*>(animator);
}

void add_joint_children(const NvJointDesc* joints, u32 count, s32 parent, oo::RawSkeleton::Joint::Children* out)
{
    for (u32 j = 0; j < count; ++j) {
        if (joints[j].parent != parent)
            continue;
        out->emplace_back();
        oo::RawSkeleton::Joint& joint = out->back();
        joint.name = joints[j].name;
        joint.transform.translation = om::Float3(joints[j].position.x, joints[j].position.y, joints[j].position.z);
        joint.transform.rotation = om::Quaternion(joints[j].rotation.x, joints[j].rotation.y, joints[j].rotation.z, joints[j].rotation.w);
        joint.transform.scale = om::Float3(joints[j].scale.x, joints[j].scale.y, joints[j].scale.z);
        add_joint_children(joints, count, (s32)j, &joint.children);
    }
}

NvMat4 to_nv(const om::Float4x4& m)
{
    NvMat4 r;
    for (int c = 0; c < 4; ++c)
        om::StorePtrU(m.cols[c], &r.e[c * 4]);
    return r;
}

// Samples a clip's root motion at `time` (seconds, within the clip).
om::Float3 sample_motion(const Clip* clip, f32 time)
{
    om::Float3 result(0.0f, 0.0f, 0.0f);
    oa::Float3TrackSamplingJob job;
    job.track = clip->motion;
    job.ratio = clip->duration > 0.0f ? time / clip->duration : 0.0f;
    job.result = &result;
    job.Run();
    return result;
}

// Multiplies the local rotation of ozz joint `joint` by `correction` (joint space).
void apply_rotation_correction(om::SoaTransform* locals, int joint, const om::SimdQuaternion& correction)
{
    om::SoaQuaternion& q = locals[joint / 4].rotation;
    int lane = joint % 4;
    float x[4], y[4], z[4], w[4], c[4];
    om::StorePtrU(q.x, x);
    om::StorePtrU(q.y, y);
    om::StorePtrU(q.z, z);
    om::StorePtrU(q.w, w);
    om::StorePtrU(correction.xyzw, c);
    NvQuat r = nv_quat_mul((NvQuat){x[lane], y[lane], z[lane], w[lane]}, (NvQuat){c[0], c[1], c[2], c[3]});
    x[lane] = r.x;
    y[lane] = r.y;
    z[lane] = r.z;
    w[lane] = r.w;
    q.x = om::simd_float4::LoadPtrU(x);
    q.y = om::simd_float4::LoadPtrU(y);
    q.z = om::simd_float4::LoadPtrU(z);
    q.w = om::simd_float4::LoadPtrU(w);
}

} // namespace

extern "C" {

void nv_anim_init(NvArena* permanent)
{
    g_arena = permanent;
    g_allocator.arena = permanent;
    ozz::memory::SetDefaulAllocator(&g_allocator);
}

NvSkeletonId nv_anim_create_skeleton(const NvJointDesc* joints, u32 joint_count, const NvMat4* inverse_bind)
{
    NV_ASSERT(g_arena && g_skeleton_count < NV_MAX_SKELETONS && joint_count <= NV_MAX_JOINTS);
    oo::RawSkeleton raw;
    add_joint_children(joints, joint_count, -1, &raw.roots);
    NV_ASSERT(raw.Validate() && raw.num_joints() == (int)joint_count);

    Skeleton* skeleton = &g_skeletons[g_skeleton_count];
    skeleton->skeleton = oo::SkeletonBuilder()(raw).release();
    NV_ASSERT(skeleton->skeleton);
    skeleton->joint_count = joint_count;
    skeleton->joints = push_array<NvJointDesc>(joint_count);
    skeleton->inverse_bind = push_array<NvMat4>(joint_count);
    skeleton->to_ozz = push_array<s32>(joint_count);
    memcpy(skeleton->joints, joints, joint_count * sizeof(NvJointDesc));
    memcpy(skeleton->inverse_bind, inverse_bind, joint_count * sizeof(NvMat4));

    // ozz orders joints depth-first; callers keep their own order, matched by name.
    auto names = skeleton->skeleton->joint_names();
    for (u32 j = 0; j < joint_count; ++j) {
        skeleton->to_ozz[j] = -1;
        for (size_t o = 0; o < names.size(); ++o) {
            if (strcmp(names[o], joints[j].name) == 0)
                skeleton->to_ozz[j] = (s32)o;
        }
        NV_ASSERT(skeleton->to_ozz[j] >= 0);
    }
    return NvSkeletonId{g_skeleton_count++};
}

u32 nv_anim_joint_count(NvSkeletonId skeleton)
{
    return get_skeleton(skeleton)->joint_count;
}

const NvJointDesc* nv_anim_joints(NvSkeletonId skeleton)
{
    return get_skeleton(skeleton)->joints;
}

s32 nv_anim_find_joint(NvSkeletonId id, const char* name)
{
    Skeleton* skeleton = get_skeleton(id);
    for (u32 j = 0; j < skeleton->joint_count; ++j) {
        if (strcmp(skeleton->joints[j].name, name) == 0)
            return (s32)j;
    }
    return -1;
}

NvClipId nv_anim_create_clip(NvSkeletonId skeleton_id, const char* name, f32 duration,
                             const NvTrackDesc* tracks, u32 track_count, NvRootMotionDesc root_motion)
{
    NV_ASSERT(g_clip_count < NV_MAX_CLIPS && duration > 0.0f);
    Skeleton* skeleton = get_skeleton(skeleton_id);

    // Every joint gets at least its rest pose: ozz treats a track without keys as identity.
    oo::RawAnimation raw;
    raw.duration = duration;
    raw.name = name;
    raw.tracks.resize(skeleton->joint_count);
    for (u32 j = 0; j < skeleton->joint_count; ++j) {
        const NvJointDesc* rest = &skeleton->joints[j];
        oo::RawAnimation::JointTrack& track = raw.tracks[skeleton->to_ozz[j]];
        track.translations.push_back({0.0f, om::Float3(rest->position.x, rest->position.y, rest->position.z)});
        track.rotations.push_back({0.0f, om::Quaternion(rest->rotation.x, rest->rotation.y, rest->rotation.z, rest->rotation.w)});
        track.scales.push_back({0.0f, om::Float3(rest->scale.x, rest->scale.y, rest->scale.z)});
    }
    for (u32 t = 0; t < track_count; ++t) {
        const NvTrackDesc* desc = &tracks[t];
        NV_ASSERT(desc->joint < skeleton->joint_count);
        oo::RawAnimation::JointTrack& track = raw.tracks[skeleton->to_ozz[desc->joint]];
        if (desc->translation_count) {
            track.translations.clear();
            track.translations.reserve(desc->translation_count);
            for (u32 k = 0; k < desc->translation_count; ++k) {
                NvVec3 v = desc->translations[k];
                track.translations.push_back({desc->translation_times[k], om::Float3(v.x, v.y, v.z)});
            }
        }
        if (desc->rotation_count) {
            track.rotations.clear();
            track.rotations.reserve(desc->rotation_count);
            for (u32 k = 0; k < desc->rotation_count; ++k) {
                NvQuat q = desc->rotations[k];
                track.rotations.push_back({desc->rotation_times[k], om::Quaternion(q.x, q.y, q.z, q.w)});
            }
        }
        if (desc->scale_count) {
            track.scales.clear();
            track.scales.reserve(desc->scale_count);
            for (u32 k = 0; k < desc->scale_count; ++k) {
                NvVec3 v = desc->scales[k];
                track.scales.push_back({desc->scale_times[k], om::Float3(v.x, v.y, v.z)});
            }
        }
    }
    if (!raw.Validate()) {
        nv_log(NV_LOG_ERROR, "nv", "clip %s has invalid keyframes", name);
        return NvClipId{0};
    }

    Clip* clip = &g_clips[g_clip_count];
    *clip = Clip{};
    clip->skeleton = skeleton_id;
    clip->duration = duration;
    snprintf(clip->name, sizeof(clip->name), "%s", name);

    if (root_motion.enabled) {
        // Horizontal root translation becomes a separate track; the pose keeps the vertical part.
        oo::MotionExtractor extractor;
        extractor.root_joint = skeleton->to_ozz[root_motion.joint];
        extractor.position_settings = {true, false, true, oo::MotionExtractor::Reference::kAnimation, true, false};
        extractor.rotation_settings = {false, false, false, oo::MotionExtractor::Reference::kAnimation, false, false};
        oo::RawFloat3Track motion_position;
        oo::RawQuaternionTrack motion_rotation;
        oo::RawAnimation extracted;
        if (extractor(raw, *skeleton->skeleton, &motion_position, &motion_rotation, &extracted)) {
            raw = extracted;
            clip->motion = oo::TrackBuilder()(motion_position).release();
        } else {
            nv_log(NV_LOG_WARNING, "nv", "root motion extraction failed for %s", name);
        }
    }

    clip->animation = oo::AnimationBuilder()(raw).release();
    NV_ASSERT(clip->animation);
    return NvClipId{g_clip_count++};
}

const char* nv_anim_clip_name(NvClipId clip)
{
    return get_clip(clip)->name;
}

f32 nv_anim_clip_duration(NvClipId clip)
{
    return get_clip(clip)->duration;
}

b32 nv_anim_clip_has_root_motion(NvClipId clip)
{
    return get_clip(clip)->motion != nullptr;
}

u32 nv_anim_clip_count(void)
{
    return g_clip_count - 1;
}

NvSkeletonId nv_anim_clip_skeleton(NvClipId clip)
{
    return get_clip(clip)->skeleton;
}

NvAnimatorId nv_anim_create_animator(NvSkeletonId skeleton_id, NvScene* scene, NvNodeId owner)
{
    NV_ASSERT(g_animator_count < NV_MAX_ANIMATORS);
    Skeleton* skeleton = get_skeleton(skeleton_id);
    int joints = skeleton->skeleton->num_joints();
    int soa_joints = skeleton->skeleton->num_soa_joints();

    AnimatorState* state = &g_animators[g_animator_count];
    *state = AnimatorState{};
    state->pub.skeleton = skeleton_id;
    state->pub.scene = scene;
    state->pub.owner = owner;
    state->pub.joint_count = skeleton->joint_count;
    state->pub.joint_model = push_array<NvMat4>(skeleton->joint_count);
    for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
        state->contexts[l] = ozz::New<oa::SamplingJob::Context>(joints);
        state->layer_locals[l] = push_array<om::SoaTransform>((u32)soa_joints);
    }
    state->locals = push_array<om::SoaTransform>((u32)soa_joints);
    state->models = push_array<om::Float4x4>((u32)joints);
    state->skinning = push_array<NvMat4>(skeleton->joint_count);

    // Start in the rest pose so the first frame has something to draw.
    auto rest = skeleton->skeleton->joint_rest_poses();
    for (int s = 0; s < soa_joints; ++s)
        state->locals[s] = rest[s];
    nv_anim_update(&state->pub, 0.0f);
    return NvAnimatorId{g_animator_count++};
}

NvAnimator* nv_anim_get(NvAnimatorId id)
{
    NV_ASSERT(id.index > 0 && id.index < g_animator_count);
    return &g_animators[id.index].pub;
}

void nv_anim_play(NvAnimator* animator, NvClipId clip, f32 fade_seconds, b32 loop)
{
    NvAnimLayer* current = &animator->layers[0];
    if (current->clip.index == clip.index) {
        current->loop = loop;
        return;
    }
    if (fade_seconds > 0.0f && current->clip.index) {
        animator->layers[1] = *current;
        animator->fade_duration = fade_seconds;
        animator->fade_elapsed = 0.0f;
        current->weight = 0.0f;
    } else {
        animator->layers[1] = NvAnimLayer{};
        animator->fade_duration = 0.0f;
        current->weight = 1.0f;
    }
    current->clip = clip;
    current->time = 0.0f;
    current->speed = current->speed != 0.0f ? current->speed : 1.0f;
    current->loop = loop;
}

void nv_anim_update(NvAnimator* animator, f32 dt)
{
    AnimatorState* state = get_state(animator);
    Skeleton* skeleton = get_skeleton(animator->skeleton);
    oa::Skeleton* ozz_skeleton = skeleton->skeleton;
    int soa_joints = ozz_skeleton->num_soa_joints();

    // Crossfade: layer 0 fades in while layer 1 fades out.
    if (animator->fade_duration > 0.0f) {
        animator->fade_elapsed += dt;
        f32 t = animator->fade_elapsed / animator->fade_duration;
        if (t >= 1.0f) {
            animator->layers[0].weight = 1.0f;
            animator->layers[1] = NvAnimLayer{};
            animator->fade_duration = 0.0f;
        } else {
            animator->layers[0].weight = t;
            animator->layers[1].weight = 1.0f - t;
        }
    }

    f32 total_weight = 0.0f;
    for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
        if (animator->layers[l].clip.index)
            total_weight += animator->layers[l].weight;
    }

    oa::BlendingJob::Layer blend_layers[NV_MAX_ANIM_LAYERS];
    int blend_count = 0;
    for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
        NvAnimLayer* layer = &animator->layers[l];
        if (!layer->clip.index)
            continue;
        Clip* clip = get_clip(layer->clip);
        NV_ASSERT(clip->skeleton.index == animator->skeleton.index);

        f32 previous = layer->time;
        f32 time = previous + dt * layer->speed;
        int wraps = 0;
        if (layer->loop) {
            while (time >= clip->duration) { time -= clip->duration; ++wraps; }
            while (time < 0.0f) { time += clip->duration; --wraps; }
        } else {
            time = time < 0.0f ? 0.0f : (time > clip->duration ? clip->duration : time);
        }
        layer->time = time;

        if (clip->motion && total_weight > 0.0f) {
            // Motion covered this frame, including whole loops when the time wrapped.
            om::Float3 start = sample_motion(clip, 0.0f);
            om::Float3 end = sample_motion(clip, clip->duration);
            om::Float3 from = sample_motion(clip, previous);
            om::Float3 to = sample_motion(clip, time);
            om::Float3 delta = to - from + (end - start) * (float)wraps;
            f32 w = layer->weight / total_weight;
            animator->root_motion = nv_vec3_add(animator->root_motion, nv_vec3(delta.x * w, delta.y * w, delta.z * w));
        }

        oa::SamplingJob sampling;
        sampling.animation = clip->animation;
        sampling.context = state->contexts[l];
        sampling.ratio = time / clip->duration;
        sampling.output = ozz::span<om::SoaTransform>(state->layer_locals[l], state->layer_locals[l] + soa_joints);
        sampling.Run();

        oa::BlendingJob::Layer& blend = blend_layers[blend_count++];
        blend.weight = layer->weight;
        blend.transform = ozz::span<const om::SoaTransform>(state->layer_locals[l], state->layer_locals[l] + soa_joints);
    }

    if (blend_count) {
        oa::BlendingJob blending;
        blending.layers = ozz::span<const oa::BlendingJob::Layer>(blend_layers, blend_layers + blend_count);
        blending.rest_pose = ozz_skeleton->joint_rest_poses();
        blending.output = ozz::span<om::SoaTransform>(state->locals, state->locals + soa_joints);
        blending.Run();
    }

    oa::LocalToModelJob local_to_model;
    local_to_model.skeleton = ozz_skeleton;
    local_to_model.input = ozz::span<const om::SoaTransform>(state->locals, state->locals + soa_joints);
    local_to_model.output = ozz::span<om::Float4x4>(state->models, state->models + ozz_skeleton->num_joints());
    local_to_model.Run();

    NvLookAt* look = &animator->look_at;
    if (look->enabled && look->joint >= 0 && look->weight > 0.0f) {
        int joint = skeleton->to_ozz[look->joint];
        om::SimdQuaternion correction;
        bool reached = false;
        oa::IKAimJob aim;
        aim.target = om::simd_float4::Load(look->target.x, look->target.y, look->target.z, 0.0f);
        aim.forward = om::simd_float4::Load(look->forward.x, look->forward.y, look->forward.z, 0.0f);
        aim.up = om::simd_float4::Load(look->up.x, look->up.y, look->up.z, 0.0f);
        aim.pole_vector = om::simd_float4::y_axis();
        aim.weight = look->weight;
        aim.joint = &state->models[joint];
        aim.joint_correction = &correction;
        aim.reached = &reached;
        if (aim.Run()) {
            apply_rotation_correction(state->locals, joint, correction);
            // Only the corrected joint and its descendants move.
            local_to_model.from = joint;
            local_to_model.Run();
        }
    }

    for (u32 j = 0; j < skeleton->joint_count; ++j) {
        animator->joint_model[j] = to_nv(state->models[skeleton->to_ozz[j]]);
        state->skinning[j] = nv_mat4_mul(animator->joint_model[j], skeleton->inverse_bind[j]);
    }
    u32 index = (u32)(state - g_animators);
    g_skins[index].matrices = state->skinning;
    g_skins[index].count = skeleton->joint_count;
}

// World matrix of a node from its local transforms up to the root. Unlike NvNode.world it is
// current, so look-at targets and owners moved this frame are used where they are now.
internal NvMat4 current_world(NvScene* scene, u32 index)
{
    NvMat4 world = nv_mat4_identity();
    while (index) {
        NvNode* node = &scene->nodes[index];
        NvMat4 local = nv_mat4_trs(node->position, node->rotation, node->scale);
        if (node->attach.animator.index)
            local = nv_mat4_mul(node->attach.joint_model, local);
        world = nv_mat4_mul(local, world);
        index = node->parent;
    }
    return world;
}

void nv_anim_update_scene(NvScene* scene, f32 dt)
{
    for (u32 a = 1; a < g_animator_count; ++a) {
        NvAnimator* animator = &g_animators[a].pub;
        if (animator->scene != scene)
            continue;
        NvNode* owner = animator->owner.index ? nv_scene_get(scene, animator->owner) : nullptr;

        NvLookAt* look = &animator->look_at;
        if (look->target_node.index && owner) {
            nv_scene_get(scene, look->target_node); // asserts the id is live
            NvMat4 to_model = nv_mat4_inverse(current_world(scene, animator->owner.index));
            NvVec3 target = nv_mat4_translation(current_world(scene, look->target_node.index));
            look->target = nv_mat4_transform_point(to_model, target);
        }

        nv_anim_update(animator, dt);

        if (owner)
            owner->position = nv_vec3_add(owner->position, nv_quat_rotate(owner->rotation, animator->root_motion));
        animator->root_motion = nv_vec3(0, 0, 0);
    }

    for (u32 index = 1; index <= scene->node_count; ++index) {
        NvNode* node = &scene->nodes[index];
        if (!(node->gen & 1) || !node->attach.animator.index)
            continue;
        NvAnimator* animator = nv_anim_get(node->attach.animator);
        NV_ASSERT(animator->scene == scene && node->attach.joint < animator->joint_count);
        node->attach.joint_model = animator->joint_model[node->attach.joint];
    }
}

const NvSkin* nv_anim_skins(void)
{
    return g_skins;
}

} // extern "C"
