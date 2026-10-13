#include "engine/win32.h"

#include "engine/window.h"
#include "engine/window_win32.h"

#include <shellapi.h>
#include <stdio.h>
#include <string.h>

// The Windows window (docs/specs/d3d12_port.md, "Platform layer"): one top-level window, its message loop, the clock and what the
// machine is. Input goes to imgui_win32.c through the message hook.

#define WINDOW_CLASS L"nv_window"
#define START_WIDTH_DIP 1600
#define START_HEIGHT_DIP 900

typedef struct Win32Window {
    NvWindow* window;
    HWND hwnd;
    NvWin32MessageHook hook;
    void* hook_data;
    b32 minimized;
    f32 fixed_ratio; // 0: the screen's scale
    LARGE_INTEGER start_counter;
    LARGE_INTEGER frequency;
} Win32Window;

global Win32Window state;

f64 nv_time_seconds(void)
{
    if (state.frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&state.frequency);
        QueryPerformanceCounter(&state.start_counter);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (f64)(now.QuadPart - state.start_counter.QuadPart) / (f64)state.frequency.QuadPart;
}

internal f32 window_pixel_ratio(void)
{
    if (state.fixed_ratio > 0.0f)
        return state.fixed_ratio;
    return state.hwnd ? (f32)GetDpiForWindow(state.hwnd) / 96.0f : 1.0f;
}

internal void call_hidden(void)
{
    NvWindow* window = state.window;
    if (window && window->on_hidden)
        window->on_hidden(window->on_hidden_data);
}

internal LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (state.hook) {
        s64 result = 0;
        if (state.hook(state.hook_data, hwnd, message, (u64)wparam, (s64)lparam, &result))
            return (LRESULT)result;
    }
    switch (message) {
    case WM_SIZE:
        state.minimized = wparam == SIZE_MINIMIZED;
        if (state.minimized)
            call_hidden();
        return 0;
    case WM_ACTIVATEAPP:
        if (!wparam)
            call_hidden();
        return 0;
    case WM_ENDSESSION:
        if (wparam)
            call_hidden();
        return 0;
    case WM_DPICHANGED: {
        // The new size and place that keep the window the same size in DIPs.
        const RECT* suggested = (const RECT*)lparam;
        if (state.fixed_ratio <= 0.0f)
            SetWindowPos(hwnd, NULL, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:
        call_hidden();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

#if !defined(NDEBUG)
// A Debug build says where it stopped (an NV_ASSERT is a breakpoint): the return addresses as offsets into the executable, which
// llvm-symbolizer turns into functions and lines (--obj=<exe> --relative-address <offset>).
internal LONG WINAPI report_crash(EXCEPTION_POINTERS* pointers)
{
    void* frames[32];
    USHORT count = CaptureStackBackTrace(0, 32, frames, NULL);
    char* base = (char*)GetModuleHandleW(NULL);
    fprintf(stderr, "[crash] exception 0x%08lx; stack, offsets into the executable:", (unsigned long)pointers->ExceptionRecord->ExceptionCode);
    for (USHORT i = 0; i < count; ++i) {
        if ((char*)frames[i] >= base && (char*)frames[i] < base + 0x10000000)
            fprintf(stderr, " 0x%llx", (unsigned long long)((char*)frames[i] - base));
    }
    fprintf(stderr, "\n");
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void nv_window_create(NvWindow* window, const char* title)
{
#if !defined(NDEBUG)
    SetUnhandledExceptionFilter(report_crash);
#endif
    NV_ASSERT(!state.hwnd);
    nv_time_seconds(); // starts the clock
    state.window = window;

    WNDCLASSEXW window_class = {
        .cbSize = sizeof(window_class),
        .style = CS_DBLCLKS,
        .lpfnWndProc = window_proc,
        .hInstance = GetModuleHandleW(NULL),
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .lpszClassName = WINDOW_CLASS,
    };
    RegisterClassExW(&window_class);

    wchar_t wide_title[256];
    nv_win32_wide_from_utf8(title, wide_title, NV_ARRAY_COUNT(wide_title));

    // The first window is 1600 x 900 DIPs on the screen it will open on (the system's, until it exists).
    u32 dpi = GetDpiForSystem();
    RECT rect = {0, 0, (LONG)(START_WIDTH_DIP * dpi / 96), (LONG)(START_HEIGHT_DIP * dpi / 96)};
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    state.hwnd = CreateWindowExW(0, WINDOW_CLASS, wide_title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                 rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, window_class.hInstance, NULL);
    NV_ASSERT(state.hwnd);
    window->handle = state.hwnd;
    window->pixel_ratio = window_pixel_ratio();
    ShowWindow(state.hwnd, SW_SHOWDEFAULT);
}

void nv_window_win32_set_message_hook(NvWindow* window, NvWin32MessageHook hook, void* userdata)
{
    (void)window;
    state.hook = hook;
    state.hook_data = userdata;
}

void nv_window_win32_set_fixed_size(NvWindow* window, u32 width, u32 height)
{
    state.fixed_ratio = 1.0f;
    window->pixel_ratio = 1.0f;
    RECT rect = {0, 0, (LONG)width, (LONG)height};
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(state.hwnd));
    SetWindowPos(state.hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

b32 nv_window_win32_minimized(void)
{
    return state.minimized;
}

void nv_window_framebuffer_size(NvWindow* window, u32* width, u32* height)
{
    window->pixel_ratio = window_pixel_ratio();
    RECT client = {0};
    GetClientRect(state.hwnd, &client);
    *width = state.minimized ? 0 : (u32)client.right;
    *height = state.minimized ? 0 : (u32)client.bottom;
}

void nv_window_download_text(char* out, umm capacity)
{
    NV_ASSERT(capacity > 0);
    (void)capacity;
    out[0] = 0; // nothing was downloaded: the files are on the disk
}

b32 nv_window_is_mac(void)
{
    return 0;
}

// Windows 10.0.<build>, as the system says it (a manifest without the newest "supported OS" would make GetVersionEx lie).
internal b32 windows_build(u32* build)
{
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    FARPROC address = ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    if (!address)
        return 0;
    RtlGetVersionFn rtl_get_version;
    memcpy(&rtl_get_version, &address, sizeof(rtl_get_version));
    OSVERSIONINFOW info = {.dwOSVersionInfoSize = sizeof(info)};
    if (rtl_get_version(&info) != 0)
        return 0;
    *build = info.dwBuildNumber;
    return 1;
}

// The CPU's name the way the registry has it ("AMD Ryzen 9 7945HX with Radeon Graphics"); empty when it does not say.
internal void cpu_name(char* out, umm capacity)
{
    out[0] = 0;
    wchar_t wide[128] = {0};
    DWORD size = sizeof(wide) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", RRF_RT_REG_SZ,
                     NULL, wide, &size) != ERROR_SUCCESS)
        return;
    nv_win32_utf8_from_wide(wide, out, (int)capacity);
    // The registry pads the name with spaces.
    umm length = strlen(out);
    while (length && out[length - 1] == ' ')
        out[--length] = 0;
}

void nv_window_system_text(char* out, umm capacity)
{
    u32 build = 0;
    b32 known = windows_build(&build);
    char cpu[128];
    cpu_name(cpu, sizeof(cpu));
    MEMORYSTATUSEX memory = {.dwLength = sizeof(memory)};
    GlobalMemoryStatusEx(&memory);
    snprintf(out, capacity, "%s build %u, %s, %u logical cores, %.0f GB", known && build >= 22000 ? "Windows 11" : "Windows 10", build,
             cpu[0] ? cpu : "unknown CPU", (u32)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
             (f64)memory.ullTotalPhys / (1024.0 * 1024.0 * 1024.0));
}

void nv_window_on_hidden(NvWindow* window, void (*hidden)(void* userdata), void* userdata)
{
    window->on_hidden = hidden;
    window->on_hidden_data = userdata;
}

void nv_window_run(NvWindow* window, NvFrameFn frame, void* userdata)
{
    (void)window;
    for (;;) {
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT)
                ExitProcess((UINT)message.wParam);
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (state.minimized) {
            WaitMessage(); // nothing to draw: sleep until the window is restored
            continue;
        }
        frame(userdata); // the swapchain's vsync sets the pace
    }
}

b32 nv_window_win32_command_line_value(const char* option, char* out, umm capacity)
{
    wchar_t wide_option[64];
    nv_win32_wide_from_utf8(option, wide_option, NV_ARRAY_COUNT(wide_option));
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    b32 found = 0;
    for (int i = 1; arguments && i + 1 < count && !found; ++i) {
        if (wcscmp(arguments[i], wide_option) == 0)
            found = nv_win32_utf8_from_wide(arguments[i + 1], out, (int)capacity) > 0;
    }
    if (arguments)
        LocalFree(arguments);
    return found;
}

#if !defined(NDEBUG)
global b32 injected_modifiers_on;
global b32 pointer_injected;
global u32 injected_modifiers;
#endif

b32 nv_window_win32_key_is_down(int virtual_key)
{
#if !defined(NDEBUG)
    if (injected_modifiers_on) {
        if (virtual_key == VK_CONTROL)
            return (injected_modifiers & 1u) != 0;
        if (virtual_key == VK_SHIFT)
            return (injected_modifiers & 2u) != 0;
        if (virtual_key == VK_MENU)
            return (injected_modifiers & 4u) != 0;
    }
#endif
    return (GetKeyState(virtual_key) & 0x8000) != 0;
}

#if !defined(NDEBUG)
b32 nv_window_win32_input_is_injected(void)
{
    return pointer_injected;
}

void nv_window_win32_inject_mouse(NvWindow* window, s32 x, s32 y, s32 button, b32 down)
{
    pointer_injected = 1;
    HWND hwnd = window->handle;
    LPARAM position = MAKELPARAM((WORD)(s16)x, (WORD)(s16)y);
    UINT message = WM_MOUSEMOVE;
    if (button == 0)
        message = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
    else if (button == 1)
        message = down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
    else if (button == 2)
        message = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
    SendMessageW(hwnd, message, 0, position);
}

void nv_window_win32_inject_wheel(NvWindow* window, s32 x, s32 y, s32 notches)
{
    POINT screen = {x, y};
    ClientToScreen(window->handle, &screen);
    SendMessageW(window->handle, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)(notches * WHEEL_DELTA)), MAKELPARAM((WORD)(s16)screen.x, (WORD)(s16)screen.y));
}

void nv_window_win32_inject_key(NvWindow* window, u32 virtual_key, b32 down)
{
    UINT scancode = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC);
    LPARAM lparam = 1 | ((LPARAM)scancode << 16) | (down ? 0 : (3u << 30));
    SendMessageW(window->handle, down ? WM_KEYDOWN : WM_KEYUP, virtual_key, lparam);
}

void nv_window_win32_inject_char(NvWindow* window, u32 codepoint)
{
    if (codepoint >= 0x10000) {
        codepoint -= 0x10000;
        SendMessageW(window->handle, WM_CHAR, 0xD800 + (codepoint >> 10), 1);
        SendMessageW(window->handle, WM_CHAR, 0xDC00 + (codepoint & 0x3FF), 1);
    } else {
        SendMessageW(window->handle, WM_CHAR, codepoint, 1);
    }
}

void nv_window_win32_inject_modifiers(u32 modifiers)
{
    injected_modifiers_on = 1;
    injected_modifiers = modifiers;
}

void nv_window_win32_inject_close(NvWindow* window)
{
    PostMessageW(window->handle, WM_CLOSE, 0, 0); // posted: the close comes between frames, not inside the one running the script
}
#endif
