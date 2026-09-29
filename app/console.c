#include "app.h"

#include <stdio.h>
#include <string.h>

// The Console tab (docs/specs/console.md): the engine's log (nv/log.h) as a list with filters.

// The row's first line is drawn up to this many bytes; the detail box below shows all of it.
#define ROW_MAX_BYTES 512
// Below this many characters of width the rows leave out the time.
#define ROW_COMPACT_CHARS 44

ImU32 console_level_color(NvLogLevel level)
{
    switch (level) {
    case NV_LOG_ERROR:   return 0xFF6666FFu;
    case NV_LOG_WARNING: return 0xFF4DCCFFu;
    default:             return 0xFFDDDDDDu;
    }
}

internal const char* level_name(NvLogLevel level)
{
    switch (level) {
    case NV_LOG_ERROR:   return "error";
    case NV_LOG_WARNING: return "warn";
    default:             return "info";
    }
}

internal void record_rect(Console* console, ConsoleRect id, ImVec2_c min, ImVec2_c max)
{
#if !defined(NDEBUG)
    f32* rect = console->rects[id];
    rect[0] = min.x;
    rect[1] = min.y;
    rect[2] = max.x;
    rect[3] = max.y;
#else
    (void)console, (void)id, (void)min, (void)max;
#endif
}

void console_record(Console* console, ConsoleRect id)
{
    record_rect(console, id, igGetItemRectMin(), igGetItemRectMax());
}

b32 console_is_compact(void)
{
    return igGetContentRegionAvail().x < (f32)ROW_COMPACT_CHARS * igCalcTextSize("0", NULL, false, -1.0f).x;
}

u32 console_unseen(App* app, NvLogLevel* worst)
{
    Console* console = &app->console;
    u32 warnings = 0;
    u32 errors = 0;
    if (!console->shown_last) {
        warnings = (u32)(nv_log_ring.arrived[NV_LOG_WARNING] - console->seen[NV_LOG_WARNING]);
        errors = (u32)(nv_log_ring.arrived[NV_LOG_ERROR] - console->seen[NV_LOG_ERROR]);
    }
    if (worst)
        *worst = errors ? NV_LOG_ERROR : warnings ? NV_LOG_WARNING : NV_LOG_INFO;
    return warnings + errors;
}

// One message as a line of text, for Copy: "  12.345 error wgpu text (x3)". Returns the length
// snprintf would write, so a first call with no buffer sizes the second.
internal umm format_line(char* out, umm capacity, const NvLogMessage* message)
{
    char repeat[24] = "";
    if (message->repeat > 1)
        snprintf(repeat, sizeof(repeat), " (x%u)", message->repeat);
    int length = snprintf(out, capacity, "%9.3f %-5s %-7s %.*s%s\n", message->time, level_name(message->level),
                          message->source, (int)message->text_size, nv_log_text(message), repeat);
    return length > 0 ? (umm)length : 0;
}

internal void copy_shown(App* app, const u32* rows, u32 row_count)
{
    NvArena* scratch = &app->scratch;
    umm mark = scratch->used;
    umm size = 1;
    for (u32 r = 0; r < row_count; ++r)
        size += format_line(NULL, 0, nv_log_message(rows[r]));
    char* text = NV_PUSH_ARRAY(scratch, size, char);
    umm at = 0;
    for (u32 r = 0; r < row_count; ++r)
        at += format_line(text + at, size - at, nv_log_message(rows[r]));
    // NOTE: The clipboard holds NV_IMGUI_CLIPBOARD_SIZE bytes; a longer copy is cut there.
    igSetClipboardText(text);
    scratch->used = mark;
}

// Whether the message passes the text filter, which sees "source text".
internal b32 passes_filter(Console* console, const NvLogMessage* message, char* buffer)
{
    if (!ImGuiTextFilter_IsActive(&console->filter))
        return 1;
    int length = snprintf(buffer, NV_LOG_MAX_MESSAGE_SIZE + NV_LOG_SOURCE_SIZE + 2, "%s %.*s", message->source,
                          (int)message->text_size, nv_log_text(message));
    return ImGuiTextFilter_PassFilter(&console->filter, buffer, buffer + length);
}

// One row: a selectable over the whole width, with the parts drawn on it. `compact` leaves out the
// time (a phone's panel is too narrow for it).
internal void draw_row(Console* console, const NvLogMessage* message, u64 number, b32 compact, f32 char_width)
{
    igPushID_Int((int)(number & 0x7FFFFFFF));
    ImVec2_c origin = igGetCursorScreenPos();
    bool selected = console->selected == number + 1;
    if (igSelectable_Bool("##row", selected, 0, (ImVec2_c){0.0f, 0.0f}))
        console->selected = selected ? 0 : number + 1;
    igPopID();

    ImDrawList* draw = igGetWindowDrawList();
    ImVec2_c at = origin;
    char part[ROW_MAX_BYTES + 8];
    if (!compact) {
        snprintf(part, sizeof(part), "%9.3f", message->time);
        ImDrawList_AddText_Vec2(draw, at, 0xFF999999u, part, NULL);
        at.x += 10.0f * char_width;
    }
    ImDrawList_AddText_Vec2(draw, at, console_level_color(message->level), level_name(message->level), NULL);
    at.x += 6.0f * char_width;
    ImDrawList_AddText_Vec2(draw, at, 0xFFBBBBBBu, message->source, NULL);
    at.x += 8.0f * char_width;
    if (message->repeat > 1) {
        snprintf(part, sizeof(part), "x%u", message->repeat);
        ImDrawList_AddText_Vec2(draw, at, 0xFF66CCFFu, part, NULL);
        at.x += (f32)(strlen(part) + 1) * char_width;
    }

    // The first line only, and no more than ROW_MAX_BYTES of it.
    const char* text = nv_log_text(message);
    umm length = message->text_size;
    b32 more = 0;
    const char* newline = memchr(text, '\n', length);
    if (newline) {
        length = (umm)(newline - text);
        more = 1;
    }
    if (length > ROW_MAX_BYTES) {
        length = ROW_MAX_BYTES;
        more = 1;
    }
    ImDrawList_AddText_Vec2(draw, at, 0xFFFFFFFFu, text, text + length);
    if (more) {
        at.x += igCalcTextSize(text, text + length, false, -1.0f).x;
        ImDrawList_AddText_Vec2(draw, at, 0xFF999999u, "...", NULL);
    }
}

internal f32 checkbox_fits(const char* label)
{
    return igGetFrameHeight() + igGetStyle()->ItemInnerSpacing.x + igCalcTextSize(label, NULL, true, -1.0f).x;
}

// The level checkboxes, Clear, Copy and Auto-scroll on one wrapping line, and the filter.
// Returns whether Clear was pressed. The log is cleared later, since the rows this frame lists are
// indices into it.
internal b32 toolbar(App* app, const u32* rows, u32 row_count, b32 compact)
{
    b32 clear = 0;
    Console* console = &app->console;
    NvLog* log = &nv_log_ring;
    // A narrow panel (a phone) gets shorter labels and no counts, to keep the toolbar to two lines.
    local_persist const char* names[NV_LOG_LEVEL_COUNT] = {"Info", "Warning", "Error"};
    local_persist const char* short_names[NV_LOG_LEVEL_COUNT] = {"Info", "Warn", "Error"};
    for (u32 level = 0; level < NV_LOG_LEVEL_COUNT; ++level) {
        char label[40];
        if (compact)
            snprintf(label, sizeof(label), "%s###level%u", short_names[level], level);
        else
            snprintf(label, sizeof(label), "%s %u###level%u", names[level], log->level_counts[level], level);
        if (level)
            ui_same_line_if_fits(checkbox_fits(label));
        bool shown = !console->hidden[level];
        igPushStyleColor_U32(ImGuiCol_Text, console_level_color((NvLogLevel)level));
        if (igCheckbox(label, &shown))
            console->hidden[level] = !shown;
        igPopStyleColor(1);
        console_record(console, (ConsoleRect)(CONSOLE_RECT_LEVEL + level));
    }
    ui_same_line_if_fits(igCalcTextSize("Clear", NULL, false, -1.0f).x + igGetStyle()->FramePadding.x * 2.0f);
    if (igButton("Clear", (ImVec2_c){0.0f, 0.0f}))
        clear = 1;
    console_record(console, CONSOLE_RECT_CLEAR);
    ui_same_line_if_fits(igCalcTextSize("Copy", NULL, false, -1.0f).x + igGetStyle()->FramePadding.x * 2.0f);
    if (igButton("Copy", (ImVec2_c){0.0f, 0.0f}))
        copy_shown(app, rows, row_count);
    console_record(console, CONSOLE_RECT_COPY);
    const char* auto_label = compact ? "Auto###auto" : "Auto-scroll###auto";
    ui_same_line_if_fits(checkbox_fits(auto_label));
    igCheckbox(auto_label, &console->auto_scroll);
    console_record(console, CONSOLE_RECT_AUTO_SCROLL);

    igSetNextItemWidth(-1.0f);
    if (igInputTextWithHint("##filter", "Filter (-word leaves it out)", console->filter.InputBuf,
                            sizeof(console->filter.InputBuf), 0, NULL, NULL))
        ImGuiTextFilter_Build(&console->filter);
    console_record(console, CONSOLE_RECT_FILTER);
    return clear;
}

void console_tab(App* app)
{
    Console* console = &app->console;
    NvLog* log = &nv_log_ring;
    console->shown_now = 1;
    f32 char_width = igCalcTextSize("0", NULL, false, -1.0f).x;
    b32 compact = console_is_compact();

    // A selected message that has been dropped or cleared is gone.
    if (console->selected && !nv_log_find(console->selected - 1))
        console->selected = 0;

    // The messages the level checkboxes and the filter let through.
    NvArena* scratch = &app->scratch;
    umm mark = scratch->used;
    u32* rows = NV_PUSH_ARRAY(scratch, log->count, u32);
    char* filter_text = NV_PUSH_ARRAY(scratch, NV_LOG_MAX_MESSAGE_SIZE + NV_LOG_SOURCE_SIZE + 2, char);
    u32 row_count = 0;
    for (u32 i = 0; i < log->count; ++i) {
        NvLogMessage* message = nv_log_message(i);
        if (!console->hidden[message->level] && passes_filter(console, message, filter_text))
            rows[row_count++] = i;
    }
    console->rows = row_count;

    b32 clear = toolbar(app, rows, row_count, compact);

    // What is left of the tab goes to the list, except for the detail box when a message is picked.
    f32 line = igGetTextLineHeightWithSpacing();
    // (On a phone's short panel less of each, and the panel scrolls to the rest.)
    f32 detail_lines = compact ? 3.0f : 5.0f;
    f32 detail_height = console->selected ? line * (detail_lines + 2.0f) : 0.0f;
    f32 list_height = igGetContentRegionAvail().y - detail_height;
    f32 list_minimum = line * (compact ? 4.0f : 6.0f);
    if (list_height < list_minimum)
        list_height = list_minimum;
    igBeginChild_Str("##log", (ImVec2_c){0.0f, list_height}, ImGuiChildFlags_Borders, 0);
    nv_imgui_touch_scroll(&app->imgui);
    ImVec2_c list_min = igGetWindowPos();
    ImVec2_c list_size = igGetWindowSize();
    record_rect(console, CONSOLE_RECT_LIST, list_min, (ImVec2_c){list_min.x + list_size.x, list_min.y + list_size.y});
    // Following new messages: only while the list was at its bottom, so scrolling up holds it still.
    // (ImGui's scroll range is last frame's, which is what this frame's scroll position is against.)
    // A finger that scrolled the list this frame has not moved it yet (ImGui applies the scroll at
    // the next Begin), so it may not be followed to the bottom.
    b32 at_bottom = igGetScrollY() >= igGetScrollMaxY() - 1.0f && app->imgui.touch_scroll == 0.0f;
    ImGuiListClipper clipper = {0};
    ImGuiListClipper_Begin(&clipper, (int)row_count, -1.0f);
    b32 first_row = 1;
    while (ImGuiListClipper_Step(&clipper)) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            u32 i = rows[r];
            draw_row(console, nv_log_message(i), nv_log_number(i), compact, char_width);
            if (first_row) {
                console_record(console, CONSOLE_RECT_FIRST_ROW);
                first_row = 0;
            }
        }
    }
    if (console->auto_scroll && at_bottom)
        igSetScrollHereY(1.0f);
    console->scroll_y = igGetScrollY();
    console->scroll_max = igGetScrollMaxY();
    igEndChild();
    scratch->used = mark;
    if (clear) {
        nv_log_clear();
        console->selected = 0;
    }

    if (console->selected) {
        NvLogMessage* message = nv_log_find(console->selected - 1);
        NV_ASSERT(message); // checked at the top
        // A message just picked: bring its detail into view, since on a phone it is below what the
        // panel shows.
        b32 scroll_to_detail = console->detail_number != console->selected;
        if (scroll_to_detail) {
            memcpy(console->detail, nv_log_text(message), message->text_size);
            console->detail[message->text_size] = 0;
            console->detail_number = console->selected;
        }
        char repeat[24] = "";
        if (message->repeat > 1)
            snprintf(repeat, sizeof(repeat), " x%u", message->repeat);
        igText("%s %s  %.3f s%s", level_name(message->level), message->source, message->time, repeat);
        // Touch has no keyboard to copy the field with.
        ui_same_line_if_fits(igCalcTextSize("Copy message", NULL, false, -1.0f).x + igGetStyle()->FramePadding.x * 2.0f);
        if (igButton("Copy message", (ImVec2_c){0.0f, 0.0f}))
            igSetClipboardText(console->detail);
        console_record(console, CONSOLE_RECT_COPY_MESSAGE);
        igInputTextMultiline("##detail", console->detail, sizeof(console->detail),
                             (ImVec2_c){-1.0f, line * detail_lines}, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap,
                             NULL, NULL);
        console_record(console, CONSOLE_RECT_DETAIL);
        if (scroll_to_detail)
            igSetScrollHereY(1.0f);
    }

    // Everything up to now has been seen.
    for (u32 level = 0; level < NV_LOG_LEVEL_COUNT; ++level)
        console->seen[level] = log->arrived[level];
}
