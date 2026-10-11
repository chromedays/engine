#include "sandbox.h"

#include <emscripten/emscripten.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

internal void resolution_text(Sandbox* sandbox, char* out, umm size);

// The Stress tab and the benchmark (docs/specs/stress.md, docs/specs/scene_file.md): live frame statistics, the rows of the shown
// scene's generators, and a benchmark that steps through the setups its scene file lists.

u32 stress_live_nodes(NvScene* scene)
{
    u32 count = 0;
    for (u32 i = 1; i <= scene->node_count; ++i)
        count += scene->nodes[i].gen & 1;
    return count;
}

// The tab is for scenes that have something to load the engine with or to measure.
b32 stress_tab_shown(Sandbox* sandbox)
{
    SceneDoc* doc = sandbox_doc(sandbox);
    if (doc->desc->has_benchmark)
        return 1;
    for (u32 i = 0; i < doc->behavior_count; ++i)
        if (scene_behavior_def(doc->behaviors[i].desc.kind)->generator)
            return 1;
    return 0;
}

//
// Benchmark
//

// Every generator's parameters as the file gave them, all of them off, and then the step's lines: the setup of step `step`.
internal void start_step(Sandbox* sandbox, u32 step, f64 now)
{
    Benchmark* benchmark = &sandbox->benchmark;
    SceneDoc* doc = sandbox_doc(sandbox);
    const StepDesc* source = &doc->desc->steps[step];
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        Behavior* behavior = &doc->behaviors[i];
        if (!scene_behavior_def(behavior->desc.kind)->generator)
            continue;
        behavior->desc = doc->desc->behaviors[i];
        u32* on = behavior_field(behavior, behavior_parameter(behavior, "on"));
        *on = 0;
    }
    for (u32 l = 0; l < source->line_count; ++l) {
        const StepLine* line = &source->lines[l];
        Behavior* behavior = &doc->behaviors[line->behavior];
        const SceneField* field = &scene_behavior_def(behavior->desc.kind)->fields[line->field];
        memcpy(behavior_field(behavior, field), &line->value, sizeof(line->value));
        *(u32*)behavior_field(behavior, behavior_parameter(behavior, "on")) = 1;
    }
    for (u32 i = 0; i < doc->behavior_count; ++i)
        benchmark->applied[i] = doc->behaviors[i].desc;
    effects_clear(sandbox); // every step starts from an empty sky
    benchmark->step = step;
    benchmark->step_start = now;
    benchmark->results[step] = (BenchmarkResult){0};
    snprintf(benchmark->step_names[step], sizeof(benchmark->step_names[step]), "%s", source->name);
}

void benchmark_start(Sandbox* sandbox)
{
    Benchmark* benchmark = &sandbox->benchmark;
    SceneDoc* doc = sandbox_doc(sandbox);
    if (benchmark->running || !doc->desc->has_benchmark)
        return;
    for (u32 i = 0; i < doc->behavior_count; ++i)
        benchmark->before[i] = doc->behaviors[i].desc;
    benchmark->scene = sandbox->shown;
    // A run needs Play; one that began it ends it.
    benchmark->started_play = !sandbox->playing;
    if (benchmark->started_play)
        sandbox_start_playing(sandbox);
    benchmark->shadows = sandbox->renderer.shadows;
    benchmark->msaa = sandbox->renderer.scene_samples;
    resolution_text(sandbox, benchmark->resolution, sizeof(benchmark->resolution));
    benchmark->result_count = 0;
    benchmark->running = 1;
    start_step(sandbox, 0, nv_time_seconds());
}

void benchmark_end(Sandbox* sandbox)
{
    sandbox->benchmark.running = 0;
    sandbox->benchmark.started_play = 0;
}

// The run is over: Stop puts everything back (the snapshot has the parameters); a run inside a Play that was already going puts
// the generators' parameters back and the Play goes on.
internal void finish_run(Sandbox* sandbox)
{
    Benchmark* benchmark = &sandbox->benchmark;
    if (benchmark->started_play) {
        sandbox_stop_playing(sandbox); // ends the run too (benchmark_end)
        return;
    }
    SceneDoc* doc = sandbox_doc(sandbox);
    for (u32 i = 0; i < doc->behavior_count; ++i)
        if (scene_behavior_def(doc->behaviors[i].desc.kind)->generator)
            doc->behaviors[i].desc = benchmark->before[i];
    benchmark_end(sandbox);
}

// What the user changed during a run: the run stops, and what was touched stays.
internal b32 touched_during_run(SceneDoc* doc, const Benchmark* benchmark)
{
    for (u32 i = 0; i < doc->behavior_count; ++i)
        if (scene_behavior_def(doc->behaviors[i].desc.kind)->generator &&
            !behavior_parameters_equal(&doc->behaviors[i].desc, &benchmark->applied[i]))
            return 1;
    return 0;
}

void stress_after_frame(Sandbox* sandbox)
{
    Benchmark* benchmark = &sandbox->benchmark;
    if (!benchmark->running)
        return;
    SceneDoc* doc = sandbox_doc(sandbox);
    if (touched_during_run(doc, benchmark)) {
        benchmark_end(sandbox);
        return;
    }
    f64 now = nv_time_seconds();
    f64 elapsed = now - benchmark->step_start;
    f64 warmup = (f64)doc->desc->warmup;
    if (elapsed < warmup)
        return;

    BenchmarkResult* result = &benchmark->results[benchmark->step];
    FrameTimes* t = &sandbox->times;
    FrameTimes* sum = &result->average; // a sum until the step ends
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
    result->particles += (f64)nv_vfx_stats(&sandbox->vfx).alive;
    if (t->frame > result->worst_frame)
        result->worst_frame = t->frame;
    ++result->frames;

    if (elapsed < warmup + (f64)doc->desc->measure)
        return;
    f64 n = (f64)result->frames;
    *sum = (FrameTimes){sum->frame / n, sum->anim / n, sum->scene / n, sum->draw / n, sum->ui / n, sum->gpu / n,
                        sum->gpu_shadow / n, sum->gpu_upscale / n, sum->gpu_bloom / n, sum->gpu_particles / n};
    result->particles /= n;
    benchmark->result_count = benchmark->step + 1;
    if (benchmark->step + 1 < doc->desc->step_count)
        start_step(sandbox, benchmark->step + 1, now);
    else
        finish_run(sandbox);
}

EM_JS_DEPS(nv_stress, "$stringToUTF8");

EM_JS(void, js_user_agent, (char* out, int size), {
    stringToUTF8(navigator.userAgent, out, size);
});

// "585 x 497, scale 1/2" or "1280 x 720 fixed, shown x2": what the scene renders at.
internal void resolution_text(Sandbox* sandbox, char* out, umm size)
{
    const NvSceneOutput* scene = &sandbox->layout.scene;
    if (sandbox->resolution.mode == NV_RESOLUTION_FIXED) {
        if (scene->pixel_width != scene->pixel_height)
            snprintf(out, size, "%u x %u fixed, stretched %.2fx%.2f", scene->width, scene->height, (f64)scene->pixel_width,
                     (f64)scene->pixel_height);
        else if (scene->pixel_width >= 1.0f && scene->pixel_width == (f32)(u32)scene->pixel_width)
            snprintf(out, size, "%u x %u fixed, shown x%.0f", scene->width, scene->height, (f64)scene->pixel_width);
        else
            snprintf(out, size, "%u x %u fixed, shown at %.2fx", scene->width, scene->height, (f64)scene->pixel_width);
    } else {
        snprintf(out, size, "%u x %u, scale 1/%u", scene->width, scene->height, sandbox->resolution.divisor);
    }
}

// "2048 32-bit float High", or "off": what the shadows were while measuring.
internal void shadow_settings_text(const NvShadowSettings* s, char* out, umm size)
{
    if (!s->size)
        snprintf(out, size, "off");
    else
        snprintf(out, size, "%u %s %s, %.0f m", s->size, s->format == NV_SHADOW_FORMAT_DEPTH16 ? "16-bit" : "32-bit float",
                 s->filter == NV_SHADOW_FILTER_HIGH ? "High" : "Low", (f64)s->distance);
}

internal void copy_results(Sandbox* sandbox)
{
    Benchmark* benchmark = &sandbox->benchmark;
    char agent[256];
    js_user_agent(agent, sizeof(agent));
    char text[4096];
    umm used = 0;
    char shadows[64];
    shadow_settings_text(&benchmark->shadows, shadows, sizeof(shadows));
    used += (umm)snprintf(text + used, sizeof(text) - used,
                          "nv benchmark: %s\ncommit: %s (%s build)\nbrowser: %s\ncanvas: %ux%u, GPU timestamps: %s\n"
                          "shadows: %s\nanti-aliasing: %s\nresolution: %s\n\n"
                          "step            frames  avg ms  worst ms  load %%  anim  scene  draw    ui    gpu  shadow  upscale  bloom  particles  alive\n",
                          sandbox_doc(sandbox)->desc->title, NV_GIT_COMMIT, NV_BUILD_NAME, agent, sandbox->gpu.width, sandbox->gpu.height, sandbox->gpu.has_timestamps ? "yes" : "no",
                          shadows, benchmark->msaa > 1 ? "MSAA 4x" : "off", benchmark->resolution);
    for (u32 i = 0; i < benchmark->result_count && used < sizeof(text); ++i) {
        const BenchmarkResult* r = &benchmark->results[i];
        used += (umm)snprintf(text + used, sizeof(text) - used, "%-15s %6u  %6.2f  %8.2f  %6.0f  %4.2f  %5.2f  %4.2f  %4.2f  %5.2f  %6.2f  %7.2f  %5.2f  %9.2f  %5.0f\n",
                              benchmark->step_names[i], r->frames, r->average.frame, r->worst_frame, sandbox_load(&r->average),
                              r->average.anim, r->average.scene, r->average.draw, r->average.ui, r->average.gpu,
                              r->average.gpu_shadow, r->average.gpu_upscale, r->average.gpu_bloom, r->average.gpu_particles, r->particles);
    }
    igSetClipboardText(text);
}

//
// Stress tab
//

// One label and value pair of the stats table: the value is `text`, or `format` with `value`.
typedef struct Stat {
    const char* label;
    const char* format;
    f64 value;
    const char* text;
    b32 visible;
} Stat;

internal void stat(Stat* stats, u32* count, const char* label, const char* format, f64 value)
{
    stats[(*count)++] = (Stat){.label = label, .format = format, .value = value};
}

internal void stat_text(Stat* stats, u32* count, const char* label, const char* text)
{
    stats[(*count)++] = (Stat){.label = label, .text = text};
}

internal void stats_section(Sandbox* sandbox)
{
    const FrameTimes* a = &sandbox->shown_average;
    const NvRenderStats* r = &sandbox->renderer.stats;
    search_section(sandbox, "Frame (1 s average, ms)");
    Stat stats[40];
    u32 count = 0;
    stat(stats, &count, "Frame", "%.2f", a->frame);
    stat(stats, &count, "Load %", "%.0f", sandbox_load(a));
    stat(stats, &count, "FPS", "%.0f", a->frame > 0.0 ? 1000.0 / a->frame : 0.0);
    stat(stats, &count, "Worst frame", "%.2f", sandbox->shown_worst_frame);
    if (sandbox->gpu.has_timestamps) {
        stat(stats, &count, "GPU scene pass", "%.2f", a->gpu);
        stat(stats, &count, "GPU shadow pass", "%.2f", a->gpu_shadow);
        stat(stats, &count, "GPU upscale pass", "%.2f", a->gpu_upscale);
        stat(stats, &count, "GPU bloom passes", "%.2f", a->gpu_bloom);
        stat(stats, &count, "GPU particle passes", "%.2f", a->gpu_particles);
    } else {
        stat_text(stats, &count, "GPU passes", "no timestamps");
    }
    stat(stats, &count, "CPU anim", "%.2f", a->anim);
    stat(stats, &count, "CPU scene", "%.2f", a->scene);
    stat(stats, &count, "CPU draw", "%.2f", a->draw);
    stat(stats, &count, "CPU ui", "%.2f", a->ui);
    stat(stats, &count, "Nodes", "%.0f", (f64)stress_live_nodes(sandbox_doc(sandbox)->scene));
    stat(stats, &count, "Draws", "%.0f", (f64)r->draws);
    stat(stats, &count, "Triangles", "%.0f", (f64)r->triangles);
    stat(stats, &count, "Skinned draws", "%.0f", (f64)r->skinned_draws);
    stat(stats, &count, "Skin matrices", "%.0f", (f64)r->skin_matrices);
    stat(stats, &count, "Debug lines", "%.0f", (f64)r->debug_lines);
    stat(stats, &count, "Pipeline changes", "%.0f", (f64)r->pipeline_changes);
    stat(stats, &count, "Material changes", "%.0f", (f64)r->material_changes);
    stat(stats, &count, "Mesh changes", "%.0f", (f64)r->mesh_changes);
    stat(stats, &count, "Shadow draws", "%.0f", (f64)r->shadow_draws);
    NvVfxStats effects = nv_vfx_stats(&sandbox->vfx);
    stat(stats, &count, "Particles alive", "%.0f", (f64)effects.alive);
    stat(stats, &count, "Particles visible", "%.0f", (f64)effects.visible);
    stat(stats, &count, "Particles dropped", "%.0f", (f64)effects.dropped);
    stat(stats, &count, "Segments", "%.0f", (f64)effects.segments);
    stat(stats, &count, "Decals", "%.0f", (f64)effects.decals);
    stat_text(stats, &count, "Anti-aliasing", sandbox->renderer.scene_samples > 1 ? "MSAA 4x" : "off");
    char resolution[64];
    resolution_text(sandbox, resolution, sizeof(resolution));
    stat_text(stats, &count, "Resolution", resolution);

    // The search decides which rows show before the table starts, so the heading is not drawn in a cell.
    u32 visible = 0;
    for (u32 i = 0; i < count; ++i) {
        stats[i].visible = search_row(sandbox, stats[i].label, NULL);
        visible += stats[i].visible;
    }
    if (!visible)
        return;
    // Label and value pairs: two per row where the panel is wide, one on phones.
    int columns = igGetContentRegionAvail().x > 520.0f * sandbox->imgui.ui_scale ? 4 : 2;
    if (!igBeginTable("stats", columns, ImGuiTableFlags_SizingStretchProp, (ImVec2_c){0, 0}, 0.0f))
        return;
    for (u32 i = 0; i < count; ++i) {
        if (!stats[i].visible)
            continue;
        igTableNextColumn();
        igTextUnformatted(T(stats[i].label), NULL);
        igTableNextColumn();
        if (stats[i].text)
            igTextUnformatted(T(stats[i].text), NULL);
        else
            igText(stats[i].format, stats[i].value);
    }
    igEndTable();
}

internal void workloads_section(Sandbox* sandbox)
{
    SceneDoc* doc = sandbox_doc(sandbox);
    search_section(sandbox, "Workloads");
    for (u32 i = 0; i < doc->behavior_count; ++i)
        if (scene_behavior_def(doc->behaviors[i].desc.kind)->generator)
            generator_rows(sandbox, &doc->behaviors[i], i);
    if (!search_plain(sandbox))
        return;
    for (u32 i = 0; i < doc->behavior_count; ++i) {
        char status[64];
        generator_status(&doc->behaviors[i], status, sizeof(status));
        if (status[0])
            igTextDisabled("%s", status);
    }
}

internal void benchmark_section(Sandbox* sandbox)
{
    Benchmark* benchmark = &sandbox->benchmark;
    SceneDoc* doc = sandbox_doc(sandbox);
    search_section(sandbox, "Benchmark");
    // One group: the buttons, the step line and the results go together.
    if (!search_group(sandbox, "Benchmark", "run stop copy results table"))
        return;
    b32 mine = benchmark->scene == sandbox->shown; // the results are of this scene's run
    if (benchmark->running) {
        f64 elapsed = nv_time_seconds() - benchmark->step_start;
        igText("Step %u/%u: %s (%s)", benchmark->step + 1, doc->desc->step_count, doc->desc->steps[benchmark->step].name,
               elapsed < (f64)doc->desc->warmup ? "warming up" : "measuring");
        if (igButton(TL("Stop"), (ImVec2_c){-1.0f, 0.0f}))
            finish_run(sandbox);
    } else if (igButton(TL("Run benchmark"), (ImVec2_c){-1.0f, 0.0f})) {
        benchmark_start(sandbox);
    }
    if (!benchmark->result_count || !mine)
        return;
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    char shadows[64];
    shadow_settings_text(&benchmark->shadows, shadows, sizeof(shadows));
    igText(T("Shadows: %s"), shadows);
    igText(T("Anti-aliasing: %s"), benchmark->msaa > 1 ? "MSAA 4x" : "off");
    igText(T("Resolution: %s"), benchmark->resolution);
    if (igBeginTable("results", 8, flags, (ImVec2_c){0, 0}, 0.0f)) {
        igTableSetupColumn("Step", 0, 0.0f, 0);
        igTableSetupColumn("Avg ms", 0, 0.0f, 0);
        igTableSetupColumn("Load %", 0, 0.0f, 0);
        igTableSetupColumn("Worst", 0, 0.0f, 0);
        igTableSetupColumn("CPU a/s/d", 0, 0.0f, 0);
        igTableSetupColumn("GPU sc/sh/up", 0, 0.0f, 0);
        igTableSetupColumn("GPU bl/pt", 0, 0.0f, 0);
        igTableSetupColumn("Alive", 0, 0.0f, 0);
        igTableHeadersRow();
        for (u32 i = 0; i < benchmark->result_count; ++i) {
            const BenchmarkResult* r = &benchmark->results[i];
            igTableNextRow(0, 0.0f);
            igTableNextColumn();
            igTextUnformatted(benchmark->step_names[i], NULL);
            igTableNextColumn();
            igText("%.2f", r->average.frame);
            igTableNextColumn();
            igText("%.0f", sandbox_load(&r->average));
            igTableNextColumn();
            igText("%.2f", r->worst_frame);
            igTableNextColumn();
            igText("%.2f/%.2f/%.2f", r->average.anim, r->average.scene, r->average.draw);
            igTableNextColumn();
            if (sandbox->gpu.has_timestamps)
                igText("%.2f/%.2f/%.2f", r->average.gpu, r->average.gpu_shadow, r->average.gpu_upscale);
            else
                igTextDisabled("-");
            igTableNextColumn();
            if (sandbox->gpu.has_timestamps)
                igText("%.2f/%.2f", r->average.gpu_bloom, r->average.gpu_particles);
            else
                igTextDisabled("-");
            igTableNextColumn();
            igText("%.0f", r->particles);
        }
        igEndTable();
    }
    if (!benchmark->running && igButton(TL("Copy results"), (ImVec2_c){-1.0f, 0.0f}))
        copy_results(sandbox);
}

void stress_ui(Sandbox* sandbox)
{
    SceneDoc* doc = sandbox_doc(sandbox);
    search_panel_begin(sandbox, SEARCH_STRESS);
    stats_section(sandbox);
    workloads_section(sandbox);
    if (doc->desc->has_benchmark)
        benchmark_section(sandbox);
    search_panel_end(sandbox);
}
