// The orbit camera in engine/camera.h: limits, the pan's distance per pixel, and where the camera node ends up.
#include <engine/camera.h>

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            printf("FAILED line %d: %s\n", __LINE__, #condition);                 \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static b32 near(f32 a, f32 b) { return fabsf(a - b) < 1e-4f; }
static b32 near3(NvVec3 a, NvVec3 b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); }

static NvOrbitCamera make_camera(void)
{
    return (NvOrbitCamera){.target = {1, 2, 3}, .yaw = 0.35f, .pitch = 0.12f, .distance = 5.0f, .min_pitch = -0.2f, .max_pitch = 1.4f,
                           .min_distance = 1.0f, .max_distance = 100.0f};
}

static NvNode make_node(void)
{
    NvNode node = {0};
    node.rotation = nv_quat_identity();
    node.scale = nv_vec3(1, 1, 1);
    node.camera = (NvCamera){.projection = NV_PROJECTION_PERSPECTIVE, .fov_y = NV_PI / 3.0f, .near_z = 0.1f, .far_z = 100.0f};
    return node;
}

int main(void)
{
    // Turning adds to the angles, and the pitch stops at its limits (the yaw has none).
    NvOrbitCamera camera = make_camera();
    nv_orbit_camera_turn(&camera, 0.5f, 0.1f, 0.0f);
    CHECK(near(camera.yaw, 0.85f) && near(camera.pitch, 0.22f) && near(camera.distance, 5.0f));
    nv_orbit_camera_turn(&camera, 100.0f, 10.0f, 0.0f);
    CHECK(near(camera.pitch, 1.4f) && near(camera.yaw, 100.85f));
    nv_orbit_camera_turn(&camera, 0.0f, -10.0f, 0.0f);
    CHECK(near(camera.pitch, -0.2f));

    // Dollying multiplies the distance by e^dolly, within its limits.
    camera = make_camera();
    nv_orbit_camera_turn(&camera, 0.0f, 0.0f, logf(2.0f));
    CHECK(near(camera.distance, 10.0f));
    nv_orbit_camera_turn(&camera, 0.0f, 0.0f, -logf(4.0f));
    CHECK(near(camera.distance, 2.5f));
    nv_orbit_camera_turn(&camera, 0.0f, 0.0f, -10.0f);
    CHECK(camera.distance == 1.0f);
    nv_orbit_camera_turn(&camera, 0.0f, 0.0f, 20.0f);
    CHECK(camera.distance == 100.0f);

    // No input changes nothing.
    camera = make_camera();
    NvOrbitCamera before = camera;
    nv_orbit_camera_turn(&camera, 0.0f, 0.0f, 0.0f);
    CHECK(camera.yaw == before.yaw && camera.pitch == before.pitch && camera.distance == before.distance);

    // Placing: straight on, the node is `distance` in front of the target on +Z with no rotation.
    NvNode node = make_node();
    camera = make_camera();
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    nv_orbit_camera_place(&camera, &node);
    CHECK(near3(node.position, nv_vec3(1, 2, 8)));
    CHECK(near(node.rotation.x, 0.0f) && near(node.rotation.y, 0.0f) && near(node.rotation.z, 0.0f) && near(node.rotation.w, 1.0f));
    CHECK(near3(nv_mat4_translation(node.world), node.position));

    // Turned a quarter around Y, the camera is on +X and looks back at the target.
    camera.yaw = NV_PI * 0.5f;
    nv_orbit_camera_place(&camera, &node);
    CHECK(near3(node.position, nv_vec3(6, 2, 3)));

    // Whatever the angles, the camera looks at the target from `distance` away (it looks down its own -Z).
    for (f32 yaw = -3.0f; yaw <= 3.0f; yaw += 1.5f) {
        for (f32 pitch = -0.2f; pitch <= 1.4f; pitch += 0.4f) {
            camera = make_camera();
            camera.yaw = yaw;
            camera.pitch = pitch;
            camera.distance = 7.0f;
            nv_orbit_camera_place(&camera, &node);
            NvVec3 to_target = nv_vec3_sub(camera.target, node.position);
            CHECK(near(sqrtf(nv_vec3_dot(to_target, to_target)), 7.0f));
            NvVec3 forward = nv_quat_rotate(node.rotation, nv_vec3(0, 0, -1));
            CHECK(near3(forward, nv_vec3_normalize(to_target)));
            CHECK(near3(nv_mat4_translation(node.world), node.position));
            // Pitched down means above the target.
            CHECK(pitch > 0.0f ? node.position.y > camera.target.y : node.position.y <= camera.target.y + 1e-5f);
        }
    }

    // Panning: with an identity rotation, right is +X and up is +Y. At 10 m with a 60 degree fov on an image 600 pixels
    // high, a pixel is 2 * 10 * tan(30 degrees) / 600 meters.
    camera = make_camera();
    camera.distance = 10.0f;
    node = make_node();
    f32 pixel = 2.0f * 10.0f * tanf(NV_PI / 6.0f) / 600.0f;
    NvVec3 move = nv_orbit_camera_pan(&camera, &node, 600.0f, 100.0f, 0.0f);
    CHECK(near3(move, nv_vec3(-100.0f * pixel, 0.0f, 0.0f))); // dragging right moves the scene right: the camera left
    move = nv_orbit_camera_pan(&camera, &node, 600.0f, 0.0f, 50.0f);
    CHECK(near3(move, nv_vec3(0.0f, 50.0f * pixel, 0.0f)));
    move = nv_orbit_camera_pan(&camera, &node, 600.0f, 0.0f, 0.0f);
    CHECK(near3(move, nv_vec3(0, 0, 0)));

    // The axes follow the node's rotation: turned a quarter around Y, right is -Z.
    node.rotation = nv_quat_axis_angle(nv_vec3(0, 1, 0), NV_PI * 0.5f);
    move = nv_orbit_camera_pan(&camera, &node, 600.0f, 100.0f, 0.0f);
    CHECK(near3(move, nv_vec3(0.0f, 0.0f, 100.0f * pixel)));

    // A tiny image does not divide by zero.
    move = nv_orbit_camera_pan(&camera, &node, 0.0f, 1.0f, 0.0f);
    CHECK(isfinite(move.x) && isfinite(move.z));

    // Panning along the ground: at yaw 0 the camera looks along -Z. Across moves as nv_orbit_camera_pan does; a pixel down the
    // screen moves 1 / sin(pitch) as far along the view (twice at 30 degrees, once looking straight down), and never up.
    camera = make_camera();
    camera.distance = 10.0f;
    camera.yaw = 0.0f;
    camera.pitch = NV_PI / 6.0f;
    node = make_node();
    move = nv_orbit_camera_pan_ground(&camera, &node, 600.0f, 100.0f, 0.0f);
    CHECK(near3(move, nv_vec3(-100.0f * pixel, 0.0f, 0.0f)));
    move = nv_orbit_camera_pan_ground(&camera, &node, 600.0f, 0.0f, 50.0f);
    CHECK(near3(move, nv_vec3(0.0f, 0.0f, -100.0f * pixel)));
    camera.pitch = NV_PI / 2.0f;
    move = nv_orbit_camera_pan_ground(&camera, &node, 600.0f, 0.0f, 50.0f);
    CHECK(near3(move, nv_vec3(0.0f, 0.0f, -50.0f * pixel)));
    // Turned a quarter (yaw 90 degrees: the camera on +X, looking along -X), across is along -Z.
    camera.yaw = NV_PI * 0.5f;
    move = nv_orbit_camera_pan_ground(&camera, &node, 600.0f, 100.0f, 0.0f);
    CHECK(near3(move, nv_vec3(0.0f, 0.0f, 100.0f * pixel)));
    // Level with the ground the slant is held at its least, so the move stays finite.
    camera.pitch = 0.0f;
    move = nv_orbit_camera_pan_ground(&camera, &node, 600.0f, 0.0f, 50.0f);
    CHECK(isfinite(move.x) && isfinite(move.z) && move.y == 0.0f);

    if (failures == 0)
        printf("camera_test: all passed\n");
    return failures ? 1 : 0;
}
