#pragma once

// The UI's languages (docs/specs/korean.md). Strings are written in English in the code, wrapped in
// T() (text) or TL() (a widget label); the table in strings.c holds the Korean for each.

#include <engine/base.h>

typedef enum Language {
    LANG_EN,
    LANG_KO,
    LANG_COUNT,
} Language;

Language strings_language(void);
void strings_set_language(Language language);
Language strings_browser_language(void); // from navigator.language: Korean if it starts with "ko"

// The text in the language in use; the English itself while that is English or there is no
// translation. A printf format keeps its conversions in the translation.
const char* T(const char* english);

// A widget label: the text in the language in use, then "###" and the English, so a widget keeps
// its ImGui id (and its state, and a tab its place) when the language changes. An English label
// that already has "##id" or "###id" keeps that id. The result is valid until 64 more TL calls.
const char* TL(const char* english);

// The Korean for an English text, or NULL: the search matches both languages.
const char* strings_find_korean(const char* english);
