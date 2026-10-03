// engine/file.h without a browser: a file is read whole into the arena with a 0 after it; a missing file, or one larger than
// what is left of the arena, fails (a zeroed NvFileData) and leaves the arena as it was. Runs under Node with NODERAWFS,
// reading this source.

#include "engine/file.h"

#include <stdio.h>
#include <string.h>

global int failures;
#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            printf("FAILED line %d: %s\n", __LINE__, #condition);                 \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

global u8 memory[NV_KILOBYTES(64)];

int main(void)
{
    NvArena arena;
    nv_arena_init(&arena, memory, sizeof(memory));
    nv_arena_push(&arena, 3, 1); // so the file's bytes need aligning

    NvFileData file = nv_file_read(&arena, FILE_TEST_SOURCE);
    CHECK(file.ok && file.bytes != NULL && ((umm)file.bytes & 15) == 0);
    CHECK(file.size > 100 && file.bytes[file.size] == 0 && strlen((const char*)file.bytes) == file.size);
    CHECK(strncmp((const char*)file.bytes, "// engine/file.h without a browser", 34) == 0);

    umm used = arena.used;
    NvFileData missing = nv_file_read(&arena, "/no/such/file");
    CHECK(!missing.ok && !missing.bytes && !missing.size && arena.used == used);

    // Less room left than the file needs.
    NvArena small;
    nv_arena_init(&small, memory, file.size); // the file and its 0 do not fit
    NvFileData too_big = nv_file_read(&small, FILE_TEST_SOURCE);
    CHECK(!too_big.ok && !too_big.bytes && !too_big.size && small.used == 0);

    if (failures == 0)
        printf("file_test: all passed\n");
    return failures ? 1 : 0;
}
