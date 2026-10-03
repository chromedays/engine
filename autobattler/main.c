// The auto-battler executable (docs/specs/battle.md, "Executable"): it sets up the window, GPU, renderer and UI, reads the
// definition files, and runs the frame: the battle ticks at a fixed 30 Hz inside it, and the view (battle_view.c) draws
// whatever state the ticks leave.

#include "game.h"

#include <emscripten/emscripten.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// IMPORTANT: Global rather than on main's stack: main returns before the first frame runs.
global Game game_state;
global u8 permanent_memory[NV_MEGABYTES(48)];
global u8 scratch_memory[NV_MEGABYTES(4)];

#define MAX_TICKS_PER_FRAME 4 // at 1x; more at higher speeds, which need more ticks a frame

// A rectangle of the canvas from CSS pixel corners.
internal NvRect css_rect(f32 x0, f32 y0, f32 x1, f32 y1, f32 ratio)
{
    u32 left = (u32)(x0 * ratio + 0.5f), top = (u32)(y0 * ratio + 0.5f);
    u32 right = (u32)(x1 * ratio + 0.5f), bottom = (u32)(y1 * ratio + 0.5f);
    if (right < left)
        right = left;
    if (bottom < top)
        bottom = top;
    return (NvRect){left, top, right - left, bottom - top};
}

// The viewport and the panel never overlap, since input that starts in the viewport skips ImGui: the panel is on the right
// of a wide canvas and under a tall one.
void game_layout(Game* game)
{
    f32 ratio = nv_window_pixel_ratio(&game->window);
    f32 width = (f32)game->gpu.width / ratio, height = (f32)game->gpu.height / ratio;
    if (width >= height * 1.1f) {
        f32 panel_width = fminf(PANEL_WIDTH * game->imgui.ui_scale, width * 0.5f);
        game->layout.viewport = css_rect(0.0f, 0.0f, width - panel_width, height, ratio);
        game->layout.panel = css_rect(width - panel_width, 0.0f, width, height, ratio);
    } else {
        f32 panel_height = height * PANEL_HEIGHT_SHARE;
        game->layout.viewport = css_rect(0.0f, 0.0f, width, height - panel_height, ratio);
        game->layout.panel = css_rect(0.0f, height - panel_height, width, height, ratio);
    }
    game->layout.scene = nv_renderer_scene_output(&game->resolution, game->layout.viewport);
}

// The text of a file packed into the executable, in the scratch arena; NULL when it cannot be read.
internal const char* read_text_file(Game* game, const char* path, umm* size)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* text = NULL;
    if (length >= 0 && (umm)length < game->scratch.size - game->scratch.used) {
        text = NV_PUSH_ARRAY(&game->scratch, (umm)length + 1, char);
        *size = fread(text, 1, (umm)length, file);
    }
    fclose(file);
    return text;
}

// Reads units.txt and stage.txt; on any failure `defs.first_error` says what, for the panel.
internal b32 load_defs(Game* game)
{
    BattleDefs* defs = &game->defs;
    umm mark = game->scratch.used;
    umm units_size = 0, stage_size = 0;
    const char* units = read_text_file(game, "/data/units.txt", &units_size);
    const char* stage = read_text_file(game, "/data/stage.txt", &stage_size);
    b32 ok = false;
    if (!units || !stage) {
        snprintf(defs->first_error, sizeof(defs->first_error), "%s: cannot read the file", units ? "stage.txt" : "units.txt");
        ++defs->error_count;
        nv_log(NV_LOG_ERROR, "battle", "%s", defs->first_error);
    } else {
        // The stage looks unit names up, so it is read only when the units are good.
        ok = defs_read_units(defs, "units.txt", units, units_size) && defs_read_stage(defs, "stage.txt", stage, stage_size);
    }
    game->scratch.used = mark;
    return ok;
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

    WGPUTextureView target = nv_gpu_begin_frame(&game->gpu);
    if (!target)
        return;
    game_layout(game);
    game->imgui.view_rect = game->layout.viewport;
    nv_imgui_new_frame(&game->imgui, dt);
    view_build_label(game);
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
// in hundredths (0 while paused).
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
    game->renderer.msaa = 4;
    game->renderer.post = (NvPostSettings){.tone = NV_TONE_PBR_NEUTRAL, .exposure = 1.0f, .bloom = 1, .bloom_intensity = 0.04f};
    // The scene is drawn at the viewport's own pixels; a phone's GPU is the limit, so at half (as the editor app does).
    game->speed = 1.0f;
    game->resolution = (NvResolution){.mode = NV_RESOLUTION_SCALE, .divisor = touch ? 2 : 1};

    game->defs_ok = load_defs(game);
    view_build(game);
    if (game->defs_ok)
        battle_init(&game->battle, &game->defs);
    game->last_time = nv_time_seconds();
    nv_window_run(&game->window, frame, game);
    return 0;
}
