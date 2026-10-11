// Building the scenes from their files (docs/specs/scene_file.md): the models, the palettes, the nodes and their components, the
// behaviors, and the view. scene_file.c reads the text into plain data; this file makes the renderer's meshes and materials, the nodes
// of the scene, and the runtime of the behaviors from it. The glb files the models name are assets for the whole sandbox, loaded by
// the first node that needs one and copied by the others.

#include "sandbox.h"

#include <engine/file.h>
#include <engine/mesh.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// The scenes of the sandbox, in the order the pickers list them. The first is the one shown at start.
global const char* const scene_files[] = {"/data/showcase.nvscene", "/data/stress.nvscene"};

//
// Assets
//

internal Asset* find_asset(Sandbox* sandbox, const char* path)
{
    for (u32 i = 0; i < sandbox->asset_count; ++i)
        if (strcmp(sandbox->assets[i].path, path) == 0)
            return &sandbox->assets[i];
    return NULL;
}

// The asset for a file model: the one already there, or a new, not yet loaded one.
Asset* asset_of(Sandbox* sandbox, const ModelDesc* model)
{
    Asset* asset = find_asset(sandbox, model->file);
    if (asset)
        return asset;
    NV_ASSERT(sandbox->asset_count < SANDBOX_MAX_ASSETS);
    asset = &sandbox->assets[sandbox->asset_count++];
    snprintf(asset->path, sizeof(asset->path), "%s", model->file);
    return asset;
}

// One instance of a file model, a new root under `parent` in `scene`. The first one loads the glb, with its clips; the others copy
// it. Fails (logged) when the file does not load.
b32 asset_instance(Sandbox* sandbox, const ModelDesc* model, Asset* asset, NvScene* scene, NvNodeId parent, NvGltfModel* out)
{
    if (asset->loaded) {
        nv_gltf_instantiate(&asset->model, scene, parent, out);
        return 1;
    }
    NvGltfLoad load = nv_gltf_load_model(model->file, scene, parent, &sandbox->renderer, &sandbox->permanent, &sandbox->scratch);
    if (!load.ok) {
        nv_log(NV_LOG_ERROR, "scene", "cannot load the model %s", model->file);
        return 0;
    }
    asset->model = load.model;
    asset->loaded = 1;
    for (u32 m = 0; m < load.model.mesh_node_count; ++m)
        asset->meshes[m] = nv_scene_get(scene, load.model.mesh_nodes[m])->mesh;
    *out = load.model;
    if (load.model.skeleton.index) {
        // A file that does not load leaves no clips (the load logged why).
        if (model->clips[0])
            asset->clip_count = nv_gltf_load_clips(model->clips, load.model.skeleton, NULL, &sandbox->scratch, asset->clips, SANDBOX_MAX_CLIPS).count;
        if (model->root_motion.path[0])
            asset->root_motion_clip_count = nv_gltf_load_clips(model->root_motion.path, load.model.skeleton, model->root_motion.joint,
                                                               &sandbox->scratch, asset->root_motion_clips, SANDBOX_MAX_CLIPS).count;
    }
    return 1;
}

//
// Shape models
//

internal NvMeshId make_shape_mesh(Sandbox* sandbox, const ModelDesc* model)
{
    umm mark = sandbox->scratch.used;
    NvMeshBuilder mesh = {
        .vertices = NV_PUSH_ARRAY(&sandbox->scratch, SCENE_MAX_VERTICES, NvVertex),
        .vertex_capacity = SCENE_MAX_VERTICES,
        .indices = NV_PUSH_ARRAY(&sandbox->scratch, SCENE_MAX_VERTICES * 6, u32), // no shape has more than six indices a vertex
        .index_capacity = SCENE_MAX_VERTICES * 6,
    };
    for (u32 i = 0; i < model->shape_count; ++i) {
        const f32* v = model->shapes[i].value;
        NvVec3 center = nv_vec3(v[0], v[1], v[2]);
        switch (model->shapes[i].kind) {
        case SHAPE_BOX: nv_mesh_append_box(&mesh, center, nv_vec3(v[3], v[4], v[5])); break;
        case SHAPE_PLANE: nv_mesh_append_plane(&mesh, v[0], v[1]); break;
        case SHAPE_SPHERE: nv_mesh_append_sphere(&mesh, center, v[3], (u32)v[4], (u32)v[5]); break;
        case SHAPE_CYLINDER: nv_mesh_append_cylinder(&mesh, center, v[3], v[4], (u32)v[5]); break;
        case SHAPE_CONE: nv_mesh_append_cone(&mesh, center, v[3], v[4], (u32)v[5]); break;
        case SHAPE_CAPSULE: nv_mesh_append_capsule(&mesh, center, v[3], v[4], (u32)v[5], (u32)v[6]); break;
        case SHAPE_TORUS: nv_mesh_append_torus(&mesh, center, v[3], v[4], (u32)v[5], (u32)v[6]); break;
        case SHAPE_COUNT: NV_INVALID_CODE_PATH; break;
        }
    }
    NvMeshData data = nv_mesh_builder_data(&mesh);
    NvMeshId id = nv_renderer_add_mesh(&sandbox->renderer, &data);
    sandbox->scratch.used = mark;
    return id;
}

//
// Behaviors
//

// The joint-space axis (+-X, +-Y or +-Z) that points most along `direction` in model space.
internal NvVec3 joint_axis_towards(NvMat4 joint_model, NvVec3 direction)
{
    NvVec3 best = nv_vec3(1, 0, 0);
    f32 best_dot = -2.0f;
    for (u32 axis = 0; axis < 3; ++axis) {
        NvVec3 column = nv_vec3_normalize(nv_vec3(joint_model.e[axis * 4], joint_model.e[axis * 4 + 1], joint_model.e[axis * 4 + 2]));
        f32 d = nv_vec3_dot(column, direction);
        for (s32 sign = -1; sign <= 1; sign += 2) {
            if (d * (f32)sign > best_dot) {
                best_dot = d * (f32)sign;
                best = nv_vec3(axis == 0 ? (f32)sign : 0.0f, axis == 1 ? (f32)sign : 0.0f, axis == 2 ? (f32)sign : 0.0f);
            }
        }
    }
    return best;
}

// A character: the animator of its model, aim IK from the rest pose (before any clip plays) and the clip it starts with.
internal b32 build_character(Behavior* behavior, const SceneDesc* desc, const NvGltfModel* model, Asset* asset,
                             const NvNodeId* node_ids)
{
    CharacterDesc* c = &behavior->desc.character;
    CharacterState* state = &behavior->character;
    state->model = *model;
    state->asset = asset;
    if (!model->animator.index) {
        nv_log(NV_LOG_ERROR, "scene", "the model %s of character '%s' has no skeleton", asset->path, desc->nodes[behavior->desc.node].id);
        return 0;
    }
    NvAnimator* animator = nv_anim_get(model->animator);
    if (c->look_target[0][0]) {
        s32 joint = nv_anim_find_joint(model->skeleton, c->look_target[0]);
        if (joint < 0) {
            nv_log(NV_LOG_ERROR, "scene", "the model %s has no joint '%s' for look_target", asset->path, c->look_target[0]);
            return 0;
        }
        NvMat4 head = animator->joint_model[joint];
        state->look_target = node_ids[c->look_node_index];
        animator->look_at = (NvLookAt){
            .joint = joint,
            .forward = joint_axis_towards(head, nv_vec3(0, 0, 1)),
            .up = joint_axis_towards(head, nv_vec3(0, 1, 0)),
            .weight = 1.0f,
            .target_node = state->look_target,
        };
    }
    // The clips the file names must be in the asset.
    if (c->clip[0] && !character_find_clip(state, c->clip).index) {
        nv_log(NV_LOG_ERROR, "scene", "the model %s has no clip '%s'", asset->path, c->clip);
        return 0;
    }
    if (c->jump.clip[0][0]) {
        for (u32 i = 0; i < 3; ++i) {
            if (!character_find_clip(state, c->jump.clip[i]).index) {
                nv_log(NV_LOG_ERROR, "scene", "the model %s has no clip '%s' for jump", asset->path, c->jump.clip[i]);
                return 0;
            }
        }
    }
    state->blend_clip = 0;
    for (u32 i = 0; i < asset->clip_count; ++i)
        if (c->blend[0] && strcmp(nv_anim_clip_name(asset->clips[i]), c->blend) == 0)
            state->blend_clip = (s32)i;
    // The parameter always names the clip that is blended in: the first, until another is chosen.
    if (asset->clip_count)
        snprintf(c->blend, sizeof(c->blend), "%s", nv_anim_clip_name(asset->clips[state->blend_clip]));
    animator->layers[0].speed = c->speed;
    return 1;
}

//
// Palettes
//

// A color wheel, so neighboring colors differ.
internal void color_wheel(u32 i, u32 count, f32* rgb)
{
    f32 h = (f32)i / (f32)count * 6.0f;
    f32 x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);
    f32 r = 0, g = 0, b = 0;
    switch ((u32)h % 6) {
    case 0: r = 1; g = x; break;
    case 1: r = x; g = 1; break;
    case 2: g = 1; b = x; break;
    case 3: g = x; b = 1; break;
    case 4: r = x; b = 1; break;
    default: r = 1; b = x; break;
    }
    rgb[0] = 0.15f + 0.8f * r;
    rgb[1] = 0.15f + 0.8f * g;
    rgb[2] = 0.15f + 0.8f * b;
}

// Color 0 is the base; the others go around the wheel.
internal void make_palette(Sandbox* sandbox, const PaletteDesc* palette, NvMaterialId* out)
{
    NvMaterialDesc base = {.base_color = {palette->base[0], palette->base[1], palette->base[2], 1.0f}};
    out[0] = nv_renderer_add_material(&sandbox->renderer, &base);
    for (u32 i = 1; i < palette->wheel; ++i) {
        NvMaterialDesc desc = {.base_color = {0, 0, 0, 1}};
        color_wheel(i, palette->wheel, desc.base_color);
        out[i] = nv_renderer_add_material(&sandbox->renderer, &desc);
    }
}

//
// Building a scene
//

internal b32 build_scene(Sandbox* sandbox, u32 index)
{
    SceneDoc* doc = &sandbox->docs[index];
    const char* path = scene_files[index];
    memset(doc, 0, sizeof(*doc)); // not a compound literal: SceneDoc is larger than the stack
    doc->file = path;
    NvArena* scratch = &sandbox->scratch;
    NvFileData file = nv_file_read(scratch, path);
    if (!file.ok) {
        nv_log(NV_LOG_ERROR, "scene", "cannot read the scene file %s", path);
        snprintf(doc->error, sizeof(doc->error), "cannot read the scene file %s", path);
        return 0;
    }
    SceneDesc* desc = NV_PUSH_STRUCT(&sandbox->permanent, SceneDesc);
    SceneRead read = scene_read(desc, path, (const char*)file.bytes, file.size);
    if (!read.ok) {
        snprintf(doc->error, sizeof(doc->error), "%s", read.first_error); // scene_read logged every error with its line
        return 0;
    }
    doc->desc = desc;
    NvScene* scene = NV_PUSH_STRUCT(&sandbox->permanent, NvScene);
    doc->scene = scene;
    doc->authored = NV_PUSH_ARRAY(&sandbox->permanent, NV_MAX_NODES, u8);

    // The shape models are meshes of their own; a file model is loaded where the first node that uses it is.
    for (u32 m = 0; m < desc->model_count; ++m)
        if (!desc->models[m].is_file)
            doc->model_meshes[m] = make_shape_mesh(sandbox, &desc->models[m]);
    for (u32 p = 0; p < desc->palette_count; ++p)
        make_palette(sandbox, &desc->palettes[p], doc->palette_materials[p]);

    // The nodes, in the file's order: a parent comes before its children. A file model's root is the file's node itself.
    NvNodeId node_ids[SCENE_MAX_NODES];
    // The model of a node that has a file model, until its behaviors and attachments are made (too large for the stack).
    NvGltfModel* file_models = NV_PUSH_ARRAY(scratch, SCENE_MAX_NODES, NvGltfModel);
    Asset* node_assets[SCENE_MAX_NODES] = {0};
    for (u32 n = 0; n < desc->node_count; ++n) {
        const NodeDesc* node = &desc->nodes[n];
        NvNodeId parent = node->parent >= 0 ? node_ids[node->parent] : (NvNodeId){0};
        const ModelDesc* model = node->model_index >= 0 ? &desc->models[node->model_index] : NULL;
        NvNodeId id;
        if (model && model->is_file) {
            Asset* asset = asset_of(sandbox, model);
            if (!asset_instance(sandbox, model, asset, scene, parent, &file_models[n]))
                return 0;
            node_assets[n] = asset;
            id = file_models[n].root;
            snprintf(nv_scene_get(scene, id)->name, NV_NODE_NAME_MAX, "%s", node->name);
        } else {
            id = nv_scene_add_node(scene, parent, node->name);
        }
        node_ids[n] = id;

        NvNode* scene_node = nv_scene_get(scene, id);
        scene_node->position = node->position;
        scene_node->rotation = node->rotation;
        scene_node->scale = node->scale;
        if (node->has_camera)
            scene_node->camera = node->camera;
        if (node->has_light)
            scene_node->light = node->light;
        NodeRuntime* runtime = &doc->nodes[doc->node_count++];
        runtime->id = id;
        runtime->visible = node->visible;
        if (model && !model->is_file) {
            runtime->mesh = doc->model_meshes[node->model_index];
            scene_node->mesh = runtime->mesh;
            if (node->has_color || node->double_sided) {
                NvMaterialDesc material = {.base_color = {1, 1, 1, 1}, .double_sided = node->double_sided};
                if (node->has_color)
                    memcpy(material.base_color, node->color, sizeof(node->color));
                scene_node->material = nv_renderer_add_material(&sandbox->renderer, &material);
            }
        }
    }
    scene->active_camera = node_ids[desc->view.camera_index];

    // A node that follows a joint of the nearest model above it.
    for (u32 n = 0; n < desc->node_count; ++n) {
        const NodeDesc* node = &desc->nodes[n];
        if (!node->attach[0])
            continue;
        s32 owner = node->parent;
        while (owner >= 0 && !file_models[owner].animator.index)
            owner = desc->nodes[owner].parent;
        NV_ASSERT(owner >= 0); // scene_file.c asked for a file model above
        const NvGltfModel* model = &file_models[owner];
        s32 joint = model->animator.index ? nv_anim_find_joint(model->skeleton, node->attach) : -1;
        if (joint < 0) {
            nv_log(NV_LOG_ERROR, "scene", "node '%s': no joint '%s' to attach to", node->id, node->attach);
            return 0;
        }
        nv_scene_get(scene, node_ids[n])->attach = (NvJointAttach){.animator = model->animator, .joint = (u32)joint};
    }

    // The behaviors, in the file's order (the benchmark's steps name them by that place).
    for (u32 b = 0; b < desc->behavior_count; ++b) {
        const BehaviorDesc* source = &desc->behaviors[b];
        Behavior* behavior = &doc->behaviors[doc->behavior_count++];
        behavior->desc = *source;
        behavior->node = node_ids[source->node];
        switch (source->kind) {
        case BEHAVIOR_SPIN:
            behavior->spin.authored = nv_scene_get(scene, behavior->node)->rotation;
            break;
        case BEHAVIOR_CHARACTER: {
            if (!build_character(behavior, desc, &file_models[source->node], node_assets[source->node], node_ids))
                return 0;
            // It starts in the clip the file names, or in the first loop of its model.
            const CharacterState* state = &behavior->character;
            NvClipId start = behavior->desc.character.clip[0] ? character_find_clip(state, behavior->desc.character.clip) : (NvClipId){0};
            for (u32 i = 0; !start.index && i < state->asset->clip_count; ++i)
                if (strstr(nv_anim_clip_name(state->asset->clips[i]), "_Loop"))
                    start = state->asset->clips[i];
            if (!start.index && state->asset->clip_count)
                start = state->asset->clips[0];
            if (start.index)
                character_play(sandbox, behavior, start);
        } break;
        case BEHAVIOR_GRID: {
            const GridDesc* grid = &behavior->desc.grid;
            if (desc->models[grid->model_index].is_file) {
                nv_log(NV_LOG_ERROR, "scene", "node '%s': a grid of file models is not supported yet", desc->nodes[source->node].id);
                return 0;
            }
            behavior->grid.mesh = doc->model_meshes[grid->model_index];
            behavior->grid.materials = doc->palette_materials[grid->palette_index];
            behavior->grid.material_count = desc->palettes[grid->palette_index].wheel;
            behavior->grid.nodes = NV_PUSH_ARRAY(&sandbox->permanent, grid->max, NvNodeId);
        } break;
        case BEHAVIOR_CHAIN: {
            const ChainDesc* chain = &behavior->desc.chain;
            if (desc->models[chain->model_index].is_file) {
                nv_log(NV_LOG_ERROR, "scene", "node '%s': a chain of file models is not supported yet", desc->nodes[source->node].id);
                return 0;
            }
            behavior->chain.mesh = doc->model_meshes[chain->model_index];
            behavior->chain.materials = doc->palette_materials[chain->palette_index];
            behavior->chain.material_count = desc->palettes[chain->palette_index].wheel;
            behavior->chain.nodes = NV_PUSH_ARRAY(&sandbox->permanent, chain->max, NvNodeId);
        } break;
        case BEHAVIOR_CROWD: {
            const CrowdDesc* crowd = &behavior->desc.crowd;
            behavior->crowd.asset = asset_of(sandbox, &desc->models[crowd->model_index]);
            behavior->crowd.models = NV_PUSH_ARRAY(&sandbox->permanent, crowd->max, NvGltfModel);
        } break;
        case BEHAVIOR_EFFECTS:
            behavior->effects.flights = NV_PUSH_ARRAY(&sandbox->permanent, EFFECTS_MAX_MISSILES, Flight);
            break;
        case BEHAVIOR_COUNT:
            NV_INVALID_CODE_PATH;
            break;
        }
    }

    // The view.
    const ViewDesc* v = &desc->view;
    doc->view = (SceneView){
        .scene = scene,
        .camera = node_ids[v->camera_index],
        .focus = v->focus_index >= 0 ? node_ids[v->focus_index] : (NvNodeId){0},
        .selected = v->select_index >= 0 ? node_ids[v->select_index] : (NvNodeId){0},
        .orbit = {.target = v->target, .yaw = v->yaw, .pitch = v->pitch, .distance = v->distance, ORBIT_LIMITS},
        .follow_selection = false,
    };
    sandbox_set_home(&doc->view);

    // Taken before anything can rename or move nodes: saves apply to nodes by their place in this tree, and only while it is the
    // same tree. What exists now is what the file made; a generator's nodes come later.
    doc->layout = save_scene_layout(scene);
    for (u32 i = 1; i <= scene->node_count; ++i) {
        if (scene->nodes[i].gen & 1) {
            doc->authored[i] = 1;
            ++doc->authored_count;
        }
    }
    doc->built = 1;
    return 1;
}

b32 scenes_init(Sandbox* sandbox)
{
    sandbox->scene_count = (u32)NV_ARRAY_COUNT(scene_files);
    NV_ASSERT(sandbox->scene_count <= SANDBOX_MAX_SCENES);
    for (u32 i = 0; i < sandbox->scene_count; ++i) {
        umm mark = sandbox->scratch.used;
        if (!build_scene(sandbox, i)) {
            SceneDoc* doc = &sandbox->docs[i];
            if (!doc->error[0])
                snprintf(doc->error, sizeof(doc->error), "%s: could not be built (see the Console)", scene_files[i]);
            nv_log(NV_LOG_ERROR, "scene", "the scene %s is not available: %s", scene_files[i], doc->error);
            doc->built = 0;
        }
        sandbox->scratch.used = mark;
    }
    return sandbox->docs[0].built;
}

SceneDoc* sandbox_doc(Sandbox* sandbox)
{
    return &sandbox->docs[sandbox->shown];
}

NodeRuntime* doc_node(SceneDoc* doc, NvNodeId id)
{
    for (u32 i = 0; i < doc->node_count; ++i)
        if (doc->nodes[i].id.index == id.index && doc->nodes[i].id.gen == id.gen)
            return &doc->nodes[i];
    return NULL;
}

b32 doc_authored(const SceneDoc* doc, u32 index)
{
    return index < NV_MAX_NODES && doc->authored[index];
}

Behavior* doc_generator_of(SceneDoc* doc, u32 index)
{
    NvScene* scene = doc->scene;
    if (doc_authored(doc, index))
        return NULL;
    while (index && !doc_authored(doc, index))
        index = scene->nodes[index].parent;
    if (!index)
        return NULL;
    // `index` is the file's node the generated one hangs from: the generator on it.
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (scene_behavior_def(behavior->desc.kind)->generator && behavior->node.index == index)
            return behavior;
    }
    return NULL;
}

Asset* sandbox_character_asset(Sandbox* sandbox)
{
    for (u32 i = 0; i < sandbox->asset_count; ++i)
        if (sandbox->assets[i].loaded && sandbox->assets[i].model.skeleton.index)
            return &sandbox->assets[i];
    return NULL;
}

u32 asset_materials(const Asset* asset, NvMaterialId* out, u32 capacity)
{
    u32 count = 0;
    for (u32 m = 0; m < asset->model.mesh_node_count; ++m) {
        NvMaterialId material = nv_scene_get(asset->model.scene, asset->model.mesh_nodes[m])->material;
        b32 seen = !material.index;
        for (u32 i = 0; i < count; ++i)
            seen = seen || out[i].index == material.index;
        if (!seen && count < capacity)
            out[count++] = material;
    }
    return count;
}
