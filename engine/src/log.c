#include "nv/log.h"

#include <emscripten/emscripten.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

EM_JS_DEPS(nv_log, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8");

NvLog nv_log_ring;

// One line for a message: the console shows "[source] text" in the color of its level.
EM_JS(void, js_log_console, (int level, const char* source, const char* text), {
    const line = "[" + UTF8ToString(source) + "] " + UTF8ToString(text);
    if (level >= 2) console.error(line);
    else if (level == 1) console.warn(line);
    else console.log(line);
});

// Takes the oldest message the page queued (Module.nvLogQueue, filled by Module.nvLog). Returns the
// byte length of its whole text, which may be more than fits in `text`; -1 when the queue is empty.
EM_JS(int, js_log_take, (int* level, char* source, int source_size, char* text, int text_size, double* time), {
    const queue = Module["nvLogQueue"];
    if (!queue || !queue.length) return -1;
    const message = queue.shift();
    HEAP32[level >> 2] = message.level;
    stringToUTF8(String(message.source), source, source_size);
    stringToUTF8(String(message.text), text, text_size);
    HEAPF64[time >> 3] = message.time;
    return lengthBytesUTF8(String(message.text));
});

// How many queued messages the page had to drop, since the last call.
EM_JS(int, js_log_take_dropped, (void), {
    const dropped = Module["nvLogDropped"] || 0;
    Module["nvLogDropped"] = 0;
    return dropped;
});

// Where a message is formatted (nv_log) or read from the page (nv_log_pump) before it goes in the
// ring; one byte more than a message holds, for the terminator.
global char message_buffer[NV_LOG_MAX_MESSAGE_SIZE + 1];

internal NvLogMessage* slot(u32 i)
{
    return &nv_log_ring.messages[(nv_log_ring.first + i) % NV_LOG_MAX_MESSAGES];
}

internal void drop_oldest(void)
{
    NvLog* log = &nv_log_ring;
    NV_ASSERT(log->count);
    --log->level_counts[log->messages[log->first].level];
    log->first = (log->first + 1) % NV_LOG_MAX_MESSAGES;
    --log->count;
}

// Adds a message to the ring. `text` is writable and holds NV_LOG_MAX_MESSAGE_SIZE bytes at least;
// `length` is the whole text's length, or NV_LOG_MAX_MESSAGE_SIZE + 1 when it is longer than that
// (only the first NV_LOG_MAX_MESSAGE_SIZE bytes need to be there).
internal void add(NvLogLevel level, const char* source, char* text, umm length, f64 time)
{
    NvLog* log = &nv_log_ring;
    if (level >= NV_LOG_LEVEL_COUNT)
        level = NV_LOG_ERROR;

    char source_copy[NV_LOG_SOURCE_SIZE] = {0};
    strncpy(source_copy, source, NV_LOG_SOURCE_SIZE - 1);

    u32 size = (u32)length;
    if (length > NV_LOG_MAX_MESSAGE_SIZE) {
        // Cut on a character boundary, so the "..." does not follow half of a UTF-8 sequence.
        u32 cut = NV_LOG_MAX_MESSAGE_SIZE - 3;
        while (cut && ((u8)text[cut] & 0xC0) == 0x80)
            --cut;
        memcpy(text + cut, "...", 3);
        size = cut + 3;
    }

    ++log->arrived[level];
    if (log->count) {
        NvLogMessage* newest = slot(log->count - 1);
        if (newest->level == level && strcmp(newest->source, source_copy) == 0 && newest->text_size == size &&
            memcmp(log->text + newest->text_offset, text, size) == 0) {
            if (newest->repeat < 0xFFFFFFFFu)
                ++newest->repeat;
            newest->time = time;
            return;
        }
    }

    if (log->count == NV_LOG_MAX_MESSAGES)
        drop_oldest();
    // A text never wraps around the ring's end: one that does not fit starts again at 0. The texts
    // in the way are the oldest messages' (they follow the free space in ring order), so those go.
    u32 start = log->count ? log->text_end : 0;
    if (start + size > NV_LOG_TEXT_SIZE)
        start = 0;
    while (log->count) {
        NvLogMessage* oldest = &log->messages[log->first];
        if (oldest->text_offset >= start + size || oldest->text_offset + oldest->text_size <= start)
            break;
        drop_oldest();
    }

    NvLogMessage* message = slot(log->count);
    message->level = level;
    memcpy(message->source, source_copy, NV_LOG_SOURCE_SIZE);
    message->repeat = 1;
    message->time = time;
    message->text_offset = start;
    message->text_size = size;
    memcpy(log->text + start, text, size);
    log->text_end = start + size;
    ++log->count;
    ++log->level_counts[level];
    ++log->added;
}

void nv_log(NvLogLevel level, const char* source, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(message_buffer, sizeof(message_buffer), format, arguments);
    va_end(arguments);
    if (length < 0)
        length = 0;
    js_log_console((int)level, source, message_buffer);
    add(level, source, message_buffer, (umm)length > NV_LOG_MAX_MESSAGE_SIZE ? NV_LOG_MAX_MESSAGE_SIZE + 1 : (umm)length,
        emscripten_get_now() / 1000.0);
}

void nv_log_pump(void)
{
    int dropped = js_log_take_dropped();
    if (dropped > 0)
        nv_log(NV_LOG_WARNING, "nv", "%d page messages were dropped before they were read", dropped);
    for (;;) {
        int level = 0;
        char source[NV_LOG_SOURCE_SIZE];
        f64 time = 0.0;
        int length = js_log_take(&level, source, (int)sizeof(source), message_buffer, (int)sizeof(message_buffer), &time);
        if (length < 0)
            break;
        add((NvLogLevel)level, source, message_buffer,
            (umm)length > NV_LOG_MAX_MESSAGE_SIZE ? NV_LOG_MAX_MESSAGE_SIZE + 1 : (umm)length, time);
    }
}

void nv_log_clear(void)
{
    NvLog* log = &nv_log_ring;
    log->first = 0;
    log->count = 0;
    log->text_end = 0;
    for (u32 i = 0; i < NV_LOG_LEVEL_COUNT; ++i)
        log->level_counts[i] = 0;
}

NvLogMessage* nv_log_message(u32 i)
{
    NV_ASSERT(i < nv_log_ring.count);
    return slot(i);
}

NvLogMessage* nv_log_find(u64 number)
{
    NvLog* log = &nv_log_ring;
    if (number >= log->added || log->added - number > log->count)
        return NULL;
    return slot((u32)(log->count - (log->added - number)));
}
