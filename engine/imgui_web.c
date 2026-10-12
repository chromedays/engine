#include "engine/imgui_platform.h"
#include "engine/window.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <float.h>
#include <math.h>
#include <string.h>

//
// Browser glue
//

// NOTE: Phones only show their on-screen keyboard for a focused text field, and a canvas is not
// one. A hidden <input> (the "text agent") takes focus while ImGui edits text; what the keyboard
// types arrives as input events and is forwarded to ImGui. The agent always holds one sentinel
// space, so a Backspace on an otherwise empty field is still visible as a shorter value.
EM_JS_DEPS(nv_imgui, "$stringToUTF8,$UTF8ToString");

EM_JS(void, js_setup_text_agent, (char* clipboard, int clipboard_size), {
    const agent = document.createElement("input");
    agent.id = "nv-text-agent";
    agent.type = "text";
    agent.setAttribute("autocomplete", "off");
    agent.setAttribute("autocapitalize", "off");
    agent.setAttribute("autocorrect", "off");
    agent.spellcheck = false;
    // 16px keeps iOS from zooming the page when the field gains focus.
    agent.style.cssText = "position:fixed;left:0;top:0;width:1px;height:1px;opacity:0;border:0;" +
                          "padding:0;font-size:16px;pointer-events:none;";
    document.body.appendChild(agent);
    // The browser draws a composing syllable with its own highlight: keep it readable, no focus ring.
    const css = document.createElement("style");
    css.textContent = "#nv-text-agent{outline:none;caret-color:transparent}" +
                      "#nv-text-agent::selection{background:#ffd54a;color:#111}";
    document.head.appendChild(css);

    const reset = () => { agent.value = " "; agent.setSelectionRange(1, 1); };
    const flush = () => {
        const value = agent.value;
        if (value.length < 1) {
            _nv_imgui_js_backspace();
        } else {
            for (const ch of value.slice(1)) _nv_imgui_js_char(ch.codePointAt(0));
        }
        reset();
    };
    // While an input method composes a syllable, the agent shows it at the text caret, drawn by
    // the browser (ImGui has no pre-edit text); the method's candidate window follows it too.
    let composing = false;
    let lineHeight = 16;
    const showComposition = (on) => {
        const style = agent.style;
        style.opacity = on ? "1" : "0";
        style.color = "#111";
        style.background = on ? "#f2f2f2" : "transparent";
        style.height = on ? lineHeight + "px" : "1px";
        style.width = on ? Math.max(lineHeight, agent.value.length * lineHeight) + "px" : "1px";
        style.fontSize = on ? Math.max(12, Math.round(lineHeight * 0.85)) + "px" : "16px";
        style.zIndex = on ? "10" : "";
    };
    agent.addEventListener("focus", reset);
    agent.addEventListener("compositionstart", () => { composing = true; showComposition(true); });
    agent.addEventListener("compositionupdate", () => showComposition(true));
    agent.addEventListener("compositionend", () => { composing = false; showComposition(false); flush(); });
    agent.addEventListener("input", (event) => {
        // A browser undo or redo on the agent's own value is not typing.
        if (event.inputType && event.inputType.startsWith("history")) { reset(); return; }
        if (!composing) flush();
        else showComposition(true);
    });
    Module.nvImeMove = (x, y, height) => {
        lineHeight = Math.max(10, height);
        agent.style.left = Math.round(x) + "px";
        agent.style.top = Math.round(y) + "px";
        if (composing) showComposition(true);
    };

    // Keyboard pastes land here; pastes into the agent (long-press menu) arrive as input instead.
    window.addEventListener("paste", (event) => {
        if (event.target === agent) return;
        const text = event.clipboardData ? event.clipboardData.getData("text/plain") : "";
        stringToUTF8(text, clipboard, clipboard_size);
        event.preventDefault();
        _nv_imgui_js_paste();
    });
});

EM_JS(void, js_ime_move, (float x, float y, float height), {
    if (Module.nvImeMove) Module.nvImeMove(x, y, height);
});

EM_JS(int, js_text_agent_focused, (void), {
    return document.activeElement === document.getElementById("nv-text-agent") ? 1 : 0;
});

EM_JS(void, js_focus_text_agent, (int focus), {
    const agent = document.getElementById("nv-text-agent");
    if (focus) agent.focus({preventScroll: true});
    else agent.blur();
});

EM_JS(void, js_write_clipboard, (const char* text), {
    if (navigator.clipboard) navigator.clipboard.writeText(UTF8ToString(text)).catch(() => {});
});

// The canvas's pointer follows the cursor ImGui asks for (a resize cursor over a splitter). The
// page sets it only when it changes.
EM_JS(void, js_set_cursor, (const char* name), {
    const canvas = document.getElementById("canvas");
    const cursor = UTF8ToString(name);
    if (canvas && canvas.style.cursor !== cursor)
        canvas.style.cursor = cursor;
});

EM_JS(int, js_touch_is_primary, (void), {
    return matchMedia("(pointer: coarse)").matches ? 1 : 0;
});

// Entry points for the JavaScript above; not part of the public API.
EMSCRIPTEN_KEEPALIVE void nv_imgui_js_char(u32 codepoint)
{
    ImGuiIO_AddInputCharacter(igGetIO_Nil(), codepoint);
}

EMSCRIPTEN_KEEPALIVE void nv_imgui_js_backspace(void)
{
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddKeyEvent(io, ImGuiKey_Backspace, 1);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_Backspace, 0);
}

// The pasted text is already in the clipboard buffer; replaying Ctrl+V makes ImGui read it.
EMSCRIPTEN_KEEPALIVE void nv_imgui_js_paste(void)
{
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, 1);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_V, 1);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_V, 0);
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, 0);
}

internal const char* get_clipboard(ImGuiContext* context)
{
    (void)context;
    NvImgui* imgui = igGetPlatformIO_Nil()->Platform_ClipboardUserData;
    return imgui->clipboard;
}

internal void set_clipboard(ImGuiContext* context, const char* text)
{
    (void)context;
    NvImgui* imgui = igGetPlatformIO_Nil()->Platform_ClipboardUserData;
    umm length = strlen(text);
    if (length > NV_IMGUI_CLIPBOARD_SIZE - 1)
        length = NV_IMGUI_CLIPBOARD_SIZE - 1;
    memcpy(imgui->clipboard, text, length);
    imgui->clipboard[length] = 0;
    // NOTE: Best effort: browsers may refuse writes that are not close to a user gesture.
    js_write_clipboard(imgui->clipboard);
}

//
// Input
//

// NOTE: Mouse and touch positions use the page's client coordinates, which equal canvas
// coordinates because the page pins the canvas to the top-left corner of the viewport.

typedef struct KeyName {
    const char* code;
    ImGuiKey key;
} KeyName;

global const KeyName named_keys[] = {
    {"Tab", ImGuiKey_Tab},
    {"ArrowLeft", ImGuiKey_LeftArrow},
    {"ArrowRight", ImGuiKey_RightArrow},
    {"ArrowUp", ImGuiKey_UpArrow},
    {"ArrowDown", ImGuiKey_DownArrow},
    {"PageUp", ImGuiKey_PageUp},
    {"PageDown", ImGuiKey_PageDown},
    {"Home", ImGuiKey_Home},
    {"End", ImGuiKey_End},
    {"Insert", ImGuiKey_Insert},
    {"Delete", ImGuiKey_Delete},
    {"Backspace", ImGuiKey_Backspace},
    {"Space", ImGuiKey_Space},
    {"Enter", ImGuiKey_Enter},
    {"NumpadEnter", ImGuiKey_KeypadEnter},
    {"Escape", ImGuiKey_Escape},
    {"ControlLeft", ImGuiKey_LeftCtrl},
    {"ShiftLeft", ImGuiKey_LeftShift},
    {"AltLeft", ImGuiKey_LeftAlt},
    {"MetaLeft", ImGuiKey_LeftSuper},
    {"ControlRight", ImGuiKey_RightCtrl},
    {"ShiftRight", ImGuiKey_RightShift},
    {"AltRight", ImGuiKey_RightAlt},
    {"MetaRight", ImGuiKey_RightSuper},
    {"Quote", ImGuiKey_Apostrophe},
    {"Comma", ImGuiKey_Comma},
    {"Minus", ImGuiKey_Minus},
    {"Period", ImGuiKey_Period},
    {"Slash", ImGuiKey_Slash},
    {"Semicolon", ImGuiKey_Semicolon},
    {"Equal", ImGuiKey_Equal},
    {"BracketLeft", ImGuiKey_LeftBracket},
    {"Backslash", ImGuiKey_Backslash},
    {"BracketRight", ImGuiKey_RightBracket},
    {"Backquote", ImGuiKey_GraveAccent},
};

// Maps a DOM KeyboardEvent.code (the physical key) to an ImGuiKey.
internal ImGuiKey key_from_code(const char* code)
{
    umm length = strlen(code);
    if (length == 4 && memcmp(code, "Key", 3) == 0 && code[3] >= 'A' && code[3] <= 'Z')
        return (ImGuiKey)(ImGuiKey_A + (code[3] - 'A'));
    if (length == 6 && memcmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9')
        return (ImGuiKey)(ImGuiKey_0 + (code[5] - '0'));
    if (length == 7 && memcmp(code, "Numpad", 6) == 0 && code[6] >= '0' && code[6] <= '9')
        return (ImGuiKey)(ImGuiKey_Keypad0 + (code[6] - '0'));
    if ((length == 2 || length == 3) && code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
        s32 n = (length == 2) ? code[1] - '0' : (code[1] - '0') * 10 + (code[2] - '0');
        if (n >= 1 && n <= 12)
            return (ImGuiKey)(ImGuiKey_F1 + (n - 1));
    }
    for (umm i = 0; i < NV_ARRAY_COUNT(named_keys); ++i) {
        if (strcmp(code, named_keys[i].code) == 0)
            return named_keys[i].key;
    }
    return ImGuiKey_None;
}

// True when `text` is exactly one UTF-8 encoded character, which is how KeyboardEvent.key
// reports printable keys ("a", "A", "é", " "), as opposed to names like "Enter".
internal b32 is_single_character(const char* text)
{
    u8 lead = (u8)text[0];
    umm expected = (lead < 0x80) ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
    return expected && strlen(text) == expected;
}

// NOTE: Cmd (metaKey) is reported as Ctrl so macOS shortcuts (Cmd+C, Cmd+V, Cmd+A) work.
internal void add_modifiers(ImGuiIO* io, const EmscriptenKeyboardEvent* event)
{
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, event->ctrlKey || event->metaKey);
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Shift, event->shiftKey);
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Alt, event->altKey);
}

// Browser callbacks return true to stop the browser's default handling. Return values and
// parameter types are fixed by Emscripten's html5.h.
internal bool on_key(int event_type, const EmscriptenKeyboardEvent* event, void* userdata)
{
    NvImgui* imgui = userdata;
    ImGuiIO* io = igGetIO_Nil();
    b32 down = (event_type == EMSCRIPTEN_EVENT_KEYDOWN);
    b32 shortcut = event->ctrlKey || event->metaKey;
    add_modifiers(io, event);

    // On-screen keyboards often leave `code` empty, but name special keys in `key`.
    ImGuiKey key = key_from_code(event->code);
    if (key == ImGuiKey_None)
        key = key_from_code(event->key);

    // Leave paste to the browser: it fires a paste event carrying the text (see the glue above).
    if (shortcut && key == ImGuiKey_V)
        return 0;

    // While the text agent has focus, its input events carry text and Backspace; taking them
    // from keydown too would apply them twice. The agent needs the default action to see them.
    if (js_text_agent_focused()) {
        // A key the input method takes ("Process") is the method's: not ImGui's, and its default
        // action must stay or the composition stops.
        if (strcmp(event->key, "Process") == 0)
            return 0;
        if (key != ImGuiKey_None && key != ImGuiKey_Backspace)
            ImGuiIO_AddKeyEvent(io, key, down);
        // Keys that do not type text act in ImGui only: the agent's own caret must not move
        // (arrows, Home, End) and chords must not select or undo in it. Typing, Backspace and
        // paste stay with the agent.
        b32 types = !shortcut && is_single_character(event->key);
        if (shortcut)
            return key != ImGuiKey_V;
        return !types && key != ImGuiKey_Backspace;
    }

    if (key != ImGuiKey_None)
        ImGuiIO_AddKeyEvent(io, key, down);

    // NOTE: Text is taken from keydown instead of keypress so keydown can be consumed below
    // (consuming keydown suppresses keypress).
    if (down && !shortcut && is_single_character(event->key))
        ImGuiIO_AddInputCharactersUTF8(io, event->key);

    // A key the app asked to keep is not the browser's either.
    b32 claimed = 0;
    if (imgui->claims_key && key != ImGuiKey_None) {
        ImGuiKeyChord chord = (ImGuiKeyChord)key | (shortcut ? ImGuiMod_Ctrl : 0) | (event->shiftKey ? ImGuiMod_Shift : 0) |
                              (event->altKey ? ImGuiMod_Alt : 0);
        claimed = imgui->claims_key(imgui->claims_key_data, chord);
    }
    return io->WantCaptureKeyboard || claimed;
}

// A press in the view drags the camera: left orbits, right and middle pan. A left press that does
// not move is a tap.
internal b32 view_mouse(NvImgui* imgui, int event_type, const EmscriptenMouseEvent* event)
{
    f32 x = (f32)event->clientX;
    f32 y = (f32)event->clientY;
    if (event_type == EMSCRIPTEN_EVENT_MOUSEDOWN) {
        if (imgui->view_mouse_button || !nv_imgui_in_view(imgui, x, y))
            return 0;
        imgui->view_mouse_button = event->button + 1;
        imgui->view_moved = 0;
        imgui->view_press_x = imgui->view_mouse_x = x;
        imgui->view_press_y = imgui->view_mouse_y = y;
        imgui->view_press_mods = (event->ctrlKey || event->metaKey ? ImGuiMod_Ctrl : 0) |
                                 (event->shiftKey ? ImGuiMod_Shift : 0) | (event->altKey ? ImGuiMod_Alt : 0);
        if (imgui->view_grab && event->button == 0) {
            imgui->view_grab_wait = 2;
            imgui->view_tap_held = 0;
            imgui->view_grab_touch = 0;
        }
        return 1;
    }
    if (!imgui->view_mouse_button)
        return 0;
    if (event_type == EMSCRIPTEN_EVENT_MOUSEMOVE) {
        f32 dx = x - imgui->view_mouse_x;
        f32 dy = y - imgui->view_mouse_y;
        imgui->view_mouse_x = x;
        imgui->view_mouse_y = y;
        if (imgui->view_mouse_button == 1) {
            imgui->view_pending.orbit_x += dx;
            imgui->view_pending.orbit_y += dy;
        } else {
            imgui->view_pending.pan_x += dx;
            imgui->view_pending.pan_y += dy;
        }
        if (fabsf(x - imgui->view_press_x) + fabsf(y - imgui->view_press_y) > 4.0f)
            imgui->view_moved = 1;
        return 1;
    }
    if (event_type == EMSCRIPTEN_EVENT_MOUSEUP && event->button + 1 == imgui->view_mouse_button) {
        if (!imgui->view_moved && imgui->view_mouse_button == 1) {
            if (imgui->view_grab_wait)
                imgui->view_tap_held = 1;
            else
                nv_imgui_view_tap(imgui, x, y);
        }
        imgui->view_mouse_button = 0;
        return 1;
    }
    return 0;
}

internal bool on_mouse(int event_type, const EmscriptenMouseEvent* event, void* userdata)
{
    NvImgui* imgui = userdata;
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_Mouse);
    ImGuiIO_AddMousePosEvent(io, (f32)event->clientX, (f32)event->clientY);
    if (view_mouse(imgui, event_type, event))
        return event_type != EMSCRIPTEN_EVENT_MOUSEMOVE;

    if (event_type == EMSCRIPTEN_EVENT_MOUSEDOWN || event_type == EMSCRIPTEN_EVENT_MOUSEUP) {
        // DOM order is left, middle, right; ImGui's is left, right, middle.
        local_persist const s32 buttons[3] = {0, 2, 1};
        if (event->button < 3)
            ImGuiIO_AddMouseButtonEvent(io, buttons[event->button], event_type == EMSCRIPTEN_EVENT_MOUSEDOWN);
    }
    return event_type != EMSCRIPTEN_EVENT_MOUSEMOVE && io->WantCaptureMouse;
}

internal bool on_wheel(int event_type, const EmscriptenWheelEvent* event, void* userdata)
{
    (void)event_type;
    NvImgui* imgui = userdata;
    ImGuiIO* io = igGetIO_Nil();
    if (nv_imgui_in_view(imgui, io->MousePos.x, io->MousePos.y)) {
        // One wheel notch (about 100 pixels, or 3 lines) zooms by about 15%.
        f32 pixels = (event->deltaMode == DOM_DELTA_PIXEL) ? 1.0f : (event->deltaMode == DOM_DELTA_LINE) ? 33.0f : 400.0f;
        imgui->view_pending.dolly += (f32)event->deltaY * pixels * 0.0014f;
        return 1;
    }
    // ImGui counts in lines; browsers usually report pixels.
    f32 scale = (event->deltaMode == DOM_DELTA_PIXEL) ? 1.0f / 100.0f : (event->deltaMode == DOM_DELTA_LINE) ? 1.0f / 3.0f : 1.0f;
    ImGuiIO_AddMouseWheelEvent(io, (f32)-event->deltaX * scale, (f32)-event->deltaY * scale);
    return io->WantCaptureMouse;
}

// A touch that is held still this long becomes a press, for widgets that are dragged in place.
#define TOUCH_PRESS_SECONDS 0.3

// Where the view's fingers are: their center and, with two, the distance between them.
internal void view_touch_center(NvImgui* imgui, f32* x, f32* y, f32* spread)
{
    *x = *y = *spread = 0.0f;
    u32 n = imgui->view_touch_count;
    for (u32 i = 0; i < n; ++i) {
        *x += imgui->view_touch_x[i] / (f32)n;
        *y += imgui->view_touch_y[i] / (f32)n;
    }
    if (n == 2) {
        f32 dx = imgui->view_touch_x[1] - imgui->view_touch_x[0];
        f32 dy = imgui->view_touch_y[1] - imgui->view_touch_y[0];
        *spread = sqrtf(dx * dx + dy * dy);
    }
}

// Touches that start in the view move the camera: one finger orbits, two pan and pinch-zoom, and
// a single finger that lifts without moving is a tap. Returns whether the event was the view's.
internal b32 view_touch(NvImgui* imgui, int event_type, const EmscriptenTouchEvent* event)
{
    if (event_type == EMSCRIPTEN_EVENT_TOUCHSTART) {
        if (imgui->touch_gesture == NV_TOUCH_NONE) {
            const EmscriptenTouchPoint* first = NULL;
            for (int i = 0; i < event->numTouches && !first; ++i) {
                if (event->touches[i].isChanged)
                    first = &event->touches[i];
            }
            if (!first || !nv_imgui_in_view(imgui, (f32)first->clientX, (f32)first->clientY))
                return 0;
            imgui->touch_gesture = NV_TOUCH_VIEW;
            imgui->view_touch_count = 0;
            imgui->view_touch_multi = 0;
            imgui->view_moved = 0;
            imgui->view_press_x = (f32)first->clientX;
            imgui->view_press_y = (f32)first->clientY;
            imgui->view_press_mods = 0;
            if (imgui->view_grab) {
                // ImGui learns where the finger is, so the app can tell what is under it.
                ImGuiIO_AddMousePosEvent(igGetIO_Nil(), imgui->view_press_x, imgui->view_press_y);
                imgui->view_grab_wait = 2;
                imgui->view_tap_held = 0;
                imgui->view_grab_touch = 1;
            }
        } else if (imgui->touch_gesture != NV_TOUCH_VIEW) {
            return 0;
        }
        for (int i = 0; i < event->numTouches; ++i) {
            const EmscriptenTouchPoint* t = &event->touches[i];
            if (!t->isChanged || imgui->view_touch_count >= NV_VIEW_MAX_TOUCHES)
                continue;
            u32 k = imgui->view_touch_count++;
            imgui->view_touch_id[k] = t->identifier;
            imgui->view_touch_x[k] = (f32)t->clientX;
            imgui->view_touch_y[k] = (f32)t->clientY;
        }
        if (imgui->view_touch_count > 1)
            imgui->view_touch_multi = 1;
        return 1;
    }
    if (imgui->touch_gesture != NV_TOUCH_VIEW)
        return 0;

    if (event_type == EMSCRIPTEN_EVENT_TOUCHMOVE) {
        f32 x0, y0, spread0;
        view_touch_center(imgui, &x0, &y0, &spread0);
        for (int i = 0; i < event->numTouches; ++i) {
            const EmscriptenTouchPoint* t = &event->touches[i];
            for (u32 k = 0; k < imgui->view_touch_count; ++k) {
                if (imgui->view_touch_id[k] == t->identifier) {
                    imgui->view_touch_x[k] = (f32)t->clientX;
                    imgui->view_touch_y[k] = (f32)t->clientY;
                }
            }
        }
        f32 x1, y1, spread1;
        view_touch_center(imgui, &x1, &y1, &spread1);
        if (imgui->view_touch_count == 1) {
            imgui->view_pending.orbit_x += x1 - x0;
            imgui->view_pending.orbit_y += y1 - y0;
            if (fabsf(x1 - imgui->view_press_x) + fabsf(y1 - imgui->view_press_y) > 8.0f * imgui->ui_scale)
                imgui->view_moved = 1;
        } else if (imgui->view_touch_count == 2) {
            imgui->view_pending.pan_x += x1 - x0;
            imgui->view_pending.pan_y += y1 - y0;
            if (spread0 > 1.0f && spread1 > 1.0f)
                imgui->view_pending.dolly += logf(spread0 / spread1);
        }
        return 1;
    }

    // Touch end or cancel: forget the lifted fingers.
    for (int i = 0; i < event->numTouches; ++i) {
        const EmscriptenTouchPoint* t = &event->touches[i];
        if (!t->isChanged)
            continue;
        for (u32 k = 0; k < imgui->view_touch_count; ++k) {
            if (imgui->view_touch_id[k] != t->identifier)
                continue;
            --imgui->view_touch_count;
            imgui->view_touch_id[k] = imgui->view_touch_id[imgui->view_touch_count];
            imgui->view_touch_x[k] = imgui->view_touch_x[imgui->view_touch_count];
            imgui->view_touch_y[k] = imgui->view_touch_y[imgui->view_touch_count];
            break;
        }
    }
    if (imgui->view_touch_count == 0) {
        if (event_type == EMSCRIPTEN_EVENT_TOUCHEND && !imgui->view_moved && !imgui->view_touch_multi) {
            if (imgui->view_grab_wait)
                imgui->view_tap_held = 1;
            else
                nv_imgui_view_tap(imgui, imgui->view_press_x, imgui->view_press_y);
        }
        imgui->touch_gesture = NV_TOUCH_NONE;
        // A lifted finger points at nothing; while a press waits, ImGui still needs to see it.
        if (!imgui->view_grab_wait)
            ImGuiIO_AddMousePosEvent(igGetIO_Nil(), -FLT_MAX, -FLT_MAX);
    }
    return 1;
}

// Touch drives ImGui's mouse with the first finger, but only once the gesture is clear:
// - moving mostly vertically past a threshold scrolls the panel, and ImGui sees no click;
// - moving sideways first, or holding still, presses (sliders drag sideways);
// - lifting before either is a tap: a press and a release together.
internal bool on_touch(int event_type, const EmscriptenTouchEvent* event, void* userdata)
{
    NvImgui* imgui = userdata;
    ImGuiIO* io = igGetIO_Nil();
    ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_TouchScreen);
    if (view_touch(imgui, event_type, event))
        return 1;
    const EmscriptenTouchPoint* touch = event->numTouches ? &event->touches[0] : NULL;
    f32 x = touch ? (f32)touch->clientX : imgui->touch_x;
    f32 y = touch ? (f32)touch->clientY : imgui->touch_y;

    if (event_type == EMSCRIPTEN_EVENT_TOUCHSTART) {
        if (imgui->touch_gesture != NV_TOUCH_NONE)
            return 1; // a second finger
        imgui->touch_gesture = NV_TOUCH_UNDECIDED;
        imgui->touch_start_x = imgui->touch_x = x;
        imgui->touch_start_y = imgui->touch_y = y;
        imgui->touch_start_time = nv_time_seconds();
        // Hovering the touched widget lets a tap on a text field be recognized (below).
        ImGuiIO_AddMousePosEvent(io, x, y);
    } else if (event_type == EMSCRIPTEN_EVENT_TOUCHMOVE) {
        f32 step = y - imgui->touch_y;
        imgui->touch_x = x;
        imgui->touch_y = y;
        if (imgui->touch_gesture == NV_TOUCH_UNDECIDED) {
            f32 dx = fabsf(x - imgui->touch_start_x);
            f32 dy = fabsf(y - imgui->touch_start_y);
            f32 threshold = 8.0f * imgui->ui_scale;
            if (dy >= threshold && dy >= dx) {
                imgui->touch_gesture = NV_TOUCH_SCROLL;
                step = y - imgui->touch_start_y;
            } else if (dx >= threshold) {
                nv_imgui_touch_press(imgui);
            }
        } else if (imgui->touch_gesture == NV_TOUCH_PRESS) {
            ImGuiIO_AddMousePosEvent(io, x, y);
        }
        if (imgui->touch_gesture == NV_TOUCH_SCROLL)
            imgui->touch_scroll_pending += step;
    } else {
        // Touch end or cancel.
        if (imgui->touch_gesture == NV_TOUCH_UNDECIDED && event_type == EMSCRIPTEN_EVENT_TOUCHEND) {
            // IMPORTANT: iOS only opens the keyboard for focus() called inside a touch handler,
            // and ImGui activates the field a frame later. The text cursor shows it is one.
            if (igGetMouseCursor() == ImGuiMouseCursor_TextInput) {
                js_focus_text_agent(1);
                imgui->text_agent_grace = 4;
            }
            ImGuiIO_AddMouseButtonEvent(io, 0, 1);
            ImGuiIO_AddMouseButtonEvent(io, 0, 0);
        } else if (imgui->touch_gesture == NV_TOUCH_PRESS) {
            ImGuiIO_AddMouseButtonEvent(io, 0, 0);
            if (io->WantTextInput)
                js_focus_text_agent(1);
        }
        imgui->touch_gesture = NV_TOUCH_NONE;
        // A lifted finger points at nothing, so the last widget touched does not stay hovered.
        ImGuiIO_AddMousePosEvent(io, -FLT_MAX, -FLT_MAX);
    }
    // Consuming touches stops the browser from also sending emulated mouse events.
    return 1;
}

// ImGui tells where the text caret is: the agent moves there, so a composing syllable and the input
// method's candidate window appear at the field being edited (docs/specs/korean.md).
internal void set_ime_data(ImGuiContext* context, ImGuiViewport* viewport, ImGuiPlatformImeData* data)
{
    (void)context, (void)viewport;
    if (data->WantVisible || data->WantTextInput)
        js_ime_move(data->InputPos.x, data->InputPos.y, data->InputLineHeight);
}

internal bool on_focus(int event_type, const EmscriptenFocusEvent* event, void* userdata)
{
    (void)event, (void)userdata;
    ImGuiIO_AddFocusEvent(igGetIO_Nil(), event_type == EMSCRIPTEN_EVENT_FOCUS);
    return 0;
}

internal void hook_input(NvImgui* imgui, NvWindow* window)
{
    const char* page = EMSCRIPTEN_EVENT_TARGET_WINDOW;
    emscripten_set_keydown_callback(page, imgui, 1, on_key);
    emscripten_set_keyup_callback(page, imgui, 1, on_key);
    emscripten_set_mousedown_callback(window->canvas_selector, imgui, 1, on_mouse);
    // Moves and releases are watched page-wide so a drag keeps working past the canvas edge.
    emscripten_set_mousemove_callback(page, imgui, 1, on_mouse);
    emscripten_set_mouseup_callback(page, imgui, 1, on_mouse);
    emscripten_set_wheel_callback(window->canvas_selector, imgui, 1, on_wheel);
    emscripten_set_touchstart_callback(window->canvas_selector, imgui, 1, on_touch);
    emscripten_set_touchmove_callback(window->canvas_selector, imgui, 1, on_touch);
    emscripten_set_touchend_callback(window->canvas_selector, imgui, 1, on_touch);
    emscripten_set_touchcancel_callback(window->canvas_selector, imgui, 1, on_touch);
    emscripten_set_focus_callback(page, NULL, 1, on_focus);
    emscripten_set_blur_callback(page, NULL, 1, on_focus);
}

void nv_imgui_platform_init(NvImgui* imgui, NvWindow* window)
{
    igGetIO_Nil()->BackendPlatformName = "nv_html5";
    ImGuiPlatformIO* platform_io = igGetPlatformIO_Nil();
    platform_io->Platform_SetImeDataFn = set_ime_data;
    platform_io->Platform_ClipboardUserData = imgui;
    platform_io->Platform_GetClipboardTextFn = get_clipboard;
    platform_io->Platform_SetClipboardTextFn = set_clipboard;

    hook_input(imgui, window);
    js_setup_text_agent(imgui->clipboard, NV_IMGUI_CLIPBOARD_SIZE);

    // Fingers need bigger text and hit areas than a mouse pointer.
    imgui->ui_scale = js_touch_is_primary() ? 1.3f : 1.0f;
}

void nv_imgui_platform_update(NvImgui* imgui)
{
    ImGuiIO* io = igGetIO_Nil();
    // Close the on-screen keyboard once ImGui stops editing text (after Enter, or a tap outside).
    if (imgui->text_agent_grace)
        --imgui->text_agent_grace;
    else if (!io->WantTextInput && js_text_agent_focused())
        js_focus_text_agent(0);
    // The desktop types through the agent too, so an input method can compose (a touch screen
    // focuses it from its tap handler instead).
    if (io->WantTextInput && imgui->ui_scale <= 1.0f && !js_text_agent_focused())
        js_focus_text_agent(1);

    // A touch held still long enough becomes a press; its scrolling is applied this frame.
    if (imgui->touch_gesture == NV_TOUCH_UNDECIDED && nv_time_seconds() - imgui->touch_start_time >= TOUCH_PRESS_SECONDS)
        nv_imgui_touch_press(imgui);
}

internal const char* css_cursor(ImGuiMouseCursor cursor)
{
    switch (cursor) {
    case ImGuiMouseCursor_TextInput: return "text";
    case ImGuiMouseCursor_ResizeEW: return "ew-resize";
    case ImGuiMouseCursor_ResizeNS: return "ns-resize";
    case ImGuiMouseCursor_Hand: return "pointer";
    default: return "default";
    }
}

void nv_imgui_platform_set_cursor(ImGuiMouseCursor cursor)
{
    js_set_cursor(css_cursor(cursor));
}
