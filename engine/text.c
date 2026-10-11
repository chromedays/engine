#include "engine/text.h"

#include <engine/log.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void nv_text_report(NvTextReader* reader, u32 line, const char* format, ...)
{
    if (!reader)
        return;
    char message[128];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    nv_utf8_trim(message); // a long message is cut at the buffer's end, perhaps inside a character

    char full[160];
    if (line)
        snprintf(full, sizeof(full), "%s:%u: %s", reader->file_name, line, message);
    else
        snprintf(full, sizeof(full), "%s: %s", reader->file_name, message);
    nv_utf8_trim(full);
    const char* source = reader->log_source ? reader->log_source : "text";
    ++reader->errors;
    u32 count = reader->total ? ++*reader->total : reader->errors;
    if (count == 1 && reader->first_error && reader->first_error_capacity) {
        snprintf(reader->first_error, reader->first_error_capacity, "%s", full);
        nv_utf8_trim(reader->first_error);
    }
    if (count <= NV_TEXT_LOGGED_ERRORS)
        nv_log(NV_LOG_ERROR, source, "%s", full);
    else if (count == NV_TEXT_LOGGED_ERRORS + 1)
        nv_log(NV_LOG_ERROR, source, "more errors, not shown");
}

int nv_text_shown(NvTextToken token)
{
    u32 length = token.length;
    if (length > 24) {
        length = 24;
        while (length > 0 && ((u8)token.text[length] & 0xC0) == 0x80)
            --length;
    }
    return (int)length;
}

b32 nv_text_token_is(NvTextToken token, const char* word)
{
    return !token.quoted && token.length == strlen(word) && memcmp(token.text, word, token.length) == 0;
}

NvTextLineRange nv_text_next_line(NvTextLines* lines)
{
    if (lines->cursor >= lines->size)
        return (NvTextLineRange){0};
    const char* begin = lines->text + lines->cursor;
    const char* limit = lines->text + lines->size;
    const char* p = begin;
    while (p < limit && *p != '\n')
        ++p;
    lines->cursor = (umm)(p - lines->text) + 1; // past the '\n'
    ++lines->number;
    return (NvTextLineRange){.ok = 1, .begin = begin, .end = p};
}

NvTextTokenized nv_text_tokenize(NvTextReader* reader, const char* begin, const char* end, u32 number)
{
    b32 quotes = reader && reader->quotes;
    const char* stop = end;
    b32 inside = 0;
    for (const char* p = begin; p < stop; ++p) {
        if (quotes && *p == '"')
            inside = !inside;
        else if (*p == '#' && !inside)
            stop = p;
    }
    while (stop > begin && (stop[-1] == ' ' || stop[-1] == '\r')) // trailing spaces and the CR of a CRLF
        --stop;
    for (const char* p = begin; p < stop; ++p) {
        if (*p == '\t') {
            nv_text_report(reader, number, "tab (indent and separate with spaces)");
            return (NvTextTokenized){0};
        }
    }

    NvTextTokenized result = {.ok = 1, .line = {.number = number}};
    NvTextLine* line = &result.line;
    const char* p = begin;
    while (p < stop && *p == ' ') {
        ++line->indent;
        ++p;
    }
    if (p == stop)
        return result;
    while (p < stop) {
        while (p < stop && *p == ' ')
            ++p;
        if (p == stop)
            break;
        const char* word = p;
        NvTextToken token;
        if (quotes && *p == '"') {
            const char* close = p + 1;
            while (close < stop && *close != '"')
                ++close;
            if (close == stop) {
                nv_text_report(reader, number, "text is not closed (the second '\"' is missing)");
                return (NvTextTokenized){0};
            }
            token = (NvTextToken){word + 1, (u32)(close - word - 1), 1};
            p = close + 1;
            if (p < stop && *p != ' ') {
                nv_text_report(reader, number, "text must be followed by a space");
                return (NvTextTokenized){0};
            }
        } else {
            while (p < stop && *p != ' ')
                ++p;
            token = (NvTextToken){word, (u32)(p - word), 0};
        }
        if (line->token_count == NV_TEXT_MAX_TOKENS) {
            nv_text_report(reader, number, "too many values");
            return (NvTextTokenized){0};
        }
        line->tokens[line->token_count++] = token;
    }
    return result;
}

internal b32 is_digit(char c)
{
    return c >= '0' && c <= '9';
}

NvTextNumber nv_text_parse_number(NvTextReader* reader, u32 line, NvTextToken token, b32 integer)
{
    char buffer[40];
    u32 i = 0, digits = 0;
    b32 valid = !token.quoted && token.length < sizeof(buffer);
    if (valid && i < token.length && (token.text[i] == '+' || token.text[i] == '-'))
        ++i;
    while (valid && i < token.length && is_digit(token.text[i])) {
        ++i;
        ++digits;
    }
    b32 has_point = valid && i < token.length && token.text[i] == '.';
    if (has_point) {
        ++i;
        while (i < token.length && is_digit(token.text[i])) {
            ++i;
            ++digits;
        }
    }
    valid = valid && i == token.length && digits > 0;
    if (!valid) {
        nv_text_report(reader, line, "'%.*s' is not a number", nv_text_shown(token), token.text);
        return (NvTextNumber){0};
    }
    if (integer && has_point) {
        nv_text_report(reader, line, "'%.*s' is not an integer", nv_text_shown(token), token.text);
        return (NvTextNumber){0};
    }
    memcpy(buffer, token.text, token.length);
    buffer[token.length] = 0;
    return (NvTextNumber){.ok = 1, .value = strtod(buffer, NULL)}; // strtod rounds correctly, so a file always gives the same values
}

b32 nv_text_parse_name(NvTextReader* reader, u32 line, NvTextToken token, char* destination, umm capacity)
{
    b32 valid = !token.quoted && token.length > 0 && token.length < capacity;
    for (u32 i = 0; valid && i < token.length; ++i) {
        char c = token.text[i];
        b32 letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        valid = letter || (i > 0 && is_digit(c));
    }
    if (!valid) {
        nv_text_report(reader, line, "'%.*s' is not a name (letters, digits and _; at most %u bytes; not starting with a digit)",
                       nv_text_shown(token), token.text, (u32)(capacity - 1));
        return 0;
    }
    memcpy(destination, token.text, token.length);
    destination[token.length] = 0;
    return 1;
}

b32 nv_text_parse_text(NvTextReader* reader, u32 line, NvTextToken token, char* destination, umm capacity)
{
    if (!token.quoted) {
        nv_text_report(reader, line, "'%.*s' is not text (put it in double quotes)", nv_text_shown(token), token.text);
        return 0;
    }
    if (token.length >= capacity) {
        nv_text_report(reader, line, "text is longer than %u bytes", (u32)(capacity - 1));
        return 0;
    }
    memcpy(destination, token.text, token.length);
    destination[token.length] = 0;
    return 1;
}
