#include "engine/strings.h"

#include <emscripten.h>
#include <stdio.h>
#include <string.h>

// The table is looked up through a hash table over the English texts (FNV-1a, linear probing) made when the table is set.
#define TABLE_SIZE 8192 // a power of two, more than twice NV_STRINGS_MAX
#define TL_SLOTS 64
#define TL_SIZE 192

global NvLanguage language = NV_LANGUAGE_EN;
global const NvStringPair* entries;
global u16 table[TABLE_SIZE]; // entry index + 1; 0 = empty

void nv_strings_set_table(const NvStringPair* pairs, u32 count)
{
    NV_ASSERT(count <= NV_STRINGS_MAX);
    entries = pairs;
    memset(table, 0, sizeof(table));
    for (u32 i = 0; i < count; ++i) {
        u32 slot = nv_fnv1a(NV_FNV1A_SEED, pairs[i].english, strlen(pairs[i].english)) & (TABLE_SIZE - 1);
        while (table[slot])
            slot = (slot + 1) & (TABLE_SIZE - 1);
        table[slot] = (u16)(i + 1);
    }
}

EM_JS(int, js_browser_is_korean, (void), {
    const language = (navigator.language || "").toLowerCase();
    return language.startsWith("ko") ? 1 : 0;
});

NvLanguage nv_strings_language(void) { return language; }
void nv_strings_set_language(NvLanguage value) { language = value < NV_LANGUAGE_COUNT ? value : NV_LANGUAGE_EN; }
NvLanguage nv_strings_browser_language(void) { return js_browser_is_korean() ? NV_LANGUAGE_KO : NV_LANGUAGE_EN; }

// The entry for `length` bytes of English, or NULL.
internal const NvStringPair* find(const char* english, umm length)
{
    u32 slot = nv_fnv1a(NV_FNV1A_SEED, english, length) & (TABLE_SIZE - 1);
    while (table[slot]) {
        const NvStringPair* entry = &entries[table[slot] - 1];
        if (strlen(entry->english) == length && memcmp(entry->english, english, length) == 0)
            return entry;
        slot = (slot + 1) & (TABLE_SIZE - 1);
    }
    return NULL;
}

const char* nv_strings_find_korean(const char* english)
{
    const NvStringPair* entry = find(english, strlen(english));
    return entry ? entry->korean : NULL;
}

const char* nv_strings_text(const char* english)
{
    if (language != NV_LANGUAGE_KO)
        return english;
    const NvStringPair* entry = find(english, strlen(english));
    return entry ? entry->korean : english;
}

const char* nv_strings_label(const char* english)
{
    local_persist char slots[TL_SLOTS][TL_SIZE];
    local_persist u32 next;
    char* out = slots[next++ % TL_SLOTS];
    // The text is what comes before any "##"; the id is what follows "###", or all of it.
    const char* mark = strstr(english, "##");
    umm text_length = mark ? (umm)(mark - english) : strlen(english);
    const char* id = english;
    const char* triple = strstr(english, "###");
    if (triple)
        id = triple + 3;
    const char* shown = english;
    if (language == NV_LANGUAGE_KO) {
        const NvStringPair* entry = find(english, text_length);
        if (entry) {
            snprintf(out, TL_SIZE, "%s###%s", entry->korean, id);
            nv_utf8_trim(out);
            return out;
        }
    }
    snprintf(out, TL_SIZE, "%.*s###%s", (int)text_length, shown, id);
    nv_utf8_trim(out);
    return out;
}
