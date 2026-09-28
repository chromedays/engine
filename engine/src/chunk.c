#include "nv/chunk.h"

#include <string.h>

//
// Writing
//

internal void store_u32(u8* at, u32 value)
{
    at[0] = (u8)value;
    at[1] = (u8)(value >> 8);
    at[2] = (u8)(value >> 16);
    at[3] = (u8)(value >> 24);
}

u32 nv_chunk_load_u32(const u8* bytes)
{
    return (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

// Room for `size` more bytes, or NULL (and overflow) when there is none.
internal u8* reserve(NvChunkWriter* writer, u32 size)
{
    if (writer->overflow || size > writer->capacity - writer->size) {
        writer->overflow = 1;
        return NULL;
    }
    u8* at = writer->bytes + writer->size;
    writer->size += size;
    return at;
}

void nv_chunk_writer_init(NvChunkWriter* writer, void* buffer, u32 capacity)
{
    *writer = (NvChunkWriter){.bytes = buffer, .capacity = capacity};
}

void nv_chunk_file_begin(NvChunkWriter* writer, u32 magic, u32 version)
{
    NV_ASSERT(writer->size == 0);
    u8* header = reserve(writer, NV_CHUNK_FILE_HEADER_SIZE);
    if (!header)
        return;
    store_u32(header, magic);
    store_u32(header + 4, version);
    store_u32(header + 8, 0);
    store_u32(header + 12, 0);
}

u32 nv_chunk_file_end(NvChunkWriter* writer)
{
    NV_ASSERT(writer->depth == 0);
    if (writer->overflow || writer->depth || writer->size < NV_CHUNK_FILE_HEADER_SIZE)
        return 0;
    u32 payload = writer->size - NV_CHUNK_FILE_HEADER_SIZE;
    store_u32(writer->bytes + 8, payload);
    store_u32(writer->bytes + 12, nv_crc32(writer->bytes + NV_CHUNK_FILE_HEADER_SIZE, payload));
    return writer->size;
}

void nv_chunk_begin(NvChunkWriter* writer, u32 tag)
{
    NV_ASSERT(writer->depth < NV_CHUNK_MAX_DEPTH);
    if (writer->depth >= NV_CHUNK_MAX_DEPTH) {
        writer->overflow = 1;
        return;
    }
    // Counted as open even when it does not fit, so every nv_chunk_end still has its begin.
    u8* header = reserve(writer, NV_CHUNK_HEADER_SIZE);
    if (header)
        store_u32(header, tag);
    writer->open[writer->depth++] = header ? writer->size - 4 : 0;
}

void nv_chunk_end(NvChunkWriter* writer)
{
    NV_ASSERT(writer->depth > 0);
    if (!writer->depth)
        return;
    u32 size_at = writer->open[--writer->depth];
    if (!writer->overflow)
        store_u32(writer->bytes + size_at, writer->size - (size_at + 4));
}

// A field's header and room for its payload.
internal u8* field(NvChunkWriter* writer, u32 tag, u32 size)
{
    u8* at = reserve(writer, NV_CHUNK_HEADER_SIZE + size);
    if (!at)
        return NULL;
    store_u32(at, tag);
    store_u32(at + 4, size);
    return at + NV_CHUNK_HEADER_SIZE;
}

void nv_chunk_u32s(NvChunkWriter* writer, u32 tag, const u32* values, u32 count)
{
    u8* at = field(writer, tag, count * 4);
    if (!at)
        return;
    for (u32 i = 0; i < count; ++i)
        store_u32(at + i * 4, values[i]);
}

void nv_chunk_f32s(NvChunkWriter* writer, u32 tag, const f32* values, u32 count)
{
    u8* at = field(writer, tag, count * 4);
    if (!at)
        return;
    for (u32 i = 0; i < count; ++i) {
        u32 bits;
        memcpy(&bits, &values[i], 4);
        store_u32(at + i * 4, bits);
    }
}

void nv_chunk_u32(NvChunkWriter* writer, u32 tag, u32 value)
{
    nv_chunk_u32s(writer, tag, &value, 1);
}

void nv_chunk_f32(NvChunkWriter* writer, u32 tag, f32 value)
{
    nv_chunk_f32s(writer, tag, &value, 1);
}

void nv_chunk_string(NvChunkWriter* writer, u32 tag, const char* text)
{
    u32 length = (u32)strlen(text);
    u8* at = field(writer, tag, length);
    if (at)
        memcpy(at, text, length);
}

//
// Reading
//

NvChunkFileStatus nv_chunk_file_open(const void* bytes, u32 size, u32 magic, u32 max_version, u32* version,
                                     NvChunk* root)
{
    *root = (NvChunk){0};
    const u8* b = bytes;
    if (size < NV_CHUNK_FILE_HEADER_SIZE)
        return NV_CHUNK_FILE_TOO_SHORT;
    if (nv_chunk_load_u32(b) != magic)
        return NV_CHUNK_FILE_WRONG_MAGIC;
    u32 payload = nv_chunk_load_u32(b + 8);
    if (payload != size - NV_CHUNK_FILE_HEADER_SIZE)
        return NV_CHUNK_FILE_WRONG_SIZE;
    if (nv_chunk_load_u32(b + 12) != nv_crc32(b + NV_CHUNK_FILE_HEADER_SIZE, payload))
        return NV_CHUNK_FILE_WRONG_CHECKSUM;
    *version = nv_chunk_load_u32(b + 4);
    if (*version > max_version)
        return NV_CHUNK_FILE_NEWER;
    *root = (NvChunk){.size = payload, .data = b + NV_CHUNK_FILE_HEADER_SIZE};
    return NV_CHUNK_FILE_OK;
}

const char* nv_chunk_file_status_name(NvChunkFileStatus status)
{
    switch (status) {
    case NV_CHUNK_FILE_OK: return "ok";
    case NV_CHUNK_FILE_TOO_SHORT: return "shorter than its header";
    case NV_CHUNK_FILE_WRONG_MAGIC: return "not a save";
    case NV_CHUNK_FILE_WRONG_SIZE: return "cut short or too long";
    case NV_CHUNK_FILE_WRONG_CHECKSUM: return "damaged (checksum)";
    case NV_CHUNK_FILE_NEWER: return "from a newer version";
    }
    return "unknown";
}

b32 nv_chunk_next(NvChunkReader* reader, NvChunk parent, NvChunk* child)
{
    if (reader->failed || !parent.data)
        return 0;
    // The next chunk starts after the current one (or at the parent's start).
    u32 offset = child->data ? (u32)(child->data - parent.data) + child->size : 0;
    if (offset == parent.size)
        return 0;
    if (offset > parent.size || parent.size - offset < NV_CHUNK_HEADER_SIZE) {
        reader->failed = 1;
        return 0;
    }
    const u8* at = parent.data + offset;
    u32 size = nv_chunk_load_u32(at + 4);
    if (size > parent.size - offset - NV_CHUNK_HEADER_SIZE) {
        reader->failed = 1;
        return 0;
    }
    *child = (NvChunk){.tag = nv_chunk_load_u32(at), .size = size, .data = at + NV_CHUNK_HEADER_SIZE};
    return 1;
}

NvChunk nv_chunk_find(NvChunkReader* reader, NvChunk parent, u32 tag)
{
    NvChunk child = {0};
    while (nv_chunk_next(reader, parent, &child)) {
        if (child.tag == tag)
            return child;
    }
    return (NvChunk){0};
}

// The field with `tag` if its size is `size` exactly; a wrong size fails the reader.
internal const u8* find_field(NvChunkReader* reader, NvChunk parent, u32 tag, u32 size)
{
    NvChunk field = nv_chunk_find(reader, parent, tag);
    if (!field.data)
        return NULL;
    if (field.size != size) {
        reader->failed = 1;
        return NULL;
    }
    return field.data;
}

b32 nv_chunk_read_u32s(NvChunkReader* reader, NvChunk parent, u32 tag, u32* out, u32 count)
{
    const u8* at = find_field(reader, parent, tag, count * 4);
    if (!at)
        return 0;
    for (u32 i = 0; i < count; ++i)
        out[i] = nv_chunk_load_u32(at + i * 4);
    return 1;
}

b32 nv_chunk_read_f32s(NvChunkReader* reader, NvChunk parent, u32 tag, f32* out, u32 count)
{
    const u8* at = find_field(reader, parent, tag, count * 4);
    if (!at)
        return 0;
    for (u32 i = 0; i < count; ++i) {
        u32 bits = nv_chunk_load_u32(at + i * 4);
        memcpy(&out[i], &bits, 4);
    }
    return 1;
}

b32 nv_chunk_read_u32_list(NvChunkReader* reader, NvChunk parent, u32 tag, u32* out, u32 max, u32* count)
{
    NvChunk field = nv_chunk_find(reader, parent, tag);
    if (!field.data)
        return 0;
    if (field.size % 4 || field.size / 4 > max) {
        reader->failed = 1;
        return 0;
    }
    *count = field.size / 4;
    for (u32 i = 0; i < *count; ++i)
        out[i] = nv_chunk_load_u32(field.data + i * 4);
    return 1;
}

b32 nv_chunk_read_string(NvChunkReader* reader, NvChunk parent, u32 tag, char* out, u32 capacity)
{
    NV_ASSERT(capacity > 0);
    NvChunk field = nv_chunk_find(reader, parent, tag);
    if (!field.data)
        return 0;
    u32 length = field.size < capacity - 1 ? field.size : capacity - 1;
    memcpy(out, field.data, length);
    out[length] = 0;
    return 1;
}

// CRC-32 (the zlib and PNG one), a bit at a time: saves are small.
u32 nv_crc32(const void* bytes, u32 size)
{
    const u8* b = bytes;
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0; i < size; ++i) {
        crc ^= b[i];
        for (u32 bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
