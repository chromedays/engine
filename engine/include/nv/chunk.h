#pragma once

#include "nv/base.h"

// A tagged binary format: every chunk is a tag, a payload size and the payload. A container's
// payload is more chunks; a field's payload is a value (u32s, f32s or a string's bytes). Readers
// find children by tag and skip the ones they do not know, so files stay readable across versions
// (docs/specs/save.md). Numbers are little-endian whatever the machine.
//
// A file is a 16-byte header (magic, version, payload size, CRC-32 of the payload) and then the
// top-level chunks.

// Four characters, stored in that order in the file: NV_TAG('N','O','D','E') reads "NODE".
#define NV_TAG(a, b, c, d) ((u32)(u8)(a) | ((u32)(u8)(b) << 8) | ((u32)(u8)(c) << 16) | ((u32)(u8)(d) << 24))

#define NV_CHUNK_HEADER_SIZE 8
#define NV_CHUNK_FILE_HEADER_SIZE 16
#define NV_CHUNK_MAX_DEPTH 16

//
// Writing
//

// Appends to a fixed buffer. Writing past its end sets `overflow` and drops the rest, so callers
// check once at the end.
typedef struct NvChunkWriter {
    u8* bytes;
    u32 size;
    u32 capacity;
    b32 overflow;
    u32 open[NV_CHUNK_MAX_DEPTH]; // where each open chunk's size field is
    u32 depth;
} NvChunkWriter;

void nv_chunk_writer_init(NvChunkWriter* writer, void* buffer, u32 capacity);

// Reserves the file header; nv_chunk_file_end fills it in.
void nv_chunk_file_begin(NvChunkWriter* writer, u32 magic, u32 version);
// Returns the file's size, or 0 if it did not fit or a chunk was left open.
u32 nv_chunk_file_end(NvChunkWriter* writer);

void nv_chunk_begin(NvChunkWriter* writer, u32 tag); // a container
void nv_chunk_end(NvChunkWriter* writer);

void nv_chunk_u32s(NvChunkWriter* writer, u32 tag, const u32* values, u32 count);
void nv_chunk_f32s(NvChunkWriter* writer, u32 tag, const f32* values, u32 count);
void nv_chunk_u32(NvChunkWriter* writer, u32 tag, u32 value);
void nv_chunk_f32(NvChunkWriter* writer, u32 tag, f32 value);
void nv_chunk_string(NvChunkWriter* writer, u32 tag, const char* text); // without its NUL

//
// Reading
//

// A chunk inside a file: its tag and payload. A zeroed chunk is a missing one.
typedef struct NvChunk {
    u32 tag;
    u32 size;
    const u8* data;
} NvChunk;

// Every read checks bounds. A chunk that runs past its parent, or a field of the wrong size, sets
// `failed`; after that every read returns nothing, so a reader checks once at the end.
typedef struct NvChunkReader {
    b32 failed;
} NvChunkReader;

typedef enum NvChunkFileStatus {
    NV_CHUNK_FILE_OK,
    NV_CHUNK_FILE_TOO_SHORT,     // smaller than the header
    NV_CHUNK_FILE_WRONG_MAGIC,
    NV_CHUNK_FILE_WRONG_SIZE,    // the header's size is not what follows it
    NV_CHUNK_FILE_WRONG_CHECKSUM,
    NV_CHUNK_FILE_NEWER,         // a version above `max_version`
} NvChunkFileStatus;

// Checks the header and returns the top level as a container chunk in `root`.
NvChunkFileStatus nv_chunk_file_open(const void* bytes, u32 size, u32 magic, u32 max_version, u32* version,
                                     NvChunk* root);
const char* nv_chunk_file_status_name(NvChunkFileStatus status);

// Steps through a container's children. Start with a zeroed `child`; returns 0 after the last one
// (or on a malformed one, which also fails the reader).
b32 nv_chunk_next(NvChunkReader* reader, NvChunk parent, NvChunk* child);

// The first child with `tag`, or a zeroed chunk.
NvChunk nv_chunk_find(NvChunkReader* reader, NvChunk parent, u32 tag);

// Fields. Each returns whether the field was there; `out` is left alone when it was not. A field
// whose size does not fit fails the reader.
b32 nv_chunk_read_u32s(NvChunkReader* reader, NvChunk parent, u32 tag, u32* out, u32 count);
b32 nv_chunk_read_f32s(NvChunkReader* reader, NvChunk parent, u32 tag, f32* out, u32 count);
// Between 0 and `max` u32s; `count` receives how many.
b32 nv_chunk_read_u32_list(NvChunkReader* reader, NvChunk parent, u32 tag, u32* out, u32 max, u32* count);
// Copies the string with a NUL, truncated to fit `capacity`.
b32 nv_chunk_read_string(NvChunkReader* reader, NvChunk parent, u32 tag, char* out, u32 capacity);

// Little-endian u32 at `bytes`.
u32 nv_chunk_load_u32(const u8* bytes);

u32 nv_crc32(const void* bytes, u32 size);
