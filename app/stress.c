#include "app.h"

#include <emscripten/emscripten.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// The stress scene (docs/specs/stress.md): workloads that load the engine with many objects,
// live frame statistics and a benchmark that steps through fixed setups.


#define GRID_COLUMNS 128
#define GRID_SPACING 0.9f
#define CROWD_COLUMNS 20
#define CROWD_SPACING 1.6f

// Nodes the scene has besides the workloads: camera, sun, ground and the three groups.
#define FIXED_NODES 6
// Nodes per crowd character: the root and its meshes.
#define CROWD_NODES 4

internal b32 same_workloads(const StressWorkloads* a, const StressWorkloads* b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

u32 stress_live_nodes(NvScene* scene)
{
    u32 count = 0;
    for (u32 i = 1; i <= scene->node_count; ++i)
        count += scene->nodes[i].gen & 1;
    return count;
}

// A color wheel, so neighboring cubes differ.
internal void color_wheel(u32 i, u32 count, f32* rgb)
{
    f32 h = (f32)i / (f32)count * 6.0f;
    f32 x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);
    f32 r = 0, g = 0, b = 0;
    switch ((u32)h % 6) {
    case 0: r = 1; g = x; break;
    case 1: r = x; g = 1; break;
    case 2: g = 1; b = x; break;
    case 3: g = x; b = 1; break;
    case 4: r = x; b = 1; break;
    default: r = 1; b = x; break;
    }
    rgb[0] = 0.15f + 0.8f * r;
    rgb[1] = 0.15f + 0.8f * g;
    rgb[2] = 0.15f + 0.8f * b;
}

void stress_build(App* app)
{
    Stress* stress = &app->stress;
    NvScene* scene = NV_PUSH_STRUCT(&app->permanent, NvScene);
    stress->scene = scene;
    NvNodeId none = {0};

    NvNodeId camera = nv_scene_add_node(scene, none, "camera");
    nv_scene_get(scene, camera)->camera = (NvCamera){
        .projection = NV_PROJECTION_PERSPECTIVE,
        .fov_y = 50.0f * NV_PI / 180.0f,
        .near_z = 0.1f,
        .far_z = 300.0f,
    };
    scene->active_camera = camera;

    NvNode* sun = nv_scene_get(scene, nv_scene_add_node(scene, none, "sun"));
    sun->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), 0.5f), nv_quat_axis_angle(nv_vec3(1, 0, 0), -0.8f));
    sun->light = (NvLight){.type = NV_LIGHT_DIRECTIONAL, .color = nv_vec3(1.0f, 0.96f, 0.9f), .intensity = 1.1f};

    stress->cube = app_box_mesh(app, nv_vec3(0.5f, 0.5f, 0.5f));
    stress->small_cube = app_box_mesh(app, nv_vec3(0.06f, 0.06f, 0.06f));
    stress->colors[0] = nv_renderer_add_material(&app->renderer, &(NvMaterialDesc){.base_color = {0.6f, 0.62f, 0.66f, 1.0f}});
    for (u32 i = 1; i < STRESS_MAX_COLORS; ++i) {
        NvMaterialDesc desc = {.base_color = {0, 0, 0, 1}};
        color_wheel(i, STRESS_MAX_COLORS, desc.base_color);
        stress->colors[i] = nv_renderer_add_material(&app->renderer, &desc);
    }

    NvNode* ground = nv_scene_get(scene, nv_scene_add_node(scene, none, "ground"));
    ground->position = nv_vec3(0, -0.05f, -50.0f);
    ground->scale = nv_vec3(260.0f, 0.1f, 260.0f);
    ground->mesh = stress->cube;
    ground->material = nv_renderer_add_material(&app->renderer, &(NvMaterialDesc){.base_color = {0.28f, 0.3f, 0.34f, 1.0f}});

    // The grid grows away from the camera, the crowd stands in front of it, the chain to its right.
    stress->grid_group = nv_scene_add_node(scene, none, "grid");
    stress->chain_group = nv_scene_add_node(scene, none, "chain");
    nv_scene_get(scene, stress->chain_group)->position = nv_vec3(14.0f, 0.5f, 8.0f);
    stress->crowd_group = nv_scene_add_node(scene, none, "crowd");
    nv_scene_get(scene, stress->crowd_group)->position = nv_vec3(0, 0, 4.0f);

    stress->want = (StressWorkloads){
        .grid_on = true,
        .grid_count = 1000,
        .color_count = 16,
        .chain_count = 300,
        .crowd_on = true,
        .crowd_count = 16,
        .churn_count = 32,
    };

    local_persist const BenchmarkStep steps[] = {
        {"Cubes 250", 250, 0},   {"Cubes 1000", 1000, 0}, {"Cubes 4000", 4000, 0},
        {"Cubes 8000", 8000, 0}, {"Cubes 16000", 16000, 0}, {"Crowd 8", 0, 8},
        {"Crowd 32", 0, 32},     {"Crowd 60", 0, 60},     {"Crowd 120", 0, 120},
        {"Crowd 200", 0, 200},
    };
    NV_ASSERT(NV_ARRAY_COUNT(steps) <= STRESS_MAX_STEPS);
    memcpy(stress->steps, steps, sizeof(steps));
    stress->step_count = NV_ARRAY_COUNT(steps);

    app->views[SCENE_STRESS] = (SceneView){
        .scene = scene,
        .camera = camera,
        .focus = stress->grid_group,
        .camera_yaw = 0.45f,
        .camera_pitch = 0.42f,
        .camera_distance = 28.0f,
        .follow_selection = true,
    };
    stress->built = 1;
}

//
// Workloads
//

internal void add_grid_cube(Stress* stress, u32 i)
{
    NvScene* scene = stress->scene;
    NvNodeId id = nv_scene_add_node(scene, stress->grid_group, "cube");
    NvNode* node = nv_scene_get(scene, id);
    u32 column = i % GRID_COLUMNS;
    u32 row = i / GRID_COLUMNS;
    node->position = nv_vec3(((f32)column - (GRID_COLUMNS - 1) * 0.5f) * GRID_SPACING, 0.25f, -(f32)row * GRID_SPACING);
    node->scale = nv_vec3(0.5f, 0.5f, 0.5f);
    node->mesh = stress->cube;
    node->material = stress->grid_colors ? stress->colors[1 + i % stress->grid_colors] : stress->colors[0];
    stress->grid[i] = id;
}

internal void update_grid(Stress* stress, u32 target, u32 colors)
{
    NvScene* scene = stress->scene;
    if (colors != stress->grid_colors) {
        stress->grid_colors = colors;
        for (u32 i = 0; i < stress->grid_built; ++i)
            nv_scene_get(scene, stress->grid[i])->material = colors ? stress->colors[1 + i % colors] : stress->colors[0];
    }
    while (stress->grid_built > target)
        nv_scene_remove_node(scene, stress->grid[--stress->grid_built]);
    while (stress->grid_built < target)
        add_grid_cube(stress, stress->grid_built++);
}

// A helix: each link is the child of the last, so moving the first moves them all.
internal void update_chain(Stress* stress, u32 target, f32 dt)
{
    NvScene* scene = stress->scene;
    while (stress->chain_built > target)
        nv_scene_remove_node(scene, stress->chain[--stress->chain_built]);
    while (stress->chain_built < target) {
        u32 i = stress->chain_built++;
        NvNodeId parent = i ? stress->chain[i - 1] : stress->chain_group;
        stress->chain[i] = nv_scene_add_node(scene, parent, "link");
        NvNode* node = nv_scene_get(scene, stress->chain[i]);
        node->position = i ? nv_vec3(0.12f, 0.03f, 0.0f) : nv_vec3(0, 0, 0);
        node->mesh = stress->small_cube;
        node->material = stress->colors[1 + (i * 7) % (STRESS_MAX_COLORS - 1)];
    }
    stress->chain_angle += dt;
    NvQuat twist = nv_quat_axis_angle(nv_vec3(0, 1, 0), 0.25f + 0.04f * sinf(stress->chain_angle));
    for (u32 i = 0; i < stress->chain_built; ++i)
        nv_scene_get(scene, stress->chain[i])->rotation = twist;
}

internal void update_crowd(App* app, u32 target)
{
    Stress* stress = &app->stress;
    NvScene* scene = stress->scene;
    const NvGltfModel* source = &app->character;
    NV_ASSERT(source->mesh_node_count + 1 == CROWD_NODES);

    // Characters are made on first use and then kept: an animator cannot be destroyed.
    while (stress->crowd_created < target) {
        u32 i = stress->crowd_created++;
        NvGltfModel* model = &stress->crowd[i];
        nv_gltf_instantiate(source, scene, stress->crowd_group, model);
        NvNode* root = nv_scene_get(scene, model->root);
        u32 column = i % CROWD_COLUMNS;
        u32 row = i / CROWD_COLUMNS;
        root->position = nv_vec3(((f32)column - (CROWD_COLUMNS - 1) * 0.5f) * CROWD_SPACING, 0.0f, (f32)row * CROWD_SPACING);
        snprintf(root->name, sizeof(root->name), "character %u", i + 1);

        // Every character loops its own clip from its own point in time.
        NvClipId loops[APP_MAX_CLIPS];
        u32 loop_count = 0;
        for (u32 c = 0; c < app->clip_count; ++c) {
            if (strstr(nv_anim_clip_name(app->clips[c]), "_Loop"))
                loops[loop_count++] = app->clips[c];
        }
        NvAnimator* animator = nv_anim_get(model->animator);
        NvClipId clip = loops[i % loop_count];
        nv_anim_play(animator, clip, 0.0f, 1);
        animator->layers[0].time = fmodf((f32)i * 0.37f, nv_anim_clip_duration(clip));
    }

    // The ones not asked for are hidden and paused.
    for (u32 i = 0; i < stress->crowd_created; ++i) {
        NvGltfModel* model = &stress->crowd[i];
        b32 active = i < target;
        nv_anim_get(model->animator)->scene = active ? scene : NULL;
        for (u32 m = 0; m < model->mesh_node_count; ++m) {
            NvNode* node = nv_scene_get(scene, model->mesh_nodes[m]);
            node->mesh = active ? nv_scene_get(source->scene, source->mesh_nodes[m])->mesh : (NvMeshId){0};
        }
    }
    stress->crowd_active = target;
}

// Removes and re-adds grid cubes, so node slots keep being freed and reused.
internal void churn(Stress* stress, u32 count)
{
    NvScene* scene = stress->scene;
    if (count > stress->grid_built)
        count = stress->grid_built;
    for (u32 k = 0; k < count; ++k) {
        u32 i = stress->churn_cursor++ % stress->grid_built;
        nv_scene_remove_node(scene, stress->grid[i]);
        add_grid_cube(stress, i);
    }
}

void stress_update(App* app, f32 dt)
{
    Stress* stress = &app->stress;
    StressWorkloads* want = &stress->want;

    // Every workload fits under NV_MAX_NODES: the crowd and the chain first, the grid gets the rest.
    u32 animators_left = NV_MAX_ANIMATORS - 2; // slot 0 and the showcase character
    u32 crowd = want->crowd_on ? (u32)want->crowd_count : 0;
    if (crowd > STRESS_MAX_CROWD)
        crowd = STRESS_MAX_CROWD;
    if (crowd > animators_left)
        crowd = animators_left;
    u32 crowd_nodes = (crowd > stress->crowd_created ? crowd : stress->crowd_created) * CROWD_NODES;
    u32 budget = NV_MAX_NODES - 1 - FIXED_NODES - crowd_nodes;
    u32 chain = want->chain_on ? (u32)want->chain_count : 0;
    if (chain > budget)
        chain = budget;
    budget -= chain;
    u32 grid = want->grid_on ? (u32)want->grid_count : 0;
    if (grid > STRESS_MAX_GRID)
        grid = STRESS_MAX_GRID;
    if (grid > budget)
        grid = budget;

    // Shrink before growing, so the node count never passes the limit on the way.
    u32 colors = want->colors_on ? (u32)want->color_count : 0;
    update_grid(stress, grid < stress->grid_built ? grid : stress->grid_built, colors);
    update_chain(stress, chain < stress->chain_built ? chain : stress->chain_built, 0.0f);
    update_crowd(app, crowd);
    update_chain(stress, chain, dt);
    update_grid(stress, grid, colors);
    if (want->churn_on)
        churn(stress, (u32)want->churn_count);
}

void stress_draw_bones(App* app)
{
    Stress* stress = &app->stress;
    for (u32 i = 0; i < stress->crowd_active; ++i) {
        NvAnimator* animator = nv_anim_get(stress->crowd[i].animator);
        const NvJointDesc* joints = nv_anim_joints(animator->skeleton);
        NvMat4 root = nv_scene_get(stress->scene, animator->owner)->world;
        for (u32 j = 0; j < animator->joint_count; ++j) {
            if (joints[j].parent < 0)
                continue;
            NvVec3 a = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[joints[j].parent]));
            NvVec3 b = nv_mat4_translation(nv_mat4_mul(root, animator->joint_model[j]));
            nv_renderer_debug_line(&app->renderer, a, b, nv_vec3(1.0f, 0.8f, 0.2f));
        }
    }
}

//
// Benchmark
//

internal void start_step(App* app, u32 step, f64 now)
{
    Stress* stress = &app->stress;
    const BenchmarkStep* s = &stress->steps[step];
    stress->want = (StressWorkloads){
        .grid_on = s->grid > 0,
        .grid_count = s->grid,
        .color_count = stress->before_benchmark.color_count,
        .chain_count = stress->before_benchmark.chain_count,
        .crowd_on = s->crowd > 0,
        .crowd_count = s->crowd,
        .churn_count = stress->before_benchmark.churn_count,
    };
    stress->benchmark_step = step;
    stress->benchmark_step_start = now;
    stress->results[step] = (BenchmarkResult){0};
}

internal void start_benchmark(App* app)
{
    Stress* stress = &app->stress;
    stress->before_benchmark = stress->want;
    stress->benchmark_running = 1;
    stress->result_count = 0;
    start_step(app, 0, nv_time_seconds());
}

internal void stop_benchmark(App* app, b32 restore)
{
    Stress* stress = &app->stress;
    stress->benchmark_running = 0;
    if (restore)
        stress->want = stress->before_benchmark;
}

#define BENCHMARK_WARMUP_SECONDS  1.0
#define BENCHMARK_MEASURE_SECONDS 3.0

void stress_after_frame(App* app)
{
    Stress* stress = &app->stress;
    if (!stress->benchmark_running)
        return;
    if (app->shown != SCENE_STRESS) {
        stop_benchmark(app, 1);
        return;
    }
    f64 now = nv_time_seconds();
    f64 elapsed = now - stress->benchmark_step_start;
    if (elapsed < BENCHMARK_WARMUP_SECONDS)
        return;

    BenchmarkResult* result = &stress->results[stress->benchmark_step];
    FrameTimes* t = &app->times;
    FrameTimes* sum = &result->average; // a sum until the step ends
    sum->frame += t->frame;
    sum->anim += t->anim;
    sum->scene += t->scene;
    sum->draw += t->draw;
    sum->ui += t->ui;
    sum->gpu += t->gpu;
    if (t->frame > result->worst_frame)
        result->worst_frame = t->frame;
    ++result->frames;

    if (elapsed < BENCHMARK_WARMUP_SECONDS + BENCHMARK_MEASURE_SECONDS)
        return;
    f64 n = (f64)result->frames;
    *sum = (FrameTimes){sum->frame / n, sum->anim / n, sum->scene / n, sum->draw / n, sum->ui / n, sum->gpu / n};
    stress->result_count = stress->benchmark_step + 1;
    if (stress->benchmark_step + 1 < stress->step_count)
        start_step(app, stress->benchmark_step + 1, now);
    else
        stop_benchmark(app, 1);
}

EM_JS_DEPS(nv_stress, "$stringToUTF8");

EM_JS(void, js_user_agent, (char* out, int size), {
    stringToUTF8(navigator.userAgent, out, size);
});

internal void copy_results(App* app)
{
    Stress* stress = &app->stress;
    char agent[256];
    js_user_agent(agent, sizeof(agent));
    char text[4096];
    umm used = 0;
    used += (umm)snprintf(text + used, sizeof(text) - used,
                          "nv stress benchmark\ncommit: %s (%s build)\nbrowser: %s\ncanvas: %ux%u, GPU timestamps: %s\n\n"
                          "step        frames  avg ms  worst ms  load %%  anim  scene  draw    ui    gpu\n",
                          NV_GIT_COMMIT, NV_BUILD_NAME, agent, app->gpu.width, app->gpu.height, app->gpu.has_timestamps ? "yes" : "no");
    for (u32 i = 0; i < stress->result_count && used < sizeof(text); ++i) {
        const BenchmarkResult* r = &stress->results[i];
        used += (umm)snprintf(text + used, sizeof(text) - used, "%-11s %6u  %6.2f  %8.2f  %6.0f  %4.2f  %5.2f  %4.2f  %4.2f  %5.2f\n",
                              stress->steps[i].name, r->frames, r->average.frame, r->worst_frame, app_load(&r->average),
                              r->average.anim, r->average.scene, r->average.draw, r->average.ui, r->average.gpu);
    }
    igSetClipboardText(text);
}

//
// Stress tab
//

// One label and value pair in the stats table.
internal void stat(const char* label, const char* format, f64 value)
{
    igTableNextColumn();
    igTextUnformatted(label, NULL);
    igTableNextColumn();
    igText(format, value);
}

internal void stats_section(App* app)
{
    const FrameTimes* a = &app->shown_average;
    const NvRenderStats* r = &app->renderer.stats;
    igSeparatorText("Frame (1 s average, ms)");
    // Label and value pairs: two per row where the panel is wide, one on phones.
    int columns = igGetContentRegionAvail().x > 520.0f * app->imgui.ui_scale ? 4 : 2;
    if (!igBeginTable("stats", columns, ImGuiTableFlags_SizingStretchProp, (ImVec2_c){0, 0}, 0.0f))
        return;
    stat("Frame", "%.2f", a->frame);
    stat("Load %", "%.0f", app_load(a));
    stat("FPS", "%.0f", a->frame > 0.0 ? 1000.0 / a->frame : 0.0);
    stat("Worst frame", "%.2f", app->shown_worst_frame);
    if (app->gpu.has_timestamps) {
        stat("GPU scene pass", "%.2f", a->gpu);
    } else {
        igTableNextColumn();
        igTextUnformatted("GPU scene pass", NULL);
        igTableNextColumn();
        igTextDisabled("no timestamps");
    }
    stat("CPU anim", "%.2f", a->anim);
    stat("CPU scene", "%.2f", a->scene);
    stat("CPU draw", "%.2f", a->draw);
    stat("CPU ui", "%.2f", a->ui);
    stat("Nodes", "%.0f", (f64)stress_live_nodes(app->stress.scene));
    stat("Draws", "%.0f", (f64)r->draws);
    stat("Triangles", "%.0f", (f64)r->triangles);
    stat("Skinned draws", "%.0f", (f64)r->skinned_draws);
    stat("Skin matrices", "%.0f", (f64)r->skin_matrices);
    stat("Debug lines", "%.0f", (f64)r->debug_lines);
    stat("Pipeline changes", "%.0f", (f64)r->pipeline_changes);
    stat("Material changes", "%.0f", (f64)r->material_changes);
    stat("Mesh changes", "%.0f", (f64)r->mesh_changes);
    igEndTable();
}

internal void workloads_section(App* app)
{
    Stress* stress = &app->stress;
    StressWorkloads* w = &stress->want;
    igSeparatorText("Workloads");
    igCheckbox("Cube grid", &w->grid_on);
    igSliderInt("Cubes", &w->grid_count, 0, STRESS_MAX_GRID, "%d", ImGuiSliderFlags_Logarithmic);
    igCheckbox("Many colors", &w->colors_on);
    igSliderInt("Colors", &w->color_count, 2, STRESS_MAX_COLORS - 1, "%d", 0);
    igCheckbox("Deep chain", &w->chain_on);
    igSliderInt("Links", &w->chain_count, 1, STRESS_MAX_CHAIN, "%d", 0);
    igCheckbox("Crowd", &w->crowd_on);
    igSliderInt("Characters", &w->crowd_count, 1, STRESS_MAX_CROWD, "%d", 0);
    igCheckbox("Churn", &w->churn_on);
    igSliderInt("Cubes / frame", &w->churn_count, 1, STRESS_MAX_CHURN, "%d", 0);
    igCheckbox("Crowd bones", &w->show_bones);
    igTextDisabled("Built: %u cubes, %u links, %u of %u characters", stress->grid_built, stress->chain_built,
                   stress->crowd_active, stress->crowd_created);
}

internal void benchmark_section(App* app)
{
    Stress* stress = &app->stress;
    igSeparatorText("Benchmark");
    if (stress->benchmark_running) {
        f64 elapsed = nv_time_seconds() - stress->benchmark_step_start;
        igText("Step %u/%u: %s (%s)", stress->benchmark_step + 1, stress->step_count, stress->steps[stress->benchmark_step].name,
               elapsed < BENCHMARK_WARMUP_SECONDS ? "warming up" : "measuring");
        if (igButton("Stop", (ImVec2_c){-1.0f, 0.0f}))
            stop_benchmark(app, 1);
    } else if (igButton("Run benchmark", (ImVec2_c){-1.0f, 0.0f})) {
        start_benchmark(app);
    }
    if (!stress->result_count)
        return;
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (igBeginTable("results", 6, flags, (ImVec2_c){0, 0}, 0.0f)) {
        igTableSetupColumn("Step", 0, 0.0f, 0);
        igTableSetupColumn("Avg ms", 0, 0.0f, 0);
        igTableSetupColumn("Load %", 0, 0.0f, 0);
        igTableSetupColumn("Worst", 0, 0.0f, 0);
        igTableSetupColumn("CPU a/s/d", 0, 0.0f, 0);
        igTableSetupColumn("GPU", 0, 0.0f, 0);
        igTableHeadersRow();
        for (u32 i = 0; i < stress->result_count; ++i) {
            const BenchmarkResult* r = &stress->results[i];
            igTableNextRow(0, 0.0f);
            igTableNextColumn();
            igTextUnformatted(stress->steps[i].name, NULL);
            igTableNextColumn();
            igText("%.2f", r->average.frame);
            igTableNextColumn();
            igText("%.0f", app_load(&r->average));
            igTableNextColumn();
            igText("%.2f", r->worst_frame);
            igTableNextColumn();
            igText("%.2f/%.2f/%.2f", r->average.anim, r->average.scene, r->average.draw);
            igTableNextColumn();
            if (app->gpu.has_timestamps)
                igText("%.2f", r->average.gpu);
            else
                igTextDisabled("-");
        }
        igEndTable();
    }
    if (!stress->benchmark_running && igButton("Copy results", (ImVec2_c){-1.0f, 0.0f}))
        copy_results(app);
}

void stress_ui(App* app)
{
    Stress* stress = &app->stress;
    StressWorkloads before = stress->want;
    stats_section(app);
    workloads_section(app);
    // Touching a workload during a run stops it and keeps what was touched.
    if (stress->benchmark_running && !same_workloads(&before, &stress->want))
        stop_benchmark(app, 0);
    benchmark_section(app);
}
