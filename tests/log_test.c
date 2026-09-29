// Checks nv/log.h without a browser: the ring wraps and drops the oldest messages, texts never wrap
// around the text ring's end, repeats collapse, long texts are cut on a character boundary, and the
// page's queue is read. Runs under Node (ctest).

#include "nv/log.h"

#include <emscripten/emscripten.h>

#include <stdio.h>
#include <string.h>

global int failures;

EM_JS_DEPS(log_test, "$UTF8ToString");

#define CHECK(expr)                                                        \
    do {                                                                   \
        if (!(expr)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

// nv_log writes to the console; record it instead of printing thousands of lines.
EM_JS(void, record_console, (void), {
    Module.consoleCalls = [];
    const record = (kind) => (text) => Module.consoleCalls.push(kind + ":" + text);
    console.log = record("log");
    console.warn = record("warn");
    console.error = record("error");
});

EM_JS(int, console_call_count, (void), {
    return Module.consoleCalls.length;
});

EM_JS(int, console_call_is, (int index, const char* expected), {
    return Module.consoleCalls[index] === UTF8ToString(expected) ? 1 : 0;
});

// What the page would have queued for nv_log_pump (web/index.html.in).
EM_JS(void, queue_page_messages, (void), {
    Module.nvLogQueue = [
        {level: 2, source: "js", text: "boom", time: 1.5},
        {level: 0, source: "stdout", text: "caf\u00e9", time: 2.5},
        {level: 1, source: "toolongsource", text: "x".repeat(6000), time: 3.5},
        {level: 2, source: "js", text: "boom", time: 4.5},
        {level: 2, source: "js", text: "boom", time: 5.5},
    ];
    Module.nvLogDropped = 7;
});

internal b32 text_is(const NvLogMessage* message, const char* expected)
{
    return message->text_size == strlen(expected) && memcmp(nv_log_text(message), expected, message->text_size) == 0;
}

// The ring after every add must hold: texts inside the text ring, none overlapping, and level
// counts matching the messages.
internal void check_invariants(void)
{
    NvLog* log = &nv_log_ring;
    CHECK(log->count <= NV_LOG_MAX_MESSAGES);
    u32 counts[NV_LOG_LEVEL_COUNT] = {0};
    u32 bytes = 0;
    for (u32 i = 0; i < log->count; ++i) {
        NvLogMessage* message = nv_log_message(i);
        CHECK(message->text_offset + message->text_size <= NV_LOG_TEXT_SIZE);
        CHECK(message->text_size <= NV_LOG_MAX_MESSAGE_SIZE);
        CHECK(message->repeat >= 1);
        ++counts[message->level];
        bytes += message->text_size;
        for (u32 j = i + 1; j < log->count; ++j) {
            NvLogMessage* other = nv_log_message(j);
            b32 apart = message->text_offset + message->text_size <= other->text_offset ||
                        other->text_offset + other->text_size <= message->text_offset;
            CHECK(apart || !message->text_size || !other->text_size);
        }
    }
    for (u32 i = 0; i < NV_LOG_LEVEL_COUNT; ++i)
        CHECK(counts[i] == log->level_counts[i]);
    CHECK(bytes <= NV_LOG_TEXT_SIZE);
}

// A valid UTF-8 string: every lead byte is followed by its continuation bytes, and nothing else.
internal b32 valid_utf8(const char* text, u32 size)
{
    for (u32 i = 0; i < size;) {
        u8 lead = (u8)text[i];
        u32 length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        if (!length || i + length > size)
            return 0;
        for (u32 k = 1; k < length; ++k) {
            if (((u8)text[i + k] & 0xC0) != 0x80)
                return 0;
        }
        i += length;
    }
    return 1;
}

internal void test_empty(void)
{
    NvLog* log = &nv_log_ring;
    CHECK(log->count == 0);
    CHECK(log->added == 0);
    CHECK(nv_log_find(0) == NULL);
    nv_log_pump(); // no page queue under Node
    CHECK(log->count == 0);
}

internal void test_add(void)
{
    NvLog* log = &nv_log_ring;
    nv_log(NV_LOG_WARNING, "wgpu", "device %d of %s", 3, "many");
    CHECK(log->count == 1);
    CHECK(log->added == 1);
    CHECK(log->level_counts[NV_LOG_WARNING] == 1);
    CHECK(log->arrived[NV_LOG_WARNING] == 1);
    NvLogMessage* message = nv_log_message(0);
    CHECK(message->level == NV_LOG_WARNING);
    CHECK(strcmp(message->source, "wgpu") == 0);
    CHECK(message->repeat == 1);
    CHECK(message->time > 0.0);
    CHECK(text_is(message, "device 3 of many"));
    CHECK(nv_log_find(0) == message);
    CHECK(nv_log_find(1) == NULL);
    CHECK(nv_log_number(0) == 0);

    // The console gets it too, in the color of its level.
    CHECK(console_call_count() == 1);
    CHECK(console_call_is(0, "warn:[wgpu] device 3 of many"));
    nv_log(NV_LOG_ERROR, "nv", "bad");
    nv_log(NV_LOG_INFO, "nv", "fine");
    CHECK(console_call_is(1, "error:[nv] bad"));
    CHECK(console_call_is(2, "log:[nv] fine"));

    // The source is cut to what fits.
    nv_log(NV_LOG_INFO, "abcdefghijkl", "long source");
    CHECK(strcmp(nv_log_message(3)->source, "abcdefg") == 0);
    check_invariants();
}

internal void test_repeats(void)
{
    NvLog* log = &nv_log_ring;
    nv_log_clear();
    u64 arrived = log->arrived[NV_LOG_ERROR];
    for (u32 i = 0; i < 240; ++i)
        nv_log(NV_LOG_ERROR, "wgpu", "same every frame");
    CHECK(log->count == 1);
    CHECK(nv_log_message(0)->repeat == 240);
    CHECK(log->arrived[NV_LOG_ERROR] == arrived + 240);
    CHECK(log->level_counts[NV_LOG_ERROR] == 1);

    // Another level, source or text is another row; going back is too (only the newest counts).
    nv_log(NV_LOG_WARNING, "wgpu", "same every frame");
    nv_log(NV_LOG_WARNING, "app", "same every frame");
    nv_log(NV_LOG_WARNING, "app", "same every frame!");
    nv_log(NV_LOG_ERROR, "wgpu", "same every frame");
    CHECK(log->count == 5);
    check_invariants();
}

internal void test_message_ring(void)
{
    NvLog* log = &nv_log_ring;
    nv_log_clear();
    u64 added = log->added;
    for (u32 i = 0; i < 1500; ++i)
        nv_log(i % 3 == 0 ? NV_LOG_INFO : i % 3 == 1 ? NV_LOG_WARNING : NV_LOG_ERROR, "app", "m%u", i);
    CHECK(log->count == NV_LOG_MAX_MESSAGES);
    CHECK(log->added == added + 1500);
    CHECK(text_is(nv_log_message(0), "m476"));
    CHECK(text_is(nv_log_message(NV_LOG_MAX_MESSAGES - 1), "m1499"));
    CHECK(log->level_counts[0] + log->level_counts[1] + log->level_counts[2] == NV_LOG_MAX_MESSAGES);
    // Numbers stay valid as the ring moves; dropped ones are gone.
    CHECK(nv_log_find(added + 1499) == nv_log_message(NV_LOG_MAX_MESSAGES - 1));
    CHECK(nv_log_find(added + 475) == NULL);
    CHECK(nv_log_find(added + 476) == nv_log_message(0));
    check_invariants();
}

// Messages of about 3000 bytes fill the text ring long before the message ring: older ones drop,
// and what is left is still intact.
internal void test_text_ring(void)
{
    NvLog* log = &nv_log_ring;
    nv_log_clear();
    u32 wrapped = 0;
    u32 previous_end = 0;
    for (u32 i = 0; i < 400; ++i) {
        char text[3100];
        u32 length = 2900 + (i * 37) % 200;
        for (u32 k = 0; k < length; ++k)
            text[k] = (char)('a' + (i + k) % 26);
        text[length] = 0;
        nv_log(NV_LOG_INFO, "app", "%s", text);
        check_invariants();
        NvLogMessage* newest = nv_log_message(log->count - 1);
        if (newest->text_offset == 0 && previous_end != 0)
            ++wrapped;
        previous_end = newest->text_offset + newest->text_size;
    }
    CHECK(wrapped >= 7);
    CHECK(log->count >= 40 && log->count <= 45);
    for (u32 j = 0; j < log->count; ++j) {
        NvLogMessage* message = nv_log_message(j);
        u32 i = (u32)nv_log_number(j) - (u32)(log->added - 400); // 0 when the ring was cleared
        u32 length = 2900 + (i * 37) % 200;
        CHECK(message->text_size == length);
        b32 intact = 1;
        for (u32 k = 0; k < length; ++k)
            intact = intact && nv_log_text(message)[k] == (char)('a' + (i + k) % 26);
        CHECK(intact);
    }
}

internal void test_cut(void)
{
    local_persist char text[5200];
    memset(text, 'x', sizeof(text));
    text[5000] = 0;
    nv_log_clear();
    nv_log(NV_LOG_INFO, "app", "%s", text);
    NvLogMessage* message = nv_log_message(0);
    CHECK(message->text_size == NV_LOG_MAX_MESSAGE_SIZE);
    CHECK(memcmp(nv_log_text(message) + NV_LOG_MAX_MESSAGE_SIZE - 3, "...", 3) == 0);
    CHECK(nv_log_text(message)[NV_LOG_MAX_MESSAGE_SIZE - 4] == 'x');

    // An exactly full message is not cut.
    text[NV_LOG_MAX_MESSAGE_SIZE] = 0;
    nv_log(NV_LOG_INFO, "app", "%s", text);
    message = nv_log_message(1);
    CHECK(message->text_size == NV_LOG_MAX_MESSAGE_SIZE);
    CHECK(nv_log_text(message)[NV_LOG_MAX_MESSAGE_SIZE - 1] == 'x');

    // Two-byte characters: the cut may not land inside one.
    for (u32 offset = 0; offset < 3; ++offset) {
        u32 at = 0;
        for (u32 i = 0; i < offset; ++i)
            text[at++] = 'y';
        while (at < 5000) {
            text[at++] = (char)0xC3;
            text[at++] = (char)0xA9; // e with an acute accent
        }
        text[at] = 0;
        nv_log(NV_LOG_ERROR, "app", "%s", text);
        message = nv_log_message(nv_log_ring.count - 1);
        CHECK(message->text_size <= NV_LOG_MAX_MESSAGE_SIZE);
        CHECK(memcmp(nv_log_text(message) + message->text_size - 3, "...", 3) == 0);
        CHECK(valid_utf8(nv_log_text(message), message->text_size));
    }
    check_invariants();
}

internal void test_clear(void)
{
    NvLog* log = &nv_log_ring;
    nv_log(NV_LOG_ERROR, "app", "one more");
    u64 added = log->added;
    u64 arrived = log->arrived[NV_LOG_ERROR];
    nv_log_clear();
    CHECK(log->count == 0);
    CHECK(log->level_counts[0] == 0 && log->level_counts[1] == 0 && log->level_counts[2] == 0);
    CHECK(log->added == added);
    CHECK(log->arrived[NV_LOG_ERROR] == arrived);
    CHECK(nv_log_find(added - 1) == NULL);
    nv_log(NV_LOG_INFO, "app", "after");
    CHECK(log->count == 1);
    CHECK(nv_log_find(added) == nv_log_message(0));
    check_invariants();
}

// The page's queue: Module.nvLogQueue holds {level, source, text, time}.
internal void test_pump(void)
{
    NvLog* log = &nv_log_ring;
    nv_log_clear();
    queue_page_messages();
    nv_log_pump();
    CHECK(log->count == 5);
    CHECK(nv_log_message(0)->level == NV_LOG_WARNING);
    CHECK(strcmp(nv_log_message(0)->source, "nv") == 0);
    CHECK(text_is(nv_log_message(0), "7 page messages were dropped before they were read"));

    NvLogMessage* boom = nv_log_message(1);
    CHECK(boom->level == NV_LOG_ERROR && strcmp(boom->source, "js") == 0 && text_is(boom, "boom"));
    CHECK(boom->time == 1.5);
    CHECK(text_is(nv_log_message(2), "caf\xC3\xA9"));
    CHECK(nv_log_message(2)->level == NV_LOG_INFO);
    NvLogMessage* long_one = nv_log_message(3);
    CHECK(strcmp(long_one->source, "toolong") == 0);
    CHECK(long_one->text_size == NV_LOG_MAX_MESSAGE_SIZE);
    CHECK(memcmp(nv_log_text(long_one) + NV_LOG_MAX_MESSAGE_SIZE - 3, "...", 3) == 0);
    CHECK(nv_log_message(4)->repeat == 2); // the two "boom"s at the end are one row
    CHECK(nv_log_message(4)->time == 5.5);
    check_invariants();

    // Read once: a second pump finds nothing.
    nv_log_pump();
    CHECK(log->count == 5);
}

int main(void)
{
    record_console();
    test_empty();
    test_add();
    test_repeats();
    test_message_ring();
    test_text_ring();
    test_cut();
    test_clear();
    test_pump();
    if (failures) {
        fprintf(stderr, "log_test: %d checks failed\n", failures);
        return 1;
    }
    printf("log_test: ok\n");
    return 0;
}
