#pragma once

#include <stddef.h>
#include <stdint.h>

// NOTE: Fixed-width aliases so sizes are always explicit.
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef float    f32;
typedef double   f64;
typedef s32      b32;
typedef size_t   umm;

// NOTE: `static` means three different things; name each use so it can be searched for.
#define internal      static
#define global        static
#define local_persist static

#if !defined(NDEBUG)
#    define NV_ASSERT(expr) do { if (!(expr)) __builtin_trap(); } while (0)
#else
#    define NV_ASSERT(expr) ((void)0)
#endif
#define NV_INVALID_CODE_PATH NV_ASSERT(!"invalid code path")

#define NV_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define NV_KILOBYTES(n) ((umm)(n) * 1024)
#define NV_MEGABYTES(n) (NV_KILOBYTES(n) * 1024)

// NOTE: Linear allocator over a block reserved once up front. Nothing is freed individually;
// memory with a shared lifetime lives in one arena and is dropped all at once with a reset.
typedef struct NvArena {
    u8* base;
    umm size;
    umm used;
} NvArena;

static inline void nv_arena_init(NvArena* arena, void* base, umm size)
{
    arena->base = (u8*)base;
    arena->size = size;
    arena->used = 0;
}

// NOTE: Returns zeroed memory, so pushed structs start in their valid zero state.
static inline void* nv_arena_push(NvArena* arena, umm size, umm align)
{
    umm start = (arena->used + (align - 1)) & ~(align - 1);
    NV_ASSERT(start + size <= arena->size);
    u8* result = arena->base + start;
    arena->used = start + size;
    for (umm i = 0; i < size; ++i)
        result[i] = 0;
    return result;
}

static inline void nv_arena_reset(NvArena* arena)
{
    arena->used = 0;
}

#define NV_PUSH_STRUCT(arena, type)       ((type*)nv_arena_push((arena), sizeof(type), _Alignof(type)))
#define NV_PUSH_ARRAY(arena, count, type) ((type*)nv_arena_push((arena), (count) * sizeof(type), _Alignof(type)))

// FNV-1a over `size` bytes, continuing from `hash`: nv_fnv1a(NV_FNV1A_SEED, bytes, size) hashes one run, and passing a
// result back in hashes several runs as one.
#define NV_FNV1A_SEED  2166136261u
#define NV_FNV1A_PRIME 16777619u
static inline u32 nv_fnv1a(u32 hash, const void* bytes, umm size)
{
    const u8* p = (const u8*)bytes;
    for (umm i = 0; i < size; ++i)
        hash = (hash ^ p[i]) * NV_FNV1A_PRIME;
    return hash;
}

// How many of the first `limit` bytes of the UTF-8 `text` to keep so no character is cut in the
// middle: a name that does not fit is shortened by whole characters (a Hangul syllable is 3 bytes).
static inline u32 nv_utf8_fit(const char* text, u32 limit)
{
    u32 length = 0;
    while (length < limit && text[length])
        ++length;
    if (!text[length])
        return length;
    // `length` == limit and more follows: back up while the next byte continues a character.
    while (length > 0 && ((u8)text[length] & 0xC0u) == 0x80u)
        --length;
    return length;
}

// How many bytes the UTF-8 character that starts with `lead` has (1 for a byte that starts none).
static inline u32 nv_utf8_length(u8 lead)
{
    return lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
}

// Drops a character cut off at the end of `text` (what snprintf leaves when a name did not fit), so
// the string stays valid UTF-8.
static inline void nv_utf8_trim(char* text)
{
    u32 length = 0;
    while (text[length])
        ++length;
    u32 start = length;
    while (start > 0 && ((u8)text[start - 1] & 0xC0u) == 0x80u)
        --start;
    if (start == 0) {
        if (length) // nothing but continuation bytes
            text[0] = 0;
        return;
    }
    if (length - (start - 1) < nv_utf8_length((u8)text[start - 1]))
        text[start - 1] = 0;
}
