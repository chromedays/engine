// The renderer's parts that need no GPU, so that tests can run them under Node (tests/resolution_test.c): the scene's
// resolution, the camera's matrices and the rays through the image. Their declarations are in engine/renderer.h.

#include "engine/renderer.h"

#include <math.h>

internal NvMat4 camera_projection(NvNode* camera_node, f32 aspect)
{
    NvCamera* camera = &camera_node->camera;
    NvMat4 proj = nv_mat4_identity();
    switch (camera->projection) {
    case NV_PROJECTION_PERSPECTIVE:
        proj = nv_mat4_perspective(camera->fov_y, aspect, camera->near_z, camera->far_z);
        break;
    case NV_PROJECTION_ORTHOGRAPHIC:
        proj = nv_mat4_orthographic(camera->ortho_height, aspect, camera->near_z, camera->far_z);
        break;
    default:
        NV_INVALID_CODE_PATH;
    }
    return proj;
}

// Reverse Z for drawing: clip z becomes w - z, so depth runs 1 (near) to 0 (far). A float depth
// buffer keeps its precision near 0, where the far range is, instead of wasting it near the camera.
internal NvMat4 reverse_depth(NvMat4 m)
{
    for (u32 column = 0; column < 4; ++column)
        m.e[column * 4 + 2] = m.e[column * 4 + 3] - m.e[column * 4 + 2];
    return m;
}

NvMat4 nv_renderer_camera_view_proj(NvNode* camera_node, f32 aspect)
{
    return nv_mat4_mul(reverse_depth(camera_projection(camera_node, aspect)), nv_mat4_inverse(camera_node->world));
}

void nv_renderer_camera_matrices(NvScene* scene, NvSceneOutput output, NvMat4* view, NvMat4* projection)
{
    NV_ASSERT(output.width && output.height);
    NvNode* camera = nv_scene_get(scene, scene->active_camera);
    *view = nv_mat4_inverse(camera->world);
    *projection = camera_projection(camera, (f32)output.width / (f32)output.height);
}

// A point through a projective matrix, with the perspective divide.
internal NvVec3 project(NvMat4 m, f32 x, f32 y, f32 z)
{
    f32 w = m.e[3] * x + m.e[7] * y + m.e[11] * z + m.e[15];
    NvVec3 p = nv_mat4_transform_point(m, nv_vec3(x, y, z));
    return nv_vec3_scale(p, 1.0f / w);
}

NvRay nv_renderer_view_ray(NvScene* scene, NvSceneOutput output, f32 x, f32 y)
{
    NV_ASSERT(output.width && output.height && output.pixel_width > 0.0f && output.pixel_height > 0.0f);
    NvNode* camera = nv_scene_get(scene, scene->active_camera);
    NvMat4 view_proj = nv_mat4_mul(camera_projection(camera, (f32)output.width / (f32)output.height), nv_mat4_inverse(camera->world));
    NvMat4 to_world = nv_mat4_inverse(view_proj);
    // Canvas pixels to scene pixels: from the image's corner, `pixel_width` x `pixel_height` screen pixels each.
    f32 scene_x = (x - (f32)output.image.x) / output.pixel_width;
    f32 scene_y = (y - (f32)output.image.y) / output.pixel_height;
    f32 ndc_x = scene_x / (f32)output.width * 2.0f - 1.0f;
    f32 ndc_y = 1.0f - scene_y / (f32)output.height * 2.0f;
    NvVec3 near_point = project(to_world, ndc_x, ndc_y, 0.0f); // depth runs 0 (near) to 1 (far)
    NvVec3 far_point = project(to_world, ndc_x, ndc_y, 1.0f);
    return (NvRay){near_point, nv_vec3_normalize(nv_vec3_sub(far_point, near_point))};
}

// The scene's resolution and where its image goes in the viewport (docs/specs/resolution.md).
NvSceneOutput nv_renderer_scene_output(const NvResolution* resolution, NvRect viewport)
{
    NvSceneOutput out = {.image = viewport, .pixel_width = 1.0f, .pixel_height = 1.0f};
    if (!viewport.width || !viewport.height) {
        out.width = out.height = 1;
        return out;
    }
    if (resolution->mode == NV_RESOLUTION_FIXED) {
        u32 width = nv_clamp_u32(resolution->fixed_width, NV_RESOLUTION_MIN, NV_RESOLUTION_MAX);
        u32 height = nv_clamp_u32(resolution->fixed_height, NV_RESOLUTION_MIN, NV_RESOLUTION_MAX);
        out.width = width;
        out.height = height;
        f32 sx = (f32)viewport.width / (f32)width, sy = (f32)viewport.height / (f32)height;
        u32 image_width, image_height;
        u32 fit_x = viewport.width / width, fit_y = viewport.height / height;
        u32 multiple = fit_x < fit_y ? fit_x : fit_y;
        if (resolution->fixed_fit == NV_FIT_STRETCH) {
            // The whole viewport, whatever the aspect ratio.
            out.pixel_width = sx;
            out.pixel_height = sy;
            image_width = viewport.width;
            image_height = viewport.height;
        } else if (resolution->fixed_fit == NV_FIT_WHOLE && multiple >= 1) {
            // The largest whole multiple that fits, so every block is the same size.
            out.pixel_width = out.pixel_height = (f32)multiple;
            image_width = width * multiple;
            image_height = height * multiple;
        } else {
            // The largest scale that fits keeping the aspect ratio: what NV_FIT_VIEWPORT always does, and
            // NV_FIT_WHOLE does when even the size itself does not fit.
            out.pixel_width = out.pixel_height = sx < sy ? sx : sy;
            image_width = nv_clamp_u32((u32)((f32)width * out.pixel_width + 0.5f), 1, viewport.width);
            image_height = nv_clamp_u32((u32)((f32)height * out.pixel_width + 0.5f), 1, viewport.height);
        }
        out.image = (NvRect){viewport.x + (viewport.width - image_width) / 2, viewport.y + (viewport.height - image_height) / 2,
                             image_width, image_height};
        return out;
    }
    u32 divisor = nv_clamp_u32(resolution->divisor, 1, 4);
    out.width = (viewport.width + divisor - 1) / divisor;
    out.height = (viewport.height + divisor - 1) / divisor;
    out.pixel_width = out.pixel_height = (f32)divisor;
    return out;
}

NvTapRay nv_renderer_tap_ray(NvScene* scene, NvSceneOutput output, f32 tap_x, f32 tap_y, f32 pixel_ratio)
{
    f32 x = tap_x * pixel_ratio, y = tap_y * pixel_ratio;
    const NvRect* image = &output.image;
    if (x < (f32)image->x || y < (f32)image->y || x >= (f32)(image->x + image->width) || y >= (f32)(image->y + image->height))
        return (NvTapRay){0};
    return (NvTapRay){.ok = 1, .ray = nv_renderer_view_ray(scene, output, x, y)};
}
