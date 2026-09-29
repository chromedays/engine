#pragma once

// The page's log: what the engine, the app and the page report, kept in one fixed ring so the app
// can show it (docs/specs/console.md). Messages also go to the browser console.

#include "nv/base.h"

typedef enum NvLogLevel {
    NV_LOG_INFO,
    NV_LOG_WARNING,
    NV_LOG_ERROR,
    NV_LOG_LEVEL_COUNT,
} NvLogLevel;

#define NV_LOG_MAX_MESSAGES     1024
#define NV_LOG_TEXT_SIZE        NV_KILOBYTES(128)
#define NV_LOG_MAX_MESSAGE_SIZE NV_KILOBYTES(4)
#define NV_LOG_SOURCE_SIZE      8

typedef struct NvLogMessage {
    NvLogLevel level;
    char source[NV_LOG_SOURCE_SIZE]; // "wgpu", NUL-terminated
    u32 repeat;      // times it arrived in a row; 1 = once
    f64 time;        // seconds since the page started, of the latest repeat
    u32 text_offset; // into NvLog.text; a message's text never wraps around the ring's end
    u32 text_size;   // without a terminator
} NvLogMessage;

typedef struct NvLog {
    NvLogMessage messages[NV_LOG_MAX_MESSAGES]; // a ring
    u32 first;                                  // the oldest message's slot
    u32 count;
    u32 level_counts[NV_LOG_LEVEL_COUNT]; // messages held, by level (repeats count once)
    u64 arrived[NV_LOG_LEVEL_COUNT];      // messages and repeats ever added, by level; never cleared
    u64 added; // messages ever added (not repeats): the i-th held is number added - count + i
    char text[NV_LOG_TEXT_SIZE];
    u32 text_end; // where the next message's text goes
} NvLog;

// The page's one log. Zero is an empty log, so it is valid before anything runs.
extern NvLog nv_log_ring;

// Adds a message and writes it to the browser console as "[source] text". `source` is cut to 7
// characters; text longer than NV_LOG_MAX_MESSAGE_SIZE is cut and ends in "...".
void nv_log(NvLogLevel level, const char* source, const char* format, ...)
    __attribute__((format(printf, 3, 4)));

// Moves the messages the page queued (Module.nvLog in web/index.html.in) into the ring. Call once
// per frame. Does nothing where the page has no queue (Node tests).
void nv_log_pump(void);

// Drops every message held; `arrived` and `added` keep counting.
void nv_log_clear(void);

// The i-th message held, 0 = the oldest.
NvLogMessage* nv_log_message(u32 i);

// The number of the i-th message held: the ever-added count, which stays valid as the ring moves.
static inline u64 nv_log_number(u32 i)
{
    return nv_log_ring.added - nv_log_ring.count + i;
}

// The message with this number, or NULL if it has been dropped or cleared.
NvLogMessage* nv_log_find(u64 number);

static inline const char* nv_log_text(const NvLogMessage* message)
{
    return nv_log_ring.text + message->text_offset;
}
