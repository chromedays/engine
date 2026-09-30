// Checks the texture memory helpers in nv/gpu.h without a browser. Runs under Node (ctest).

#include "nv/gpu.h"

#include <stdio.h>

global int failures;

#define CHECK(expr)                                                        \
    do {                                                                   \
        if (!(expr)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

int main(void)
{
    CHECK(nv_gpu_format_bytes(WGPUTextureFormat_RGBA8UnormSrgb) == 4);
    CHECK(nv_gpu_format_bytes(WGPUTextureFormat_Depth16Unorm) == 2);
    CHECK(nv_gpu_format_bytes(WGPUTextureFormat_Depth32Float) == 4);
    CHECK(nv_gpu_format_bytes(WGPUTextureFormat_R8Unorm) == 0);

    // One level: the texels.
    CHECK(nv_gpu_texture_bytes(2048, 2048, 1, WGPUTextureFormat_Depth32Float) == 2048ull * 2048 * 4);
    CHECK(nv_gpu_texture_bytes(1, 1, 1, WGPUTextureFormat_RGBA8Unorm) == 4);
    // A full chain of a square power of two: 4/3 of the top level, less a third of the last texel.
    u64 chain = nv_gpu_texture_bytes(1024, 1024, 11, WGPUTextureFormat_RGBA8UnormSrgb);
    CHECK(chain == (1024ull * 1024 * 4 * 4 - 4) / 3);
    // A non-square chain: levels stop halving at 1 on the short side (4x1, 2x1, 1x1).
    CHECK(nv_gpu_texture_bytes(4, 1, 3, WGPUTextureFormat_RGBA8Unorm) == (4 + 2 + 1) * 4);
    // Odd sizes round down: 5x3, 2x1, 1x1.
    CHECK(nv_gpu_texture_bytes(5, 3, 3, WGPUTextureFormat_RGBA8Unorm) == (15 + 2 + 1) * 4);

    if (failures) {
        fprintf(stderr, "gpu_format_test: %d checks failed\n", failures);
        return 1;
    }
    printf("gpu_format_test: ok\n");
    return 0;
}
