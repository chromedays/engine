// The scene's resolution and the rays through its image (engine/renderer_cpu.c): Scale and Fixed with their three fits,
// viewports that are too small or empty, and a tap on a black bar.
#include <engine/renderer.h>

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
static b32 same_rect(NvRect r, u32 x, u32 y, u32 width, u32 height) { return r.x == x && r.y == y && r.width == width && r.height == height; }

static NvScene scene; // large: not on the stack

int main(void)
{
    NvRect viewport = {260, 28, 680, 552};

    // Scale: a whole fraction of the viewport, rounded up, filling it. A divisor outside 1 to 4 is clamped.
    for (u32 divisor = 1; divisor <= 4; ++divisor) {
        NvResolution scale = {.mode = NV_RESOLUTION_SCALE, .divisor = divisor};
        NvSceneOutput out = nv_renderer_scene_output(&scale, viewport);
        CHECK(out.width == (680 + divisor - 1) / divisor && out.height == (552 + divisor - 1) / divisor);
        CHECK(out.pixel_width == (f32)divisor && out.pixel_height == (f32)divisor);
        CHECK(same_rect(out.image, 260, 28, 680, 552));
    }
    NvResolution scale = {.mode = NV_RESOLUTION_SCALE, .divisor = 0};
    CHECK(nv_renderer_scene_output(&scale, viewport).pixel_width == 1.0f);
    scale.divisor = 9;
    CHECK(nv_renderer_scene_output(&scale, viewport).pixel_width == 4.0f);
    scale.divisor = 3;
    CHECK(nv_renderer_scene_output(&scale, viewport).width == 227); // 680 / 3, rounded up

    // A viewport with no area gives a 1 x 1 scene.
    NvSceneOutput empty = nv_renderer_scene_output(&scale, (NvRect){10, 20, 0, 50});
    CHECK(empty.width == 1 && empty.height == 1 && empty.pixel_width == 1.0f);
    empty = nv_renderer_scene_output(&scale, (NvRect){0});
    CHECK(empty.width == 1 && empty.height == 1);

    // Fixed, whole multiple: 320 x 180 in 680 x 552 fits twice across (and three times down), centered.
    NvResolution fixed = {.mode = NV_RESOLUTION_FIXED, .fixed_fit = NV_FIT_WHOLE, .fixed_width = 320, .fixed_height = 180};
    NvSceneOutput out = nv_renderer_scene_output(&fixed, viewport);
    CHECK(out.width == 320 && out.height == 180);
    CHECK(out.pixel_width == 2.0f && out.pixel_height == 2.0f);
    CHECK(same_rect(out.image, 280, 124, 640, 360));

    // Fixed, fitted to the viewport: the largest scale that keeps the aspect ratio.
    fixed.fixed_fit = NV_FIT_VIEWPORT;
    out = nv_renderer_scene_output(&fixed, viewport);
    CHECK(near(out.pixel_width, 2.125f) && near(out.pixel_height, 2.125f));
    CHECK(same_rect(out.image, 260, 112, 680, 383));

    // Fixed, stretched: the whole viewport, uneven pixels.
    fixed.fixed_fit = NV_FIT_STRETCH;
    out = nv_renderer_scene_output(&fixed, viewport);
    CHECK(near(out.pixel_width, 2.125f) && near(out.pixel_height, 552.0f / 180.0f));
    CHECK(same_rect(out.image, 260, 28, 680, 552));

    // A fixed size larger than the viewport: whole multiples do not fit, so the image is shrunk to fit.
    fixed.fixed_fit = NV_FIT_WHOLE;
    fixed.fixed_width = 1280;
    fixed.fixed_height = 720;
    out = nv_renderer_scene_output(&fixed, viewport);
    CHECK(near(out.pixel_width, 680.0f / 1280.0f) && out.pixel_width == out.pixel_height);
    CHECK(out.image.width == 680 && out.image.height == 383 && out.image.x == 260);

    // The size is kept in its limits.
    fixed.fixed_width = 4;
    fixed.fixed_height = 9999;
    out = nv_renderer_scene_output(&fixed, viewport);
    CHECK(out.width == NV_RESOLUTION_MIN && out.height == NV_RESOLUTION_MAX);

    // The image never leaves the viewport, whatever the fit.
    for (u32 fit = NV_FIT_WHOLE; fit <= NV_FIT_STRETCH; ++fit) {
        for (u32 width = 16; width <= 4096; width *= 4) {
            for (u32 height = 16; height <= 4096; height *= 4) {
                NvResolution r = {.mode = NV_RESOLUTION_FIXED, .fixed_fit = (NvFixedFit)fit, .fixed_width = width, .fixed_height = height};
                NvSceneOutput o = nv_renderer_scene_output(&r, viewport);
                CHECK(o.image.x >= viewport.x && o.image.y >= viewport.y);
                CHECK(o.image.x + o.image.width <= viewport.x + viewport.width);
                CHECK(o.image.y + o.image.height <= viewport.y + viewport.height);
                CHECK(o.image.width >= 1 && o.image.height >= 1);
            }
        }
    }

    // Taps. A camera at (0, 0, 5) looking down -Z, 60 degrees, with the fixed 320 x 180 image in the middle of the viewport.
    NvNodeId none = {0};
    NvNodeId camera = nv_scene_add_node(&scene, none, "camera");
    NvNode* node = nv_scene_get(&scene, camera);
    node->position = nv_vec3(0, 0, 5);
    node->camera = (NvCamera){.projection = NV_PROJECTION_PERSPECTIVE, .fov_y = NV_PI / 3.0f, .near_z = 0.1f, .far_z = 100.0f};
    scene.active_camera = camera;
    nv_scene_update(&scene);

    fixed = (NvResolution){.mode = NV_RESOLUTION_FIXED, .fixed_fit = NV_FIT_WHOLE, .fixed_width = 320, .fixed_height = 180};
    out = nv_renderer_scene_output(&fixed, viewport); // the image is at (280, 124), 640 x 360
    NvRay ray = {0};
    CHECK(nv_renderer_tap_ray(&scene, out, 600.0f, 304.0f, 1.0f, &ray)); // the image's center
    CHECK(near(ray.direction.x, 0.0f) && near(ray.direction.y, 0.0f) && near(ray.direction.z, -1.0f));
    CHECK(near(ray.origin.x, 0.0f) && near(ray.origin.y, 0.0f) && near(ray.origin.z, 4.9f)); // the near plane

    // The same point from CSS pixels on a screen with two canvas pixels each.
    NvRay ray2 = {0};
    CHECK(nv_renderer_tap_ray(&scene, out, 300.0f, 152.0f, 2.0f, &ray2));
    CHECK(near(ray2.direction.z, -1.0f) && near(ray2.origin.z, 4.9f));

    // The bars around the image: above, left, right of it, and the image's far edges (exclusive).
    NvRay untouched = {nv_vec3(7, 7, 7), nv_vec3(7, 7, 7)};
    ray = untouched;
    CHECK(!nv_renderer_tap_ray(&scene, out, 600.0f, 60.0f, 1.0f, &ray));
    CHECK(!nv_renderer_tap_ray(&scene, out, 270.0f, 304.0f, 1.0f, &ray));
    CHECK(!nv_renderer_tap_ray(&scene, out, 930.0f, 304.0f, 1.0f, &ray));
    CHECK(!nv_renderer_tap_ray(&scene, out, 920.0f, 304.0f, 1.0f, &ray)); // x = 920 is the first column past the image
    CHECK(!nv_renderer_tap_ray(&scene, out, 600.0f, 484.0f, 1.0f, &ray)); // y = 484 is the first row past the image
    CHECK(ray.origin.x == 7.0f && ray.direction.z == 7.0f); // a miss leaves the ray alone
    CHECK(nv_renderer_tap_ray(&scene, out, 280.0f, 124.0f, 1.0f, &ray));  // the image's first pixel
    CHECK(nv_renderer_tap_ray(&scene, out, 919.0f, 483.0f, 1.0f, &ray));  // its last

    // The camera matrices the scene pass draws with: reverse Z, so the near plane maps to depth 1 and the far plane to 0.
    NvMat4 view_proj = nv_renderer_camera_view_proj(node, 16.0f / 9.0f);
    NvVec3 near_point = nv_mat4_transform_point(view_proj, nv_vec3(0, 0, 5.0f - 0.1f));
    NvVec3 far_point = nv_mat4_transform_point(view_proj, nv_vec3(0, 0, 5.0f - 100.0f));
    f32 near_w = view_proj.e[3] * 0 + view_proj.e[7] * 0 + view_proj.e[11] * (5.0f - 0.1f) + view_proj.e[15];
    f32 far_w = view_proj.e[11] * (5.0f - 100.0f) + view_proj.e[15];
    CHECK(near(near_point.z / near_w, 1.0f));
    CHECK(fabsf(far_point.z / far_w) < 1e-3f);

    if (failures == 0)
        printf("resolution_test: all passed\n");
    return failures ? 1 : 0;
}
