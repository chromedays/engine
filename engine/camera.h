#pragma once

#include "engine/scene.h"

// An orbit camera: it looks at `target` from `distance` meters away, turned by `yaw` around Y and tilted down by `pitch`. The
// math is here and nothing else: what the target follows, and where input comes from, is the caller's. The limits are
// fields, since a zeroed limit would clamp everything to 0: set them when making the camera.
typedef struct NvOrbitCamera {
    NvVec3 target;           // the point it orbits
    f32 yaw, pitch, distance; // radians, radians looking down, meters
    f32 min_pitch, max_pitch;
    f32 min_distance, max_distance;
} NvOrbitCamera;

// Turns by `yaw` and `pitch` radians and moves toward or away by the factor e^dolly (a negative dolly is closer), then keeps
// the pitch and the distance in their limits.
void nv_orbit_camera_turn(NvOrbitCamera* camera, f32 yaw, f32 pitch, f32 dolly);

// The world move for a pan of (pan_x, pan_y) pixels on an image `image_height` pixels high (in the same pixels): the scene
// follows the finger, one pixel being the height the view covers at the target divided by the image's height. Along the
// camera node's right and up axes; its fov is the node's.
NvVec3 nv_orbit_camera_pan(const NvOrbitCamera* camera, const NvNode* camera_node, f32 image_height, f32 pan_x, f32 pan_y);

// Sets the camera node's position and rotation, and its world matrix too, since the node is a top-level one and
// nv_scene_update may already have run this frame.
void nv_orbit_camera_place(const NvOrbitCamera* camera, NvNode* camera_node);
