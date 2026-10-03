#include "engine/camera.h"

void nv_orbit_camera_turn(NvOrbitCamera* camera, f32 yaw, f32 pitch, f32 dolly)
{
    camera->yaw += yaw;
    camera->pitch = nv_clamp_f32(camera->pitch + pitch, camera->min_pitch, camera->max_pitch);
    camera->distance = nv_clamp_f32(camera->distance * expf(dolly), camera->min_distance, camera->max_distance);
}

// How far a pixel of the image spans at the target: the height the view covers there over the image's height.
internal f32 meters_per_pixel(const NvOrbitCamera* camera, const NvNode* camera_node, f32 image_height)
{
    return 2.0f * camera->distance * tanf(camera_node->camera.fov_y * 0.5f) / (image_height > 1.0f ? image_height : 1.0f);
}

NvVec3 nv_orbit_camera_pan(const NvOrbitCamera* camera, const NvNode* camera_node, f32 image_height, f32 pan_x, f32 pan_y)
{
    f32 meters = meters_per_pixel(camera, camera_node, image_height);
    NvVec3 right = nv_quat_rotate(camera_node->rotation, nv_vec3(1, 0, 0));
    NvVec3 up = nv_quat_rotate(camera_node->rotation, nv_vec3(0, 1, 0));
    return nv_vec3_add(nv_vec3_scale(right, -pan_x * meters), nv_vec3_scale(up, pan_y * meters));
}

NvVec3 nv_orbit_camera_pan_ground(const NvOrbitCamera* camera, const NvNode* camera_node, f32 image_height, f32 pan_x, f32 pan_y)
{
    f32 meters = meters_per_pixel(camera, camera_node, image_height);
    // The camera sits at +(sin yaw, ., cos yaw) from the target, so it looks along -(sin yaw, 0, cos yaw) on the ground.
    NvVec3 right = nv_vec3(cosf(camera->yaw), 0.0f, -sinf(camera->yaw));
    NvVec3 forward = nv_vec3(-sinf(camera->yaw), 0.0f, -cosf(camera->yaw));
    f32 slant = fmaxf(sinf(camera->pitch), 0.05f);
    return nv_vec3_add(nv_vec3_scale(right, -pan_x * meters), nv_vec3_scale(forward, pan_y * meters / slant));
}

void nv_orbit_camera_place(const NvOrbitCamera* camera, NvNode* camera_node)
{
    f32 d = camera->distance;
    f32 cp = cosf(camera->pitch);
    camera_node->position = nv_vec3_add(camera->target, nv_vec3(sinf(camera->yaw) * cp * d, sinf(camera->pitch) * d, cosf(camera->yaw) * cp * d));
    camera_node->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), camera->yaw),
                                        nv_quat_axis_angle(nv_vec3(1, 0, 0), -camera->pitch));
    camera_node->world = nv_mat4_trs(camera_node->position, camera_node->rotation, camera_node->scale);
}
