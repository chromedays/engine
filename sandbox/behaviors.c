// The behaviors of a scene (docs/specs/scene_file.md): what runs on its nodes from the values the file gave. `spin` turns a node
// about an axis while the scene plays; `character` drives a model's animator (the clip it plays, crossfades and blends, root
// motion, jumps, where the head looks) and has its Inspector section. The generators (`grid`, `chain`, `crowd`, `effects`) make
// nodes and effects to load the engine with: their counts are parameters, and rows of the Stress tab and of their node's Inspector
// section. A behavior's parameters are its values now; the file's are in the SceneDesc.

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
    NvNode* root = nv_scene_get(sandbox_doc(sandbox)->scene, behavior->node);
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
// Generators
//

// Cube `i` of a grid, a child of the grid's node.
internal void add_cube(SceneDoc* doc, Behavior* behavior, u32 i)
{
    const GridDesc* grid = &behavior->desc.grid;
    GridState* state = &behavior->grid;
    NvScene* scene = doc->scene;
    NvNodeId id = nv_scene_add_node(scene, behavior->node, "cube");
    NvNode* node = nv_scene_get(scene, id);
    u32 column = i % grid->columns;
    u32 row = i / grid->columns;
    node->position = nv_vec3(((f32)column - (f32)(grid->columns - 1) * 0.5f) * grid->spacing, grid->height, -(f32)row * grid->spacing);
    node->scale = nv_vec3(grid->scale, grid->scale, grid->scale);
    node->mesh = state->mesh;
    node->material = state->materials[state->painted ? 1 + i % state->painted : 0];
    state->nodes[i] = id;
}

// Makes the grid hold `target` cubes painted with `colors` colors (0 = one).
internal void update_grid(SceneDoc* doc, Behavior* behavior, u32 target, u32 colors)
{
    GridState* state = &behavior->grid;
    NvScene* scene = doc->scene;
    if (colors != state->painted) {
        state->painted = colors;
        for (u32 i = 0; i < state->built; ++i)
            nv_scene_get(scene, state->nodes[i])->material = state->materials[colors ? 1 + i % colors : 0];
    }
    while (state->built > target)
        nv_scene_remove_node(scene, state->nodes[--state->built]);
    while (state->built < target)
        add_cube(doc, behavior, state->built++);
}

// Removes and re-adds cubes, so node slots keep being freed and reused.
internal void churn_grid(SceneDoc* doc, Behavior* behavior)
{
    GridState* state = &behavior->grid;
    u32 count = behavior->desc.grid.churn;
    if (count > state->built)
        count = state->built;
    for (u32 k = 0; k < count; ++k) {
        u32 i = state->churn_cursor++ % state->built;
        nv_scene_remove_node(doc->scene, state->nodes[i]);
        add_cube(doc, behavior, i);
    }
}

// A helix: each link is the child of the last, so moving the first moves them all.
internal void resize_chain(SceneDoc* doc, Behavior* behavior, u32 target)
{
    const ChainDesc* chain = &behavior->desc.chain;
    ChainState* state = &behavior->chain;
    NvScene* scene = doc->scene;
    while (state->built > target)
        nv_scene_remove_node(scene, state->nodes[--state->built]);
    while (state->built < target) {
        u32 i = state->built++;
        NvNodeId parent = i ? state->nodes[i - 1] : behavior->node;
        state->nodes[i] = nv_scene_add_node(scene, parent, "link");
        NvNode* node = nv_scene_get(scene, state->nodes[i]);
        node->position = i ? chain->link : nv_vec3(0, 0, 0);
        node->mesh = state->mesh;
        node->material = state->materials[1 + (i * chain->stride) % (state->material_count - 1)];
    }
}

// The links twist by an angle that wobbles with the chain's time, which runs only while playing.
internal void wobble_chain(SceneDoc* doc, Behavior* behavior, f32 dt)
{
    const ChainDesc* chain = &behavior->desc.chain;
    ChainState* state = &behavior->chain;
    state->time += dt;
    NvQuat twist = nv_quat_axis_angle(nv_vec3(0, 1, 0), chain->twist + chain->wobble * sinf(state->time));
    for (u32 i = 0; i < state->built; ++i)
        nv_scene_get(doc->scene, state->nodes[i])->rotation = twist;
}

// Characters are made on first use and then kept: an animator cannot be destroyed. The ones not asked for are hidden and paused.
internal void update_crowd(Sandbox* sandbox, SceneDoc* doc, Behavior* behavior, u32 target)
{
    const CrowdDesc* crowd = &behavior->desc.crowd;
    CrowdState* state = &behavior->crowd;
    NvScene* scene = doc->scene;
    Asset* asset = state->asset;
    const ModelDesc* model = &doc->desc->models[crowd->model_index];
    while (state->created < target && !state->failed) {
        u32 i = state->created;
        NvGltfModel* made = &state->models[i];
        if (!asset_instance(sandbox, model, asset, scene, behavior->node, made)) {
            state->failed = 1;
            break;
        }
        ++state->created;
        NvNode* root = nv_scene_get(scene, made->root);
        u32 column = i % crowd->columns;
        u32 row = i / crowd->columns;
        root->position = nv_vec3(((f32)column - (f32)(crowd->columns - 1) * 0.5f) * crowd->spacing, 0.0f, (f32)row * crowd->spacing);
        snprintf(root->name, sizeof(root->name), "character %u", i + 1);

        // Every character loops its own clip from its own point in time.
        NvClipId loops[SANDBOX_MAX_CLIPS];
        u32 loop_count = 0;
        for (u32 c = 0; c < asset->clip_count; ++c) {
            if (is_loop(asset->clips[c]))
                loops[loop_count++] = asset->clips[c];
        }
        if (loop_count && made->animator.index) {
            NvAnimator* animator = nv_anim_get(made->animator);
            NvClipId clip = loops[i % loop_count];
            nv_anim_play(animator, clip, 0.0f, 1);
            animator->layers[0].time = fmodf((f32)i * crowd->offset, nv_anim_clip_duration(clip));
        }
    }
    for (u32 i = 0; i < state->created; ++i) {
        const NvGltfModel* made = &state->models[i];
        b32 active = i < target;
        if (made->animator.index)
            nv_anim_get(made->animator)->scene = active ? scene : NULL;
        for (u32 m = 0; m < made->mesh_node_count; ++m)
            nv_scene_get(scene, made->mesh_nodes[m])->mesh = active ? asset->meshes[m] : (NvMeshId){0};
    }
    state->active = target < state->created ? target : state->created;
}

//
// The effects workload
//

// A point on the ground in the field around the effects node.
internal NvVec3 field_point(Sandbox* sandbox, NvVec3 center, const EffectsDesc* effects, f32 height)
{
    NvRandom* rng = &sandbox->effects.rng;
    return nv_vec3(center.x + (nv_random_f32(rng) * 2.0f - 1.0f) * effects->area[0], height,
                   center.z + (nv_random_f32(rng) * 2.0f - 1.0f) * effects->area[1]);
}

internal void new_flight(Sandbox* sandbox, NvVec3 center, const EffectsDesc* effects, Flight* flight)
{
    NvRandom* rng = &sandbox->effects.rng;
    f32 angle = nv_random_f32(rng) * 6.2831853f;
    f32 speed = 6.0f + nv_random_f32(rng) * 4.0f;
    flight->pos = field_point(sandbox, center, effects, 0.3f);
    flight->vel = nv_vec3(cosf(angle) * speed, 3.0f + nv_random_f32(rng) * 2.0f, sinf(angle) * speed);
    flight->life = 1.2f + nv_random_f32(rng) * 0.8f;
}

// Keeps the asked numbers of missiles, beams, particles, explosions and decals going around the node. Only while playing.
internal void update_effects(Sandbox* sandbox, SceneDoc* doc, Behavior* behavior, f32 dt)
{
    const EffectsDesc* effects_desc = &behavior->desc.effects;
    EffectsState* state = &behavior->effects;
    Effects* effects = &sandbox->effects;
    NvVfx* vfx = &sandbox->vfx;
    NvVec3 center = nv_mat4_translation(nv_scene_get(doc->scene, behavior->node)->world);
    if (dt > 0.1f)
        dt = 0.1f;
    b32 on = effects_desc->on;
    u32 missiles = on ? effects_desc->missiles : 0;
    if (missiles > EFFECTS_MAX_MISSILES)
        missiles = EFFECTS_MAX_MISSILES;
    u32 beams = on ? effects_desc->beams : 0;

    // Missiles that fly, leave a trail and smoke, and start again from another point when their time is up.
    if (state->flight_count > missiles)
        state->flight_count = missiles;
    while (state->flight_count < missiles) {
        Flight* flight = &state->flights[state->flight_count++];
        new_flight(sandbox, center, effects_desc, flight);
        flight->life *= nv_random_f32(&effects->rng); // out of step with each other
    }
    for (u32 i = 0; i < state->flight_count; ++i) {
        Flight* flight = &state->flights[i];
        NvVec3 before = flight->pos;
        flight->vel.y -= 4.0f * dt;
        flight->pos = nv_vec3_add(flight->pos, nv_vec3_scale(flight->vel, dt));
        flight->life -= dt;
        if (dt > 0.0f) {
            NvVec3 step = nv_vec3_sub(flight->pos, before);
            // A thinner smoke than the Missile button's: 2,000 of them would fill the particle buffer.
            nv_vfx_emit(vfx, effects->missile, before, flight->pos, (u32)(sqrtf(nv_vec3_dot(step, step)) * 12.0f) + 1);
            nv_vfx_trail(vfx, &effects->missile_trail, before, flight->pos);
        }
        if (flight->life <= 0.0f || flight->pos.y < 0.05f)
            new_flight(sandbox, center, effects_desc, flight);
    }

    // Beams: each is held by calling again every frame with a short life, between fixed points of its own.
    for (u32 i = 0; i < beams; ++i) {
        u32 h = i * 2654435761u + 12345u;
        f32 ax = (f32)(h & 1023u) / 1023.0f, az = (f32)((h >> 10) & 1023u) / 1023.0f;
        f32 bx = (f32)((h >> 20) & 1023u) / 1023.0f, bz = (f32)((h * 40503u >> 8) & 1023u) / 1023.0f;
        NvVec3 from = nv_vec3(center.x + (ax * 2.0f - 1.0f) * effects_desc->area[0], 7.0f, center.z + (az * 2.0f - 1.0f) * effects_desc->area[1]);
        NvVec3 to = nv_vec3(center.x + (bx * 2.0f - 1.0f) * effects_desc->area[0], 0.05f, center.z + (bz * 2.0f - 1.0f) * effects_desc->area[1]);
        nv_vfx_beam(vfx, &effects->laser, from, to, dt * 1.5f + 0.02f);
    }

    if (!on)
        return;

    // Particles: embers in bursts of 512, at the rate that replaces what dies (they live about 2.5 s).
    f32 rate = (f32)effects_desc->particles / 2.5f;
    state->swarm_carry += rate * dt / 512.0f;
    u32 limit = 4000; // bursts a frame
    while (state->swarm_carry >= 1.0f) {
        state->swarm_carry -= 1.0f;
        if (limit) {
            nv_vfx_burst(vfx, effects->swarm, field_point(sandbox, center, effects_desc, 0.05f), nv_vec3(0.0f, 1.0f, 0.0f), 1.0f);
            --limit;
        }
    }
    state->explosion_carry += (f32)effects_desc->explosions * dt;
    for (u32 n = 0; state->explosion_carry >= 1.0f && n < 300; ++n) {
        state->explosion_carry -= 1.0f;
        nv_vfx_burst(vfx, effects->explosion, field_point(sandbox, center, effects_desc, 0.05f), nv_vec3(0.0f, 1.0f, 0.0f), 1.0f);
    }
    if (state->explosion_carry > 1.0f)
        state->explosion_carry = 0.0f;
    state->decal_carry += (f32)effects_desc->decals * dt;
    for (u32 n = 0; state->decal_carry >= 1.0f && n < 2000; ++n) {
        state->decal_carry -= 1.0f;
        effects_scorch(sandbox, field_point(sandbox, center, effects_desc, 0.0f), 0.6f + nv_random_f32(&effects->rng) * 1.2f);
    }
}

// Every generator's counts, in the node budget (docs/specs/scene_file.md): crowds first, then chains, then grids, each holding
// what the nodes and animators left allow. The frame's order keeps the node count under the limit on the way: shrink, grow.
internal void update_generators(Sandbox* sandbox, SceneDoc* doc, f32 dt)
{
    u32 targets[SCENE_MAX_BEHAVIORS] = {0};
    s32 nodes_left = (s32)(NV_MAX_NODES - 1 - doc->authored_count);
    u32 animators_left = nv_anim_free_animators();
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind != BEHAVIOR_CROWD)
            continue;
        const CrowdDesc* crowd = &behavior->desc.crowd;
        const CrowdState* state = &behavior->crowd;
        u32 want = crowd->on ? crowd->count : 0;
        if (want > crowd->max)
            want = crowd->max;
        if (want > state->created + animators_left)
            want = state->created + animators_left;
        if (want > state->created)
            animators_left -= want - state->created;
        // A character is its root and its meshes; the ones made keep their nodes whether asked for or not.
        u32 per = state->asset->loaded ? state->asset->model.mesh_node_count + 1 : 1;
        u32 held = (want > state->created ? want : state->created) * per;
        nodes_left -= (s32)held;
        targets[i] = want;
    }
    if (nodes_left < 0)
        nodes_left = 0;
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        const Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind != BEHAVIOR_CHAIN)
            continue;
        u32 want = behavior->desc.chain.on ? behavior->desc.chain.count : 0;
        if (want > behavior->desc.chain.max)
            want = behavior->desc.chain.max;
        if (want > (u32)nodes_left)
            want = (u32)nodes_left;
        nodes_left -= (s32)want;
        targets[i] = want;
    }
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        const Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind != BEHAVIOR_GRID)
            continue;
        u32 want = behavior->desc.grid.on ? behavior->desc.grid.count : 0;
        if (want > behavior->desc.grid.max)
            want = behavior->desc.grid.max;
        if (want > (u32)nodes_left)
            want = (u32)nodes_left;
        nodes_left -= (s32)want;
        targets[i] = want;
    }

    // Shrink before growing, so the node count never passes the limit on the way.
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind == BEHAVIOR_GRID && targets[i] < behavior->grid.built)
            update_grid(doc, behavior, targets[i], behavior->grid.painted);
        else if (behavior->desc.kind == BEHAVIOR_CHAIN && targets[i] < behavior->chain.built)
            resize_chain(doc, behavior, targets[i]);
    }
    for (u32 i = 0; i < doc->behavior_count; ++i)
        if (doc->behaviors[i].desc.kind == BEHAVIOR_CROWD)
            update_crowd(sandbox, doc, &doc->behaviors[i], targets[i]);
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind == BEHAVIOR_CHAIN) {
            resize_chain(doc, behavior, targets[i]);
            wobble_chain(doc, behavior, sandbox->playing ? dt : 0.0f);
        }
    }
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind != BEHAVIOR_GRID)
            continue;
        const GridDesc* grid = &behavior->desc.grid;
        u32 colors = grid->colors_on ? grid->colors : 0;
        if (colors > behavior->grid.material_count - 1)
            colors = behavior->grid.material_count - 1;
        update_grid(doc, behavior, targets[i], colors);
        if (sandbox->playing && grid->churn_on)
            churn_grid(doc, behavior);
    }
    if (!sandbox->playing)
        return;
    for (u32 i = 0; i < doc->behavior_count; ++i)
        if (doc->behaviors[i].desc.kind == BEHAVIOR_EFFECTS)
            update_effects(sandbox, doc, &doc->behaviors[i], dt);
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
    NvScene* scene = doc->scene;
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
            break; // the generators: below, together
        }
    }
    update_generators(sandbox, doc, dt);
    // What is shown: a node's own mesh, unless it is hidden or a behavior hides it.
    for (u32 i = 0; i < doc->node_count; ++i) {
        NodeRuntime* runtime = &doc->nodes[i];
        if (runtime->mesh.index)
            nv_scene_get(scene, runtime->id)->mesh = runtime->visible && !runtime->suppressed ? runtime->mesh : (NvMeshId){0};
    }
}

void behaviors_play_start(Sandbox* sandbox, SceneDoc* doc)
{
    NvScene* scene = doc->scene;
    (void)sandbox;
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (behavior->desc.kind == BEHAVIOR_SPIN) {
            behavior->spin.authored = nv_scene_get(scene, behavior->node)->rotation;
            behavior->spin.angle = 0.0f;
        } else if (behavior->desc.kind == BEHAVIOR_CHAIN) {
            behavior->chain.time = 0.0f;
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
        else if (behavior->desc.kind == BEHAVIOR_CHAIN)
            behavior->chain.time = 0.0f;
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

// Every joint's line to its parent, for an animator of the scene.
internal void draw_skeleton(Sandbox* sandbox, NvScene* scene, NvAnimatorId id)
{
    NvAnimator* animator = nv_anim_get(id);
    const NvJointDesc* joints = nv_anim_joints(animator->skeleton);
    NvMat4 root = nv_scene_get(scene, animator->owner)->world;
    for (u32 j = 0; j < animator->joint_count; ++j) {
        if (joints[j].parent < 0)
            continue;
        NvVec3 a = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[joints[j].parent]));
        NvVec3 p = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[j]));
        nv_renderer_debug_line(&sandbox->renderer, a, p, nv_vec3(1.0f, 0.8f, 0.2f));
    }
}

void behaviors_draw_bones(Sandbox* sandbox, SceneDoc* doc)
{
    NvScene* scene = doc->scene;
    for (u32 b = 0; b < doc->behavior_count; ++b) {
        Behavior* behavior = &doc->behaviors[b];
        if (behavior->desc.kind == BEHAVIOR_CROWD) {
            for (u32 i = 0; i < behavior->crowd.active; ++i)
                draw_skeleton(sandbox, scene, behavior->crowd.models[i].animator);
            continue;
        }
        if (behavior->desc.kind != BEHAVIOR_CHARACTER)
            continue;
        NvAnimator* animator = nv_anim_get(behavior->character.model.animator);
        NvMat4 root = nv_scene_get(scene, animator->owner)->world;
        draw_skeleton(sandbox, scene, behavior->character.model.animator);

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

//
// Generator parameters in the UI
//

void* behavior_field(Behavior* behavior, const SceneField* field)
{
    return (u8*)&behavior->desc.spin + field->offset; // every desc starts where the union does
}

const SceneField* behavior_parameter(const Behavior* behavior, const char* key)
{
    const BehaviorDef* def = scene_behavior_def(behavior->desc.kind);
    for (u32 f = 0; f < def->field_count; ++f)
        if (def->fields[f].parameter && strcmp(def->fields[f].key, key) == 0)
            return &def->fields[f];
    return NULL;
}

// The bytes a field takes in its desc.
internal u32 field_size(const SceneField* field)
{
    switch (field->kind) {
    case FIELD_NAME: return SCENE_ID_SIZE;
    case FIELD_BOOL: return 4;
    default: return field->count * 4;
    }
}

b32 behavior_parameters_equal(const BehaviorDesc* a, const BehaviorDesc* b)
{
    if (a->kind != b->kind)
        return 0;
    const BehaviorDef* def = scene_behavior_def(a->kind);
    for (u32 f = 0; f < def->field_count; ++f) {
        const SceneField* field = &def->fields[f];
        if (field->parameter && memcmp((const u8*)&a->spin + field->offset, (const u8*)&b->spin + field->offset, field_size(field)) != 0)
            return 0;
    }
    return 1;
}

internal u32 generator_max(const Behavior* behavior)
{
    switch (behavior->desc.kind) {
    case BEHAVIOR_GRID: return behavior->desc.grid.max;
    case BEHAVIOR_CHAIN: return behavior->desc.chain.max;
    case BEHAVIOR_CROWD: return behavior->desc.crowd.max;
    default: return 0;
    }
}

void generator_rows(Sandbox* sandbox, Behavior* behavior, u32 index)
{
    const BehaviorDef* def = scene_behavior_def(behavior->desc.kind);
    igPushID_Int((int)index);
    for (u32 f = 0; f < def->field_count; ++f) {
        const SceneField* field = &def->fields[f];
        if (!field->parameter || !field->label)
            continue;
        u32* value = behavior_field(behavior, field);
        if (!search_row(sandbox, field->label, field->keywords))
            continue;
        if (field->kind == FIELD_BOOL) {
            bool flag = *value != 0;
            if (igCheckbox(TL(field->label), &flag))
                *value = flag;
        } else {
            // A count goes up to what the generator can hold; the colors to what its palette has.
            int low = (int)field->min;
            int high = (int)field->max;
            if (strcmp(field->key, "count") == 0)
                high = (int)generator_max(behavior);
            else if (behavior->desc.kind == BEHAVIOR_GRID && strcmp(field->key, "colors") == 0)
                high = (int)behavior->grid.material_count - 1;
            int number = (int)*value;
            if (igSliderInt(TL(field->label), &number, low, high, field->format, field->logarithmic ? ImGuiSliderFlags_Logarithmic : 0))
                *value = (u32)number;
        }
    }
    igPopID();
}

// What a generator has made so far.
void generator_status(Behavior* behavior, char* out, umm capacity)
{
    switch (behavior->desc.kind) {
    case BEHAVIOR_GRID: snprintf(out, capacity, T("Built: %u cubes"), behavior->grid.built); break;
    case BEHAVIOR_CHAIN: snprintf(out, capacity, T("Built: %u links"), behavior->chain.built); break;
    case BEHAVIOR_CROWD:
        snprintf(out, capacity, T("Built: %u of %u characters"), behavior->crowd.active, behavior->crowd.created);
        break;
    default: out[0] = 0; break;
    }
}

// A generator's Inspector section: the same rows as the Stress tab's.
void generator_section(Sandbox* sandbox, Behavior* behavior, u32 index)
{
    switch (behavior->desc.kind) {
    case BEHAVIOR_GRID: search_section(sandbox, "Grid"); break;
    case BEHAVIOR_CHAIN: search_section(sandbox, "Chain"); break;
    case BEHAVIOR_CROWD: search_section(sandbox, "Crowd"); break;
    case BEHAVIOR_EFFECTS: search_section(sandbox, "Effects workload"); break;
    default: return;
    }
    generator_rows(sandbox, behavior, index);
    char status[64];
    generator_status(behavior, status, sizeof(status));
    if (status[0] && search_plain(sandbox))
        igTextDisabled("%s", status);
}
