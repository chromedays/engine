#include "engine/gltf.h"
#include "engine/log.h"

#include <cgltf.h>
#include <emscripten/emscripten.h>

#include <stdio.h>
#include <string.h>

// cgltf allocates from the scratch arena and never frees; the caller drops it all at once.
internal void* cgltf_arena_alloc(void* user, cgltf_size size)
{
    return nv_arena_push((NvArena*)user, size, 16);
}

internal void cgltf_arena_free(void* user, void* ptr)
{
    (void)user, (void)ptr;
}

internal cgltf_data* parse_file(const char* path, NvArena* scratch)
{
    cgltf_options options = {0};
    options.memory.alloc_func = cgltf_arena_alloc;
    options.memory.free_func = cgltf_arena_free;
    options.memory.user_data = scratch;

    cgltf_data* data = NULL;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success ||
        cgltf_load_buffers(&options, data, path) != cgltf_result_success ||
        cgltf_validate(data) != cgltf_result_success) {
        nv_log(NV_LOG_ERROR, "nv", "failed to load %s", path);
        return NULL;
    }
    return data;
}

//
// Images: decoded by the browser (JPEG, PNG and WebP all work) instead of a C image library.
//

EM_ASYNC_JS(int, js_decode_image, (const u8* data, int size, const char* mime, int* width, int* height), {
    const bytes = HEAPU8.slice(data, data + size);
    try {
        const bitmap = await createImageBitmap(new Blob([bytes], {type: UTF8ToString(mime)}),
                                               {colorSpaceConversion: "none", premultiplyAlpha: "none"});
        const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
        const context = canvas.getContext("2d");
        context.drawImage(bitmap, 0, 0);
        Module.nvDecodedImage = context.getImageData(0, 0, bitmap.width, bitmap.height).data;
        HEAP32[width >> 2] = bitmap.width;
        HEAP32[height >> 2] = bitmap.height;
        return 1;
    } catch (error) {
        Module.nvLog(2, "nv", "image decode failed: " + (error && error.message || error));
        return 0;
    }
});

EM_JS(void, js_take_decoded_image, (u8* out), {
    HEAPU8.set(Module.nvDecodedImage, out);
    Module.nvDecodedImage = null;
});

internal NvTextureId load_texture(const cgltf_image* image, NvRenderer* renderer, NvArena* scratch)
{
    if (!image || !image->buffer_view) {
        // TODO: Images referenced by URI; the shipped assets embed theirs in .glb files.
        nv_log(NV_LOG_WARNING, "nv", "skipping image without an embedded buffer");
        return (NvTextureId){0};
    }
    const u8* bytes = cgltf_buffer_view_data(image->buffer_view);
    s32 width = 0;
    s32 height = 0;
    const char* mime = image->mime_type ? image->mime_type : "image/png";
    if (!js_decode_image(bytes, (int)image->buffer_view->size, mime, &width, &height))
        return (NvTextureId){0};

    umm mark = scratch->used;
    u8* pixels = NV_PUSH_ARRAY(scratch, (umm)width * height * 4, u8);
    js_take_decoded_image(pixels);
    NvTextureId texture = nv_renderer_add_texture(renderer, image->name, (u32)width, (u32)height, pixels, 1, scratch);
    scratch->used = mark;
    return texture;
}

//
// Models
//

internal void copy_name(char* dst, const char* src)
{
    u32 i = 0;
    for (; src && src[i] && i < NV_NODE_NAME_MAX - 1; ++i)
        dst[i] = src[i];
    dst[i] = 0;
}

internal NvMeshId load_primitive(const cgltf_primitive* prim, const cgltf_node* node, NvRenderer* renderer,
                                 NvArena* scratch)
{
    const cgltf_accessor* positions = NULL;
    const cgltf_accessor* normals = NULL;
    const cgltf_accessor* uvs = NULL;
    const cgltf_accessor* joints = NULL;
    const cgltf_accessor* weights = NULL;
    for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
        const cgltf_attribute* attribute = &prim->attributes[i];
        if (attribute->index != 0)
            continue;
        switch (attribute->type) {
        case cgltf_attribute_type_position: positions = attribute->data; break;
        case cgltf_attribute_type_normal: normals = attribute->data; break;
        case cgltf_attribute_type_texcoord: uvs = attribute->data; break;
        case cgltf_attribute_type_joints: joints = attribute->data; break;
        case cgltf_attribute_type_weights: weights = attribute->data; break;
        default: break;
        }
    }
    NV_ASSERT(positions && prim->type == cgltf_primitive_type_triangles);
    b32 skinned = node->skin && joints && weights;

    // Static meshes have their node's world transform baked in; skinned meshes are posed by their
    // joints, which glTF defines to ignore the mesh node's own transform.
    f32 world[16];
    cgltf_node_transform_world(node, world);
    NvMat4 transform;
    memcpy(transform.e, world, sizeof(world));

    u32 count = (u32)positions->count;
    NvMeshData data = {.vertex_count = count};
    umm mark = scratch->used;
    NvVertex* vertices = skinned ? NULL : NV_PUSH_ARRAY(scratch, count, NvVertex);
    NvSkinnedVertex* skinned_vertices = skinned ? NV_PUSH_ARRAY(scratch, count, NvSkinnedVertex) : NULL;
    for (u32 v = 0; v < count; ++v) {
        f32 position[3] = {0};
        f32 normal[3] = {0, 1, 0};
        f32 uv[2] = {0};
        cgltf_accessor_read_float(positions, v, position, 3);
        if (normals)
            cgltf_accessor_read_float(normals, v, normal, 3);
        if (uvs)
            cgltf_accessor_read_float(uvs, v, uv, 2);

        if (skinned) {
            NvSkinnedVertex* out = &skinned_vertices[v];
            memcpy(out->position, position, sizeof(position));
            memcpy(out->normal, normal, sizeof(normal));
            memcpy(out->uv, uv, sizeof(uv));
            cgltf_uint joint_indices[4] = {0};
            cgltf_accessor_read_uint(joints, v, joint_indices, 4);
            for (u32 k = 0; k < 4; ++k)
                out->joints[k] = (u16)joint_indices[k];
            cgltf_accessor_read_float(weights, v, out->weights, 4);
        } else {
            NvVertex* out = &vertices[v];
            NvVec3 p = nv_vec3(position[0], position[1], position[2]);
            NvVec3 n = nv_vec3(normal[0], normal[1], normal[2]);
            const f32* m = transform.e;
            out->position[0] = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
            out->position[1] = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
            out->position[2] = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
            out->normal[0] = m[0] * n.x + m[4] * n.y + m[8] * n.z;
            out->normal[1] = m[1] * n.x + m[5] * n.y + m[9] * n.z;
            out->normal[2] = m[2] * n.x + m[6] * n.y + m[10] * n.z;
            memcpy(out->uv, uv, sizeof(uv));
        }
    }
    data.vertices = vertices;
    data.skinned_vertices = skinned_vertices;

    u32 index_count = prim->indices ? (u32)prim->indices->count : count;
    u32* indices = NV_PUSH_ARRAY(scratch, index_count, u32);
    for (u32 i = 0; i < index_count; ++i)
        indices[i] = prim->indices ? (u32)cgltf_accessor_read_index(prim->indices, i) : i;
    data.indices = indices;
    data.index_count = index_count;

    NvMeshId mesh = nv_renderer_add_mesh(renderer, &data);
    scratch->used = mark;
    return mesh;
}

internal NvMaterialId load_material(const cgltf_material* material, NvRenderer* renderer, NvArena* scratch)
{
    if (!material)
        return (NvMaterialId){0};
    NvMaterialDesc desc = {.base_color = {1, 1, 1, 1}, .double_sided = material->double_sided};
    if (material->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness* pbr = &material->pbr_metallic_roughness;
        memcpy(desc.base_color, pbr->base_color_factor, sizeof(desc.base_color));
        if (pbr->base_color_texture.texture)
            desc.base_color_texture = load_texture(pbr->base_color_texture.texture->image, renderer, scratch);
    }
    return nv_renderer_add_material(renderer, &desc);
}

b32 nv_gltf_load_model(const char* path, NvScene* scene, NvRenderer* renderer, NvArena* permanent,
                       NvArena* scratch, NvGltfModel* out)
{
    *out = (NvGltfModel){0};
    umm mark = scratch->used;
    cgltf_data* data = parse_file(path, scratch);
    if (!data) {
        scratch->used = mark;
        return 0;
    }

    // Each glTF material becomes one renderer material, shared by the primitives that use it.
    NvMaterialId* materials = NV_PUSH_ARRAY(scratch, data->materials_count + 1, NvMaterialId);
    for (cgltf_size i = 0; i < data->materials_count; ++i)
        materials[i] = load_material(&data->materials[i], renderer, scratch);

    const char* name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    out->scene = scene;
    out->root = nv_scene_add_node(scene, (NvNodeId){0}, name);

    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node* node = &data->nodes[n];
        if (!node->mesh)
            continue;
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            const cgltf_primitive* prim = &node->mesh->primitives[p];
            NV_ASSERT(out->mesh_node_count < NV_GLTF_MAX_MESH_NODES);
            NvNodeId id = nv_scene_add_node(scene, out->root, node->name ? node->name : node->mesh->name);
            NvNode* scene_node = nv_scene_get(scene, id);
            scene_node->mesh = load_primitive(prim, node, renderer, scratch);
            scene_node->material = prim->material ? materials[prim->material - data->materials] : (NvMaterialId){0};
            out->mesh_nodes[out->mesh_node_count++] = id;
        }
    }

    if (data->skins_count) {
        const cgltf_skin* skin = &data->skins[0];
        NV_ASSERT(skin->joints_count <= NV_MAX_JOINTS);
        out->joint_count = (u32)skin->joints_count;
        out->joints = NV_PUSH_ARRAY(permanent, out->joint_count, NvJointDesc);
        out->inverse_bind = NV_PUSH_ARRAY(permanent, out->joint_count, NvMat4);
        for (u32 j = 0; j < out->joint_count; ++j) {
            const cgltf_node* joint = skin->joints[j];
            NvJointDesc* desc = &out->joints[j];
            copy_name(desc->name, joint->name);
            desc->parent = -1;
            for (u32 k = 0; k < out->joint_count; ++k) {
                if (skin->joints[k] == joint->parent)
                    desc->parent = (s32)k;
            }
            desc->position = joint->has_translation ? nv_vec3(joint->translation[0], joint->translation[1], joint->translation[2]) : nv_vec3(0, 0, 0);
            desc->rotation = joint->has_rotation ? (NvQuat){joint->rotation[0], joint->rotation[1], joint->rotation[2], joint->rotation[3]} : nv_quat_identity();
            desc->scale = joint->has_scale ? nv_vec3(joint->scale[0], joint->scale[1], joint->scale[2]) : nv_vec3(1, 1, 1);
            if (skin->inverse_bind_matrices)
                cgltf_accessor_read_float(skin->inverse_bind_matrices, j, out->inverse_bind[j].e, 16);
            else
                out->inverse_bind[j] = nv_mat4_identity();
        }

        out->skeleton = nv_anim_create_skeleton(out->joints, out->joint_count, out->inverse_bind);
        out->animator = nv_anim_create_animator(out->skeleton, scene, out->root);
        for (u32 i = 0; i < out->mesh_node_count; ++i) {
            NvNode* mesh_node = nv_scene_get(scene, out->mesh_nodes[i]);
            if (renderer->meshes[mesh_node->mesh.index].skinned)
                mesh_node->animator = out->animator;
        }
    }

    scratch->used = mark;
    return 1;
}

void nv_gltf_instantiate(const NvGltfModel* model, NvScene* scene, NvNodeId parent, NvGltfModel* out)
{
    *out = *model;
    out->scene = scene;
    out->root = nv_scene_add_node(scene, parent, nv_scene_get(model->scene, model->root)->name);
    if (model->skeleton.index)
        out->animator = nv_anim_create_animator(model->skeleton, scene, out->root);
    for (u32 i = 0; i < model->mesh_node_count; ++i) {
        NvNode* source = nv_scene_get(model->scene, model->mesh_nodes[i]);
        out->mesh_nodes[i] = nv_scene_add_node(scene, out->root, source->name);
        NvNode* node = nv_scene_get(scene, out->mesh_nodes[i]);
        node->mesh = source->mesh;
        node->material = source->material;
        node->animator = source->animator.index ? out->animator : (NvAnimatorId){0};
    }
}

//
// Clips
//

// Reads a sampler's keys into scratch memory; `components` is 3 or 4. Cubic spline samplers keep
// only their values (the middle of each in-tangent, value, out-tangent triple).
internal u32 read_keys(const cgltf_animation_sampler* sampler, u32 components, NvArena* scratch,
                       const f32** times_out, const f32** values_out)
{
    u32 count = (u32)sampler->input->count;
    f32* times = NV_PUSH_ARRAY(scratch, count, f32);
    f32* values = NV_PUSH_ARRAY(scratch, (umm)count * components, f32);
    b32 cubic = sampler->interpolation == cgltf_interpolation_type_cubic_spline;
    for (u32 k = 0; k < count; ++k) {
        cgltf_accessor_read_float(sampler->input, k, &times[k], 1);
        cgltf_accessor_read_float(sampler->output, cubic ? k * 3 + 1 : k, &values[k * components], components);
    }
    *times_out = times;
    *values_out = values;
    return count;
}

u32 nv_gltf_load_clips(const char* path, NvSkeletonId skeleton, const char* root_motion_joint,
                       NvArena* scratch, NvClipId* clips, u32 max_clips)
{
    umm mark = scratch->used;
    cgltf_data* data = parse_file(path, scratch);
    if (!data) {
        scratch->used = mark;
        return 0;
    }

    u32 joint_count = nv_anim_joint_count(skeleton);
    s32 motion_joint = root_motion_joint ? nv_anim_find_joint(skeleton, root_motion_joint) : -1;
    u32 clip_count = 0;
    for (cgltf_size a = 0; a < data->animations_count && clip_count < max_clips; ++a) {
        const cgltf_animation* animation = &data->animations[a];
        umm clip_mark = scratch->used;
        NvTrackDesc* tracks = NV_PUSH_ARRAY(scratch, joint_count, NvTrackDesc);
        for (u32 j = 0; j < joint_count; ++j)
            tracks[j].joint = j;

        f32 duration = 0.0f;
        for (cgltf_size c = 0; c < animation->channels_count; ++c) {
            const cgltf_animation_channel* channel = &animation->channels[c];
            if (!channel->target_node || !channel->target_node->name)
                continue;
            s32 joint = nv_anim_find_joint(skeleton, channel->target_node->name);
            if (joint < 0)
                continue;
            NvTrackDesc* track = &tracks[joint];
            const cgltf_animation_sampler* sampler = channel->sampler;
            const f32* times = NULL;
            const f32* values = NULL;
            u32 count = 0;
            switch (channel->target_path) {
            case cgltf_animation_path_type_translation:
                count = read_keys(sampler, 3, scratch, &times, &values);
                track->translation_count = count;
                track->translation_times = times;
                track->translations = (const NvVec3*)values;
                break;
            case cgltf_animation_path_type_rotation:
                count = read_keys(sampler, 4, scratch, &times, &values);
                track->rotation_count = count;
                track->rotation_times = times;
                track->rotations = (const NvQuat*)values;
                break;
            case cgltf_animation_path_type_scale:
                count = read_keys(sampler, 3, scratch, &times, &values);
                track->scale_count = count;
                track->scale_times = times;
                track->scales = (const NvVec3*)values;
                break;
            default:
                break;
            }
            if (count && times[count - 1] > duration)
                duration = times[count - 1];
        }

        NvRootMotionDesc root_motion = {0};
        if (motion_joint >= 0) {
            root_motion.joint = (u32)motion_joint;
            root_motion.enabled = 1;
        }
        if (duration > 0.0f)
            clips[clip_count++] = nv_anim_create_clip(skeleton, animation->name ? animation->name : "clip", duration,
                                                      tracks, joint_count, root_motion);
        scratch->used = clip_mark;
    }

    scratch->used = mark;
    return clip_count;
}
