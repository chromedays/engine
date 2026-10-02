// Checks engine/chunk.h without a browser: a round trip, and that malformed input fails the reader
// instead of reading out of bounds. Runs under Node (ctest).

#include "engine/chunk.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

global int failures;

#define CHECK(expr)                                                        \
    do {                                                                   \
        if (!(expr)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

#define MAGIC NV_TAG('T', 'E', 'S', 'T')
#define TAG_BODY NV_TAG('B', 'O', 'D', 'Y')
#define TAG_NUMS NV_TAG('N', 'U', 'M', 'S')
#define TAG_VECT NV_TAG('V', 'E', 'C', 'T')
#define TAG_NAME NV_TAG('N', 'A', 'M', 'E')
#define TAG_LIST NV_TAG('L', 'I', 'S', 'T')
#define TAG_NEWS NV_TAG('N', 'E', 'W', 'S')

internal u32 write_sample(u8* buffer, u32 capacity)
{
    NvChunkWriter w;
    nv_chunk_writer_init(&w, buffer, capacity);
    nv_chunk_file_begin(&w, MAGIC, 2);
    nv_chunk_begin(&w, TAG_BODY);
    nv_chunk_string(&w, TAG_NEWS, "a tag the reader does not know");
    u32 nums[2] = {7, 0xFFFFFFFFu};
    nv_chunk_u32s(&w, TAG_NUMS, nums, 2);
    f32 vect[3] = {1.5f, -0.0f, 3.14159274f};
    nv_chunk_f32s(&w, TAG_VECT, vect, 3);
    nv_chunk_string(&w, TAG_NAME, "moon");
    u32 list[3] = {4, 0, 2};
    nv_chunk_u32s(&w, TAG_LIST, list, 3);
    nv_chunk_end(&w);
    return nv_chunk_file_end(&w);
}

internal void test_round_trip(void)
{
    u8 buffer[512];
    u32 size = write_sample(buffer, sizeof(buffer));
    CHECK(size > NV_CHUNK_FILE_HEADER_SIZE);

    u32 version = 0;
    NvChunk root;
    CHECK(nv_chunk_file_open(buffer, size, MAGIC, 2, &version, &root) == NV_CHUNK_FILE_OK);
    CHECK(version == 2);
    NvChunkReader r = {0};
    NvChunk body = nv_chunk_find(&r, root, TAG_BODY);
    CHECK(body.data != NULL);
    u32 nums[2] = {0};
    CHECK(nv_chunk_read_u32s(&r, body, TAG_NUMS, nums, 2));
    CHECK(nums[0] == 7 && nums[1] == 0xFFFFFFFFu);
    f32 vect[3] = {0};
    CHECK(nv_chunk_read_f32s(&r, body, TAG_VECT, vect, 3));
    CHECK(vect[0] == 1.5f && vect[2] == 3.14159274f && signbit(vect[1]));
    char name[3];
    CHECK(nv_chunk_read_string(&r, body, TAG_NAME, name, sizeof(name)));
    CHECK(strcmp(name, "mo") == 0); // truncated to fit
    u32 list[4];
    u32 count = 0;
    CHECK(nv_chunk_read_u32_list(&r, body, TAG_LIST, list, 4, &count));
    CHECK(count == 3 && list[0] == 4 && list[2] == 2);
    u32 missing = 99;
    CHECK(!nv_chunk_read_u32s(&r, body, NV_TAG('N', 'O', 'N', 'E'), &missing, 1));
    CHECK(missing == 99);
    CHECK(!r.failed);

    // A newer version is refused; so is another magic.
    CHECK(nv_chunk_file_open(buffer, size, MAGIC, 1, &version, &root) == NV_CHUNK_FILE_NEWER);
    CHECK(nv_chunk_file_open(buffer, size, NV_TAG('O', 'T', 'H', 'R'), 2, &version, &root) == NV_CHUNK_FILE_WRONG_MAGIC);
}

internal void test_damaged_files(void)
{
    u8 buffer[512];
    u32 size = write_sample(buffer, sizeof(buffer));
    u32 version;
    NvChunk root;
    // Every truncation is caught by the header's size.
    for (u32 cut = 0; cut < size; ++cut)
        CHECK(nv_chunk_file_open(buffer, cut, MAGIC, 2, &version, &root) != NV_CHUNK_FILE_OK);
    // Every flipped bit after the header is caught by the checksum.
    for (u32 i = NV_CHUNK_FILE_HEADER_SIZE; i < size; ++i) {
        for (u32 bit = 0; bit < 8; ++bit) {
            buffer[i] ^= (u8)(1u << bit);
            CHECK(nv_chunk_file_open(buffer, size, MAGIC, 2, &version, &root) == NV_CHUNK_FILE_WRONG_CHECKSUM);
            buffer[i] ^= (u8)(1u << bit);
        }
    }
}

// Chunks that lie about their size must fail the reader, never read past the parent.
internal void test_malformed_chunks(void)
{
    u8 bytes[64] = {0};
    // A child claiming more bytes than its parent has.
    memcpy(bytes, "NUMS", 4);
    bytes[4] = 200;
    NvChunk parent = {.size = 16, .data = bytes};
    NvChunkReader r = {0};
    NvChunk child = {0};
    CHECK(!nv_chunk_next(&r, parent, &child));
    CHECK(r.failed);

    // A parent too short for a chunk header.
    r = (NvChunkReader){0};
    child = (NvChunk){0};
    parent.size = 5;
    CHECK(!nv_chunk_next(&r, parent, &child));
    CHECK(r.failed);

    // A field of the wrong size.
    memset(bytes, 0, sizeof(bytes));
    memcpy(bytes, "NUMS", 4);
    bytes[4] = 4; // one u32
    parent.size = 12;
    r = (NvChunkReader){0};
    u32 two[2];
    CHECK(!nv_chunk_read_u32s(&r, parent, TAG_NUMS, two, 2));
    CHECK(r.failed);
    // After a failure every read returns nothing.
    u32 one = 5;
    CHECK(!nv_chunk_read_u32s(&r, parent, TAG_NUMS, &one, 1));
    CHECK(one == 5);

    // A list longer than the caller's room, and one that is not whole u32s.
    r = (NvChunkReader){0};
    u32 count;
    CHECK(!nv_chunk_read_u32_list(&r, parent, TAG_NUMS, two, 0, &count));
    CHECK(r.failed);
    bytes[4] = 3;
    r = (NvChunkReader){0};
    CHECK(!nv_chunk_read_u32_list(&r, parent, TAG_NUMS, two, 2, &count));
    CHECK(r.failed);

    // An empty parent has no children, and that is not a failure.
    r = (NvChunkReader){0};
    child = (NvChunk){0};
    parent.size = 0;
    CHECK(!nv_chunk_next(&r, parent, &child));
    CHECK(!r.failed);
}

internal void test_overflow(void)
{
    u8 buffer[512];
    u32 full = write_sample(buffer, sizeof(buffer));
    for (u32 capacity = 0; capacity < full; ++capacity)
        CHECK(write_sample(buffer, capacity) == 0);
    CHECK(write_sample(buffer, full) == full);
}

int main(void)
{
    // The CRC-32 check value.
    CHECK(nv_crc32("123456789", 9) == 0xCBF43926u);
    test_round_trip();
    test_damaged_files();
    test_malformed_chunks();
    test_overflow();
    if (failures) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    printf("chunk_test: all checks passed\n");
    return 0;
}
