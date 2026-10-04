#pragma once

// The UI's languages (docs/specs/korean.md). Strings are written in English in the code, wrapped in T() (text) or TL() (a
// widget label); each executable gives the engine its table of English and Korean pairs once, at start.

#include "engine/base.h"

typedef enum NvLanguage {
    NV_LANGUAGE_EN,
    NV_LANGUAGE_KO,
    NV_LANGUAGE_COUNT,
} NvLanguage;

// One row of an executable's table: the English text exactly as written in the code, and its Korean.
typedef struct NvStringPair {
    const char* english;
    const char* korean;
} NvStringPair;

#define NV_STRINGS_MAX 4096 // rows in an executable's table

// The executable's table, kept (not copied), so it must outlive the program: a static array. Call once at start, before the
// first T().
void nv_strings_set_table(const NvStringPair* pairs, u32 count);

NvLanguage nv_strings_language(void);
void nv_strings_set_language(NvLanguage language);
NvLanguage nv_strings_browser_language(void); // from navigator.language: Korean if it starts with "ko"

// The Korean for an English text, or NULL: the search matches both languages.
const char* nv_strings_find_korean(const char* english);

// The text in the language in use; the English itself while that is English or there is no translation. A printf format keeps
// its conversions in the translation.
const char* nv_strings_text(const char* english);

// A widget label: the text in the language in use, then "###" and the English, so a widget keeps its ImGui id (and its state,
// and a tab its place) when the language changes. An English label that already has "##id" or "###id" keeps that id. The
// result is valid until 64 more labels have been made.
const char* nv_strings_label(const char* english);

// NOTE: Short on purpose, since they wrap every UI text: the one exception to the naming rules.
#define T(english)  nv_strings_text(english)
#define TL(english) nv_strings_label(english)
