// Compares two capture directories of the D3D12 port (docs/specs/d3d12_port.md): compare [options] <baseline dir> <current dir>
// The one comparison tool of both builds: plain C17 on the C library alone (its own PNG reader and writer, inflate and JSON reader),
// so a Windows PC with MSVC and the cloud with any C compiler run the same code:
//   cl /nologo /std:c17 /I<repo> compare.c      cc -std=c17 -I<repo> compare.c -lm -o compare
//
// Screens (PNG, with the masks of the baseline's sidecar .json left out):
//   default   the largest channel difference may be 2/255 and at most 0.1% of the pixels may exceed it. The effects' screens (V*,
//             unsorted translucent particles) are held to the largest difference of the 16x16 block averages instead.
//   --exact   no channel of any pixel may differ (a build of the same renderer: stages 0-2, the web against its own baseline); what the
//             baseline itself does not repeat between two runs is held to what it does repeat (the effects' screens to their block
//             averages, S10 to 0.01% of its pixels, V12-250k left out).
//   --blocks  another renderer than the baseline's (the D3D12 build on a GPU against the SwiftShader baseline): the picture is cut
//             in 8x8 blocks; the largest channel difference between the blocks' averages may be --tolerance (default 4/255) in at
//             most --over (a share, default 0.03) of the blocks, and in none more than --largest (default 40/255). The two limits
//             are for thin lines, which the two rasterizers draw differently (the debug lines are 1 pixel and aliased on a GPU,
//             antialiased in SwiftShader): a line moves the averages of a few blocks by a lot, a real mistake much more or in many
//             more. The effects' screens (V*) are held to five times the share and 2.75 times the largest block, since their
//             translucent particles are unsorted and blend in the order the GPU's threads left them in. `--diff <dir>` writes a picture of the differences (a block's brightness is its difference, 16 to the level;
//             blocks over the tolerance are red).
// Effects (JSON dumps): counters, arguments and counts are exact (simulate's workgroup count may be the one a frame earlier or later, see compare_effects); the live particles are compared sorted by (emitter, seed) with a
// relative error of 1e-4 (1e-3, a half float's ulp, for the values the shader keeps as halves).
// --present: only what the current directory holds (a run of some cases). Exits with 1 when anything differs.

#if defined(_WIN32)
#include <windows.h> // before base.h: its `internal` macro
#else
#include <dirent.h>
#endif

#include <engine/base.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// Memory: one block, reserved at the start. A tool may call malloc for that; the engine does not.
//

global NvArena arena;

internal void* push(umm size)
{
    return nv_arena_push(&arena, size, 16);
}

internal void fail(const char* message, const char* what)
{
    fprintf(stderr, "compare: %s %s\n", message, what ? what : "");
    exit(2);
}

//
// Files
//

internal u8* read_file(const char* path, umm* size_out)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    u8* bytes = push((umm)size + 1);
    if (fread(bytes, 1, (umm)size, file) != (umm)size)
        fail("cannot read", path);
    fclose(file);
    if (size_out)
        *size_out = (umm)size;
    return bytes;
}

#define MAX_NAMES 512
#define NAME_SIZE 128

internal int compare_names(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b);
}

// The names in a directory, sorted.
internal u32 list_directory(const char* directory, char (*names)[NAME_SIZE])
{
    u32 count = 0;
#if defined(_WIN32)
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", directory);
    WIN32_FIND_DATAA found;
    HANDLE handle = FindFirstFileA(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE)
        fail("cannot list", directory);
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && count < MAX_NAMES)
            snprintf(names[count++], NAME_SIZE, "%s", found.cFileName);
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
#else
    DIR* handle = opendir(directory);
    if (!handle)
        fail("cannot list", directory);
    for (struct dirent* found = readdir(handle); found; found = readdir(handle)) {
        if (found->d_name[0] != '.' && count < MAX_NAMES)
            snprintf(names[count++], NAME_SIZE, "%s", found->d_name);
    }
    closedir(handle);
#endif
    qsort(names, count, NAME_SIZE, compare_names);
    return count;
}

//
// PNG: the reader (8-bit, not interlaced) and a writer that stores its data uncompressed
//

typedef struct Image {
    u32 width, height;
    u8* rgba; // width * height * 4
} Image;

typedef struct Huffman {
    u16 count[16];
    u16 symbol[288];
} Huffman;

typedef struct Inflater {
    const u8* in;
    umm size, position;
    u32 bits, bit_count;
    u8* out;
    umm out_size, out_position;
} Inflater;

internal u32 get_bits(Inflater* z, u32 count)
{
    u32 value = z->bits;
    while (z->bit_count < count) {
        if (z->position >= z->size)
            fail("a PNG ends too soon", NULL);
        value |= (u32)z->in[z->position++] << z->bit_count;
        z->bit_count += 8;
    }
    z->bits = value >> count;
    z->bit_count -= count;
    return value & ((1u << count) - 1);
}

internal void build_huffman(Huffman* h, const u8* lengths, u32 n)
{
    memset(h->count, 0, sizeof(h->count));
    for (u32 i = 0; i < n; ++i)
        ++h->count[lengths[i]];
    u16 offsets[16];
    offsets[1] = 0;
    for (u32 len = 1; len < 15; ++len)
        offsets[len + 1] = (u16)(offsets[len] + h->count[len]);
    for (u32 i = 0; i < n; ++i) {
        if (lengths[i])
            h->symbol[offsets[lengths[i]]++] = (u16)i;
    }
}

// As Mark Adler's puff: a canonical code read bit by bit.
internal u32 decode_symbol(Inflater* z, const Huffman* h)
{
    s32 code = 0, first = 0, index = 0;
    for (u32 len = 1; len <= 15; ++len) {
        code |= (s32)get_bits(z, 1);
        s32 count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    fail("a PNG has a bad code", NULL);
    return 0;
}

global const u16 length_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
global const u16 length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
global const u16 distance_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
global const u16 distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

internal void inflate_block(Inflater* z, const Huffman* literals, const Huffman* distances)
{
    for (;;) {
        u32 symbol = decode_symbol(z, literals);
        if (symbol < 256) {
            if (z->out_position >= z->out_size)
                fail("a PNG has more data than its size says", NULL);
            z->out[z->out_position++] = (u8)symbol;
        } else if (symbol == 256) {
            return;
        } else {
            symbol -= 257;
            if (symbol >= 29)
                fail("a PNG has a bad length", NULL);
            u32 length = length_base[symbol] + get_bits(z, length_extra[symbol]);
            u32 distance_symbol = decode_symbol(z, distances);
            if (distance_symbol >= 30)
                fail("a PNG has a bad distance", NULL);
            u32 distance = distance_base[distance_symbol] + get_bits(z, distance_extra[distance_symbol]);
            if (distance > z->out_position || z->out_position + length > z->out_size)
                fail("a PNG copies from outside", NULL);
            for (u32 i = 0; i < length; ++i, ++z->out_position)
                z->out[z->out_position] = z->out[z->out_position - distance];
        }
    }
}

// zlib data (RFC 1950 around RFC 1951) into `out`, which has to be as large as the data.
internal void inflate(const u8* data, umm size, u8* out, umm out_size)
{
    Inflater z = {.in = data, .size = size, .position = 2, .out = out, .out_size = out_size}; // past the zlib header
    for (u32 last = 0; !last;) {
        last = get_bits(&z, 1);
        u32 type = get_bits(&z, 2);
        if (type == 0) {
            z.bits = 0;
            z.bit_count = 0;
            if (z.position + 4 > z.size)
                fail("a PNG ends too soon", NULL);
            u32 length = z.in[z.position] | ((u32)z.in[z.position + 1] << 8);
            z.position += 4;
            if (z.position + length > z.size || z.out_position + length > z.out_size)
                fail("a PNG has a bad stored block", NULL);
            memcpy(z.out + z.out_position, z.in + z.position, length);
            z.position += length;
            z.out_position += length;
        } else if (type == 1) {
            u8 lengths[288 + 30];
            for (u32 i = 0; i < 144; ++i) lengths[i] = 8;
            for (u32 i = 144; i < 256; ++i) lengths[i] = 9;
            for (u32 i = 256; i < 280; ++i) lengths[i] = 7;
            for (u32 i = 280; i < 288; ++i) lengths[i] = 8;
            for (u32 i = 0; i < 30; ++i) lengths[288 + i] = 5;
            Huffman literals, distances;
            build_huffman(&literals, lengths, 288);
            build_huffman(&distances, lengths + 288, 30);
            inflate_block(&z, &literals, &distances);
        } else if (type == 2) {
            u32 literal_count = get_bits(&z, 5) + 257;
            u32 distance_count = get_bits(&z, 5) + 1;
            u32 code_count = get_bits(&z, 4) + 4;
            local_persist const u8 order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            u8 lengths[320] = {0};
            for (u32 i = 0; i < code_count; ++i)
                lengths[order[i]] = (u8)get_bits(&z, 3);
            Huffman code_lengths;
            build_huffman(&code_lengths, lengths, 19);
            u8 all[320] = {0};
            for (u32 i = 0; i < literal_count + distance_count;) {
                u32 symbol = decode_symbol(&z, &code_lengths);
                if (symbol < 16) {
                    all[i++] = (u8)symbol;
                } else {
                    u32 repeat, value = 0;
                    if (symbol == 16) {
                        if (i == 0)
                            fail("a PNG repeats nothing", NULL);
                        value = all[i - 1];
                        repeat = 3 + get_bits(&z, 2);
                    } else if (symbol == 17) {
                        repeat = 3 + get_bits(&z, 3);
                    } else {
                        repeat = 11 + get_bits(&z, 7);
                    }
                    if (i + repeat > literal_count + distance_count)
                        fail("a PNG has too many code lengths", NULL);
                    while (repeat--)
                        all[i++] = (u8)value;
                }
            }
            Huffman literals, distances;
            build_huffman(&literals, all, literal_count);
            build_huffman(&distances, all + literal_count, distance_count);
            inflate_block(&z, &literals, &distances);
        } else {
            fail("a PNG has a bad block", NULL);
        }
    }
}

internal u32 read_u32_be(const u8* p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

internal Image read_png(const char* path)
{
    umm size;
    u8* file = read_file(path, &size);
    if (!file || size < 8 || memcmp(file, "\x89PNG\r\n\x1a\n", 8) != 0)
        fail("not a PNG:", path);
    u32 width = 0, height = 0, color_type = 0;
    u8* compressed = push(size);
    umm compressed_size = 0;
    for (umm position = 8; position + 12 <= size;) {
        u32 length = read_u32_be(file + position);
        const u8* type = file + position + 4;
        const u8* data = file + position + 8;
        if (memcmp(type, "IHDR", 4) == 0) {
            width = read_u32_be(data);
            height = read_u32_be(data + 4);
            if (data[8] != 8 || data[12] != 0)
                fail("only 8-bit PNGs that are not interlaced are read:", path);
            color_type = data[9];
        } else if (memcmp(type, "IDAT", 4) == 0) {
            memcpy(compressed + compressed_size, data, length);
            compressed_size += length;
        }
        position += 12 + (umm)length;
    }
    u32 channels = color_type == 6 ? 4 : color_type == 2 ? 3 : color_type == 0 ? 1 : color_type == 4 ? 2 : 0;
    if (!channels)
        fail("a color type of this PNG is not read:", path);
    umm stride = (umm)width * channels;
    u8* raw = push((stride + 1) * height);
    inflate(compressed, compressed_size, raw, (stride + 1) * height);

    // Undo the filters, a row at a time, in place.
    for (u32 y = 0; y < height; ++y) {
        u8* row = raw + (stride + 1) * y;
        u8 filter = row[0];
        u8* current = row + 1;
        const u8* above = y ? raw + (stride + 1) * (y - 1) + 1 : NULL;
        for (umm x = 0; x < stride; ++x) {
            int a = x >= channels ? current[x - channels] : 0;
            int b = above ? above[x] : 0;
            int c = (above && x >= channels) ? above[x - channels] : 0;
            int predictor = 0;
            switch (filter) {
            case 1: predictor = a; break;
            case 2: predictor = b; break;
            case 3: predictor = (a + b) / 2; break;
            case 4: {
                int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                predictor = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            default: break;
            }
            current[x] = (u8)(current[x] + predictor);
        }
    }

    Image image = {.width = width, .height = height, .rgba = push((umm)width * height * 4)};
    for (u32 y = 0; y < height; ++y) {
        const u8* in = raw + (stride + 1) * y + 1;
        u8* out = image.rgba + (umm)y * width * 4;
        for (u32 x = 0; x < width; ++x, in += channels, out += 4) {
            if (channels >= 3) {
                out[0] = in[0], out[1] = in[1], out[2] = in[2];
                out[3] = channels == 4 ? in[3] : 255;
            } else {
                out[0] = out[1] = out[2] = in[0];
                out[3] = channels == 2 ? in[1] : 255;
            }
        }
    }
    return image;
}

internal u32 crc32_update(u32 crc, const u8* data, umm size)
{
    local_persist u32 table[256];
    if (!table[1]) {
        for (u32 n = 0; n < 256; ++n) {
            u32 c = n;
            for (u32 k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
    }
    crc = ~crc;
    for (umm i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

internal void write_chunk(FILE* file, const char* type, const u8* data, u32 length)
{
    u8 header[8] = {(u8)(length >> 24), (u8)(length >> 16), (u8)(length >> 8), (u8)length, (u8)type[0], (u8)type[1], (u8)type[2], (u8)type[3]};
    fwrite(header, 1, 8, file);
    if (length)
        fwrite(data, 1, length, file);
    u32 crc = crc32_update(0, header + 4, 4);
    crc = crc32_update(crc, data, length);
    u8 tail[4] = {(u8)(crc >> 24), (u8)(crc >> 16), (u8)(crc >> 8), (u8)crc};
    fwrite(tail, 1, 4, file);
}

// An RGB picture as a PNG whose data is stored, not compressed: the differences are for looking at, not for keeping small.
internal void write_png(const char* path, u32 width, u32 height, const u8* rgb)
{
    umm stride = (umm)width * 3;
    umm raw_size = (stride + 1) * height;
    u8* raw = push(raw_size);
    for (u32 y = 0; y < height; ++y)
        memcpy(raw + (stride + 1) * y + 1, rgb + stride * y, stride); // filter 0 (the byte before) is already 0
    umm blocks = (raw_size + 65534) / 65535;
    u8* z = push(raw_size + blocks * 5 + 6);
    umm n = 0;
    z[n++] = 0x78, z[n++] = 0x01;
    u32 a = 1, b = 0;
    for (umm done = 0; done < raw_size;) {
        u32 length = (u32)(raw_size - done < 65535 ? raw_size - done : 65535);
        z[n++] = done + length == raw_size;
        z[n++] = (u8)length, z[n++] = (u8)(length >> 8), z[n++] = (u8)~length, z[n++] = (u8)(~length >> 8);
        memcpy(z + n, raw + done, length);
        for (u32 i = 0; i < length; ++i) {
            a = (a + raw[done + i]) % 65521;
            b = (b + a) % 65521;
        }
        n += length;
        done += length;
    }
    u32 adler = (b << 16) | a;
    z[n++] = (u8)(adler >> 24), z[n++] = (u8)(adler >> 16), z[n++] = (u8)(adler >> 8), z[n++] = (u8)adler;

    FILE* file = fopen(path, "wb");
    if (!file)
        fail("cannot write", path);
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, file);
    u8 ihdr[13] = {(u8)(width >> 24), (u8)(width >> 16), (u8)(width >> 8), (u8)width, (u8)(height >> 24), (u8)(height >> 16), (u8)(height >> 8),
                   (u8)height, 8, 2, 0, 0, 0};
    write_chunk(file, "IHDR", ihdr, 13);
    write_chunk(file, "IDAT", z, (u32)n);
    write_chunk(file, "IEND", NULL, 0);
    fclose(file);
}

//
// JSON: numbers, strings, arrays and objects, which is what the sidecars and the dumps hold
//

typedef enum JsonKind { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } JsonKind;

typedef struct Json Json;
struct Json {
    JsonKind kind;
    f64 number; // a number, or a bool as 0 or 1
    const char* string;
    u32 count;      // elements of an array, or members of an object
    Json* items;    // an array's elements, or an object's values
    const char** keys; // an object's keys
};

typedef struct JsonReader {
    const char* p;
    const char* path;
} JsonReader;

internal void skip_space(JsonReader* r)
{
    while (*r->p == ' ' || *r->p == '\n' || *r->p == '\r' || *r->p == '\t')
        ++r->p;
}

internal const char* parse_string(JsonReader* r)
{
    ++r->p; // the opening quote
    const char* start = r->p;
    while (*r->p != '"') {
        if (!*r->p)
            fail("a JSON string does not end:", r->path);
        if (*r->p == '\\')
            ++r->p;
        ++r->p;
    }
    umm length = (umm)(r->p - start);
    char* copy = push(length + 1);
    memcpy(copy, start, length); // no escapes occur in the files this reads
    ++r->p;
    return copy;
}

internal Json parse_value(JsonReader* r);

internal Json parse_container(JsonReader* r, b32 is_object)
{
    // The members are read into a growing list on the arena, then moved to place: the list ends where the next push begins.
    Json result = {.kind = is_object ? JSON_OBJECT : JSON_ARRAY};
    char close = is_object ? '}' : ']';
    ++r->p;
    skip_space(r);
    u32 capacity = 0;
    Json* items = NULL;
    const char** keys = NULL;
    while (*r->p != close) {
        if (result.count == capacity) {
            u32 new_capacity = capacity ? capacity * 2 : 8;
            Json* new_items = push((umm)new_capacity * sizeof(Json));
            if (items)
                memcpy(new_items, items, (umm)result.count * sizeof(Json));
            items = new_items;
            if (is_object) {
                const char** new_keys = push((umm)new_capacity * sizeof(char*));
                if (keys)
                    memcpy(new_keys, keys, (umm)result.count * sizeof(char*));
                keys = new_keys;
            }
            capacity = new_capacity;
        }
        if (is_object) {
            skip_space(r);
            keys[result.count] = parse_string(r);
            skip_space(r);
            if (*r->p++ != ':')
                fail("a JSON object has no colon:", r->path);
        }
        skip_space(r);
        items[result.count++] = parse_value(r);
        skip_space(r);
        if (*r->p == ',')
            ++r->p;
        skip_space(r);
    }
    ++r->p;
    result.items = items;
    result.keys = keys;
    return result;
}

internal Json parse_value(JsonReader* r)
{
    skip_space(r);
    Json value = {0};
    char c = *r->p;
    if (c == '{') return parse_container(r, 1);
    if (c == '[') return parse_container(r, 0);
    if (c == '"') {
        value.kind = JSON_STRING;
        value.string = parse_string(r);
        return value;
    }
    if (strncmp(r->p, "true", 4) == 0) { r->p += 4; value.kind = JSON_BOOL; value.number = 1; return value; }
    if (strncmp(r->p, "false", 5) == 0) { r->p += 5; value.kind = JSON_BOOL; return value; }
    if (strncmp(r->p, "null", 4) == 0) { r->p += 4; return value; }
    char* end;
    value.kind = JSON_NUMBER;
    value.number = strtod(r->p, &end);
    if (end == r->p)
        fail("bad JSON in", r->path);
    r->p = end;
    return value;
}

internal b32 read_json(const char* path, Json* out)
{
    u8* bytes = read_file(path, NULL);
    if (!bytes)
        return 0;
    JsonReader reader = {.p = (const char*)bytes, .path = path};
    *out = parse_value(&reader);
    return 1;
}

internal const Json* member(const Json* object, const char* key)
{
    if (object->kind != JSON_OBJECT)
        return NULL;
    for (u32 i = 0; i < object->count; ++i) {
        if (strcmp(object->keys[i], key) == 0)
            return &object->items[i];
    }
    return NULL;
}

internal b32 json_equal(const Json* a, const Json* b)
{
    if (a->kind != b->kind)
        return 0;
    switch (a->kind) {
    case JSON_NULL: return 1;
    case JSON_BOOL:
    case JSON_NUMBER: return a->number == b->number;
    case JSON_STRING: return strcmp(a->string, b->string) == 0;
    case JSON_ARRAY:
        if (a->count != b->count)
            return 0;
        for (u32 i = 0; i < a->count; ++i) {
            if (!json_equal(&a->items[i], &b->items[i]))
                return 0;
        }
        return 1;
    case JSON_OBJECT:
        if (a->count != b->count)
            return 0;
        for (u32 i = 0; i < a->count; ++i) {
            const Json* other = member(b, a->keys[i]);
            if (!other || !json_equal(&a->items[i], other))
                return 0;
        }
        return 1;
    }
    return 0;
}

//
// What is compared
//

typedef struct Options {
    b32 exact, blocks, present;
    f64 block_tolerance; // --blocks: the largest difference between block averages, in 1/255, that most blocks keep to
    f64 over_fraction;   // --blocks: the share of the blocks that may be over it
    f64 block_largest;   // --blocks: what no block may be over
    const char* diff_directory;
} Options;

global Options options = {.block_tolerance = 4.0, .over_fraction = 0.03, .block_largest = 40.0};
global u32 checked, failed;


typedef struct Report {
    u32 count;
    char text[6][400];
} Report;

internal void problem(Report* report, const char* format, ...)
{
    if (report->count < 6) {
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(report->text[report->count], sizeof(report->text[0]), format, arguments);
        va_end(arguments);
    }
    ++report->count;
}

internal void finish(const char* name, const Report* report, const char* note)
{
    ++checked;
    if (report->count) {
        ++failed;
        printf("FAIL %s\n", name);
        for (u32 i = 0; i < report->count && i < 6; ++i)
            printf("     %s\n", report->text[i]);
    } else {
        printf("ok   %s%s%s\n", name, note[0] ? "  " : "", note);
    }
}

//
// Screens
//

typedef struct ImageDifference {
    u32 counted, over;
    u32 max;         // the largest channel difference of any pixel outside the masks
    f64 block_max;   // the largest difference between the averages of two blocks, in 1/255
    u32 blocks_over; // blocks whose averages differ by more than the block tolerance
    u32 worst_x, worst_y; // where the block of block_max starts
    u32 blocks;
} ImageDifference;

internal ImageDifference compare_images(const Image* a, const Image* b, const Json* masks, u32 channel_tolerance, u32 block_size,
                                        u32 block_minimum, f64 block_tolerance, const char* diff_path)
{
    u32 width = a->width, height = a->height;
    u8* masked = push((umm)width * height);
    for (u32 m = 0; masks && m < masks->count; ++m) {
        const Json* rect = &masks->items[m];
        u32 x0 = (u32)rect->items[0].number, y0 = (u32)rect->items[1].number;
        u32 x1 = x0 + (u32)rect->items[2].number, y1 = y0 + (u32)rect->items[3].number;
        for (u32 y = y0; y < y1 && y < height; ++y) {
            for (u32 x = x0; x < x1 && x < width; ++x)
                masked[(umm)y * width + x] = 1;
        }
    }
    ImageDifference result = {0};
    for (umm k = 0; k < (umm)width * height; ++k) {
        if (masked[k])
            continue;
        ++result.counted;
        u32 d = 0;
        for (u32 c = 0; c < 3; ++c) {
            u32 p = a->rgba[k * 4 + c], q = b->rgba[k * 4 + c];
            u32 diff = p > q ? p - q : q - p;
            if (diff > d)
                d = diff;
        }
        if (d > result.max)
            result.max = d;
        if (d > channel_tolerance)
            ++result.over;
    }

    // The averages of block_size x block_size blocks: what is left when single pixels move but areas do not.
    u8* diff_picture = diff_path ? push((umm)width * height * 3) : NULL;
    for (u32 by = 0; by + block_size <= height; by += block_size) {
        for (u32 bx = 0; bx + block_size <= width; bx += block_size) {
            f64 sum_a[3] = {0}, sum_b[3] = {0};
            u32 n = 0;
            for (u32 y = by; y < by + block_size; ++y) {
                for (u32 x = bx; x < bx + block_size; ++x) {
                    umm k = (umm)y * width + x;
                    if (masked[k])
                        continue;
                    ++n;
                    for (u32 c = 0; c < 3; ++c) {
                        sum_a[c] += a->rgba[k * 4 + c];
                        sum_b[c] += b->rgba[k * 4 + c];
                    }
                }
            }
            if (n < block_minimum)
                continue;
            f64 block_difference = 0.0;
            for (u32 c = 0; c < 3; ++c) {
                f64 d = fabs(sum_a[c] - sum_b[c]) / n;
                if (d > block_difference)
                    block_difference = d;
            }
            ++result.blocks;
            if (block_difference > result.block_max) {
                result.block_max = block_difference;
                result.worst_x = bx;
                result.worst_y = by;
            }
            b32 over = block_difference > block_tolerance;
            result.blocks_over += over;
            if (diff_picture) {
                u32 level = (u32)(block_difference * 16.0);
                if (level > 255)
                    level = 255;
                for (u32 y = by; y < by + block_size; ++y) {
                    for (u32 x = bx; x < bx + block_size; ++x) {
                        u8* out = diff_picture + ((umm)y * width + x) * 3;
                        out[0] = over ? 255 : (u8)level;
                        out[1] = over ? 0 : (u8)level;
                        out[2] = over ? 0 : (u8)level;
                    }
                }
            }
        }
    }
    if (diff_picture)
        write_png(diff_path, width, height, diff_picture);
    return result;
}

//
// Effects
//

global const char* const field_names[12] = {"emitter", "seed", "x", "y", "z", "age", "vx", "vy", "vz", "life", "scale", "rotation"};

// Whether the particle value `field` (2 to 11: position, age, velocity, life, scale, rotation) of two builds is the same enough.
// Between two builds of one renderer the error is relative (1e-4; 1e-3, a half float's ulp, for what the shader keeps as halves).
// With --blocks, another GPU: sin, cos and exp differ in the last bits and flip the rounding of the half floats the particles keep
// their velocity, life, scale and rotation in, one ulp a step, so the error is absolute and as large as a few steps of a half
// float at the sizes the values have (the largest differences measured over every case: position 8 mm, velocity 0.008).
internal b32 close_enough(f64 a, f64 b, u32 field)
{
    if (options.exact)
        return a == b;
    if (options.blocks) {
        local_persist const f64 absolute[12] = {0, 0, 0.02, 0.02, 0.02, 1e-4, 0.02, 0.02, 0.02, 0.005, 0.005, 0.02};
        return fabs(a - b) <= absolute[field];
    }
    b32 half = field >= 6;
    f64 scale = fmax(fabs(a), fabs(b));
    return fabs(a - b) <= (half ? 1e-3 : 1e-4) * scale + (half ? 6e-8 : 1e-6);
}

// A short text of a value, for a message: numbers and arrays of them, cut where `capacity` ends.
internal void json_text(const Json* value, char* out, umm capacity)
{
    umm used = 0;
    if (value->kind == JSON_NUMBER) {
        snprintf(out, capacity, "%.9g", value->number);
        return;
    }
    if (value->kind != JSON_ARRAY) {
        snprintf(out, capacity, "...");
        return;
    }
    used += (umm)snprintf(out + used, capacity - used, "[");
    for (u32 i = 0; i < value->count && used + 24 < capacity; ++i)
        used += (umm)snprintf(out + used, capacity - used, "%s%.9g", i ? "," : "", value->items[i].number);
    snprintf(out + used, capacity - used, "]");
}

// What the culling counted may differ between two GPUs: a particle is visible when its sphere touches the frustum, and its position comes
// from the GPU's own sin, cos and exp (the turbulence and the drag), so a few of the many particles near a plane fall on the other side.
// Another renderer's run (--blocks) may count up to 3% (and 8) more or fewer visible particles; everything else is exact.
internal b32 visible_count_slot(const char* key, u32 index)
{
    if (strcmp(key, "counters") == 0)
        return index == 3 || index == 4 || index == 9;
    if (strcmp(key, "drawArgs") == 0)
        return index == 0 || index == 4;
    return 0;
}

internal b32 effects_array_matches(const char* key, const Json* p, const Json* q)
{
    if (json_equal(p, q))
        return 1;
    if (!options.blocks || p->kind != JSON_ARRAY || q->kind != JSON_ARRAY || p->count != q->count)
        return 0;
    for (u32 i = 0; i < p->count; ++i) {
        f64 a = p->items[i].number, b = q->items[i].number;
        if (a == b)
            continue;
        // A draw argument is six vertices a particle.
        f64 unit = strcmp(key, "drawArgs") == 0 ? 6.0 : 1.0;
        if (!visible_count_slot(key, i) || fabs(a - b) / unit > fmax(8.0, 0.03 * fmax(a, b) / unit))
            return 0;
    }
    return 1;
}

internal void compare_effects(const Json* a, const Json* b, Report* report)
{
    const Json* frames_a = member(a, "frames");
    const Json* frames_b = member(b, "frames");
    if (!frames_a || !frames_b) {
        problem(report, "no frames");
        return;
    }
    // The frames of both, the first one's in its order.
    const char* names[256];
    u32 name_count = 0;
    for (u32 i = 0; i < frames_a->count && name_count < 256; ++i)
        names[name_count++] = frames_a->keys[i];
    for (u32 i = 0; i < frames_b->count && name_count < 256; ++i) {
        b32 known = 0;
        for (u32 k = 0; k < name_count; ++k)
            known = known || strcmp(names[k], frames_b->keys[i]) == 0;
        if (!known)
            names[name_count++] = frames_b->keys[i];
    }
    local_persist const char* const exact_keys[] = {"capacity", "counters", "drawArgs", "segments", "decals"};
    for (u32 n = 0; n < name_count; ++n) {
        const Json* x = member(frames_a, names[n]);
        const Json* y = member(frames_b, names[n]);
        if (!x || !y) {
            problem(report, "frame %s: only in one", names[n]);
            continue;
        }
        const Json* violations = member(y, "violations");
        if (violations && violations->number != 0)
            problem(report, "frame %s: invariants violated (%g)", names[n], violations->number);
        for (u32 k = 0; k < NV_ARRAY_COUNT(exact_keys); ++k) {
            const Json* p = member(x, exact_keys[k]);
            const Json* q = member(y, exact_keys[k]);
            if (!p || !q || !effects_array_matches(exact_keys[k], p, q)) {
                char text_p[200] = "", text_q[200] = "";
                if (p) json_text(p, text_p, sizeof(text_p));
                if (q) json_text(q, text_q, sizeof(text_q));
                problem(report, "frame %s: %s %s against %s", names[n], exact_keys[k], text_p, text_q);
            }
        }
        // simulate's workgroup count is made from the alive count at the start of the frame that wrote it, and the frames after it write it
        // again: where the count crosses a multiple of 64, a dump a frame earlier or later reads the other value, and a Play that is still
        // running (V12, whose count changes by thousands a frame with the real time) reads one of many. So the count may be one
        // frame's away: up to 5% (and 1) apart, with the other two words equal.
        const Json* sim_a = member(x, "simulateArgs");
        const Json* sim_b = member(y, "simulateArgs");
        if (!sim_a || !sim_b) {
            problem(report, "frame %s: simulateArgs only in one", names[n]);
        } else if (!json_equal(sim_a, sim_b)) {
            b32 same_rest = sim_a->count == sim_b->count && sim_a->count >= 3;
            for (u32 i = 1; same_rest && i < sim_a->count; ++i)
                same_rest = sim_a->items[i].number == sim_b->items[i].number;
            f64 first_a = sim_a->count ? sim_a->items[0].number : 0.0, first_b = sim_b->count ? sim_b->items[0].number : 0.0;
            b32 timing = same_rest && !options.exact && fabs(first_a - first_b) <= fmax(1.0, 0.05 * fmax(first_a, first_b));
            if (!timing) {
                char text_p[200] = "", text_q[200] = "";
                json_text(sim_a, text_p, sizeof(text_p));
                json_text(sim_b, text_q, sizeof(text_q));
                problem(report, "frame %s: simulateArgs %s against %s", names[n], text_p, text_q);
            }
        }
        if (options.exact && !json_equal(x, y))
            problem(report, "frame %s: the dump differs", names[n]);
        const Json* particles_a = member(x, "particles");
        const Json* particles_b = member(y, "particles");
        if (!particles_a || !particles_b)
            continue;
        if (particles_a->count != particles_b->count) {
            problem(report, "frame %s: %u against %u particles", names[n], particles_a->count, particles_b->count);
            continue;
        }
        u32 bad = 0;
        f64 largest_relative = 0.0;
        f64 position_sum = 0.0, position_largest = 0.0, velocity_largest = 0.0;
        u32 position_count = 0;
        char example[160] = "";
        for (u32 i = 0; i < particles_a->count; ++i) {
            const Json* row_a = &particles_a->items[i];
            const Json* row_b = &particles_b->items[i];
            for (u32 f = 0; f < 12 && f < row_a->count && f < row_b->count; ++f) {
                f64 p = row_a->items[f].number, q = row_b->items[f].number;
                b32 ok = f < 2 ? p == q : close_enough(p, q, f);
                if (f >= 2 && f <= 4) {
                    f64 d = fabs(p - q);
                    position_sum += d;
                    ++position_count;
                    if (d > position_largest)
                        position_largest = d;
                }
                if (f >= 6 && f <= 8 && fabs(p - q) > velocity_largest)
                    velocity_largest = fabs(p - q);
                if (f >= 2 && fmax(fabs(p), fabs(q)) > 0.0 && fabs(p - q) / fmax(fabs(p), fabs(q)) > largest_relative)
                    largest_relative = fabs(p - q) / fmax(fabs(p), fabs(q));
                if (!ok && !bad++)
                    snprintf(example, sizeof(example), "particle %u %s %.9g against %.9g", i, field_names[f], p, q);
            }
        }
        if (bad)
            problem(report, "frame %s: %u value(s) differ (largest relative %.3g; position error mean %.3g largest %.3g m, velocity largest %.3g), first: %s", names[n], bad,
                    largest_relative, position_count ? position_sum / position_count : 0.0, position_largest, velocity_largest, example);
    }
}

//
// The directories
//

internal b32 ends_with(const char* text, const char* suffix)
{
    umm length = strlen(text), tail = strlen(suffix);
    return length >= tail && strcmp(text + length - tail, suffix) == 0;
}

// The name of an effects case: V<digits>..., a .json.
internal b32 is_effects_name(const char* name)
{
    return name[0] == 'V' && name[1] >= '0' && name[1] <= '9' && ends_with(name, ".json");
}

internal b32 exists(const char* directory, const char* name)
{
    char path[640];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE* file = fopen(path, "rb");
    if (file)
        fclose(file);
    return file != NULL;
}

internal void path_in(char* out, umm size, const char* directory, const char* name)
{
    snprintf(out, size, "%s/%s", directory, name);
}

// The block averages the effects' screens are held to, by case: unsorted translucent particles blend in another order from run to
// run of one build, and from one renderer to another.
internal f64 effects_block_tolerance(const char* name)
{
    struct { const char* prefix; f64 tolerance; } table[] = {{"V1-", 3}, {"V2-", 40}, {"V3-", 3}, {"V4-", 200}, {"V11-", 30}};
    for (u32 i = 0; i < NV_ARRAY_COUNT(table); ++i) {
        if (strncmp(name, table[i].prefix, strlen(table[i].prefix)) == 0)
            return table[i].tolerance;
    }
    return 20.0;
}

internal void compare_screen(const char* baseline, const char* current, const char* name)
{
    Report report = {0};
    char path_a[640], path_b[640], sidecar_name[NAME_SIZE], sidecar[640];
    snprintf(sidecar_name, sizeof(sidecar_name), "%.*s.json", (int)(strlen(name) - 4), name);
    path_in(sidecar, sizeof(sidecar), baseline, sidecar_name);
    Json sidecar_json = {0};
    if (!read_json(sidecar, &sidecar_json))
        fail("no sidecar", sidecar);
    const Json* masks = member(&sidecar_json, "masks");

    path_in(path_a, sizeof(path_a), baseline, name);
    path_in(path_b, sizeof(path_b), current, name);
    Image a = read_png(path_a), b = read_png(path_b);
    if (a.width != b.width || a.height != b.height) {
        problem(&report, "size %ux%u against %ux%u", a.width, a.height, b.width, b.height);
        finish(name, &report, "");
        return;
    }

    char note[200];
    if (options.blocks) {
        char diff_path[640] = "";
        if (options.diff_directory) {
            char diff_name[NAME_SIZE];
            snprintf(diff_name, sizeof(diff_name), "%.*s.diff.png", (int)(strlen(name) - 4), name);
            path_in(diff_path, sizeof(diff_path), options.diff_directory, diff_name);
        }
        ImageDifference d = compare_images(&a, &b, masks, 0, 8, 32, options.block_tolerance, diff_path[0] ? diff_path : NULL);
        // The scene drawn at a lower resolution (S7, S8) and shown with the nearest filter makes every line two to six screen pixels
        // wide, and the same line of the two rasterizers differs that much more.
        b32 low_resolution = strncmp(name, "S7-", 3) == 0 || strncmp(name, "S8-", 3) == 0;
        // The effects' screens (V*) draw translucent particles unsorted, on purpose: their order is the order the GPU's threads put them in
        // the alive list, so the same smoke has another color on top on another GPU, and a cloud of it moves many blocks by a lot.
        b32 effects_screen = name[0] == 'V' && name[1] >= '0' && name[1] <= '9';
        f64 over_fraction = options.over_fraction * (low_resolution ? 2.0 : effects_screen ? 5.0 : 1.0);
        f64 largest = options.block_largest * (low_resolution ? 1.75 : effects_screen ? 2.75 : 1.0);
        if (d.blocks_over > over_fraction * d.blocks || d.block_max > largest)
            problem(&report, "%u of %u blocks of 8x8 differ by more than %g/255 in their averages (largest %.2f; %.1f%% of the blocks and up to %g/255 are allowed)",
                    d.blocks_over, d.blocks, options.block_tolerance, d.block_max, over_fraction * 100.0, largest);
        snprintf(note, sizeof(note), "largest block %.2f/255 at (%u, %u), %u of %u blocks over %g, pixels up to %u/255", d.block_max, d.worst_x,
                 d.worst_y, d.blocks_over, d.blocks, options.block_tolerance, d.max);
        finish(name, &report, note);
        return;
    }

    u32 channel_tolerance = options.exact ? 0 : 2;
    ImageDifference d = compare_images(&a, &b, masks, channel_tolerance, 16, 128, 0.0, NULL);
    f64 fraction = (f64)d.over / (d.counted ? d.counted : 1);
    // The effects' screens hold unsorted translucent particles: their pixels may differ run to run, so they are held to the block
    // averages instead.
    b32 loose = name[0] == 'V' && name[1] >= '0' && name[1] <= '9';
    f64 tolerance = effects_block_tolerance(name);
    f64 allowed = (options.exact && strncmp(name, "S10", 3) == 0) ? 0.0001 : (options.exact ? 0.0 : 0.001);
    if (loose ? d.block_max > tolerance : fraction > allowed) {
        if (loose)
            problem(&report, "16x16 block averages differ by up to %.2f/255 (more than %g)", d.block_max, tolerance);
        else
            problem(&report, "%.3f%% of the pixels differ by more than %u/255 (largest %u)", fraction * 100.0, channel_tolerance, d.max);
    }
    snprintf(note, sizeof(note), "largest %u/255, %.4f%% over, blocks %.2f", d.max, fraction * 100.0, d.block_max);
    finish(name, &report, note);
}

internal void compare_effects_file(const char* baseline, const char* current, const char* name)
{
    Report report = {0};
    char path_a[640], path_b[640];
    path_in(path_a, sizeof(path_a), baseline, name);
    path_in(path_b, sizeof(path_b), current, name);
    Json a, b;
    if (!read_json(path_b, &b)) {
        problem(&report, "missing from the current run");
        finish(name, &report, "");
        return;
    }
    if (!read_json(path_a, &a))
        fail("cannot read", path_a);
    compare_effects(&a, &b, &report);
    const Json* frames = member(&a, "frames");
    char note[64];
    snprintf(note, sizeof(note), "%u frame(s)", frames ? frames->count : 0);
    finish(name, &report, note);
}

int main(int argc, char** argv)
{
    const char* directories[2] = {NULL, NULL};
    u32 directory_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--exact") == 0)
            options.exact = 1;
        else if (strcmp(argv[i], "--blocks") == 0)
            options.blocks = 1;
        else if (strcmp(argv[i], "--present") == 0)
            options.present = 1;
        else if (strcmp(argv[i], "--diff") == 0 && i + 1 < argc)
            options.diff_directory = argv[++i];
        else if (strcmp(argv[i], "--tolerance") == 0 && i + 1 < argc)
            options.block_tolerance = atof(argv[++i]);
        else if (strcmp(argv[i], "--over") == 0 && i + 1 < argc)
            options.over_fraction = atof(argv[++i]);
        else if (strcmp(argv[i], "--largest") == 0 && i + 1 < argc)
            options.block_largest = atof(argv[++i]);
        else if (argv[i][0] != '-' && directory_count < 2)
            directories[directory_count++] = argv[i];
        else {
            directory_count = 0;
            break;
        }
    }
    if (directory_count != 2) {
        fprintf(stderr, "usage: compare [--exact | --blocks [--tolerance n] [--over share] [--largest n] [--diff dir]] [--present] <baseline dir> <current dir>\n");
        return 2;
    }
    umm arena_size = NV_MEGABYTES(1024);
    void* block = malloc(arena_size);
    if (!block)
        fail("no memory", NULL);
    nv_arena_init(&arena, block, arena_size);

    local_persist char names[MAX_NAMES][NAME_SIZE];
    u32 count = list_directory(directories[0], names);
    for (u32 i = 0; i < count; ++i) {
        const char* name = names[i];
        umm mark = arena.used; // each file's memory is dropped when it is done
        if (options.present && !exists(directories[1], name))
            continue;
        if (ends_with(name, ".png")) {
            if (!exists(directories[1], name)) {
                Report report = {0};
                problem(&report, "missing from the current run");
                finish(name, &report, "");
            } else {
                compare_screen(directories[0], directories[1], name);
            }
        } else if (options.exact && strcmp(name, "V12-250k.json") == 0) {
            printf("skip %s  does not repeat on the baseline itself\n", name);
        } else if (is_effects_name(name)) {
            char screen[NAME_SIZE];
            snprintf(screen, sizeof(screen), "%.*s.png", (int)(strlen(name) - 5), name);
            if (!exists(directories[0], screen))
                compare_effects_file(directories[0], directories[1], name);
        }
        arena.used = mark;
    }
    printf("%u of %u match\n", checked - failed, checked);
    return failed ? 1 : 0;
}
