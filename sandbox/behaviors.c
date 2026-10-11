// The behaviors of a scene (docs/specs/scene_file.md): what runs on its nodes from the values the file gave. `spin` turns a node
// about an axis while the scene plays; `character` drives a model's animator (the clip it plays, crossfades and blends, root
// motion, jumps, where the head looks) and has its Inspector section. A behavior's parameters are its values now; the file's
// are in the SceneDesc.

#include "sandbox.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

//
// Clips
//

NvClipId character_find_clip(const CharacterState* state, const char* name)
{
    for (u32 i = 0; i < state->asset->clip_count; ++i) {
        if (strcmp(nv_anim_clip_name(state->asset->clips[i]), name) == 0)
            return state->asset->clips[i];
    }
    return (NvClipId){0};
}

internal NvClipId find_root_motion_clip(const CharacterState* state, const char* name)
{
    for (u32 i = 0; i < state->asset->root_motion_clip_count; ++i) {
        if (strcmp(nv_anim_clip_name(state->asset->root_motion_clips[i]), name) == 0)
            return state->asset->root_motion_clips[i];
    }
    return (NvClipId){0};
}

internal b32 is_loop(NvClipId clip)
{
    return strstr(nv_anim_clip_name(clip), "_Loop") != NULL;
}

NvClipId character_regular_clip(const CharacterState* state, NvClipId clip)
{
    NvClipId regular = character_find_clip(state, nv_anim_clip_name(clip));
    return regular.index ? regular : clip;
}

void character_play(Sandbox* sandbox, Behavior* behavior, NvClipId clip)
{
    CharacterState* state = &behavior->character;
    CharacterDesc* c = &behavior->desc.character;
    // Root motion moves the node, which only happens while playing; Edit mode previews in place.
    NvClipId moving = find_root_motion_clip(state, nv_anim_clip_name(clip));
    if (c->root_motion && sandbox->playing && moving.index)
        clip = moving;
    state->jump = JUMP_NONE;
    nv_anim_play(nv_anim_get(state->model.animator), clip, c->fade, is_loop(clip));
    // The parameter names the clip the character is set to play, whatever copy of it plays.
    snprintf(c->clip, sizeof(c->clip), "%s", nv_anim_clip_name(character_regular_clip(state, clip)));
}

// Jump is three clips chained: take-off, a short loop in the air, landing, then back to the loop that was playing before.
void character_jump(Sandbox* sandbox, Behavior* behavior)
{
    CharacterState* state = &behavior->character;
    CharacterDesc* c = &behavior->desc.character;
    if (!c->jump.clip[0][0] || state->jump != JUMP_NONE || !sandbox->playing)
        return;
    NvAnimator* animator = nv_anim_get(state->model.animator);
    state->jump_return = animator->layers[0].clip;
    nv_anim_play(animator, character_find_clip(state, c->jump.clip[0]), 0.1f, 0);
    state->jump = JUMP_START;
}

internal void update_jump(Behavior* behavior, f32 dt)
{
    CharacterState* state = &behavior->character;
    CharacterDesc* c = &behavior->desc.character;
    NvAnimator* animator = nv_anim_get(state->model.animator);
    NvAnimLayer* layer = &animator->layers[0];
    b32 finished = layer->clip.index && layer->time >= nv_anim_clip_duration(layer->clip) - 0.001f;
    switch (state->jump) {
    case JUMP_START:
        if (finished) {
            nv_anim_play(animator, character_find_clip(state, c->jump.clip[1]), 0.05f, 1);
            state->jump_air_time = 0.0f;
            state->jump = JUMP_AIR;
        }
        break;
    case JUMP_AIR:
        state->jump_air_time += dt;
        if (state->jump_air_time >= c->jump.air_time) {
            nv_anim_play(animator, character_find_clip(state, c->jump.clip[2]), 0.05f, 0);
            state->jump = JUMP_LAND;
        }
        break;
    case JUMP_LAND:
        if (finished) {
            nv_anim_play(animator, state->jump_return, 0.2f, is_loop(state->jump_return));
            state->jump = JUMP_NONE;
        }
        break;
    case JUMP_NONE:
        break;
    }
}

// Blends a second clip into the current one, time-synchronized so feet stay in step.
internal void update_blend(Behavior* behavior)
{
    CharacterState* state = &behavior->character;
    CharacterDesc* c = &behavior->desc.character;
    NvAnimator* animator = nv_anim_get(state->model.animator);
    NvAnimLayer* base = &animator->layers[0];
    NvAnimLayer* blend = &animator->layers[2];
    NvClipId other = state->asset->clips[state->blend_clip];
    b32 active = c->blend_weight > 0.0f && base->clip.index && other.index != base->clip.index && animator->fade_duration == 0.0f &&
                 state->jump == JUMP_NONE;
    if (!active) {
        *blend = (NvAnimLayer){0};
        if (animator->fade_duration == 0.0f)
            base->weight = 1.0f;
        return;
    }
    f32 base_duration = nv_anim_clip_duration(base->clip);
    f32 other_duration = nv_anim_clip_duration(other);
    *blend = (NvAnimLayer){
        .clip = other,
        .time = base->time / base_duration * other_duration,
        .speed = base->speed * other_duration / base_duration,
        .weight = c->blend_weight,
        .loop = 1,
    };
    base->weight = 1.0f - c->blend_weight;
}

void character_back_to_center(Sandbox* sandbox, Behavior* behavior)
{
    NvNode* root = nv_scene_get(sandbox->views[sandbox->shown].scene, behavior->node);
    root->position = nv_vec3(0, 0, 0);
    root->rotation = nv_quat_identity();
}

// Turns the character while root motion walks it (nv_anim_update_scene moves it).
internal void apply_turn(Sandbox* sandbox, Behavior* behavior, NvScene* scene, f32 dt)
{
    (void)sandbox;
    CharacterDesc* c = &behavior->desc.character;
    NvNode* root = nv_scene_get(scene, behavior->node);
    if (c->root_motion)
        root->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), c->turn * dt), root->rotation);
}

// The head's aim IK follows the target. While playing, the target sweeps in front of the character; in Edit mode it stays where it
// was put. With look-at off the target is not drawn.
internal void update_look_target(Sandbox* sandbox, Behavior* behavior, SceneDoc* doc, NvScene* scene)
{
    CharacterState* state = &behavior->character;
    CharacterDesc* c = &behavior->desc.character;
    if (!state->look_target.index)
        return;
    NvAnimator* animator = nv_anim_get(state->model.animator);
    NodeRuntime* runtime = doc_node(doc, state->look_target);
    if (runtime)
        runtime->suppressed = !c->look_at;
    animator->look_at.enabled = c->look_at;
    if (!c->look_at || !sandbox->playing)
        return;
    NvNode* target = nv_scene_get(scene, state->look_target);
    f32 t = sandbox->play_time;
    f32 angle = sinf(t * 0.7f) * 1.3f;
    NvVec3 local = nv_vec3(sinf(angle) * 1.2f, 1.75f + 0.3f * sinf(t * 1.3f), cosf(angle) * 1.2f);
    NvNode* root = nv_scene_get(scene, behavior->node);
    target->position = nv_vec3_add(root->position, nv_quat_rotate(root->rotation, local));
    target->rotation = nv_quat_axis_angle(nv_vec3(0, 1, 0), t * 2.0f);
}

// Restarts the character's clip from its start with no crossfade, as a run begins and ends. With `playing` set, root motion
// picks the clip's moving copy.
internal void restart_clip(Sandbox* sandbox, Behavior* behavior)
{
    CharacterState* state = &behavior->character;
    CharacterDesc* c = &behavior->desc.character;
    NvAnimator* animator = nv_anim_get(state->model.animator);
    NvClipId clip = state->jump != JUMP_NONE ? state->jump_return : animator->layers[0].clip;
    clip = character_regular_clip(state, clip);
    f32 fade = c->fade;
    c->fade = 0.0f;
    animator->layers[0].clip = (NvClipId){0}; // so the same clip restarts too
    character_play(sandbox, behavior, clip);
    c->fade = fade;
    animator->layers[0].time = 0.0f;
}

//
// The scene's behaviors
//

Behavior* behavior_find(SceneDoc* doc, BehaviorKind kind, NvNodeId node)
{
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind == kind && behavior->node.index == node.index && behavior->node.gen == node.gen)
            return behavior;
    }
    return NULL;
}

Behavior* behavior_character_of(SceneDoc* doc, NvAnimatorId animator)
{
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind == BEHAVIOR_CHARACTER && behavior->character.model.animator.index == animator.index)
            return behavior;
    }
    return NULL;
}

void behaviors_update(Sandbox* sandbox, SceneDoc* doc, f32 dt)
{
    NvScene* scene = sandbox->views[sandbox->shown].scene;
    for (u32 i = 0; i < doc->node_count; ++i)
        doc->nodes[i].suppressed = 0;
    if (sandbox->playing)
        sandbox->play_time += dt;
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        switch (behavior->desc.kind) {
        case BEHAVIOR_SPIN: {
            // The spin turns on top of the rotation authored in Edit mode, and only while playing.
            if (!sandbox->playing)
                break;
            SpinDesc* spin = &behavior->desc.spin;
            behavior->spin.angle += spin->speed * dt;
            nv_scene_get(scene, behavior->node)->rotation =
                nv_quat_mul(nv_quat_axis_angle(spin->axis, behavior->spin.angle), behavior->spin.authored);
        } break;
        case BEHAVIOR_CHARACTER: {
            CharacterDesc* c = &behavior->desc.character;
            if (sandbox->playing) {
                update_jump(behavior, dt);
                apply_turn(sandbox, behavior, scene, dt);
            }
            nv_anim_get(behavior->character.model.animator)->layers[0].speed = c->speed;
            update_blend(behavior);
            update_look_target(sandbox, behavior, doc, scene);
        } break;
        case BEHAVIOR_GRID:
        case BEHAVIOR_CHAIN:
        case BEHAVIOR_CROWD:
        case BEHAVIOR_EFFECTS:
        case BEHAVIOR_COUNT:
            break;
        }
    }
    // What is shown: a node's own mesh, unless it is hidden or a behavior hides it.
    for (u32 i = 0; i < doc->node_count; ++i) {
        NodeRuntime* runtime = &doc->nodes[i];
        if (runtime->mesh.index)
            nv_scene_get(scene, runtime->id)->mesh = runtime->visible && !runtime->suppressed ? runtime->mesh : (NvMeshId){0};
    }
}

void behaviors_play_start(Sandbox* sandbox, SceneDoc* doc)
{
    NvScene* scene = sandbox->views[sandbox->shown].scene;
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind == BEHAVIOR_SPIN) {
            behavior->spin.authored = nv_scene_get(scene, behavior->node)->rotation;
            behavior->spin.angle = 0.0f;
        }
    }
}

// After Play has begun (`playing` is set): each character restarts its clip.
void behaviors_restart_clips(Sandbox* sandbox, SceneDoc* doc)
{
    for (u32 i = 0; i < doc->behavior_count; ++i)
        if (doc->behaviors[i].desc.kind == BEHAVIOR_CHARACTER)
            restart_clip(sandbox, &doc->behaviors[i]);
}

// After Stop has put the scene back from its snapshot: what ran is over.
void behaviors_stop(Sandbox* sandbox, SceneDoc* doc)
{
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind == BEHAVIOR_SPIN)
            behavior->spin.angle = 0.0f;
        else if (behavior->desc.kind == BEHAVIOR_CHARACTER)
            behavior->character.jump = JUMP_NONE;
    }
    behaviors_restart_clips(sandbox, doc);
}

// The parameters were set from a save or an undo step: make what runs follow them.
void behavior_apply(Sandbox* sandbox, Behavior* behavior)
{
    if (behavior->desc.kind != BEHAVIOR_CHARACTER)
        return;
    CharacterState* state = &behavior->character;
    CharacterDesc* c = &behavior->desc.character;
    NvAnimator* animator = nv_anim_get(state->model.animator);
    state->blend_clip = 0;
    for (u32 i = 0; i < state->asset->clip_count; ++i)
        if (c->blend[0] && strcmp(nv_anim_clip_name(state->asset->clips[i]), c->blend) == 0)
            state->blend_clip = (s32)i;
    if (state->asset->clip_count)
        snprintf(c->blend, sizeof(c->blend), "%s", nv_anim_clip_name(state->asset->clips[state->blend_clip]));
    // Straight into the clip, without a crossfade from whatever played before. The same clip keeps playing where it is; root
    // motion on or off picks its copy.
    NvClipId clip = c->clip[0] ? character_find_clip(state, c->clip) : (NvClipId){0};
    if (clip.index) {
        f32 fade = c->fade;
        c->fade = 0.0f;
        character_play(sandbox, behavior, clip);
        c->fade = fade;
    }
    animator->layers[0].speed = c->speed;
}

//
// Bones
//

void behaviors_draw_bones(Sandbox* sandbox, SceneDoc* doc)
{
    NvScene* scene = sandbox->views[sandbox->shown].scene;
    for (u32 b = 0; b < doc->behavior_count; ++b) {
        Behavior* behavior = &doc->behaviors[b];
        if (behavior->desc.kind != BEHAVIOR_CHARACTER)
            continue;
        NvAnimator* animator = nv_anim_get(behavior->character.model.animator);
        const NvJointDesc* joints = nv_anim_joints(animator->skeleton);
        NvMat4 root = nv_scene_get(scene, animator->owner)->world;
        for (u32 j = 0; j < animator->joint_count; ++j) {
            if (joints[j].parent < 0)
                continue;
            NvVec3 a = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[joints[j].parent]));
            NvVec3 p = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[j]));
            nv_renderer_debug_line(&sandbox->renderer, a, p, nv_vec3(1.0f, 0.8f, 0.2f));
        }

        // Aim IK: the head's forward axis (green) against the direction to the target (red).
        NvLookAt* look = &animator->look_at;
        if (look->enabled) {
            NvMat4 head = nv_mat4_mul(root, animator->joint_model[look->joint]);
            NvVec3 f = look->forward;
            NvVec3 origin = nv_mat4_translation(head);
            NvVec3 forward = nv_vec3(head.e[0] * f.x + head.e[4] * f.y + head.e[8] * f.z, head.e[1] * f.x + head.e[5] * f.y + head.e[9] * f.z,
                                     head.e[2] * f.x + head.e[6] * f.y + head.e[10] * f.z);
            nv_renderer_debug_line(&sandbox->renderer, origin, nv_vec3_add(origin, nv_vec3_scale(nv_vec3_normalize(forward), 0.6f)), nv_vec3(0.2f, 1.0f, 0.3f));
            nv_renderer_debug_line(&sandbox->renderer, origin, nv_mat4_translation(nv_scene_get(scene, behavior->character.look_target)->world),
                                   nv_vec3(1.0f, 0.2f, 0.2f));
        }
    }
}

//
// Inspector sections
//

// Spin's Inspector section: its one parameter, the speed.
void spin_section(Sandbox* sandbox, Behavior* behavior)
{
    search_section(sandbox, "Spin");
    const SceneField* speed = &scene_behavior_def(BEHAVIOR_SPIN)->fields[1];
    if (search_row(sandbox, speed->label, speed->keywords))
        igSliderFloat(TL("Speed"), &behavior->desc.spin.speed, (f32)speed->ui_min, (f32)speed->ui_max, speed->format, 0);
}

// The character's Inspector section: its clips and the settings of its playback (docs/specs/animation.md).
void character_section(Sandbox* sandbox, Behavior* behavior)
{
    CharacterState* state = &behavior->character;
    CharacterDesc* c = &behavior->desc.character;
    NvAnimator* animator = nv_anim_get(state->model.animator);
    search_section(sandbox, "Animator");

    // Clips of this skeleton, without the root-motion copies (character_play picks those).
    NvClipId current = animator->layers[0].clip.index ? character_regular_clip(state, animator->layers[0].clip) : (NvClipId){0};
    u32 clip_count = nv_anim_clip_count();
    for (u32 i = 1; i <= clip_count; ++i) {
        NvClipId clip = {i};
        if (nv_anim_clip_skeleton(clip).index != animator->skeleton.index || nv_anim_clip_has_root_motion(clip))
            continue;
        if (search_group(sandbox, nv_anim_clip_name(clip), "clip animation play")) {
            if (igSelectable_Bool(nv_anim_clip_name(clip), clip.index == current.index, 0, (ImVec2_c){0, 0}))
                character_play(sandbox, behavior, clip);
        }
    }
    if (c->jump.clip[0][0]) {
        igBeginDisabled(!sandbox->playing); // a jump is something that happens while playing
        if (search_group(sandbox, "Jump", "play")) {
            if (igButton(TL("Jump"), (ImVec2_c){-1.0f, 0.0f}))
                character_jump(sandbox, behavior);
        }
        igEndDisabled();
    }

    const SceneField* fields = scene_behavior_def(BEHAVIOR_CHARACTER)->fields;
    const SceneField* speed = &fields[3];
    const SceneField* fade = &fields[4];
    const SceneField* weight = &fields[6];
    const SceneField* turn = &fields[8];
    if (search_row(sandbox, speed->label, speed->keywords))
        igSliderFloat(TL("Speed"), &c->speed, 0.0f, 2.0f, speed->format, 0);
    if (search_row(sandbox, fade->label, fade->keywords))
        igSliderFloat(TL("Fade"), &c->fade, 0.0f, 1.0f, fade->format, 0);
    if (search_row(sandbox, "Blend", "clip")) {
        const char* names[SANDBOX_MAX_CLIPS];
        for (u32 i = 0; i < state->asset->clip_count; ++i)
            names[i] = nv_anim_clip_name(state->asset->clips[i]);
        if (igCombo_Str_arr(TL("Blend"), &state->blend_clip, names, (int)state->asset->clip_count, -1))
            snprintf(c->blend, sizeof(c->blend), "%s", names[state->blend_clip]);
    }
    if (search_row(sandbox, "Weight", weight->keywords))
        igSliderFloat(TL("Weight"), &c->blend_weight, 0.0f, 1.0f, weight->format, 0);

    // What the layers play now is no setting: shown only without a search.
    if (search_plain(sandbox)) {
        f32 total = 0.0f;
        for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
            if (animator->layers[l].clip.index)
                total += animator->layers[l].weight;
        }
        for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
            NvAnimLayer* layer = &animator->layers[l];
            if (!layer->clip.index)
                continue;
            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%s %.2f", nv_anim_clip_name(layer->clip), layer->weight);
            igProgressBar(total > 0.0f ? layer->weight / total : 0.0f, (ImVec2_c){-1.0f, 0.0f}, overlay);
        }
    }

    bool root_motion = c->root_motion != 0;
    if (search_row(sandbox, "Root motion", "walk")) {
        if (igCheckbox(TL("Root motion"), &root_motion)) {
            c->root_motion = root_motion;
            if (state->jump == JUMP_NONE)
                character_play(sandbox, behavior, character_regular_clip(state, animator->layers[0].clip));
        }
    }
    igBeginDisabled(!c->root_motion);
    if (search_row(sandbox, turn->label, turn->keywords))
        igSliderFloat(TL("Turn"), &c->turn, (f32)turn->ui_min, (f32)turn->ui_max, turn->format, 0);
    if (search_group(sandbox, "Back to center", "root motion")) {
        if (igButton(TL("Back to center"), (ImVec2_c){-1.0f, 0.0f}))
            character_back_to_center(sandbox, behavior);
    }
    igEndDisabled();
    if (c->look_target[0][0]) {
        bool look_at = c->look_at != 0;
        if (search_row(sandbox, "Look at target", "head ik aim")) {
            if (igCheckbox(TL("Look at target"), &look_at))
                c->look_at = look_at;
        }
    }
}
