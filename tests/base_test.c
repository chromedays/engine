// The small helpers in engine/base.h, engine/math.h and engine/window.h: FNV-1a, clamps, PCG32, the pixel ratio, rectangles
// from CSS pixels.
#include <engine/math.h>
#include <engine/window.h>

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            printf("FAILED line %d: %s\n", __LINE__, #condition);                 \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

int main(void)
{
    // FNV-1a's published test vectors (isthe.com/chongo/tech/comp/fnv).
    CHECK(nv_fnv1a(NV_FNV1A_SEED, "", 0) == 0x811c9dc5u);
    CHECK(nv_fnv1a(NV_FNV1A_SEED, "a", 1) == 0xe40c292cu);
    CHECK(nv_fnv1a(NV_FNV1A_SEED, "foobar", 6) == 0xbf9cf968u);
    // Runs chain: the hash of "foo" then "bar" is the hash of "foobar".
    CHECK(nv_fnv1a(nv_fnv1a(NV_FNV1A_SEED, "foo", 3), "bar", 3) == 0xbf9cf968u);

    CHECK(nv_clamp_f32(-1.0f, 0.0f, 2.0f) == 0.0f);
    CHECK(nv_clamp_f32(3.0f, 0.0f, 2.0f) == 2.0f);
    CHECK(nv_clamp_f32(1.5f, 0.0f, 2.0f) == 1.5f);
    CHECK(nv_clamp_u32(0, 1, 4) == 1);
    CHECK(nv_clamp_u32(9, 1, 4) == 4);
    CHECK(nv_clamp_u32(3, 1, 4) == 3);

    // PCG32 seeded 42, sequence 54: the first outputs of the reference implementation's demo (pcg-c, pcg32-demo).
    NvRandom random = {0};
    nv_random_seed(&random, 42, 54);
    const u32 expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (u32 i = 0; i < NV_ARRAY_COUNT(expected); ++i)
        CHECK(nv_random_u32(&random) == expected[i]);

    // The same seed gives the same numbers, another sequence gives others.
    NvRandom a = {0}, b = {0}, c = {0};
    nv_random_seed(&a, 7, 1);
    nv_random_seed(&b, 7, 1);
    nv_random_seed(&c, 7, 2);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    CHECK(memcmp(&a, &c, sizeof(a)) != 0);
    u32 from_a = nv_random_u32(&a), from_b = nv_random_u32(&b), from_c = nv_random_u32(&c);
    CHECK(from_a == from_b);
    CHECK(from_a != from_c);

    // A zeroed generator works, and nv_random_f32 stays in [0, 1).
    NvRandom zero = {0};
    f32 lowest = 1.0f, highest = 0.0f;
    for (u32 i = 0; i < 10000; ++i) {
        f32 value = nv_random_f32(&zero);
        CHECK(value >= 0.0f && value < 1.0f);
        lowest = value < lowest ? value : lowest;
        highest = value > highest ? value : highest;
    }
    CHECK(lowest < 0.01f && highest > 0.99f);

    NvWindow window = {0};
    CHECK(nv_window_pixel_ratio(&window) == 1.0f); // before the browser says
    window.pixel_ratio = 2.0f;
    CHECK(nv_window_pixel_ratio(&window) == 2.0f);

    // A rectangle from CSS corners: each corner is rounded on its own, so at a ratio like 1.5 two rectangles that share an
    // edge in CSS pixels still share it (10.3 * 1.5 = 15.45, the edge of both), with no gap or overlap.
    NvRect left = nv_window_framebuffer_rect_from_css(0.0f, 0.0f, 10.3f, 20.0f, 1.5f);
    NvRect right = nv_window_framebuffer_rect_from_css(10.3f, 0.0f, 30.0f, 20.0f, 1.5f);
    CHECK(left.x == 0 && left.y == 0 && left.width == 15 && left.height == 30);
    CHECK(right.x == left.x + left.width && right.width == 30 && right.height == 30);
    // Corners in the wrong order make an empty rectangle at the first corner.
    NvRect empty = nv_window_framebuffer_rect_from_css(5.0f, 5.0f, 2.0f, 1.0f, 2.0f);
    CHECK(empty.x == 10 && empty.y == 10 && empty.width == 0 && empty.height == 0);
    // Corners left of or above the canvas (a window narrower than its docks) are held at 0.
    NvRect clipped = nv_window_framebuffer_rect_from_css(-40.0f, -3.0f, 10.0f, -1.0f, 2.0f);
    CHECK(clipped.x == 0 && clipped.y == 0 && clipped.width == 20 && clipped.height == 0);

    if (failures == 0)
        printf("base_test: all passed\n");
    return failures ? 1 : 0;
}
