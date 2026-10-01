#include "app.h"

// The phone UI (docs/specs/layout.md): a top bar with Undo, Play / Stop and Redo, the scene
// viewport below it, and one panel of tabs under the viewport.

#define PHONE_TOP_BAR 48.0f        // CSS pixels
#define PHONE_VIEWPORT_SHARE 0.6f  // of the height under the top bar
#define PHONE_BUTTON_HEIGHT 38.0f
#define PHONE_SIDE_BUTTON_WIDTH 64.0f
#define PHONE_FIND_WIDTH 52.0f     // between Undo and Play (docs/specs/search.md)
#define PHONE_MULTI_WIDTH 52.0f    // between Play and Redo (docs/specs/selection.md)
#define PHONE_BUTTON_GAP 4.0f
#define PHONE_PLAY_WIDTH 104.0f
#define PHONE_MARGIN 8.0f

void phone_layout(App* app, f32 width, f32 height, f32 ratio)
{
    f32 split = PHONE_TOP_BAR + (height - PHONE_TOP_BAR) * PHONE_VIEWPORT_SHARE;
    app->layout = (Layout){
        .top_bar = ui_rect(0.0f, 0.0f, width, PHONE_TOP_BAR, ratio),
        .viewport = ui_rect(0.0f, PHONE_TOP_BAR, width, split, ratio),
        .panel = ui_rect(0.0f, split, width, height, ratio),
    };
}

internal void top_bar(App* app)
{
    b32 tint = ui_push_play_tint(app);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    bool open = nv_imgui_begin_panel_ex(&app->imgui, "Top bar", app->layout.top_bar, flags);
    ui_pop_play_tint(tint);
    if (open) {
        f32 width = igGetWindowWidth();
        f32 y = (PHONE_TOP_BAR - PHONE_BUTTON_HEIGHT) * 0.5f;
        ImVec2_c side = {PHONE_SIDE_BUTTON_WIDTH, PHONE_BUTTON_HEIGHT};
        igSetCursorPos((ImVec2_c){PHONE_MARGIN, y});
        undo_button(app, -1, 0, side);
        igSetCursorPos((ImVec2_c){PHONE_MARGIN + PHONE_SIDE_BUTTON_WIDTH + PHONE_BUTTON_GAP, y});
        if (igButton(TL("Find"), (ImVec2_c){PHONE_FIND_WIDTH, PHONE_BUTTON_HEIGHT}))
            search_open_palette(app);
        // The Play button's center is the screen's center.
        igSetCursorPos((ImVec2_c){(width - PHONE_PLAY_WIDTH) * 0.5f, y});
        ui_play_button(app, (ImVec2_c){PHONE_PLAY_WIDTH, PHONE_BUTTON_HEIGHT});
        // Multi: while on, every tap adds or removes a node instead of selecting only it.
        igSetCursorPos((ImVec2_c){width - PHONE_MARGIN - PHONE_SIDE_BUTTON_WIDTH - PHONE_BUTTON_GAP - PHONE_MULTI_WIDTH, y});
        b32 multi = app->multi_select;
        if (multi)
            igPushStyleColor_Vec4(ImGuiCol_Button, igGetStyle()->Colors[ImGuiCol_ButtonActive]);
        if (igButton(TL("Multi"), (ImVec2_c){PHONE_MULTI_WIDTH, PHONE_BUTTON_HEIGHT}))
            app->multi_select = !app->multi_select;
        if (multi)
            igPopStyleColor(1);
        igSetCursorPos((ImVec2_c){width - PHONE_MARGIN - PHONE_SIDE_BUTTON_WIDTH, y});
        undo_button(app, 1, 0, side);
    }
    // The palette's Reset command opens the confirmation here, at the window's top level.
    if (open && app->request_reset)
        igOpenPopup_Str(TL("Reset everything?"), 0);
    app->request_reset = 0;
    if (open)
        save_reset_popup(app);
    igEnd();
}

void phone_build_ui(App* app)
{
    top_bar(app);

    b32 tint = ui_push_play_tint(app);
    b32 panel_open = nv_imgui_begin_panel(&app->imgui, "Editor", app->layout.panel);
    ui_pop_play_tint(tint);
    if (panel_open)
        ui_playing_note(app);
    // Shrink, not scroll: all the tabs stay in view on a narrow screen, their labels cut if need be.
    if (panel_open && igBeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
        if (igBeginTabItem(TL("Scene"), NULL, 0)) {
            ui_scene_tab(app);
            igEndTabItem();
        }
        // NOTE: Picking a node in the Scene tab jumps here, since that is where it is edited.
        ImGuiTabItemFlags inspector_flags = app->open_inspector ? ImGuiTabItemFlags_SetSelected : 0;
        app->open_inspector = 0;
        if (igBeginTabItem(TL("Inspector"), NULL, inspector_flags)) {
            ui_inspector_tab(app);
            igEndTabItem();
        }
        ImGuiTabItemFlags view_flags = app->open_view ? ImGuiTabItemFlags_SetSelected : 0;
        app->open_view = 0;
        if (igBeginTabItem(TL("View"), NULL, view_flags)) {
            ui_view_tab(app);
            igEndTabItem();
        }
        if (ui_begin_textures_tab(app)) {
            textures_tab(app);
            igEndTabItem();
        }
        if (ui_begin_console_tab(app)) {
            console_tab(app);
            igEndTabItem();
        }
        if (app->shown == SCENE_STRESS) {
            ImGuiTabItemFlags stress_flags = app->open_stress ? ImGuiTabItemFlags_SetSelected : 0;
            app->open_stress = 0;
            if (igBeginTabItem(TL("Stress"), NULL, stress_flags)) {
                stress_ui(app);
                igEndTabItem();
            }
        }
        igEndTabBar();
    }
    igEnd();
    search_palette(app);
}
