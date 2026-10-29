#pragma once

#include "engine/base.h"

// Reading line-based text files (docs/specs/scene_file.md): one statement per line, a key and its values separated by
// spaces, '#' comments, nesting by indentation with spaces. This is the shared part of the readers (the auto-battler's project
// file); what the keys mean is the reader's. No GPU, so tests run it under Node.
//
// A bad line never stops a read: every error is reported (nv_log, with the file name and line), counted, and the read fails at
// its end. A reader that is NULL reports nothing, for scans of text already read without errors.

#define NV_TEXT_MAX_TOKENS 33 // a key and up to 32 values (a .abproj line may list every role)
#define NV_TEXT_LOGGED_ERRORS 20 // errors of a read that are logged by default; the rest are counted

typedef struct NvTextToken {
    const char* text; // points into the file's text; for a quoted token, inside the quotes
    u32 length;
    b32 quoted; // written "like this" (only in a read with `quotes`); a quoted token is never a key, a number or a name
} NvTextToken;

typedef struct NvTextLine {
    u32 number; // from 1
    u32 indent; // spaces
    u32 token_count;
    NvTextToken tokens[NV_TEXT_MAX_TOKENS];
} NvTextLine;

// What a read reports to. `total` and `first_error` belong to the caller and may be NULL: `total` counts the errors of every
// read that shares it, and `first_error` (of `first_error_capacity` bytes) takes the first one, as "file:line: message".
typedef struct NvTextReader {
    const char* file_name;
    const char* log_source; // nv_log's source, at most NV_LOG_SOURCE_SIZE - 1 bytes
    b32 quotes;             // a token that starts with '"' runs to the next '"' and may hold spaces and '#'; no escapes
    u32 errors;             // found by this read
    u32* total;
    char* first_error;
    umm first_error_capacity;
} NvTextReader;

// Reports an error at `line` (0 = the file as a whole). The first goes to `first_error`; the first NV_TEXT_LOGGED_ERRORS of
// `*total` are logged.
NV_PRINTF_FORMAT(3, 4) void nv_text_report(NvTextReader* reader, u32 line, const char* format, ...);

// How many bytes of a token a message shows: at most 24, cut before a character rather than inside it.
int nv_text_shown(NvTextToken token);

b32 nv_text_token_is(NvTextToken token, const char* word);

// The lines of a text, one at a time.
typedef struct NvTextLines {
    const char* text;
    umm size;
    umm cursor;
    u32 number;
} NvTextLines;

// A line's text, without its '\n'. Fails (zeroed) after the last line.
typedef struct NvTextLineRange {
    b32 ok;
    const char* begin;
    const char* end;
} NvTextLineRange;

NvTextLineRange nv_text_next_line(NvTextLines* lines);

// Splitting a line. A comment runs from '#' to the end of the line (a '#' inside quotes is text); a line with nothing else is
// blank (no tokens). A tab is an error, since indentation is spaces only. Fails on an error, which it reports.
typedef struct NvTextTokenized {
    b32 ok;
    NvTextLine line;
} NvTextTokenized;

NvTextTokenized nv_text_tokenize(NvTextReader* reader, const char* begin, const char* end, u32 number);

// A decimal number: an optional sign, digits, an optional point with digits ("120", "0.5", "-3", ".5"); no exponent. With
// `integer`, a point is an error too. Fails on an error, which it reports.
typedef struct NvTextNumber {
    b32 ok;
    f64 value;
} NvTextNumber;

NvTextNumber nv_text_parse_number(NvTextReader* reader, u32 line, NvTextToken token, b32 integer);

// A name: a letter or '_', then letters, digits and '_'; at most `capacity` - 1 bytes. Copies it into `destination`. Fails on
// an error, which it reports.
b32 nv_text_parse_name(NvTextReader* reader, u32 line, NvTextToken token, char* destination, umm capacity);

// Quoted text ("look target"): copies it into `destination` (`capacity` bytes with the NUL). A token that is not quoted, or does
// not fit, is an error, which it reports.
b32 nv_text_parse_text(NvTextReader* reader, u32 line, NvTextToken token, char* destination, umm capacity);
