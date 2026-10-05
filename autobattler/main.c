// The auto-battler executable (docs/specs/battle.md, "Executable"): it sets up the window, GPU, renderer and UI, reads the
// project file, and runs the frame: the battle ticks at a fixed 30 Hz inside it, and the view (battle_view.c) draws
// whatever state the ticks leave.

#include "game.h"

#include <engine/file.h>

#include <emscripten/emscripten.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// IMPORTANT: Global rather than on main's stack: main returns before the first frame runs.
global Game game_state;
global u8 permanent_memory[NV_MEGABYTES(48)];
global u8 scratch_memory[NV_MEGABYTES(4)];

#define MAX_TICKS_PER_FRAME 4 // at 1x; more at higher speeds, which need more ticks a frame

// The viewport and the panel never overlap, since input that starts in the viewport skips ImGui: the panel is on the right
// of a wide canvas and under a tall one.
void game_layout(Game* game)
{
    f32 ratio = nv_window_pixel_ratio(&game->window);
    f32 width_css = (f32)game->gpu.width / ratio, height_css = (f32)game->gpu.height / ratio;
    if (width_css >= height_css * 1.1f) {
        f32 panel_width_css = fminf(PANEL_WIDTH * game->imgui.ui_scale, width_css * 0.5f);
        game->layout.viewport = nv_window_framebuffer_rect_from_css(0.0f, 0.0f, width_css - panel_width_css, height_css, ratio);
        game->layout.panel = nv_window_framebuffer_rect_from_css(width_css - panel_width_css, 0.0f, width_css, height_css, ratio);
    } else {
        f32 panel_height_css = height_css * PANEL_HEIGHT_SHARE;
        game->layout.viewport = nv_window_framebuffer_rect_from_css(0.0f, 0.0f, width_css, height_css - panel_height_css, ratio);
        game->layout.panel = nv_window_framebuffer_rect_from_css(0.0f, height_css - panel_height_css, width_css, height_css, ratio);
    }
    game->layout.scene = nv_renderer_scene_output(&game->resolution, game->layout.viewport);
}

// What the view draws when the project file could not be read: an empty field and the error. The battle never runs on it.
global const BattleRules fallback_rules = {
    .cell_size = 2.0f, .grid_width = 32, .grid_length = 48, .zone_rows = 14, .round_ticks = 1800, .retarget_ticks = 8,
    .gravity = 9.8f, .stop_fraction = 0.9f, .min_damage_fraction = 0.25f};

internal void set_message(Game* game, b32 bad, const char* format, ...) __attribute__((format(printf, 3, 4)));

internal void set_message(Game* game, b32 bad, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(game->project_message, sizeof(game->project_message), format, arguments);
    va_end(arguments);
    nv_utf8_trim(game->project_message);
    game->project_message_bad = bad;
}

b32 game_load_project(Game* game, const char* name, const char* text, umm size)
{
    umm mark = game->scratch.used;
    BattleDefs* next = NV_PUSH_STRUCT(&game->scratch, BattleDefs);
    b32 ok = false;
    if (size > PROJECT_MAX_SIZE) {
        snprintf(next->first_error, sizeof(next->first_error), "%s: larger than %u KB", name, (u32)(PROJECT_MAX_SIZE / 1024));
        nv_utf8_trim(next->first_error);
        next->error_count = 1;
        nv_log(NV_LOG_ERROR, "battle", "%s", next->first_error);
    } else {
        ok = defs_read_project(next, name, text, size);
    }

    if (ok) {
        game->defs = *next;
        game->defs_ok = true;
        memcpy(game->project_text, text, size);
        game->project_size = size;
        snprintf(game->project_name, sizeof(game->project_name), "%s", name);
        nv_utf8_trim(game->project_name);
        ++game->project_loads;
        battle_init(&game->battle, &game->defs);
        view_apply_project(game);
    } else if (!game->defs_ok) {
        // Nothing good to keep: the panel shows this one's errors over an empty field.
        game->defs = *next;
        game->defs.rules = fallback_rules;
        game->project_size = 0;
        snprintf(game->project_name, sizeof(game->project_name), "%s", name);
        nv_utf8_trim(game->project_name);
        view_apply_project(game);
    } else {
        set_message(game, true, T("%s has %u error(s), so the project in use stays. The first: %s"), name, next->error_count,
                    next->first_error);
    }
    game->scratch.used = mark;
    return ok;
}

b32 game_project_action_allowed(const Game* game, ProjectAction action)
{
    if (game->local_file.status == NV_LOCAL_FILE_BUSY)
        return false;
    switch (action) {
    case PROJECT_OPEN: return true;
    case PROJECT_RELOAD: return game->local_file.kept;
    // In place only to the project's own file: after a bad file was opened the browser keeps that one instead.
    case PROJECT_SAVE: return game->project_size && game->kept_is_project && game->local_file.kept;
    case PROJECT_SAVE_AS: return game->project_size > 0;
    }
    return false;
}

b32 game_project_action(Game* game, ProjectAction action)
{
    if (!game_project_action_allowed(game, action))
        return false;
    switch (action) {
    case PROJECT_OPEN: return nv_local_file_open(PROJECT_EXTENSION);
    case PROJECT_RELOAD: return nv_local_file_reload();
    case PROJECT_SAVE: return nv_local_file_save(game->project_name, game->project_text, game->project_size, false);
    case PROJECT_SAVE_AS: return nv_local_file_save(game->project_name, game->project_text, game->project_size, true);
    }
    return false;
}

// The project packed with the game; on any failure `defs.first_error` says what, for the panel.
internal void load_built_in_project(Game* game)
{
    umm mark = game->scratch.used;
    NvFileData project = nv_file_read(&game->scratch, "/data/" PROJECT_BUILT_IN);
    if (project.ok) {
        game_load_project(game, PROJECT_BUILT_IN, (const char*)project.bytes, project.size);
    } else {
        BattleDefs* defs = &game->defs;
        snprintf(defs->first_error, sizeof(defs->first_error), PROJECT_BUILT_IN ": cannot read the file");
        ++defs->error_count;
        nv_log(NV_LOG_ERROR, "battle", "%s", defs->first_error);
        defs->rules = fallback_rules;
        snprintf(game->project_name, sizeof(game->project_name), PROJECT_BUILT_IN);
        view_apply_project(game);
    }
    game->scratch.used = mark;
}

// What the browser's file dialogs came back with (docs/specs/local_files.md): a file read is loaded as the project.
internal void poll_local_file(Game* game)
{
    NvLocalFile* file = &game->local_file;
    nv_local_file_poll(file);
    switch (file->status) {
    case NV_LOCAL_FILE_READ: {
        umm mark = game->scratch.used;
        NvFileData data = nv_local_file_take(&game->scratch, PROJECT_MAX_SIZE);
        if (!data.ok) {
            set_message(game, true, T("%s is larger than %u KB"), file->name, (u32)(PROJECT_MAX_SIZE / 1024));
            game->kept_is_project = false;
        } else if (game_load_project(game, file->name, (const char*)data.bytes, data.size)) {
            set_message(game, false, T("Opened %s"), file->name);
            game->kept_is_project = file->kept;
        } else {
            game->kept_is_project = false; // the browser now keeps the bad file: Reload reads it again, Save must not write it
            if (!game->defs_ok)
                set_message(game, true, T("%s has %u error(s)"), file->name, game->defs.error_count);
        }
        game->scratch.used = mark;
    } break;
    case NV_LOCAL_FILE_WRITTEN:
        if (file->kept) {
            snprintf(game->project_name, sizeof(game->project_name), "%s", file->name);
            nv_utf8_trim(game->project_name);
            game->kept_is_project = true;
        }
        set_message(game, false, T("Saved %s"), file->name);
        break;
    case NV_LOCAL_FILE_FAILED:
        set_message(game, true, T("The file could not be opened or saved: %s"), file->error);
        break;
    case NV_LOCAL_FILE_IDLE:
    case NV_LOCAL_FILE_BUSY:
    case NV_LOCAL_FILE_CANCELED:
        break;
    }
}

internal void frame(void* userdata)
{
    Game* game = userdata;
    nv_log_pump();
    f64 now = nv_time_seconds();
    f32 dt = (f32)(now - game->last_time);
    game->last_time = now;
    if (dt > 0.1f)
        dt = 0.1f; // a hidden tab's long pause is not caught up on

    NvGpuFrame gpu_frame = nv_gpu_begin_frame(&game->gpu);
    if (!gpu_frame.ok)
        return;
    WGPUTextureView target = gpu_frame.view;
    game_layout(game);
    game->imgui.view_rect = game->layout.viewport;
    nv_imgui_new_frame(&game->imgui, dt);
    poll_local_file(game);
    view_draw_build_label(game);
    view_panel(game);
    view_input(game);

    // The game clock: real time times the speed, stopped while paused. The battle runs it in whole ticks, so a speed only
    // changes how many ticks a frame runs (the result is the same at any speed); what is left over decides where between
    // two ticks the view draws. The effects follow the same clock.
    f32 game_dt = game->paused ? 0.0f : dt * game->speed;
    f32 effects_dt = game_dt;
    Battle* battle = &game->battle;
    // A step moves the paused battle one tick: forward runs it (its effects get that tick's time), back replays the round to
    // the tick before, and the effects, which cannot run backward, are cleared.
    if (game->defs_ok && game->step > 0 && battle->phase == BATTLE_FIGHT) {
        battle_tick(battle);
        view_on_tick(game);
        effects_dt = BATTLE_TICK_SECONDS; // not the battle's clock: that stays paused
    } else if (game->defs_ok && game->step < 0 && battle->phase != BATTLE_DEPLOY && battle->tick > 0) {
        battle_seek(battle, battle->tick - 1);
        nv_vfx_clear(&game->vfx);
    }
    game->step = 0;
    if (game->defs_ok && battle->phase == BATTLE_FIGHT) {
        game->accumulator += game_dt;
        u32 max_ticks = (u32)ceilf(MAX_TICKS_PER_FRAME * fmaxf(game->speed, 1.0f));
        for (u32 ticks = 0; game->accumulator >= BATTLE_TICK_SECONDS && battle->phase == BATTLE_FIGHT; ++ticks) {
            if (ticks == max_ticks) {
                game->accumulator = 0.0f; // too far behind: run slower rather than spiral
                break;
            }
            battle_tick(battle);
            view_on_tick(game);
            game->accumulator -= BATTLE_TICK_SECONDS;
        }
    } else {
        game->accumulator = 0.0f;
    }
    view_update(game, effects_dt);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(game->gpu.device, NULL);
    nv_renderer_draw(&game->renderer, game->scene, NULL, game->layout.scene, encoder, target);
    nv_imgui_render(&game->imgui, encoder, target);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(game->gpu.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    nv_renderer_end_frame(&game->renderer);
    nv_gpu_end_frame(&game->gpu);
}

#if !defined(NDEBUG)
// For tests (Debug builds). Module._battle_debug(n): 0 phase, 1 tick, 2 outcome, 3 player units alive, 4 enemy units alive,
// 5 the state's hash, 6 units, 7 shells, 8 supply left, 9 definitions loaded, 10 chosen unit type, 11 the game clock's speed
// in hundredths (0 while paused), 12 the shadow map's size (0 without one), 13 the shadow distance in meters, 14 projects put
// in place (the packed one included), 15 whether Save would write the kept local file in place, 16 the grid's width, 17 the
// local file's status (NvLocalFileStatus, as last polled).
EMSCRIPTEN_KEEPALIVE int battle_debug(int which)
{
    const Game* game = &game_state;
    const Battle* battle = &game->battle;
    switch (which) {
    case 0: return (int)battle->phase;
    case 1: return (int)battle->tick;
    case 2: return (int)battle->outcome;
    case 3: return (int)battle_alive_count(battle, TEAM_PLAYER);
    case 4: return (int)battle_alive_count(battle, TEAM_ENEMY);
    case 5: return (int)battle_hash(battle);
    case 6: return (int)battle->unit_count;
    case 7: return (int)battle->projectile_count;
    case 8: return game->defs_ok ? (int)battle_supply_left(battle) : -1;
    case 9: return game->defs_ok;
    case 11: return game->paused ? 0 : (int)(game->speed * 100.0f + 0.5f);
    case 12: return (int)game->renderer.shadow_size;
    case 13: return (int)(game->renderer.shadows.distance + 0.5f);
    case 14: return (int)game->project_loads;
    case 15: return game->kept_is_project && game->local_file.kept;
    case 16: return (int)game->defs.rules.grid_width;
    case 17: return (int)game->local_file.status;
    default: return (int)game->selected_def;
    }
}

// Sets the game clock's speed as the panel's buttons do: 0 pauses (keeping the speed), anything else is the speed.
EMSCRIPTEN_KEEPALIVE void battle_debug_set_speed(float speed)
{
    game_state.paused = speed <= 0.0f;
    if (speed > 0.0f)
        game_state.speed = speed;
}

// Asks for a step as the panel's buttons do (+1 forward, -1 back), taken by the next frame; it pauses.
EMSCRIPTEN_KEEPALIVE void battle_debug_step(int direction)
{
    game_state.step = direction > 0 ? 1 : -1;
    game_state.paused = true;
}

// Loads `text` as a project named `name`, as a local file opened in the panel would be; 1 when it was good.
EMSCRIPTEN_KEEPALIVE int battle_debug_load(const char* name, const char* text)
{
    return game_load_project(&game_state, name, text, strlen(text));
}

// Presses a project button of the panel (ProjectAction: 0 Open..., 1 Reload, 2 Save, 3 Save as...); 1 when it started, 0 when
// the button is disabled.
EMSCRIPTEN_KEEPALIVE int battle_debug_project_action(int action)
{
    return game_project_action(&game_state, (ProjectAction)action);
}

// The panel's project message, for tests.
EMSCRIPTEN_KEEPALIVE const char* battle_debug_project_message(void)
{
    return game_state.project_message;
}

// Places player unit type `def` on cell (x, row) as a tap would; 1 when it was placed.
EMSCRIPTEN_KEEPALIVE int battle_debug_deploy(int def, int x, int row)
{
    return game_state.defs_ok && battle_place(&game_state.battle, (u32)def, x, row);
}

EMSCRIPTEN_KEEPALIVE int battle_debug_start(void)
{
    nv_vfx_clear(&game_state.vfx);
    game_state.accumulator = 0.0f;
    game_state.paused = false; // as the panel's Start does
    return game_state.defs_ok && battle_start(&game_state.battle);
}

// Runs `ticks` ticks at once, with the view's effects, as if the frames had been that long.
EMSCRIPTEN_KEEPALIVE void battle_debug_run(int ticks)
{
    for (int i = 0; i < ticks && game_state.battle.phase == BATTLE_FIGHT; ++i) {
        battle_tick(&game_state.battle);
        view_on_tick(&game_state);
    }
}

// A region's rectangle in CSS pixels: region 0 the viewport, 1 the panel; component 0 x, 1 y, 2 width, 3 height.
EMSCRIPTEN_KEEPALIVE float battle_debug_layout(int region, int component)
{
    const Game* game = &game_state;
    f32 ratio = nv_window_pixel_ratio(&game->window);
    const NvRect* r = region ? &game->layout.panel : &game->layout.viewport;
    f32 values[4] = {(f32)r->x / ratio, (f32)r->y / ratio, (f32)r->width / ratio, (f32)r->height / ratio};
    return values[component];
}

// Where a world point is on screen, in CSS pixels (axis 0 x, 1 y), as the camera drew the last frame.
EMSCRIPTEN_KEEPALIVE float battle_debug_project(float x, float y, float z, int axis)
{
    Game* game = &game_state;
    NvMat4 view, projection;
    nv_renderer_camera_matrices(game->scene, game->layout.scene, &view, &projection);
    NvMat4 view_projection = nv_mat4_mul(projection, view);
    f32 w = view_projection.e[3] * x + view_projection.e[7] * y + view_projection.e[11] * z + view_projection.e[15];
    NvVec3 p = nv_mat4_transform_point(view_projection, nv_vec3(x, y, z));
    const NvSceneOutput* out = &game->layout.scene;
    f32 screen_x = (f32)out->image.x + (p.x / w * 0.5f + 0.5f) * (f32)out->width * out->pixel_width;
    f32 screen_y = (f32)out->image.y + (0.5f - p.y / w * 0.5f) * (f32)out->height * out->pixel_height;
    return (axis ? screen_y : screen_x) / nv_window_pixel_ratio(&game->window);
}
#endif

int main(void)
{
    Game* game = &game_state;
    nv_arena_init(&game->permanent, permanent_memory, sizeof(permanent_memory));
    nv_arena_init(&game->scratch, scratch_memory, sizeof(scratch_memory));
    game_strings_init();
    game->scene = NV_PUSH_STRUCT(&game->permanent, NvScene);

    nv_window_create(&game->window, "autobattler");
    if (!nv_gpu_create(&game->gpu, &game->window)) {
        nv_log(NV_LOG_ERROR, "battle", "fatal: failed to initialize WebGPU");
        return 1;
    }
    nv_renderer_init(&game->renderer, &game->gpu, &game->permanent);
    nv_imgui_init(&game->imgui, &game->gpu, &game->window, &game->permanent);
    nv_imgui_load_ui_font(&game->imgui, &game->permanent);
    nv_window_download_text(game->download_text, sizeof(game->download_text));
    nv_strings_set_language(nv_strings_browser_language());

    b32 touch = game->imgui.ui_scale > 1.0f;
    // Shadows (docs/specs/shadows.md) as in the sandbox: lighter on touch screens. The distance follows the camera
    // (view_update).
    game->renderer.shadows = (NvShadowSettings){
        .size = touch ? 1024 : 2048,
        .format = NV_SHADOW_FORMAT_DEPTH32F,
        .filter = touch ? NV_SHADOW_FILTER_LOW : NV_SHADOW_FILTER_HIGH,
    };
    game->renderer.msaa = 4;
    game->renderer.post = (NvPostSettings){.tone = NV_TONE_PBR_NEUTRAL, .exposure = 1.0f, .bloom = 1, .bloom_intensity = 0.04f};
    // The scene is drawn at the viewport's own pixels; a phone's GPU is the limit, so at half (as the sandbox does).
    game->speed = 1.0f;
    game->resolution = (NvResolution){.mode = NV_RESOLUTION_SCALE, .divisor = touch ? 2 : 1};

    game->project_text = NV_PUSH_ARRAY(&game->permanent, PROJECT_MAX_SIZE, char);
    view_build(game);
    load_built_in_project(game);
    game->last_time = nv_time_seconds();
    nv_window_run(&game->window, frame, game);
    return 0;
}
