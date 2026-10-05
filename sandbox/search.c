#include "sandbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Search (docs/specs/search.md): the box at the top of each editor panel, the rows that go through
// it, the Scene tree's filter and the command palette.

#define COLOR_ACCENT 0x4440B4E6u // translucent amber (ABGR)
#define PALETTE_ROWS 12
#define PALETTE_WIDTH 560.0f // CSS pixels
#define PALETTE_MARGIN 12.0f

local_persist const char* panel_names[SEARCH_PANEL_COUNT] = {"Scene", "Inspector", "View", "Textures", "Stress"};

//
// Matching
//

// The lowercase of a code point in the scripts the names and labels use (ASCII, Latin-1 and Latin
// Extended-A, Greek, Cyrillic), which keeps the number of UTF-8 bytes: a folded text has the same
// offsets as the text. Hangul and the other scripts without case map to themselves.
internal u32 fold_code_point(u32 c)
{
    if (c < 0x80)
        return c >= 'A' && c <= 'Z' ? c + 32 : c;
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7))
        return c + 32;
    if (c >= 0x100 && c <= 0x137)
        return (c & 1) ? c : c + 1;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
        return (c & 1) ? c + 1 : c;
    if (c >= 0x14A && c <= 0x177)
        return (c & 1) ? c : c + 1;
    if (c == 0x178)
        return 0xFF;
    if (c == 0x386)
        return 0x3AC;
    if (c >= 0x388 && c <= 0x38A)
        return c + 37;
    if (c == 0x38C)
        return 0x3CC;
    if (c == 0x38E || c == 0x38F)
        return c + 63;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2)
        return c + 32;
    if (c >= 0x410 && c <= 0x42F)
        return c + 32;
    if (c >= 0x400 && c <= 0x40F)
        return c + 80;
    return c;
}

// `text` with every character folded to lowercase, in `out` (cut at a whole character).
internal void fold_text(const char* text, char* out, umm capacity)
{
    umm used = 0;
    while (*text) {
        u32 length = nv_utf8_length((u8)*text);
        if (used + length + 1 > capacity)
            break;
        if (length == 1) {
            out[used++] = (char)fold_code_point((u8)*text);
            ++text;
        } else if (length == 2 && ((u8)text[1] & 0xC0) == 0x80) {
            u32 c = fold_code_point((((u8)text[0] & 0x1Fu) << 6) | ((u8)text[1] & 0x3Fu));
            out[used++] = (char)(0xC0 | (c >> 6));
            out[used++] = (char)(0x80 | (c & 0x3F));
            text += 2;
        } else {
            for (u32 i = 0; i < length && *text; ++i) // 3 and 4 byte characters have no case here
                out[used++] = *text++;
        }
    }
    out[used] = 0;
}

// Where a word may start: the text's start, or after anything that is not a letter or digit (bytes
// of a multibyte character count as letters).
internal b32 is_boundary(char c)
{
    return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (u8)c >= 0x80);
}

// Words are cut at spaces: the ASCII one and the ideographic space (U+3000) Korean keyboards can type.
internal u32 separator_length(const char* p)
{
    if (*p == ' ')
        return 1;
    if ((u8)p[0] == 0xE3 && (u8)p[1] == 0x80 && (u8)p[2] == 0x80)
        return 3;
    return 0;
}

internal void query_parse(SearchQuery* query, const char* text)
{
    query->count = 0;
    const char* p = text;
    while (*p && query->count < SEARCH_MAX_WORDS) {
        u32 skip;
        while ((skip = separator_length(p)))
            p += skip;
        if (!*p)
            break;
        char* word = query->words[query->count];
        char raw[SEARCH_WORD_MAX * 2];
        u32 n = 0;
        while (*p && !separator_length(p)) {
            // Whole characters only: a word cut inside one would match nothing.
            u32 length = nv_utf8_length((u8)*p);
            if (n + length < sizeof(raw)) {
                for (u32 i = 0; i < length && p[i]; ++i)
                    raw[n++] = p[i];
            }
            for (u32 i = 0; i < length && *p; ++i)
                ++p;
        }
        raw[n] = 0;
        fold_text(raw, word, SEARCH_WORD_MAX);
        ++query->count;
    }
}

// The first place `word` (folded) is in `text` (folded), preferring one that starts a word; -1 if none.
internal s32 find_word(const char* text, const char* word)
{
    u32 length = (u32)strlen(word);
    s32 first = -1;
    for (u32 i = 0; text[i]; ++i) {
        u32 k = 0;
        while (k < length && text[i + k] && text[i + k] == word[k])
            ++k;
        if (k == length) {
            if (i == 0 || is_boundary(text[i - 1]))
                return (s32)i;
            if (first < 0)
                first = (s32)i;
        }
    }
    return first;
}

// Whether every word is in the text, ignoring case in the scripts that have it. `score` is the
// number of words that matched in the middle of a word: lower ranks higher.
typedef struct QueryMatch {
    b32 ok;
    u32 score;
} QueryMatch;

internal QueryMatch query_match(const SearchQuery* query, const char* original)
{
    char text[256];
    fold_text(original, text, sizeof(text));
    u32 middle = 0;
    for (u32 w = 0; w < query->count; ++w) {
        s32 at = find_word(text, query->words[w]);
        if (at < 0)
            return (QueryMatch){0};
        if (at > 0 && !is_boundary(text[at - 1]))
            ++middle;
    }
    return (QueryMatch){.ok = 1, .score = middle};
}

// Appends `text` to `out`, after a space when there is something before it; whole characters only.
internal void append_text(char* out, umm capacity, umm* used, const char* text, b32 cut_id)
{
    if (!text || !text[0])
        return;
    if (*used && *used + 1 < capacity)
        out[(*used)++] = ' ';
    while (*text) {
        if (cut_id && text[0] == '#' && text[1] == '#')
            break;
        u32 length = nv_utf8_length((u8)*text);
        if (*used + length + 1 > capacity)
            break;
        for (u32 i = 0; i < length && *text; ++i)
            out[(*used)++] = *text++;
    }
    out[*used] = 0;
}

// The label's text without its "##id" part, cut at a whole character.
internal void copy_label(char* out, umm capacity, const char* label)
{
    umm used = 0;
    out[0] = 0;
    append_text(out, capacity, &used, label, 1);
}

// Label, section and keywords as one candidate, with the Korean of the label and the section too,
// so a search finds a row in either language; the label's "##id" part is left out.
internal void candidate_text(char* out, umm capacity, const char* label, const char* section, const char* keywords)
{
    umm used = 0;
    out[0] = 0;
    char plain[96];
    plain[0] = 0;
    if (label) {
        copy_label(plain, sizeof(plain), label);
    }
    append_text(out, capacity, &used, plain, 0);
    append_text(out, capacity, &used, nv_strings_find_korean(plain), 0);
    append_text(out, capacity, &used, section, 0);
    if (section && section[0])
        append_text(out, capacity, &used, nv_strings_find_korean(section), 0);
    append_text(out, capacity, &used, keywords, 0);
}


//
// Marking the matched parts
//

// Draws a translucent box behind each query word found in `text`, which is drawn at `origin`.
void search_mark(Sandbox* sandbox, const char* text, ImVec2_c origin, f32 height)
{
    const SearchQuery* query = &sandbox->search.query;
    ImDrawList* draw = igGetWindowDrawList();
    char folded[256];
    fold_text(text, folded, sizeof(folded)); // the same offsets as `text`
    for (u32 w = 0; w < query->count; ++w) {
        s32 at = find_word(folded, query->words[w]);
        if (at < 0)
            continue;
        u32 length = (u32)strlen(query->words[w]);
        f32 x0 = origin.x + igCalcTextSize(text, text + at, false, -1.0f).x;
        f32 x1 = x0 + igCalcTextSize(text + at, text + at + length, false, -1.0f).x;
        ImDrawList_AddRectFilled(draw, (ImVec2_c){x0, origin.y}, (ImVec2_c){x1, origin.y + height}, COLOR_ACCENT, 2.0f, 0);
    }
}

// Marks the label of the row just drawn: its widget's label sits at the right end of its rectangle.
internal void flush_mark(Sandbox* sandbox)
{
    Search* s = &sandbox->search;
    if (!s->pending_highlight)
        return;
    s->pending_highlight = 0;
    f32 width = igCalcTextSize(s->pending_label, NULL, false, -1.0f).x;
    ImVec2_c min = igGetItemRectMin(), max = igGetItemRectMax();
    f32 pad = igGetStyle()->FramePadding.y;
    search_mark(sandbox, s->pending_label, (ImVec2_c){max.x - width, min.y + pad}, igGetFontSize());
}

//
// Rows
//

void search_frame(Sandbox* sandbox)
{
    Search* s = &sandbox->search;
    s->hover_panel = -1;
    s->right_panel = -1;
    s->tree_filtering = 0;
}

b32 search_active(Sandbox* sandbox)
{
    Search* s = &sandbox->search;
    return !s->collecting && s->queries[s->panel][0];
}

b32 search_plain(Sandbox* sandbox)
{
    Search* s = &sandbox->search;
    return !s->collecting && !s->queries[s->panel][0];
}

b32 search_match(Sandbox* sandbox, const char* text)
{
    Search* s = &sandbox->search;
    if (!s->queries[s->panel][0])
        return 1;
    char candidate[200];
    candidate_text(candidate, sizeof(candidate), text, s->section, NULL);
    return query_match(&s->query, candidate).ok;
}

void search_set_query(Sandbox* sandbox, SearchPanel panel, const char* text)
{
    snprintf(sandbox->search.queries[panel], SEARCH_QUERY_MAX, "%s", text);
    nv_utf8_trim(sandbox->search.queries[panel]);
}

void search_section(Sandbox* sandbox, const char* heading)
{
    Search* s = &sandbox->search;
    if (!s->collecting)
        flush_mark(sandbox);
    snprintf(s->section, sizeof(s->section), "%s", heading ? heading : "");
    nv_utf8_trim(s->section);
    s->section_pending = heading != NULL;
}

internal b32 row(Sandbox* sandbox, const char* label, const char* keywords, b32 mark)
{
    Search* s = &sandbox->search;
    if (s->collecting) {
        if (s->setting_count < SEARCH_MAX_SETTINGS) {
            SearchSetting* setting = &s->settings[s->setting_count++];
            setting->panel = (u8)s->panel;
            copy_label(setting->label, sizeof(setting->label), label);
            snprintf(setting->section, sizeof(setting->section), "%s", s->section);
            nv_utf8_trim(setting->section);
            snprintf(setting->keywords, sizeof(setting->keywords), "%s", keywords ? keywords : "");
            nv_utf8_trim(setting->keywords);
        }
        return 0;
    }
    flush_mark(sandbox);
    const char* query = s->queries[s->panel];
    if (query[0]) {
        char candidate[200];
        candidate_text(candidate, sizeof(candidate), label, s->section, keywords);
        if (!query_match(&s->query, candidate).ok)
            return 0;
    }
    if (s->section_pending) {
        igSeparatorText(T(s->section));
        s->section_pending = 0;
    }
    ++s->rows_now[s->panel];
    if (mark && query[0]) {
        copy_label(s->pending_label, sizeof(s->pending_label), label);
        s->pending_highlight = 1;
    }
    return 1;
}

b32 search_row(Sandbox* sandbox, const char* label, const char* keywords)
{
    return row(sandbox, label, keywords, 1);
}

b32 search_group(Sandbox* sandbox, const char* label, const char* keywords)
{
    return row(sandbox, label, keywords, 0);
}

//
// The box and the panel's child window
//

void search_panel_begin(Sandbox* sandbox, SearchPanel panel)
{
    Search* s = &sandbox->search;
    s->panel = panel;
    s->section[0] = 0;
    s->section_pending = 0;
    s->pending_highlight = 0;
    s->rows_now[panel] = 0;
    query_parse(&s->query, s->queries[panel]);
    if (s->collecting)
        return;

    char* query = s->queries[panel];
    ImVec2_c top = igGetCursorScreenPos();
    igPushID_Int((int)panel);
    f32 clear_width = igGetFrameHeight();
    f32 spacing = igGetStyle()->ItemSpacing.x;
    igSetNextItemWidth(igGetContentRegionAvail().x - (query[0] ? clear_width + spacing : 0.0f));
    if (s->focus_panel == (s32)panel + 1) {
        igSetKeyboardFocusHere(0);
        s->focus_panel = 0;
    }
    // Escape clears the box and leaves it; Enter leaves it (ImGui deactivates a field on both).
    ImGuiID id = igGetID_Str("##search");
    b32 was_active = igGetActiveID() == id;
    igInputTextWithHint("##search", T("Search"), query, SEARCH_QUERY_MAX, 0, NULL, NULL);
    if (was_active && igIsKeyPressed_Bool(ImGuiKey_Escape, false))
        query[0] = 0;
    if (query[0]) {
        igSameLine(0.0f, -1.0f);
        if (igButton("x##clear", (ImVec2_c){clear_width, 0.0f}))
            query[0] = 0;
        igSetItemTooltip("%s", T("Clear the search"));
    }
    igPopID();
    query_parse(&s->query, query);

    // Ctrl+F looks for the panel under the pointer.
    ImVec2_c avail = igGetContentRegionAvail();
    ImVec2_c bottom = {top.x + avail.x, igGetCursorScreenPos().y + avail.y};
    if (igIsMouseHoveringRect(top, bottom, false))
        s->hover_panel = (s32)panel;
    if (panel != SEARCH_SCENE && panel != SEARCH_STRESS)
        s->right_panel = (s32)panel;

    igPushID_Int((int)panel + 100);
    igBeginChild_Str("##content", (ImVec2_c){0.0f, 0.0f}, 0, 0);
    nv_imgui_touch_scroll(&sandbox->imgui);
}

void search_panel_end(Sandbox* sandbox)
{
    Search* s = &sandbox->search;
    if (s->collecting)
        return;
    flush_mark(sandbox);
    if (s->queries[s->panel][0] && s->rows_now[s->panel] == 0)
        igTextDisabled(T("No match for '%s'"), s->queries[s->panel]);
    igEndChild();
    igPopID();
    s->rows[s->panel] = s->rows_now[s->panel];
}

void search_focus_box(Sandbox* sandbox)
{
    Search* s = &sandbox->search;
    if (s->hover_panel >= 0)
        s->focus_panel = s->hover_panel + 1;
    else if (s->right_panel >= 0)
        s->focus_panel = s->right_panel + 1;
    else
        s->focus_panel = SEARCH_SCENE + 1;
}

//
// The Scene tree's filter, used by node_tree in ui.c through these (see ui_scene_tab).
//

//
// The command palette
//

void search_open_palette(Sandbox* sandbox)
{
    sandbox->search.palette_request = 1;
}

internal u64 make_key(b32 disabled, u32 score, u32 length, u32 kind, u32 seq)
{
    if (length > 255)
        length = 255;
    if (score > 255)
        score = 255;
    return ((u64)(disabled ? 1 : 0) << 56) | ((u64)score << 48) | ((u64)length << 40) | ((u64)kind << 32) | seq;
}

internal int compare_results(const void* a, const void* b)
{
    u64 ka = ((const PaletteResult*)a)->key, kb = ((const PaletteResult*)b)->key;
    return ka < kb ? -1 : ka > kb ? 1 : 0;
}

internal void add_result(Search* s, PaletteKind kind, b32 enabled, u32 index, u64 key)
{
    if (s->result_count >= SEARCH_MAX_RESULTS)
        return;
    s->results[s->result_count++] = (PaletteResult){.kind = (u8)kind, .enabled = enabled, .index = index, .key = key};
}

// Everything the query finds: actions, settings of the panels, nodes of the shown scene.
internal void build_results(Sandbox* sandbox)
{
    Search* s = &sandbox->search;
    s->result_count = 0;
    SearchQuery query;
    query_parse(&query, s->palette_query);

    if (!query.count) {
        // The last actions run from here, then every action in table order.
        for (u32 i = 0; i < s->recent_count; ++i) {
            u32 id = (u32)s->recent[i];
            add_result(s, PALETTE_ACTION, command_enabled(sandbox, id), id, make_key(!command_enabled(sandbox, id), 0, 0, 0, i));
        }
        for (u32 id = 0; id < SHORTCUT_COUNT; ++id) {
            if (!command_listed(id))
                continue;
            b32 recent = 0;
            for (u32 i = 0; i < s->recent_count; ++i)
                recent |= (u32)s->recent[i] == id;
            if (!recent)
                add_result(s, PALETTE_ACTION, command_enabled(sandbox, id), id, make_key(!command_enabled(sandbox, id), 1, 0, 0, id));
        }
        qsort(s->results, s->result_count, sizeof(PaletteResult), compare_results);
        return;
    }

    char text[200];
    for (u32 id = 0; id < SHORTCUT_COUNT; ++id) {
        if (!command_listed(id))
            continue;
        candidate_text(text, sizeof(text), command_name(id), command_group(id), NULL);
        QueryMatch match = query_match(&query, text);
        if (match.ok)
            add_result(s, PALETTE_ACTION, command_enabled(sandbox, id), id,
                       make_key(!command_enabled(sandbox, id), match.score, (u32)strlen(command_name(id)), PALETTE_ACTION, id));
    }
    for (u32 i = 0; i < s->setting_count; ++i) {
        const SearchSetting* setting = &s->settings[i];
        candidate_text(text, sizeof(text), setting->label, setting->section, setting->keywords);
        QueryMatch match = query_match(&query, text);
        if (match.ok)
            add_result(s, PALETTE_SETTING, 1, i, make_key(0, match.score, (u32)strlen(setting->label), PALETTE_SETTING, i));
    }

    // Nodes: the best SEARCH_MAX_NODES by rank, found without sorting thousands.
    NvScene* scene = sandbox_view(sandbox)->scene;
    PaletteResult best[SEARCH_MAX_NODES];
    u32 best_count = 0, matched = 0;
    for (u32 i = 1; i <= scene->node_count; ++i) {
        const NvNode* node = &scene->nodes[i];
        if (!(node->gen & 1))
            continue;
        QueryMatch match = query_match(&query, node->name);
        if (!match.ok)
            continue;
        ++matched;
        u64 key = make_key(0, match.score, (u32)strlen(node->name), PALETTE_NODE, i);
        if (best_count == SEARCH_MAX_NODES && key >= best[best_count - 1].key)
            continue;
        u32 at = best_count < SEARCH_MAX_NODES ? best_count++ : SEARCH_MAX_NODES - 1;
        while (at > 0 && best[at - 1].key > key) {
            best[at] = best[at - 1];
            --at;
        }
        best[at] = (PaletteResult){.kind = PALETTE_NODE, .enabled = 1, .index = i, .key = key};
    }
    for (u32 i = 0; i < best_count; ++i)
        add_result(s, PALETTE_NODE, 1, best[i].index, best[i].key);
    qsort(s->results, s->result_count, sizeof(PaletteResult), compare_results);
    if (matched > best_count)
        add_result(s, PALETTE_MORE, 0, matched - best_count, ~0ull);
}

internal void close_palette(Search* s)
{
    s->palette_open = 0;
    igCloseCurrentPopup();
}

internal void run_result(Sandbox* sandbox, const PaletteResult* result, b32 shift)
{
    Search* s = &sandbox->search;
    if (!result->enabled)
        return;
    if (result->kind == PALETTE_ACTION) {
        u32 id = result->index;
        // Most recent first, without repeats.
        u32 at = 0;
        while (at < s->recent_count && (u32)s->recent[at] != id)
            ++at;
        if (at == s->recent_count && s->recent_count < SEARCH_RECENT)
            ++s->recent_count;
        if (at >= s->recent_count)
            at = s->recent_count - 1;
        for (; at > 0; --at)
            s->recent[at] = s->recent[at - 1];
        s->recent[0] = (s32)id;
        close_palette(s);
        command_run(sandbox, id);
    } else if (result->kind == PALETTE_NODE) {
        SceneView* view = sandbox_view(sandbox);
        selection_set(view, (NvNodeId){result->index, view->scene->nodes[result->index].gen});
        sandbox->open_inspector = 1;
        if (sandbox->ui_mode == UI_DESKTOP)
            sandbox->docks.show_right = 1;
        if (shift)
            sandbox_focus_selection(sandbox);
        close_palette(s);
    } else if (result->kind == PALETTE_SETTING) {
        // Shows the row's tab with its label typed into the panel's box, so it is the first thing there.
        const SearchSetting* setting = &s->settings[result->index];
        search_set_query(sandbox, (SearchPanel)setting->panel, setting->label);
        if (setting->panel == SEARCH_INSPECTOR)
            sandbox->open_inspector = 1;
        else if (setting->panel == SEARCH_VIEW)
            sandbox->open_view = 1;
        else if (setting->panel == SEARCH_STRESS)
            sandbox->open_stress = 1;
        if (sandbox->ui_mode == UI_DESKTOP && setting->panel != SEARCH_STRESS)
            sandbox->docks.show_right = 1;
        close_palette(s);
    }
}

internal void draw_result_row(Sandbox* sandbox, u32 i, f32 width, f32 row_height)
{
    Search* s = &sandbox->search;
    const PaletteResult* result = &s->results[i];
    igPushID_Int((int)i);
    ImVec2_c pos = igGetCursorScreenPos();
    b32 highlighted = (s32)i == s->highlight;
    igBeginDisabled(!result->enabled && result->kind != PALETTE_MORE);
    b32 clicked = igSelectable_Bool("##row", highlighted, result->kind == PALETTE_MORE ? ImGuiSelectableFlags_Disabled : 0,
                                    (ImVec2_c){width, row_height});
    igEndDisabled();
    if (clicked)
        run_result(sandbox, result, igGetIO_Nil()->KeyShift);
    if (highlighted && s->scroll_to_highlight)
        igSetScrollHereY(0.5f);

    char name[64] = "", right[96] = "", kind[16] = "";
    switch (result->kind) {
    case PALETTE_ACTION:
        snprintf(kind, sizeof(kind), "%s", T("Action"));
        snprintf(name, sizeof(name), "%s", T(command_name(result->index)));
        snprintf(right, sizeof(right), "%s", shortcut_label((ShortcutId)result->index));
        break;
    case PALETTE_SETTING: {
        const SearchSetting* setting = &s->settings[result->index];
        snprintf(kind, sizeof(kind), "%s", T("Setting"));
        snprintf(name, sizeof(name), "%s", T(setting->label));
        if (setting->section[0])
            snprintf(right, sizeof(right), "%s > %s", T(panel_names[setting->panel]), T(setting->section));
        else
            snprintf(right, sizeof(right), "%s", T(panel_names[setting->panel]));
        break;
    }
    case PALETTE_NODE: {
        const NvScene* scene = sandbox_view(sandbox)->scene;
        const NvNode* node = &scene->nodes[result->index];
        snprintf(kind, sizeof(kind), "%s", T("Node"));
        snprintf(name, sizeof(name), "%s", node->name);
        if (node->parent)
            snprintf(right, sizeof(right), "%s", scene->nodes[node->parent].name);
        break;
    }
    case PALETTE_MORE:
        snprintf(name, sizeof(name), T("and %u more: keep typing"), result->index);
        break;
    }

    nv_utf8_trim(name);
    nv_utf8_trim(right);
    ImDrawList* draw = igGetWindowDrawList();
    ImU32 dim = igGetColorU32_Col(ImGuiCol_TextDisabled, 1.0f);
    ImU32 text = igGetColorU32_Col(result->enabled || result->kind == PALETTE_MORE ? ImGuiCol_Text : ImGuiCol_TextDisabled, 1.0f);
    f32 pad = igGetStyle()->FramePadding.x;
    f32 text_y = pos.y + (row_height - igGetFontSize()) * 0.5f;
    f32 kind_width = igCalcTextSize(T("Setting"), NULL, false, -1.0f).x + pad * 2.0f;
    f32 right_width = igCalcTextSize(right, NULL, false, -1.0f).x;
    if (right_width > width * 0.4f)
        right_width = width * 0.4f;
    ImDrawList_AddText_Vec2(draw, (ImVec2_c){pos.x + pad, text_y}, dim, kind, NULL);
    f32 name_x = pos.x + pad + kind_width;
    f32 name_end = pos.x + width - pad * 2.0f - right_width;
    igPushClipRect((ImVec2_c){name_x, pos.y}, (ImVec2_c){name_end, pos.y + row_height}, true);
    if (result->kind != PALETTE_MORE) {
        SearchQuery query;
        query_parse(&query, s->palette_query);
        SearchQuery saved = s->query;
        s->query = query;
        search_mark(sandbox, name, (ImVec2_c){name_x, text_y}, igGetFontSize());
        s->query = saved;
    }
    ImDrawList_AddText_Vec2(draw, (ImVec2_c){name_x, text_y}, text, name, NULL);
    igPopClipRect();
    igPushClipRect((ImVec2_c){pos.x + width - pad - right_width, pos.y}, (ImVec2_c){pos.x + width, pos.y + row_height}, true);
    ImDrawList_AddText_Vec2(draw, (ImVec2_c){pos.x + width - pad - right_width, text_y}, dim, right, NULL);
    igPopClipRect();
    igPopID();
}

// Notes the settings of the searchable panels as they are now, by running them without drawing.
internal void collect_settings(Sandbox* sandbox)
{
    Search* s = &sandbox->search;
    s->setting_count = 0;
    s->collecting = 1;
    igSetNextWindowPos((ImVec2_c){-4000.0f, -4000.0f}, ImGuiCond_Always, (ImVec2_c){0.0f, 0.0f});
    igSetNextWindowSize((ImVec2_c){400.0f, 400.0f}, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav;
    if (igBegin("##search collect", NULL, flags)) {
        ui_inspector_tab(sandbox);
        ui_view_tab(sandbox);
        if (sandbox->shown == SCENE_STRESS)
            stress_ui(sandbox);
    }
    igEnd();
    s->collecting = 0;
}

void search_palette(Sandbox* sandbox)
{
    Search* s = &sandbox->search;
    local_persist const char* title = "Command palette";
    if (s->palette_request) {
        s->palette_request = 0;
        if (!igIsPopupOpen_Str(title, 0)) {
            igOpenPopup_Str(title, 0);
            s->palette_open = 1;
            s->palette_focus = 1;
            s->palette_opened_frame = igGetFrameCount();
            s->palette_query[0] = 0;
            s->highlight = 0;
            s->scroll_to_top = 1;
        }
    }
    if (!s->palette_open)
        return;
    collect_settings(sandbox);
    build_results(sandbox);

    ImGuiViewport* viewport = igGetMainViewport();
    f32 ratio = igGetIO_Nil()->DisplayFramebufferScale.x;
    f32 scale = sandbox->imgui.ui_scale;
    f32 width = PALETTE_WIDTH * scale;
    if (width > viewport->Size.x - PALETTE_MARGIN * 2.0f)
        width = viewport->Size.x - PALETTE_MARGIN * 2.0f;
    f32 top = (f32)(sandbox->layout.top_bar.y + sandbox->layout.top_bar.height) / ratio + 8.0f;
    ImGuiStyle* style = igGetStyle();
    f32 row_height = igGetFrameHeight();
    u32 shown_rows = s->result_count < PALETTE_ROWS ? s->result_count : PALETTE_ROWS;
    f32 list_height = (f32)shown_rows * (row_height + style->ItemSpacing.y);
    f32 inner = width - style->WindowPadding.x * 2.0f;
    f32 height = style->WindowPadding.y * 2.0f + row_height + style->ItemSpacing.y + list_height;
    igSetNextWindowPos((ImVec2_c){viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + top}, ImGuiCond_Always,
                       (ImVec2_c){0.5f, 0.0f});
    igSetNextWindowSize((ImVec2_c){width, height}, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!igBeginPopupModal(title, NULL, flags)) {
        s->palette_open = 0;
        return;
    }

    // Typing anywhere goes to the box; the arrows move the highlight.
    if (s->palette_focus) {
        igSetKeyboardFocusHere(0);
        s->palette_focus = 0;
    }
    igSetNextItemWidth(inner);
    if (igInputTextWithHint("##palette", T("Type a command, node or setting"), s->palette_query, SEARCH_QUERY_MAX, 0, NULL, NULL)) {
        s->highlight = 0;
        s->scroll_to_top = 1;
        build_results(sandbox);
    }
    s32 count = (s32)s->result_count;
    s32 page = PALETTE_ROWS - 1;
    s32 step = 0;
    if (igIsKeyPressed_Bool(ImGuiKey_DownArrow, true))
        step = 1;
    if (igIsKeyPressed_Bool(ImGuiKey_UpArrow, true))
        step = -1;
    if (igIsKeyPressed_Bool(ImGuiKey_PageDown, true))
        step = page;
    if (igIsKeyPressed_Bool(ImGuiKey_PageUp, true))
        step = -page;
    if (step && count) {
        // Arrows wrap; pages stop at the ends. "and N more" is not a row to land on.
        s32 selectable = count - (s->results[count - 1].kind == PALETTE_MORE ? 1 : 0);
        if (selectable > 0) {
            s32 next = s->highlight + step;
            if (step == 1 || step == -1)
                next = (next % selectable + selectable) % selectable;
            else
                next = next < 0 ? 0 : next >= selectable ? selectable - 1 : next;
            s->highlight = next;
            s->scroll_to_highlight = 1;
        }
    }
    if (s->highlight >= count)
        s->highlight = count ? count - 1 : 0;
    if ((igIsKeyPressed_Bool(ImGuiKey_Enter, false) || igIsKeyPressed_Bool(ImGuiKey_KeypadEnter, false)) && count)
        run_result(sandbox, &s->results[s->highlight], igGetIO_Nil()->KeyShift);

    b32 close = igGetFrameCount() != s->palette_opened_frame &&
                (igIsKeyPressed_Bool(ImGuiKey_Escape, false) ||
                 (igIsMouseClicked_Bool(ImGuiMouseButton_Left, false) && !igIsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)));
    if (count) {
        if (igBeginChild_Str("##results", (ImVec2_c){inner, list_height}, 0, ImGuiWindowFlags_NoScrollWithMouse * 0)) {
            nv_imgui_touch_scroll(&sandbox->imgui);
            if (s->scroll_to_top) {
                igSetScrollY_Float(0.0f);
                s->scroll_to_top = 0;
            }
            f32 row_width = inner - (s->result_count > PALETTE_ROWS ? style->ScrollbarSize : 0.0f);
            for (u32 i = 0; i < s->result_count; ++i)
                draw_result_row(sandbox, i, row_width, row_height);
            s->scroll_to_highlight = 0;
        }
        igEndChild();
    } else {
        igTextDisabled(T("No match"));
    }
    if (close && s->palette_open)
        close_palette(s);
    igEndPopup();
}
