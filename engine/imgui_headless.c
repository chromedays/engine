#include "engine/imgui_platform.h"

// ImGui's platform on Linux is headless: there is no pointer, keyboard or clipboard, so nothing
// ever reaches the UI. The draw side (imgui.c) still works; a program that needs the UI on this
// platform has no input to give it, which is the honest state of a renderer without a window.

void nv_imgui_platform_init(NvImgui* imgui, NvWindow* window)
{
    (void)window;
    igGetIO_Nil()->BackendPlatformName = "nv_headless";
    imgui->ui_scale = 1.0f;
}

void nv_imgui_platform_update(NvImgui* imgui)
{
    (void)imgui;
}

void nv_imgui_platform_set_cursor(ImGuiMouseCursor cursor)
{
    (void)cursor; // no cursor to show
}
