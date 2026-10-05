#include "sandbox.h"

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

void phone_layout(Sandbox* sandbox, f32 width, f32 height, f32 ratio)
{
    f32 split = PHONE_TOP_BAR + (height - PHONE_TOP_BAR) * PHONE_VIEWPORT_SHARE;
    sandbox->layout = (Layout){
        .top_bar = nv_window_framebuffer_rect_from_css(0.0f, 0.0f, width, PHONE_TOP_BAR, ratio),
        .viewport = nv_window_framebuffer_rect_from_css(0.0f, PHONE_TOP_BAR, width, split, ratio),
        .panel = nv_window_framebuffer_rect_from_css(0.0f, split, width, height, ratio),
    };
}

internal void top_bar(Sandbox* sandbox)
{
    b32 tint = ui_push_play_tint(sandbox);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    bool open = nv_imgui_begin_panel_ex(&sandbox->imgui, "Top bar", sandbox->layout.top_bar, flags);
    ui_pop_play_tint(tint);
    if (open) {
        f32 width = igGetWindowWidth();
        f32 y = (PHONE_TOP_BAR - PHONE_BUTTON_HEIGHT) * 0.5f;
        ImVec2_c side = {PHONE_SIDE_BUTTON_WIDTH, PHONE_BUTTON_HEIGHT};
        igSetCursorPos((ImVec2_c){PHONE_MARGIN, y});
        undo_button(sandbox, -1, 0, side);
        igSetCursorPos((ImVec2_c){PHONE_MARGIN + PHONE_SIDE_BUTTON_WIDTH + PHONE_BUTTON_GAP, y});
        if (igButton(TL("Find"), (ImVec2_c){PHONE_FIND_WIDTH, PHONE_BUTTON_HEIGHT}))
            search_open_palette(sandbox);
        // The Play button's center is the screen's center.
        igSetCursorPos((ImVec2_c){(width - PHONE_PLAY_WIDTH) * 0.5f, y});
        ui_play_button(sandbox, (ImVec2_c){PHONE_PLAY_WIDTH, PHONE_BUTTON_HEIGHT});
        // Multi: while on, every tap adds or removes a node instead of selecting only it.
        igSetCursorPos((ImVec2_c){width - PHONE_MARGIN - PHONE_SIDE_BUTTON_WIDTH - PHONE_BUTTON_GAP - PHONE_MULTI_WIDTH, y});
        b32 multi = sandbox->multi_select;
        if (multi)
            igPushStyleColor_Vec4(ImGuiCol_Button, igGetStyle()->Colors[ImGuiCol_ButtonActive]);
        if (igButton(TL("Multi"), (ImVec2_c){PHONE_MULTI_WIDTH, PHONE_BUTTON_HEIGHT}))
            sandbox->multi_select = !sandbox->multi_select;
        if (multi)
            igPopStyleColor(1);
        igSetCursorPos((ImVec2_c){width - PHONE_MARGIN - PHONE_SIDE_BUTTON_WIDTH, y});
        undo_button(sandbox, 1, 0, side);
    }
    // The palette's Reset command opens the confirmation here, at the window's top level.
    if (open && sandbox->request_reset)
        igOpenPopup_Str(TL("Reset everything?"), 0);
    sandbox->request_reset = 0;
    if (open)
        save_reset_popup(sandbox);
    igEnd();
}

void phone_build_ui(Sandbox* sandbox)
{
    top_bar(sandbox);

    b32 tint = ui_push_play_tint(sandbox);
    b32 panel_open = nv_imgui_begin_panel(&sandbox->imgui, "Editor", sandbox->layout.panel);
    ui_pop_play_tint(tint);
    if (panel_open)
        ui_playing_note(sandbox);
    // Shrink, not scroll: all the tabs stay in view on a narrow screen, their labels cut if need be.
    if (panel_open && igBeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
        if (igBeginTabItem(TL("Scene"), NULL, 0)) {
            ui_scene_tab(sandbox);
            igEndTabItem();
        }
        // NOTE: Picking a node in the Scene tab jumps here, since that is where it is edited.
        ImGuiTabItemFlags inspector_flags = sandbox->open_inspector ? ImGuiTabItemFlags_SetSelected : 0;
        sandbox->open_inspector = 0;
        if (igBeginTabItem(TL("Inspector"), NULL, inspector_flags)) {
            ui_inspector_tab(sandbox);
            igEndTabItem();
        }
        ImGuiTabItemFlags view_flags = sandbox->open_view ? ImGuiTabItemFlags_SetSelected : 0;
        sandbox->open_view = 0;
        if (igBeginTabItem(TL("View"), NULL, view_flags)) {
            ui_view_tab(sandbox);
            igEndTabItem();
        }
        if (ui_begin_textures_tab(sandbox)) {
            textures_tab(sandbox);
            igEndTabItem();
        }
        if (ui_begin_console_tab(sandbox)) {
            console_tab(sandbox);
            igEndTabItem();
        }
        if (sandbox->shown == SCENE_STRESS) {
            ImGuiTabItemFlags stress_flags = sandbox->open_stress ? ImGuiTabItemFlags_SetSelected : 0;
            sandbox->open_stress = 0;
            if (igBeginTabItem(TL("Stress"), NULL, stress_flags)) {
                stress_ui(sandbox);
                igEndTabItem();
            }
        }
        igEndTabBar();
    }
    igEnd();
    search_palette(sandbox);
}
