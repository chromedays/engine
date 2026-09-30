#include "app.h"

#include <stdio.h>

// The desktop UI (docs/specs/layout.md): a top bar with the menus and the centered Play / Stop,
// docks left (Scene), right (Inspector, View) and below (Console, Stress), and the scene viewport
// in the middle.

#define DESKTOP_TOP_BAR 28.0f     // CSS pixels
#define DESKTOP_STRIP 28.0f       // the bottom dock while collapsed
#define DESKTOP_SPLITTER 6.0f     // the hit area of a border, inside the dock
#define DESKTOP_PLAY_WIDTH 84.0f
#define DESKTOP_MIN_VIEWPORT_SHARE 0.4f // of the canvas width and of the height under the top bar
#define DESKTOP_MIN_VIEWPORT 64.0f      // never less than this, however small the window

typedef enum DockSplitter {
    SPLITTER_NONE,
    SPLITTER_LEFT,
    SPLITTER_RIGHT,
    SPLITTER_BOTTOM,
} DockSplitter;

internal f32 clampf(f32 v, f32 lo, f32 hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// The docks' sizes for a window: the wanted ones, kept inside the splitters' range, then shrunk so
// the viewport keeps a share of the window (the right dock gives way first), and never less than
// a few pixels.
internal void dock_sizes(const Docks* docks, f32 width, f32 height, f32* left, f32* right, f32* bottom)
{
    f32 area = height - DESKTOP_TOP_BAR;
    f32 l = docks->show_left ? clampf(docks->left_width, DOCK_LEFT_MIN, DOCK_SIDE_MAX) : 0.0f;
    f32 r = docks->show_right ? clampf(docks->right_width, DOCK_RIGHT_MIN, DOCK_SIDE_MAX) : 0.0f;
    f32 room = width * (1.0f - DESKTOP_MIN_VIEWPORT_SHARE);
    if (l + r > room) {
        r = clampf(room - l, docks->show_right ? DOCK_RIGHT_MIN : 0.0f, r);
        if (l + r > room)
            l = clampf(room - r, docks->show_left ? DOCK_LEFT_MIN : 0.0f, l);
    }
    f32 most = width - DESKTOP_MIN_VIEWPORT;
    if (most < 0.0f)
        most = 0.0f;
    if (l + r > most) {
        f32 scale = most / (l + r);
        l *= scale;
        r *= scale;
    }

    f32 b = 0.0f;
    if (docks->show_bottom) {
        b = docks->bottom_open ? clampf(docks->bottom_height, DOCK_BOTTOM_MIN, DOCK_BOTTOM_MAX) : DESKTOP_STRIP;
        f32 limit = area * (1.0f - DESKTOP_MIN_VIEWPORT_SHARE);
        if (docks->bottom_open && b > limit)
            b = limit > DOCK_BOTTOM_MIN ? limit : DOCK_BOTTOM_MIN;
        f32 tall = area - DESKTOP_MIN_VIEWPORT;
        if (b > tall)
            b = tall > 0.0f ? tall : 0.0f;
    }
    *left = l;
    *right = r;
    *bottom = b;
}

void desktop_layout(App* app, f32 width, f32 height, f32 ratio)
{
    Docks* docks = &app->docks;
    // A message for a tab that lives in the bottom dock shows the dock.
    if (app->open_console || app->open_stress) {
        docks->show_bottom = 1;
        docks->bottom_open = 1;
    }
    f32 left, right, bottom;
    dock_sizes(docks, width, height, &left, &right, &bottom);
    f32 top = DESKTOP_TOP_BAR;
    f32 middle = height - bottom;
    app->layout = (Layout){
        .top_bar = ui_rect(0.0f, 0.0f, width, top, ratio),
        .left = ui_rect(0.0f, top, left, middle, ratio),
        .right = ui_rect(width - right, top, width, middle, ratio),
        .bottom = ui_rect(0.0f, middle, width, height, ratio),
        .viewport = ui_rect(left, top, width - right, middle, ratio),
    };
}

//
// Top bar
//

internal void file_menu(App* app, b32* open_reset)
{
    if (!igBeginMenu("File", true))
        return;
    b32 storage = app->storage.available;
    if (igMenuItem_Bool("Save now", NULL, false, storage))
        save_now(app, 1);
    if (igMenuItem_Bool("Show save", NULL, app->show_save, storage)) {
        if (app->show_save) {
            app->show_save = false;
        } else {
            save_show_viewer(app);
            app->docks.show_right = 1;
            app->open_view = 1; // the viewer is in the View tab
        }
    }
    igSeparator();
    if (igMenuItem_Bool("Reset...", NULL, false, storage))
        *open_reset = 1;
    igEndMenu();
}

internal void view_menu(App* app)
{
    if (!igBeginMenu("View", true))
        return;
    Docks* docks = &app->docks;
    igMenuItem_BoolPtr("Scene dock", NULL, (bool*)&docks->show_left, true);
    igMenuItem_BoolPtr("Inspector dock", NULL, (bool*)&docks->show_right, true);
    igMenuItem_BoolPtr("Console dock", NULL, (bool*)&docks->show_bottom, true);
    igSeparator();
    local_persist const char* scenes[SCENE_COUNT] = {"Showcase scene", "Stress scene"};
    for (u32 scene = 0; scene < SCENE_COUNT; ++scene) {
        if (igMenuItem_Bool(scenes[scene], NULL, app->shown == (SceneKind)scene, true))
            app_show_scene(app, (SceneKind)scene);
    }
    igEndMenu();
}

internal void top_bar(App* app)
{
    b32 tint = ui_push_play_tint(app);
    // The menu bar is as high as a frame, so the frame padding makes it the top bar's height.
    igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2_c){8.0f, (DESKTOP_TOP_BAR - igGetFontSize()) * 0.5f});
    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    bool open = nv_imgui_begin_panel_ex(&app->imgui, "Top bar", app->layout.top_bar, flags);
    ui_pop_play_tint(tint);
    b32 open_reset = 0;
    if (open && igBeginMenuBar()) {
        file_menu(app, &open_reset);
        if (igBeginMenu("Edit", true)) {
            undo_menu_items(app);
            igEndMenu();
        }
        view_menu(app);

        // The Play button's center is the screen's center.
        f32 width = igGetWindowWidth();
        igSetCursorPosX((width - DESKTOP_PLAY_WIDTH) * 0.5f);
        ui_play_button(app, (ImVec2_c){DESKTOP_PLAY_WIDTH, 0.0f});
        if (app->play_box[2] > app->play_box[0]) {
            igSetItemTooltip("%s", app->playing ? "Stop and restore the scene (Space)" : "Run the scene (Space)");
            if (app->playing) {
                igSameLine(0.0f, -1.0f);
                igTextColored((ImVec4_c){0.55f, 0.85f, 1.0f, 1.0f}, "Playing: edits are lost on Stop.");
            }
        }

        char status[64];
        snprintf(status, sizeof(status), "%.1f ms  %s", 1000.0f / igGetIO_Nil()->Framerate,
                 app->shown == SCENE_SHOWCASE ? "Showcase" : "Stress");
        igSetCursorPosX(width - igCalcTextSize(status, NULL, false, -1.0f).x - 12.0f);
        igTextDisabled("%s", status);
        igEndMenuBar();
    }
    igPopStyleVar(1);
    // NOTE: A popup opened from inside a menu would live in the menu's ID scope; this one is opened
    // and drawn at the window's top level, where save_reset_popup looks for it.
    if (open_reset)
        igOpenPopup_Str("Reset everything?", 0);
    if (open)
        save_reset_popup(app);
    igEnd();
}

//
// Docks
//

internal void left_dock(App* app)
{
    if (!app->docks.show_left)
        return;
    b32 tint = ui_push_play_tint(app);
    bool open = nv_imgui_begin_panel(&app->imgui, "Scene dock", app->layout.left);
    ui_pop_play_tint(tint);
    if (open && igBeginTabBar("left tabs", 0)) {
        if (igBeginTabItem("Scene", NULL, 0)) {
            ui_scene_tab(app);
            igEndTabItem();
        }
        igEndTabBar();
    }
    igEnd();
}

internal void right_dock(App* app)
{
    if (!app->docks.show_right)
        return;
    b32 tint = ui_push_play_tint(app);
    bool open = nv_imgui_begin_panel(&app->imgui, "Inspector dock", app->layout.right);
    ui_pop_play_tint(tint);
    if (open && igBeginTabBar("right tabs", 0)) {
        // NOTE: Picking a node jumps to the Inspector, since that is where it is edited.
        ImGuiTabItemFlags inspector_flags = app->open_inspector ? ImGuiTabItemFlags_SetSelected : 0;
        app->open_inspector = 0;
        if (igBeginTabItem("Inspector", NULL, inspector_flags)) {
            ui_inspector_tab(app);
            igEndTabItem();
        }
        ImGuiTabItemFlags view_flags = app->open_view ? ImGuiTabItemFlags_SetSelected : 0;
        app->open_view = 0;
        if (igBeginTabItem("View", NULL, view_flags)) {
            ui_view_tab(app);
            igEndTabItem();
        }
        igEndTabBar();
    }
    igEnd();
}

internal void bottom_dock(App* app)
{
    Docks* docks = &app->docks;
    if (!docks->show_bottom)
        return;
    b32 tint = ui_push_play_tint(app);
    // The collapsed strip has room for one row of buttons only.
    if (!docks->bottom_open)
        igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2_c){8.0f, 4.0f});
    bool open = nv_imgui_begin_panel(&app->imgui, "Console dock", app->layout.bottom);
    if (!docks->bottom_open)
        igPopStyleVar(1);
    ui_pop_play_tint(tint);
    if (open && !docks->bottom_open) {
        if (igButton("Show Console###bottom", (ImVec2_c){0.0f, 0.0f}))
            docks->bottom_open = 1;
        NvLogLevel worst;
        u32 unseen = console_unseen(app, &worst);
        if (unseen) {
            igSameLine(0.0f, -1.0f);
            igTextColored(igColorConvertU32ToFloat4(console_level_color(worst)), "%u new", unseen);
        }
    } else if (open) {
        // The Hide button sits at the tab strip's right end, drawn before the tabs so they keep the cursor.
        ImVec2_c start = igGetCursorPos();
        f32 hide_width = igCalcTextSize("Hide", NULL, false, -1.0f).x + igGetStyle()->FramePadding.x * 2.0f;
        igSetCursorPos((ImVec2_c){igGetWindowWidth() - hide_width - igGetStyle()->WindowPadding.x, start.y});
        if (igButton("Hide###bottom", (ImVec2_c){0.0f, 0.0f}))
            docks->bottom_open = 0;
        igSetItemTooltip("%s", "Collapse the dock to its strip");
        igSetCursorPos(start);
        if (igBeginTabBar("bottom tabs", 0)) {
            if (ui_begin_console_tab(app)) {
                console_tab(app);
                igEndTabItem();
            }
            if (app->shown == SCENE_STRESS) {
                ImGuiTabItemFlags stress_flags = app->open_stress ? ImGuiTabItemFlags_SetSelected : 0;
                app->open_stress = 0;
                if (igBeginTabItem("Stress", NULL, stress_flags)) {
                    stress_ui(app);
                    igEndTabItem();
                }
            }
            igEndTabBar();
        }
    }
    igEnd();
}

//
// Splitters: the docks' borders, dragged with the mouse. Each is a transparent window over the
// dock's edge strip, inside the dock, so a press on it never belongs to the viewport.
//

internal void splitter(App* app, const char* name, NvRect rect, DockSplitter which)
{
    Docks* docks = &app->docks;
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2_c){0.0f, 0.0f});
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    bool open = nv_imgui_begin_panel_ex(&app->imgui, name, rect, flags);
    igPopStyleVar(1);
    if (open) {
        f32 ratio = igGetIO_Nil()->DisplayFramebufferScale.x;
        ImVec2_c size = {(f32)rect.width / ratio, (f32)rect.height / ratio};
        igInvisibleButton(name, size, 0);
        b32 horizontal = which != SPLITTER_BOTTOM; // the border runs along y, the drag is along x
        ImVec2_c mouse = igGetIO_Nil()->MousePos;
        f32 along = horizontal ? mouse.x : mouse.y;
        if (igIsItemHovered(0) || igIsItemActive())
            igSetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        if (igIsItemActivated()) {
            docks->dragging = which;
            docks->drag_start_mouse = along;
            docks->drag_start_size = which == SPLITTER_LEFT ? docks->left_width
                                     : which == SPLITTER_RIGHT ? docks->right_width
                                                               : docks->bottom_height;
        }
        if (igIsItemActive() && docks->dragging == (s32)which) {
            f32 moved = along - docks->drag_start_mouse;
            // The right and bottom docks grow as their border moves away from the window's edge.
            if (which == SPLITTER_LEFT)
                docks->left_width = clampf(docks->drag_start_size + moved, DOCK_LEFT_MIN, DOCK_SIDE_MAX);
            else if (which == SPLITTER_RIGHT)
                docks->right_width = clampf(docks->drag_start_size - moved, DOCK_RIGHT_MIN, DOCK_SIDE_MAX);
            else
                docks->bottom_height = clampf(docks->drag_start_size - moved, DOCK_BOTTOM_MIN, DOCK_BOTTOM_MAX);
        }
        if (!igIsItemActive() && docks->dragging == (s32)which)
            docks->dragging = SPLITTER_NONE;
    }
    igEnd();
}

internal void splitters(App* app)
{
    Docks* docks = &app->docks;
    const Layout* layout = &app->layout;
    f32 ratio = igGetIO_Nil()->DisplayFramebufferScale.x;
    u32 thickness = (u32)(DESKTOP_SPLITTER * ratio + 0.5f);
    if (docks->show_left && layout->left.width > thickness) {
        NvRect r = layout->left;
        r.x += r.width - thickness;
        r.width = thickness;
        splitter(app, "Left splitter", r, SPLITTER_LEFT);
    }
    if (docks->show_right && layout->right.width > thickness) {
        NvRect r = layout->right;
        r.width = thickness;
        splitter(app, "Right splitter", r, SPLITTER_RIGHT);
    }
    if (docks->show_bottom && docks->bottom_open && layout->bottom.height > thickness) {
        NvRect r = layout->bottom;
        r.height = thickness;
        splitter(app, "Bottom splitter", r, SPLITTER_BOTTOM);
    }
}

//
// Shortcuts
//

internal void shortcuts(App* app)
{
    ImGuiIO* io = igGetIO_Nil();
    // Not while a field or a widget holds the keyboard, or a popup is open.
    b32 free = !io->WantTextInput && !igIsAnyItemActive() && !io->KeyCtrl && !io->KeyAlt &&
               !igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!free)
        return;
    if (app->shown == SCENE_SHOWCASE && igIsKeyPressed_Bool(ImGuiKey_Space, false)) {
        if (app->playing)
            app_stop_playing(app);
        else
            app_start_playing(app);
    }
    if (igIsKeyPressed_Bool(ImGuiKey_F, false))
        app_focus_selection(app);
}

void desktop_build_ui(App* app)
{
    top_bar(app);
    left_dock(app);
    right_dock(app);
    bottom_dock(app);
    splitters(app);
    shortcuts(app);
}
