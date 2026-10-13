#include "engine/win32.h"

#include "engine/imgui_platform.h"
#include "engine/window_win32.h"

#include <float.h>
#include <imm.h>
#include <string.h>

// Dear ImGui's input on Windows (docs/specs/d3d12_port.md, "Platform layer"): the window's messages become ImGui's key, text, mouse
// and focus events, the camera drag in the view (imgui.c), the cursor, the clipboard and the input method's place. Positions
// ImGui and the app see are in CSS pixels, which are DIPs here: the window's pixels divided by its pixel ratio.

typedef struct Win32Input {
    NvImgui* imgui;
    HWND hwnd;
    HIMC default_input_context; // the window's, put back while ImGui wants text
    b32 input_method_on;
    b32 tracking_leave;
    HCURSOR cursor;
    u32 mouse_buttons_down; // bit i is mouse button i of ImGui's numbering
} Win32Input;

global Win32Input input;

//
// Keys
//

internal b32 key_is_down(int virtual_key)
{
    return nv_window_win32_key_is_down(virtual_key);
}

internal void add_modifiers(ImGuiIO* io)
{
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, key_is_down(VK_CONTROL));
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Shift, key_is_down(VK_SHIFT));
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Alt, key_is_down(VK_MENU));
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Super, key_is_down(VK_LWIN) || key_is_down(VK_RWIN));
}

internal u32 modifier_bits(void)
{
    return (key_is_down(VK_CONTROL) ? ImGuiMod_Ctrl : 0) | (key_is_down(VK_SHIFT) ? ImGuiMod_Shift : 0) |
           (key_is_down(VK_MENU) ? ImGuiMod_Alt : 0);
}

// The ImGui key of a key message. The shift, control and alt keys come as one virtual key each; the scan code and the
// "extended" bit say which side.
internal ImGuiKey key_from_message(WPARAM virtual_key, LPARAM lparam)
{
    b32 extended = (HIWORD(lparam) & KF_EXTENDED) != 0;
    if (virtual_key >= '0' && virtual_key <= '9')
        return (ImGuiKey)(ImGuiKey_0 + (virtual_key - '0'));
    if (virtual_key >= 'A' && virtual_key <= 'Z')
        return (ImGuiKey)(ImGuiKey_A + (virtual_key - 'A'));
    if (virtual_key >= VK_NUMPAD0 && virtual_key <= VK_NUMPAD9)
        return (ImGuiKey)(ImGuiKey_Keypad0 + (virtual_key - VK_NUMPAD0));
    if (virtual_key >= VK_F1 && virtual_key <= VK_F12)
        return (ImGuiKey)(ImGuiKey_F1 + (virtual_key - VK_F1));
    switch (virtual_key) {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return extended ? ImGuiKey_KeypadEnter : ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_OEM_7: return ImGuiKey_Apostrophe;
    case VK_OEM_COMMA: return ImGuiKey_Comma;
    case VK_OEM_MINUS: return ImGuiKey_Minus;
    case VK_OEM_PERIOD: return ImGuiKey_Period;
    case VK_OEM_2: return ImGuiKey_Slash;
    case VK_OEM_1: return ImGuiKey_Semicolon;
    case VK_OEM_PLUS: return ImGuiKey_Equal;
    case VK_OEM_4: return ImGuiKey_LeftBracket;
    case VK_OEM_5: return ImGuiKey_Backslash;
    case VK_OEM_6: return ImGuiKey_RightBracket;
    case VK_OEM_3: return ImGuiKey_GraveAccent;
    case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case VK_ADD: return ImGuiKey_KeypadAdd;
    case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
    case VK_DIVIDE: return ImGuiKey_KeypadDivide;
    case VK_SHIFT: return MapVirtualKeyW(LOBYTE(HIWORD(lparam)), MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? ImGuiKey_RightShift : ImGuiKey_LeftShift;
    case VK_CONTROL: return extended ? ImGuiKey_RightCtrl : ImGuiKey_LeftCtrl;
    case VK_MENU: return extended ? ImGuiKey_RightAlt : ImGuiKey_LeftAlt;
    case VK_LWIN: return ImGuiKey_LeftSuper;
    case VK_RWIN: return ImGuiKey_RightSuper;
    default: return ImGuiKey_None;
    }
}

//
// Messages
//

internal u32 ime_pixels(f32 css_pixels)
{
    return (u32)(css_pixels * nv_window_pixel_ratio(input.imgui->window) + 0.5f);
}

internal b32 on_message(void* userdata, void* hwnd_raw, u32 message, u64 wparam, s64 lparam, s64* result)
{
    (void)userdata;
    NvImgui* imgui = input.imgui;
    ImGuiIO* io = igGetIO_Nil();
    HWND hwnd = hwnd_raw;
    f32 ratio = nv_window_pixel_ratio(imgui->window);
    *result = 0;

    switch (message) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP: {
        // A key the input method takes arrives as VK_PROCESSKEY: not ImGui's, the method's.
        if (wparam >= 256 || wparam == VK_PROCESSKEY)
            return 0;
        b32 down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        add_modifiers(io);
        ImGuiKey key = key_from_message((WPARAM)wparam, (LPARAM)lparam);
        if (key != ImGuiKey_None)
            ImGuiIO_AddKeyEvent(io, key, down);
        if (wparam == VK_SNAPSHOT && !down) { // Print Screen only reports its release
            ImGuiIO_AddKeyEvent(io, ImGuiKey_PrintScreen, 1);
            ImGuiIO_AddKeyEvent(io, ImGuiKey_PrintScreen, 0);
        }
        // Alt+F4 closes the window (the default handling); the rest of the Alt keys are the app's.
        return (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) && wparam != VK_F4;
    }
    case WM_SYSCOMMAND:
        // Pressing and releasing Alt would otherwise start the window menu's modal loop and stop the frames.
        return (wparam & 0xFFF0) == SC_KEYMENU;
    case WM_CHAR:
        ImGuiIO_AddInputCharacterUTF16(io, (ImWchar16)wparam);
        return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        ImGuiIO_AddFocusEvent(io, message == WM_SETFOCUS);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT && input.cursor) {
            SetCursor(input.cursor);
            *result = TRUE;
            return 1;
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!input.tracking_leave) {
            TRACKMOUSEEVENT track = {.cbSize = sizeof(track), .dwFlags = TME_LEAVE, .hwndTrack = hwnd};
            TrackMouseEvent(&track);
            input.tracking_leave = 1;
        }
        f32 x = (f32)(s16)LOWORD(lparam) / ratio;
        f32 y = (f32)(s16)HIWORD(lparam) / ratio;
        ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_Mouse);
        ImGuiIO_AddMousePosEvent(io, x, y);
        nv_imgui_view_mouse(imgui, NV_MOUSE_MOVE, 0, x, y, modifier_bits());
        return 1;
    }
    case WM_MOUSELEAVE:
        input.tracking_leave = 0;
#if !defined(NDEBUG)
        if (nv_window_win32_input_is_injected())
            return 1;
#endif
        // While a button is held the capture keeps the moves coming; the pointer only leaves when none is.
        if (!input.mouse_buttons_down)
            ImGuiIO_AddMousePosEvent(io, -FLT_MAX, -FLT_MAX);
        return 1;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        b32 down = message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDOWN ||
                   message == WM_RBUTTONDBLCLK || message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK;
        s32 dom_button = (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP) ? 0
                         : (message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK || message == WM_MBUTTONUP) ? 1
                                                                                                                   : 2;
        s32 imgui_button = dom_button == 0 ? 0 : dom_button == 1 ? 2 : 1; // DOM: left, middle, right; ImGui: left, right, middle
        f32 x = (f32)(s16)LOWORD(lparam) / ratio;
        f32 y = (f32)(s16)HIWORD(lparam) / ratio;
        ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_Mouse);
        ImGuiIO_AddMousePosEvent(io, x, y);
        if (down) {
            // The window keeps the mouse until every button is up, so a drag goes on past its edge.
            if (!input.mouse_buttons_down)
                SetCapture(hwnd);
            input.mouse_buttons_down |= 1u << imgui_button;
        } else {
            input.mouse_buttons_down &= ~(1u << imgui_button);
            if (!input.mouse_buttons_down)
                ReleaseCapture();
        }
        if (!nv_imgui_view_mouse(imgui, down ? NV_MOUSE_DOWN : NV_MOUSE_UP, dom_button, x, y, modifier_bits()))
            ImGuiIO_AddMouseButtonEvent(io, imgui_button, down);
        return 1;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        f32 notches = (f32)GET_WHEEL_DELTA_WPARAM(wparam) / (f32)WHEEL_DELTA;
        if (message == WM_MOUSEWHEEL && nv_imgui_view_wheel(imgui, -notches * 100.0f))
            return 1;
        ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_Mouse);
        ImGuiIO_AddMouseWheelEvent(io, message == WM_MOUSEHWHEEL ? notches : 0.0f, message == WM_MOUSEWHEEL ? notches : 0.0f);
        return 1;
    }
    default:
        return 0;
    }
}

//
// The clipboard
//

internal const char* get_clipboard(ImGuiContext* context)
{
    (void)context;
    NvImgui* imgui = input.imgui;
    imgui->clipboard[0] = 0;
    if (!OpenClipboard(input.hwnd))
        return imgui->clipboard;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const wchar_t* wide = data ? GlobalLock(data) : NULL;
    if (wide) {
        // UTF-8 for the buffer, with the line ends as ImGui has them: "\n".
        char* text = imgui->clipboard;
        int length = nv_win32_utf8_from_wide(wide, text, NV_IMGUI_CLIPBOARD_SIZE);
        int kept = 0;
        for (int i = 0; i < length; ++i) {
            if (text[i] != '\r')
                text[kept++] = text[i];
        }
        text[kept] = 0;
        GlobalUnlock(data);
    }
    CloseClipboard();
    return imgui->clipboard;
}

internal void set_clipboard(ImGuiContext* context, const char* text)
{
    (void)context;
    NvImgui* imgui = input.imgui;
    umm length = strlen(text);
    if (length > NV_IMGUI_CLIPBOARD_SIZE - 1)
        length = NV_IMGUI_CLIPBOARD_SIZE - 1;
    memcpy(imgui->clipboard, text, length);
    imgui->clipboard[length] = 0;
    nv_utf8_trim(imgui->clipboard);

    // UTF-16 with "\r\n" line ends, which is what other programs expect.
    int capacity = (int)(2 * strlen(imgui->clipboard) + 2);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)capacity * sizeof(wchar_t));
    if (!memory)
        return;
    wchar_t* wide = GlobalLock(memory);
    const char* in = imgui->clipboard;
    int out = 0;
    while (*in) {
        // A character is one to four bytes; convert it alone and copy the line break specially.
        if (*in == '\n') {
            wide[out++] = L'\r';
            wide[out++] = L'\n';
            ++in;
            continue;
        }
        u32 size = nv_utf8_length((u8)*in);
        char one[5] = {0};
        memcpy(one, in, size);
        wchar_t converted[3] = {0};
        int count = nv_win32_wide_from_utf8(one, converted, 3);
        for (int i = 0; i < count; ++i)
            wide[out++] = converted[i];
        in += size;
    }
    wide[out] = 0;
    GlobalUnlock(memory);
    if (OpenClipboard(input.hwnd)) {
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, memory))
            GlobalFree(memory); // the clipboard owns it once set
        CloseClipboard();
    } else {
        GlobalFree(memory);
    }
}

//
// The input method
//

// ImGui tells where the text caret is: a composing syllable and the method's candidate list appear at the field being edited
// (docs/specs/korean.md). The method draws them itself; it only needs to know where.
internal void set_ime_data(ImGuiContext* context, ImGuiViewport* viewport, ImGuiPlatformImeData* data)
{
    (void)context, (void)viewport;
    if (!data->WantVisible && !data->WantTextInput)
        return;
    HIMC context_handle = ImmGetContext(input.hwnd);
    if (!context_handle)
        return;
    LONG x = (LONG)ime_pixels(data->InputPos.x);
    LONG y = (LONG)ime_pixels(data->InputPos.y);
    LONG line = (LONG)ime_pixels(data->InputLineHeight);
    COMPOSITIONFORM composition = {.dwStyle = CFS_FORCE_POSITION, .ptCurrentPos = {x, y}};
    ImmSetCompositionWindow(context_handle, &composition);
    // The candidate list stays out of the line being edited.
    CANDIDATEFORM candidate = {.dwIndex = 0, .dwStyle = CFS_EXCLUDE, .ptCurrentPos = {x, y}, .rcArea = {x, y, x + 1, y + line}};
    ImmSetCandidateWindow(context_handle, &candidate);
    ImmReleaseContext(input.hwnd, context_handle);
}

//
// The platform's functions
//

void nv_imgui_platform_init(NvImgui* imgui, NvWindow* window)
{
    input.imgui = imgui;
    input.hwnd = window->handle;
    input.cursor = LoadCursorW(NULL, IDC_ARROW);
    input.default_input_context = ImmGetContext(input.hwnd);
    input.input_method_on = 1;

    igGetIO_Nil()->BackendPlatformName = "nv_win32";
    ImGuiPlatformIO* platform_io = igGetPlatformIO_Nil();
    platform_io->Platform_SetImeDataFn = set_ime_data;
    platform_io->Platform_GetClipboardTextFn = get_clipboard;
    platform_io->Platform_SetClipboardTextFn = set_clipboard;
    nv_window_win32_set_message_hook(window, on_message, NULL);

    imgui->ui_scale = 1.0f; // a mouse: the touch UI is the browser's
}

void nv_imgui_platform_update(NvImgui* imgui)
{
    (void)imgui;
    // The input method only belongs to a text field: otherwise it would compose Hangul out of the keys of the viewport's shortcuts.
    b32 wanted = igGetIO_Nil()->WantTextInput;
    if (wanted != input.input_method_on) {
        ImmAssociateContext(input.hwnd, wanted ? input.default_input_context : NULL);
        input.input_method_on = wanted;
    }
}

void nv_imgui_platform_set_cursor(ImGuiMouseCursor cursor)
{
    LPCWSTR shape = IDC_ARROW;
    switch (cursor) {
    case ImGuiMouseCursor_TextInput: shape = IDC_IBEAM; break;
    case ImGuiMouseCursor_ResizeEW: shape = IDC_SIZEWE; break;
    case ImGuiMouseCursor_ResizeNS: shape = IDC_SIZENS; break;
    case ImGuiMouseCursor_Hand: shape = IDC_HAND; break;
    default: break;
    }
    input.cursor = LoadCursorW(NULL, shape);
    SetCursor(input.cursor);
}
