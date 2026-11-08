// The stress sample's shell (docs/specs/samples.md): the window, the frame, the orbit camera, the panel (stats and results), the
// benchmark that runs by itself, and the read-only debug exports.

#include "stress.h"

// The commit's hash and subject line and the build type (nv_version.h, written by cmake/version.cmake on every build).
#include "nv_version.h"

#include <stdio.h>
#include <string.h>

global Stress stress_state;
global u8 permanent_memory[NV_MEGABYTES(48)];
global u8 scratch_memory[NV_MEGABYTES(32)]; // holds a glb while it loads
global u8 anim_memory[NV_MEGABYTES(48)];

#define ORBIT_RADIANS_PER_PIXEL 0.008f
#define PANEL_WIDTH 340.0f // CSS pixels, times the UI scale

// A step starts from here: no workload, no effects.
void stress_reset(Stress* stress)
{
    nv_vfx_clear(&stress->vfx);
    workloads_reset(stress);
}

u32 stress_live_nodes(const NvScene* scene)
{
    u32 count = 0;
    for (u32 i = 1; i <= scene->node_count; ++i)
        count += scene->nodes[i].gen & 1;
    return count;
}

//
// Frame times
//

internal f64 now_ms(void)
{
    return nv_time_seconds() * 1000.0;
}

f64 stress_load(const FrameTimes* t)
{
    f64 cpu = t->anim + t->scene + t->draw + t->ui;
    f64 gpu = t->gpu + t->gpu_shadow + t->gpu_upscale + t->gpu_bloom + t->gpu_particles;
    f64 busy = cpu > gpu ? cpu : gpu;
    return t->frame > 0.0 ? busy / t->frame * 100.0 : 0.0;
}

internal void add_times(FrameTimes* sum, const FrameTimes* t)
{
    sum->frame += t->frame;
    sum->anim += t->anim;
    sum->scene += t->scene;
    sum->draw += t->draw;
    sum->ui += t->ui;
    sum->gpu += t->gpu;
    sum->gpu_shadow += t->gpu_shadow;
    sum->gpu_upscale += t->gpu_upscale;
    sum->gpu_bloom += t->gpu_bloom;
    sum->gpu_particles += t->gpu_particles;
}

internal FrameTimes divide_times(const FrameTimes* sum, f64 n)
{
    return (FrameTimes){sum->frame / n, sum->anim / n, sum->scene / n, sum->draw / n, sum->ui / n, sum->gpu / n,
                        sum->gpu_shadow / n, sum->gpu_upscale / n, sum->gpu_bloom / n, sum->gpu_particles / n};
}

// Averages the frame times over windows of about a second, for the stats.
internal void accumulate_times(Stress* stress, f64 now)
{
    add_times(&stress->window_sum, &stress->times);
    if (stress->times.frame > stress->window_worst_frame)
        stress->window_worst_frame = stress->times.frame;
    ++stress->window_frames;
    if (now - stress->window_start < 1.0)
        return;
    stress->shown_average = divide_times(&stress->window_sum, (f64)stress->window_frames);
    stress->shown_worst_frame = stress->window_worst_frame;
    stress->window_sum = (FrameTimes){0};
    stress->window_worst_frame = 0.0;
    stress->window_frames = 0;
    stress->window_start = now;
}

// "585 x 497, scale 1/2": what the scene renders at.
internal void resolution_text(Stress* stress, char* out, umm size)
{
    snprintf(out, size, "%u x %u, scale 1/%u", stress->scene_output.width, stress->scene_output.height, stress->resolution.divisor);
}

// "2048 32-bit float High, 30 m", or "off".
internal void shadow_settings_text(const NvShadowSettings* s, char* out, umm size)
{
    if (!s->size)
        snprintf(out, size, "off");
    else
        snprintf(out, size, "%u %s %s, %.0f m", s->size, s->format == NV_SHADOW_FORMAT_DEPTH16 ? "16-bit" : "32-bit float",
                 s->filter == NV_SHADOW_FILTER_HIGH ? "High" : "Low", (f64)s->distance);
}

//
// Benchmark
//

// Step `step`: the workloads from their start, then the step's settings.
internal void start_step(Stress* stress, u32 step, f64 now)
{
    Benchmark* benchmark = &stress->benchmark;
    stress_reset(stress);
    workloads_apply_step(stress, step);
    benchmark->step = step;
    benchmark->step_start = now;
    benchmark->results[step] = (BenchmarkResult){0};
}

// The last whole run as a text table (the log gets it at the end of each run).
internal void benchmark_table_text(Stress* stress, char* text, umm capacity)
{
    Benchmark* benchmark = &stress->benchmark;
    char agent[256];
    nv_window_system_text(agent, sizeof(agent));
    // Where the browser says nothing of the GPU, the backend knows its name (Direct3D 12).
    if (stress->gpu.adapter_name[0]) {
        umm used_by_agent = strlen(agent);
        snprintf(agent + used_by_agent, sizeof(agent) - used_by_agent, ", GPU %s (%s)", stress->gpu.adapter_name,
                 stress->gpu.backend_name);
    }
    char shadows[64];
    shadow_settings_text(&stress->renderer.shadows, shadows, sizeof(shadows));
    char resolution[64];
    resolution_text(stress, resolution, sizeof(resolution));
    umm used = (umm)snprintf(text, capacity,
                             "nv stress benchmark, run %u\ncommit: %s (%s build)\nbrowser: %s\ncanvas: %ux%u, GPU timestamps: %s\n"
                             "shadows: %s\nanti-aliasing: %s\nresolution: %s\n\n"
                             "step            frames  avg ms  worst ms  load %%  anim  scene  draw    ui    gpu  shadow  upscale  bloom  particles  alive\n",
                             benchmark->runs, NV_GIT_COMMIT, NV_BUILD_NAME, agent, stress->gpu.width, stress->gpu.height,
                             stress->gpu.has_timestamps ? "yes" : "no", shadows, stress->renderer.scene_samples > 1 ? "MSAA 4x" : "off",
                             resolution);
    for (u32 i = 0; i < workloads_step_count() && used < capacity; ++i) {
        const BenchmarkResult* r = &benchmark->finished[i];
        const FrameTimes* a = &r->average;
        used += (umm)snprintf(text + used, capacity - used,
                              "%-15s %6u  %6.2f  %8.2f  %6.0f  %4.2f  %5.2f  %4.2f  %4.2f  %5.2f  %6.2f  %7.2f  %5.2f  %9.2f  %5.0f\n",
                              workloads_step_name(i), r->frames, a->frame, r->worst_frame, stress_load(a), a->anim, a->scene,
                              a->draw, a->ui, a->gpu, a->gpu_shadow, a->gpu_upscale, a->gpu_bloom, a->gpu_particles, r->particles);
    }
}

// Measures the step that is on, and goes to the next when its time is up; after the last, the run starts again.
internal void benchmark_after_frame(Stress* stress)
{
    Benchmark* benchmark = &stress->benchmark;
    f64 now = nv_time_seconds();
    f64 elapsed = now - benchmark->step_start;
    if (elapsed < BENCHMARK_WARMUP)
        return;
    BenchmarkResult* result = &benchmark->results[benchmark->step];
    add_times(&result->average, &stress->times); // a sum until the step ends
    result->particles += (f64)nv_vfx_stats(&stress->vfx).alive;
    if (stress->times.frame > result->worst_frame)
        result->worst_frame = stress->times.frame;
    ++result->frames;
    if (elapsed < BENCHMARK_WARMUP + BENCHMARK_MEASURE)
        return;
    f64 n = (f64)result->frames;
    result->average = divide_times(&result->average, n);
    result->particles /= n;
    if (benchmark->step + 1 < workloads_step_count()) {
        start_step(stress, benchmark->step + 1, now);
        return;
    }
    memcpy(benchmark->finished, benchmark->results, sizeof(benchmark->finished));
    ++benchmark->runs;
    char table[BENCHMARK_TABLE_SIZE];
    benchmark_table_text(stress, table, sizeof(table));
    nv_log(NV_LOG_INFO, "stress", "%s", table);
    start_step(stress, 0, now);
}

//
// Panel
//

internal void stat_row(const char* label, const char* format, f64 value)
{
    igTableNextColumn();
    igTextUnformatted(label, NULL);
    igTableNextColumn();
    igText(format, value);
}

internal void stats_section(Stress* stress)
{
    const FrameTimes* a = &stress->shown_average;
    const NvRenderStats* r = &stress->renderer.stats;
    NvVfxStats effects = nv_vfx_stats(&stress->vfx);
    igSeparatorText("Frame (1 s average, ms)");
    // Label and value pairs: two per row where the panel is wide, one where it is not.
    int columns = igGetContentRegionAvail().x > 520.0f * stress->imgui.ui_scale ? 4 : 2;
    if (!igBeginTable("stats", columns, ImGuiTableFlags_SizingStretchProp, (ImVec2_c){0, 0}, 0.0f))
        return;
    stat_row("Frame", "%.2f", a->frame);
    stat_row("Load %", "%.0f", stress_load(a));
    stat_row("FPS", "%.0f", a->frame > 0.0 ? 1000.0 / a->frame : 0.0);
    stat_row("Worst frame", "%.2f", stress->shown_worst_frame);
    if (stress->gpu.has_timestamps) {
        stat_row("GPU scene pass", "%.2f", a->gpu);
        stat_row("GPU shadow pass", "%.2f", a->gpu_shadow);
        stat_row("GPU upscale pass", "%.2f", a->gpu_upscale);
        stat_row("GPU bloom passes", "%.2f", a->gpu_bloom);
        stat_row("GPU particle passes", "%.2f", a->gpu_particles);
    } else {
        igTableNextColumn();
        igTextUnformatted("GPU passes", NULL);
        igTableNextColumn();
        igTextUnformatted("no timestamps", NULL);
    }
    stat_row("CPU anim", "%.2f", a->anim);
    stat_row("CPU scene", "%.2f", a->scene);
    stat_row("CPU draw", "%.2f", a->draw);
    stat_row("CPU ui", "%.2f", a->ui);
    stat_row("Nodes", "%.0f", (f64)stress_live_nodes(stress->scene));
    stat_row("Draws", "%.0f", (f64)r->draws);
    stat_row("Triangles", "%.0f", (f64)r->triangles);
    stat_row("Skinned draws", "%.0f", (f64)r->skinned_draws);
    stat_row("Skin matrices", "%.0f", (f64)r->skin_matrices);
    stat_row("Debug lines", "%.0f", (f64)r->debug_lines);
    stat_row("Pipeline changes", "%.0f", (f64)r->pipeline_changes);
    stat_row("Material changes", "%.0f", (f64)r->material_changes);
    stat_row("Mesh changes", "%.0f", (f64)r->mesh_changes);
    stat_row("Shadow draws", "%.0f", (f64)r->shadow_draws);
    stat_row("Particles alive", "%.0f", (f64)effects.alive);
    stat_row("Particles visible", "%.0f", (f64)effects.visible);
    stat_row("Particles dropped", "%.0f", (f64)effects.dropped);
    stat_row("Segments", "%.0f", (f64)effects.segments);
    stat_row("Decals", "%.0f", (f64)effects.decals);
    igEndTable();
}

// The step that is on, then the results: the last whole run's, or this run's so far before the first has ended.
internal void benchmark_section(Stress* stress)
{
    Benchmark* benchmark = &stress->benchmark;
    u32 steps = workloads_step_count();
    igSeparatorText("Benchmark");
    f64 elapsed = nv_time_seconds() - benchmark->step_start;
    igText("Run %u, step %u/%u: %s (%s)", benchmark->runs + 1, benchmark->step + 1, steps, workloads_step_name(benchmark->step),
           elapsed < BENCHMARK_WARMUP ? "warming up" : "measuring");
    if (stress->workloads.character_failed)
        igTextDisabled("The character did not load: the crowd steps have no characters.");
    const BenchmarkResult* results = benchmark->runs ? benchmark->finished : benchmark->results;
    u32 count = benchmark->runs ? steps : benchmark->step;
    if (!count)
        return;
    igTextDisabled(benchmark->runs ? "The last whole run:" : "This run so far:");
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (!igBeginTable("results", 8, flags, (ImVec2_c){0, 0}, 0.0f))
        return;
    igTableSetupColumn("Step", 0, 0.0f, 0);
    igTableSetupColumn("Avg ms", 0, 0.0f, 0);
    igTableSetupColumn("Load %", 0, 0.0f, 0);
    igTableSetupColumn("Worst", 0, 0.0f, 0);
    igTableSetupColumn("CPU a/s/d", 0, 0.0f, 0);
    igTableSetupColumn("GPU sc/sh/up", 0, 0.0f, 0);
    igTableSetupColumn("GPU bl/pt", 0, 0.0f, 0);
    igTableSetupColumn("Alive", 0, 0.0f, 0);
    igTableHeadersRow();
    for (u32 i = 0; i < count; ++i) {
        const BenchmarkResult* r = &results[i];
        igTableNextRow(0, 0.0f);
        igTableNextColumn();
        igTextUnformatted(workloads_step_name(i), NULL);
        igTableNextColumn();
        igText("%.2f", r->average.frame);
        igTableNextColumn();
        igText("%.0f", stress_load(&r->average));
        igTableNextColumn();
        igText("%.2f", r->worst_frame);
        igTableNextColumn();
        igText("%.2f/%.2f/%.2f", r->average.anim, r->average.scene, r->average.draw);
        igTableNextColumn();
        if (stress->gpu.has_timestamps)
            igText("%.2f/%.2f/%.2f", r->average.gpu, r->average.gpu_shadow, r->average.gpu_upscale);
        else
            igTextDisabled("-");
        igTableNextColumn();
        if (stress->gpu.has_timestamps)
            igText("%.2f/%.2f", r->average.gpu_bloom, r->average.gpu_particles);
        else
            igTextDisabled("-");
        igTableNextColumn();
        igText("%.0f", r->particles);
    }
    igEndTable();
}

internal void build_ui(Stress* stress)
{
    char text[160];
    snprintf(text, sizeof(text), "%s build %s%s%s", NV_BUILD_NAME, NV_GIT_COMMIT, stress->download_text[0] ? " \xC2\xB7 " : "",
             stress->download_text);
    nv_imgui_draw_build_label(&stress->imgui, stress->viewport, text, 0.0f, "Commit: " NV_GIT_SUBJECT);
    if (nv_imgui_begin_panel(&stress->imgui, "Stress", stress->panel)) {
        benchmark_section(stress);
        stats_section(stress);
    }
    igEnd();
}

//
// Frame
//

// The panel on the right where the canvas is wide, at the bottom (40%) where it is tall; the viewport is the rest.
internal void layout(Stress* stress)
{
    f32 ratio = nv_window_pixel_ratio(&stress->window);
    f32 width_css = (f32)stress->gpu.width / ratio;
    f32 height_css = (f32)stress->gpu.height / ratio;
    if (width_css >= height_css) {
        f32 panel = PANEL_WIDTH * stress->imgui.ui_scale;
        if (panel > width_css * 0.5f)
            panel = width_css * 0.5f;
        stress->viewport = nv_window_framebuffer_rect_from_css(0.0f, 0.0f, width_css - panel, height_css, ratio);
        stress->panel = nv_window_framebuffer_rect_from_css(width_css - panel, 0.0f, width_css, height_css, ratio);
    } else {
        f32 top = height_css * 0.6f;
        stress->viewport = nv_window_framebuffer_rect_from_css(0.0f, 0.0f, width_css, top, ratio);
        stress->panel = nv_window_framebuffer_rect_from_css(0.0f, top, width_css, height_css, ratio);
    }
    stress->scene_output = nv_renderer_scene_output(&stress->resolution, stress->viewport);
}

// The camera needs no input, but a drag still orbits and pans it and the wheel and pinches dolly it, to look closer.
internal void apply_view_input(Stress* stress)
{
    const NvViewInput* in = &stress->imgui.view;
    nv_orbit_camera_turn(&stress->orbit, -in->orbit_x * ORBIT_RADIANS_PER_PIXEL, in->orbit_y * ORBIT_RADIANS_PER_PIXEL, in->dolly);
    if (in->pan_x != 0.0f || in->pan_y != 0.0f) {
        // The image's height in CSS pixels: the scene's height times the screen pixels each of its pixels takes.
        const NvSceneOutput* output = &stress->scene_output;
        f32 height = (f32)output->height * output->pixel_height / nv_window_pixel_ratio(&stress->window);
        NvVec3 move = nv_orbit_camera_pan(&stress->orbit, nv_scene_get(stress->scene, stress->camera), height, in->pan_x, in->pan_y);
        stress->orbit.target = nv_vec3_add(stress->orbit.target, move);
    }
}

internal void frame(void* userdata)
{
    Stress* stress = userdata;
    nv_log_pump();
    f64 now = nv_time_seconds();
    f32 real_dt = (f32)(now - stress->last_time);
    stress->last_time = now;

    NvGpuFrame gpu_frame = nv_gpu_begin_frame(&stress->gpu);
    if (!gpu_frame.ok)
        return;
    FrameTimes* times = &stress->times;
    times->frame = (f64)real_dt * 1000.0;

    f64 t = now_ms();
    layout(stress);
    stress->imgui.view_rect = stress->viewport;
    nv_imgui_new_frame(&stress->imgui, real_dt);
    build_ui(stress);
    times->ui = now_ms() - t;

    // The workloads, the animation and the effects advance by a fixed step, so each frame of a step is the same on every run;
    // the frame time is measured as it is.
    NvScene* scene = stress->scene;
    t = now_ms();
    workloads_update(stress, FIXED_DT);
    nv_vfx_update(&stress->vfx, FIXED_DT);
    times->scene = now_ms() - t;
    t = now_ms();
    nv_anim_update_scene(scene, FIXED_DT);
    times->anim = now_ms() - t;
    t = now_ms();
    nv_scene_update(scene);
    times->scene += now_ms() - t;
    apply_view_input(stress);
    nv_orbit_camera_place(&stress->orbit, nv_scene_get(scene, stress->camera)); // after nv_scene_update: it sets the world too
    workloads_after_update(stress);

    t = now_ms();
    NvRenderer* renderer = &stress->renderer;
    nv_renderer_draw(renderer, scene, nv_anim_skins(), stress->scene_output, gpu_frame.view);
    times->draw = now_ms() - t;
    times->gpu = renderer->gpu_ms;
    times->gpu_shadow = renderer->shadows.size ? renderer->gpu_shadow_ms : 0.0;
    times->gpu_upscale = renderer->gpu_upscale_ms;
    times->gpu_bloom = renderer->post.bloom ? renderer->gpu_bloom_ms : 0.0;
    times->gpu_particles = renderer->gpu_particles_ms;
    t = now_ms();
    nv_imgui_render(&stress->imgui, gpu_frame.view);
    times->ui += now_ms() - t;
    nv_gpu_end_frame(&stress->gpu);
    nv_renderer_end_frame(renderer);

    ++stress->frames;
    accumulate_times(stress, now);
    benchmark_after_frame(stress);
}

//
// Debug exports (docs/specs/samples.md): they read, they never set
//

#if NV_DEBUG
// 0 frames drawn, 1 nodes, 2 draws, 3 particles alive, 4 warnings and errors in the log.
NV_DEBUG_EXPORT int stress_debug_stat(int which)
{
    Stress* stress = &stress_state;
    switch (which) {
    case 0: return (int)stress->frames;
    case 1: return (int)stress_live_nodes(stress->scene);
    case 2: return (int)stress->renderer.stats.draws;
    case 3: return (int)nv_vfx_stats(&stress->vfx).alive;
    case 4: return (int)(nv_log_ring.level_counts[NV_LOG_WARNING] + nv_log_ring.level_counts[NV_LOG_ERROR]);
    }
    return -1;
}

// 0 cubes, 1 links, 2 characters shown.
NV_DEBUG_EXPORT int stress_debug_built(int which)
{
    const Workloads* workloads = &stress_state.workloads;
    switch (which) {
    case 0: return (int)workloads->cubes_built;
    case 1: return (int)workloads->links_built;
    case 2: return (int)workloads->characters_shown;
    }
    return -1;
}

// 0 runs finished, 1 the step that is on, 2 steps in a run, 3 frames the last whole run measured in all.
NV_DEBUG_EXPORT int stress_debug_benchmark(int which)
{
    const Benchmark* benchmark = &stress_state.benchmark;
    switch (which) {
    case 0: return (int)benchmark->runs;
    case 1: return (int)benchmark->step;
    case 2: return (int)workloads_step_count();
    case 3: {
        u32 frames = 0;
        for (u32 i = 0; i < workloads_step_count(); ++i)
            frames += benchmark->finished[i].frames;
        return (int)frames;
    }
    }
    return -1;
}
#endif

int main(void)
{
    Stress* stress = &stress_state;
    nv_arena_init(&stress->permanent, permanent_memory, sizeof(permanent_memory));
    nv_arena_init(&stress->scratch, scratch_memory, sizeof(scratch_memory));
    nv_arena_init(&stress->anim_memory, anim_memory, sizeof(anim_memory));

    nv_window_create(&stress->window, "nv stress sample");
    if (!nv_gpu_create(&stress->gpu, &stress->window)) {
        nv_log(NV_LOG_ERROR, "stress", "fatal: failed to initialize the GPU");
        return 1;
    }
    nv_renderer_init(&stress->renderer, &stress->gpu, &stress->permanent);
    nv_imgui_init(&stress->imgui, &stress->gpu, &stress->window, &stress->permanent);
    nv_imgui_load_ui_font(&stress->imgui, &stress->permanent);
    nv_window_download_text(stress->download_text, sizeof(stress->download_text));
    if (!stress->download_text[0]) // nothing was downloaded (a native build, whose files are on the disk): the label names the backend
        snprintf(stress->download_text, sizeof(stress->download_text), "%s", stress->gpu.backend_name);
    nv_anim_init(&stress->anim_memory);
    nv_vfx_init(&stress->vfx, &stress->gpu, (NvVfxCapacity){0}, &stress->permanent);
    stress->renderer.vfx = &stress->vfx;

    // The sandbox's settings: lighter on touch screens, where the GPU is the limit. Fixed: the benchmark measures with them.
    b32 touch = stress->imgui.ui_scale > 1.0f;
    stress->renderer.shadows = (NvShadowSettings){
        .size = touch ? 1024 : 2048,
        .format = NV_SHADOW_FORMAT_DEPTH32F,
        .filter = touch ? NV_SHADOW_FILTER_LOW : NV_SHADOW_FILTER_HIGH,
        .distance = 30.0f,
    };
    stress->renderer.msaa = 4;
    stress->renderer.post = (NvPostSettings){.tone = NV_TONE_PBR_NEUTRAL, .exposure = 1.0f, .bloom = 1, .bloom_intensity = 0.04f};
    stress->resolution = (NvResolution){.mode = NV_RESOLUTION_SCALE, .divisor = touch ? 2 : 1};

    stress->scene = NV_PUSH_STRUCT(&stress->permanent, NvScene);
    workloads_build(stress);

    stress->window_start = stress->last_time = nv_time_seconds();
    start_step(stress, 0, stress->last_time);
    nv_window_run(&stress->window, frame, stress);
    return 0;
}
