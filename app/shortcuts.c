#include "app.h"

#include <cimguizmo.h>
#include <emscripten.h>
#include <stdio.h>
#include <string.h>

// The commands (docs/specs/shortcuts.md, search.md): one table drives the desktop UI's keys, the
// menus' labels, the help window and the command palette's actions. A row without keys is
// palette only.

EM_JS(int, js_is_mac, (void), {
    return /Mac|iPhone|iPad/.test(navigator.platform) ? 1 : 0;
});

// What must hold for a shortcut to apply.
enum {
    WHEN_SHOWCASE = 1 << 0,
    WHEN_EDITING = 1 << 1,   // the showcase, not playing
    WHEN_SELECTION = 1 << 2, // a node is selected
    WHEN_STORAGE = 1 << 3,   // browser storage is available
    WHEN_DESKTOP = 1 << 4,   // the desktop UI (docks and the help window do not exist on the phone)
};

typedef struct Command {
    const char* group;
    const char* name;
    ImGuiKeyChord chords[2]; // 0 = none; a second chord is another way to the same action
    const char* keys_text;   // shown instead of the chords' text (a held modifier, a shifted key)
    u32 when;
    b32 repeat; // fires again while the key is held
    void (*run)(App*);
} Command;

internal void run_save(App* app) { save_now(app, 1); }
internal void run_undo(App* app) { app->undo.request = -1; }
internal void run_redo(App* app) { app->undo.request = 1; }
internal void run_deselect(App* app) { app_view(app)->selected = (NvNodeId){0}; }
internal void run_play(App* app)
{
    if (app->playing)
        app_stop_playing(app);
    else
        app_start_playing(app);
}
internal void run_move(App* app) { app->gizmo_operation = GIZMO_MOVE; }
internal void run_rotate(App* app) { app->gizmo_operation = GIZMO_ROTATE; }
internal void run_scale(App* app) { app->gizmo_operation = GIZMO_SCALE; }
internal void run_axes(App* app) { app->gizmo_local = !app->gizmo_local; }
internal void run_focus(App* app) { app_focus_selection(app); }
internal void run_follow(App* app)
{
    SceneView* view = app_view(app);
    view->follow_selection = !view->follow_selection;
}
internal void run_home(App* app)
{
    SceneView* view = app_view(app);
    view->camera_yaw = view->home_yaw;
    view->camera_pitch = view->home_pitch;
    view->camera_distance = view->home_distance;
    view->orbit_point = view->home_orbit;
    view->pan = nv_vec3(0, 0, 0);
}
internal void run_dock_left(App* app) { app->docks.show_left = !app->docks.show_left; }
internal void run_dock_right(App* app) { app->docks.show_right = !app->docks.show_right; }
internal void run_dock_bottom(App* app)
{
    // Shows the dock, opens it if it is a strip, and hides it when it is already open.
    Docks* docks = &app->docks;
    if (!docks->show_bottom) {
        docks->show_bottom = 1;
        docks->bottom_open = 1;
    } else if (!docks->bottom_open) {
        docks->bottom_open = 1;
    } else {
        docks->show_bottom = 0;
    }
}
internal void run_help(App* app) { app->show_shortcuts = 1; }
internal void run_palette(App* app) { search_open_palette(app); }
internal void run_find(App* app) { search_focus_box(app); }
internal void run_show_save(App* app)
{
    search_set_query(app, SEARCH_VIEW, ""); // the viewer is drawn under the Reset row
    save_show_viewer(app);
    app->docks.show_right = 1;
    app->open_view = 1;
}
internal void run_reset(App* app) { app->request_reset = 1; }
internal void run_scene_showcase(App* app) { app_show_scene(app, SCENE_SHOWCASE); }
internal void run_scene_stress(App* app) { app_show_scene(app, SCENE_STRESS); }
internal void run_textures(App* app)
{
    app->open_textures = 1;
    app->docks.show_right = 1;
}
internal void run_clear_console(App* app)
{
    (void)app;
    nv_log_clear();
}

#define CTRL ImGuiMod_Ctrl
#define SHIFT ImGuiMod_Shift

local_persist const Command shortcuts[SHORTCUT_COUNT] = {
    [SC_SAVE] = {"File", "Save now", {CTRL | ImGuiKey_S}, NULL, WHEN_STORAGE, 0, run_save},
    [SC_UNDO] = {"Edit", "Undo", {CTRL | ImGuiKey_Z}, NULL, WHEN_EDITING, 1, run_undo},
    [SC_REDO] = {"Edit", "Redo", {CTRL | SHIFT | ImGuiKey_Z, CTRL | ImGuiKey_Y}, NULL, WHEN_EDITING, 1, run_redo},
    [SC_DESELECT] = {"Edit", "Clear the selection", {ImGuiKey_Escape}, NULL, WHEN_SELECTION, 0, run_deselect},
    [SC_PLAY] = {"Play", "Play or Stop", {ImGuiKey_Space}, NULL, WHEN_SHOWCASE, 0, run_play},
    [SC_MOVE] = {"Gizmo", "Move", {ImGuiKey_W}, NULL, 0, 0, run_move},
    [SC_ROTATE] = {"Gizmo", "Rotate", {ImGuiKey_E}, NULL, 0, 0, run_rotate},
    [SC_SCALE] = {"Gizmo", "Scale", {ImGuiKey_R}, NULL, 0, 0, run_scale},
    [SC_AXES] = {"Gizmo", "Local or world axes", {ImGuiKey_X}, NULL, 0, 0, run_axes},
    [SC_SNAP_HELD] = {"Gizmo", "Snap, the other way round, while dragging", {0}, "Ctrl (held)", 0, 0, NULL},
    [SC_FOCUS] = {"View", "Move the orbit point to the selection", {ImGuiKey_F}, NULL, WHEN_SELECTION, 0, run_focus},
    [SC_FOLLOW] = {"View", "Camera follows selection on or off", {SHIFT | ImGuiKey_F}, NULL, 0, 0, run_follow},
    [SC_HOME] = {"View", "Back to the start view", {ImGuiKey_Home}, NULL, 0, 0, run_home},
    [SC_DOCK_LEFT] = {"Docks", "Scene dock", {CTRL | ImGuiKey_B}, NULL, WHEN_DESKTOP, 0, run_dock_left},
    [SC_DOCK_RIGHT] = {"Docks", "Inspector dock", {CTRL | ImGuiKey_I}, NULL, WHEN_DESKTOP, 0, run_dock_right},
    [SC_DOCK_BOTTOM] = {"Docks", "Console dock", {ImGuiKey_GraveAccent}, NULL, WHEN_DESKTOP, 0, run_dock_bottom},
    [SC_PALETTE] = {"Find", "Command palette", {CTRL | SHIFT | ImGuiKey_P, ImGuiKey_F1}, NULL, 0, 0, run_palette},
    [SC_FIND] = {"Find", "Search the panel", {CTRL | ImGuiKey_F}, NULL, WHEN_DESKTOP, 0, run_find},
    [SC_HELP] = {"Help", "Keyboard shortcuts", {SHIFT | ImGuiKey_Slash}, "?", WHEN_DESKTOP, 0, run_help},
    [SC_SHOW_SAVE] = {"File", "Show save", {0}, NULL, WHEN_STORAGE, 0, run_show_save},
    [SC_RESET] = {"File", "Reset everything...", {0}, NULL, WHEN_STORAGE, 0, run_reset},
    [SC_SCENE_SHOWCASE] = {"View", "Show the Showcase scene", {0}, NULL, 0, 0, run_scene_showcase},
    [SC_SCENE_STRESS] = {"View", "Show the Stress scene", {0}, NULL, 0, 0, run_scene_stress},
    [SC_TEXTURES] = {"View", "Open the Textures tab", {0}, NULL, 0, 0, run_textures},
    [SC_CLEAR_CONSOLE] = {"View", "Clear the console", {0}, NULL, 0, 0, run_clear_console},
};

internal b32 applies(App* app, u32 when)
{
    b32 showcase = app->shown == SCENE_SHOWCASE;
    if ((when & WHEN_SHOWCASE) && !showcase)
        return 0;
    if ((when & WHEN_EDITING) && !(showcase && !app->playing))
        return 0;
    if ((when & WHEN_SELECTION) && !app_view(app)->selected.index)
        return 0;
    if ((when & WHEN_STORAGE) && !app->storage.available)
        return 0;
    if ((when & WHEN_DESKTOP) && app->ui_mode != UI_DESKTOP)
        return 0;
    return 1;
}

// Nothing fires while a field is being edited, a widget is held (a slider drag), a popup is open
// or the gizmo is being dragged: those own the keys.
internal b32 keys_are_free(void)
{
    ImGuiIO* io = igGetIO_Nil();
    return !io->WantTextInput && !igIsAnyItemActive() && !ImGuizmo_IsUsingAny() &&
           !igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void shortcuts_update(App* app)
{
    if (!keys_are_free())
        return;
    for (u32 i = 0; i < SHORTCUT_COUNT; ++i) {
        const Command* shortcut = &shortcuts[i];
        if (!shortcut->run || !applies(app, shortcut->when))
            continue;
        ImGuiInputFlags flags = ImGuiInputFlags_RouteGlobal | (shortcut->repeat ? ImGuiInputFlags_Repeat : 0);
        for (u32 c = 0; c < 2; ++c) {
            if (shortcut->chords[c] && igShortcut_Nil(shortcut->chords[c], flags)) {
                shortcut->run(app);
                break;
            }
        }
    }
}

b32 shortcuts_claim(void* data, ImGuiKeyChord chord)
{
    App* app = data;
    if (app->ui_mode != UI_DESKTOP || igGetIO_Nil()->WantTextInput)
        return 0;
    // Escape stays the browser's (it leaves full screen).
    if ((chord & ~ImGuiMod_Mask_) == ImGuiKey_Escape)
        return 0;
    for (u32 i = 0; i < SHORTCUT_COUNT; ++i) {
        for (u32 c = 0; c < 2; ++c) {
            if (shortcuts[i].chords[c] == chord)
                return 1;
        }
    }
    return 0;
}

internal const char* key_name(ImGuiKey key, char* out)
{
    if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
        out[0] = (char)('A' + (key - ImGuiKey_A));
        out[1] = 0;
        return out;
    }
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
        out[0] = (char)('0' + (key - ImGuiKey_0));
        out[1] = 0;
        return out;
    }
    switch (key) {
    case ImGuiKey_Escape: return "Esc";
    case ImGuiKey_GraveAccent: return "`";
    case ImGuiKey_Slash: return "/";
    default: return igGetKeyName(key);
    }
}

internal b32 is_mac(void)
{
    local_persist s32 mac = -1;
    if (mac < 0)
        mac = js_is_mac();
    return mac;
}

// "Ctrl+Shift+Z / Ctrl+Y": the modifiers spelled out (the default font has no command symbol), Cmd on
// a Mac.
const char* shortcut_label(ShortcutId id)
{
    local_persist char labels[SHORTCUT_COUNT][64];
    char* out = labels[id];
    const Command* shortcut = &shortcuts[id];
    if (shortcut->keys_text) {
        const char* text = T(shortcut->keys_text);
        if (is_mac() && strncmp(text, "Ctrl", 4) == 0)
            snprintf(out, sizeof(labels[id]), "Cmd%s", text + 4);
        else
            snprintf(out, sizeof(labels[id]), "%s", text);
        return out;
    }
    umm used = 0;
    out[0] = 0;
    for (u32 c = 0; c < 2; ++c) {
        ImGuiKeyChord chord = shortcut->chords[c];
        if (!chord)
            continue;
        char key[8];
        used += (umm)snprintf(out + used, sizeof(labels[id]) - used, "%s%s%s%s%s", used ? " / " : "",
                              (chord & ImGuiMod_Ctrl) ? (is_mac() ? "Cmd+" : "Ctrl+") : "", (chord & ImGuiMod_Shift) ? "Shift+" : "",
                              (chord & ImGuiMod_Alt) ? "Alt+" : "", key_name((ImGuiKey)(chord & ~ImGuiMod_Mask_), key));
    }
    return out;
}

void shortcuts_help(App* app)
{
    local_persist s32 opened_frame;
    const char* help_title = TL("Keyboard shortcuts");
    if (app->show_shortcuts && !igIsPopupOpen_Str(help_title, 0)) {
        igOpenPopup_Str(help_title, 0);
        opened_frame = igGetFrameCount();
    }
    ImGuiViewport* viewport = igGetMainViewport();
    igSetNextWindowPos((ImVec2_c){viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f},
                       ImGuiCond_Appearing, (ImVec2_c){0.5f, 0.5f});
    bool open = true;
    if (igBeginPopupModal(help_title, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGuiIO* io = igGetIO_Nil();
        // Escape and the key that opened it close it.
        // (Not in the frame it opened: the press that opened it is still the frame's press.)
        if (igGetFrameCount() != opened_frame &&
            (igIsKeyPressed_Bool(ImGuiKey_Escape, false) || (io->KeyShift && igIsKeyPressed_Bool(ImGuiKey_Slash, false))))
            open = false;
        const char* group = NULL;
        for (u32 i = 0; i < SHORTCUT_COUNT; ++i) {
            const Command* shortcut = &shortcuts[i];
            if (!shortcut->chords[0] && !shortcut->keys_text)
                continue; // palette only
            if (!group || strcmp(group, shortcut->group) != 0) {
                group = shortcut->group;
                igSeparatorText(T(group));
            }
            // Greyed while it does not apply (no selection, playing, ...).
            igBeginDisabled(!applies(app, shortcut->when));
            igTextUnformatted(shortcut_label((ShortcutId)i), NULL);
            igSameLine(170.0f, 0.0f);
            igTextUnformatted(T(shortcut->name), NULL);
            igEndDisabled();
        }
        igSeparator();
        igTextDisabled(T("Keys do nothing while a text field is being edited or a popup is open."));
        if (!open) {
            app->show_shortcuts = 0;
            igCloseCurrentPopup();
        }
        igEndPopup();
    } else if (app->show_shortcuts) {
        app->show_shortcuts = 0;
    }
}

//
// The command palette's actions: every row that has something to run, except the ones that open
// the palette itself or a box.
//

b32 command_listed(u32 id)
{
    return shortcuts[id].run && id != SC_PALETTE && id != SC_FIND;
}

const char* command_name(u32 id) { return shortcuts[id].name; }
const char* command_group(u32 id) { return shortcuts[id].group; }

b32 command_enabled(App* app, u32 id)
{
    return applies(app, shortcuts[id].when);
}

void command_run(App* app, u32 id)
{
    if (shortcuts[id].run && applies(app, shortcuts[id].when))
        shortcuts[id].run(app);
}
