// The GPU-free parts of the effects system (docs/specs/vfx.md): how a frame's bursts split into
// workgroup jobs, and how the segment and decal rings hand out slots.
#include "../engine/src/vfx_cpu.h"

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

static void test_jobs(void)
{
    NvVfxBurst bursts[4] = {{.count = 1}, {.count = 64}, {.count = 65}, {.count = 0}};
    u32 jobs[64] = {0};
    u32 used = 0, lost = 0;
    u32 n = nv_vfx_split_bursts(bursts, 4, 16, jobs, &used, &lost);
    // 1 -> one group; 64 -> one group; 65 -> two groups; 0 -> none.
    CHECK(n == 4);
    CHECK(used == 3); // the empty last burst has no job, so it is not uploaded
    CHECK(lost == 0);
    CHECK(jobs[0] == 0 && jobs[1] == 0);
    CHECK(jobs[2] == 1 && jobs[3] == 0);
    CHECK(jobs[4] == 2 && jobs[5] == 0);
    CHECK(jobs[6] == 2 && jobs[7] == 64); // the second workgroup of the third burst starts at particle 64

    // A burst larger than the room is cut to it; the ones after it are lost.
    NvVfxBurst big[3] = {{.count = 64 * 3}, {.count = 64 * 5}, {.count = 64}};
    used = 0, lost = 0;
    n = nv_vfx_split_bursts(big, 3, 5, jobs, &used, &lost);
    CHECK(n == 5);
    CHECK(big[1].count == 64 * 2); // 3 + 2 groups
    CHECK(used == 2);
    CHECK(lost == 1); // the third had no room left
    CHECK(jobs[2 * 2] == 0 || jobs[2 * 2] == 1);
    CHECK(jobs[4 * 2] == 1 && jobs[4 * 2 + 1] == 64);

    // Nothing to do.
    used = 7, lost = 0;
    CHECK(nv_vfx_split_bursts(big, 0, 5, jobs, &used, &lost) == 0);
    CHECK(used == 0 && lost == 0);
}

static void test_ring(void)
{
    u32 head = 0, filled = 0;
    // Fills in order, then wraps and overwrites the oldest first.
    for (u32 i = 0; i < 4; ++i)
        CHECK(nv_vfx_ring_take(&head, &filled, 4) == i);
    CHECK(head == 0 && filled == 4);
    CHECK(nv_vfx_ring_take(&head, &filled, 4) == 0);
    CHECK(nv_vfx_ring_take(&head, &filled, 4) == 1);
    CHECK(head == 2 && filled == 4);
    // A ring of one always gives slot 0.
    head = filled = 0;
    CHECK(nv_vfx_ring_take(&head, &filled, 1) == 0);
    CHECK(nv_vfx_ring_take(&head, &filled, 1) == 0);
    CHECK(head == 0 && filled == 1);
}

int main(void)
{
    test_jobs();
    test_ring();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("vfx_test: ok\n");
    return 0;
}
