#include <nv/gpu.h>
#include <nv/imgui.h>
#include <nv/renderer.h>
#include <nv/scene.h>
#include <nv/window.h>

#include <stdio.h>

typedef struct App {
    NvWindow window;
    NvGpu gpu;
    NvArena arena;
    NvScene* scene;
    NvRenderer renderer;
    NvImgui imgui;

    NvMeshId cube;
    NvMaterialId planet_material;
    NvMaterialId moon_material;
    NvMaterialId ground_material;
    NvNodeId planet;
    NvNodeId moon;

    f64 last_time;
    f32 orbit_angle;
    f32 orbit_speed;   // radians per second
    NvNodeId selected; // node shown in the inspector
} App;

// IMPORTANT: Global rather than on main's stack: main returns before the first frame runs.
global App app_state;
global u8 permanent_memory[NV_MEGABYTES(8)];

internal NvMeshId create_cube_mesh(NvRenderer* renderer)
{
    // Each face: normal n and in-plane axes u, v with u x v = n, so the corners below wind
    // counter-clockwise when seen from outside.
    local_persist const f32 faces[6][3][3] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    local_persist const f32 corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

    NvVertex vertices[24] = {0};
    u32 indices[36];
    for (u32 face = 0; face < 6; ++face) {
        const f32* n = faces[face][0];
        const f32* u = faces[face][1];
        const f32* v = faces[face][2];
        for (u32 corner = 0; corner < 4; ++corner) {
            NvVertex* vertex = &vertices[face * 4 + corner];
            for (u32 axis = 0; axis < 3; ++axis) {
                vertex->position[axis] = 0.5f * (n[axis] + corners[corner][0] * u[axis] + corners[corner][1] * v[axis]);
                vertex->normal[axis] = n[axis];
            }
        }
        u32 base = face * 4;
        u32* out = &indices[face * 6];
        out[0] = base; out[1] = base + 1; out[2] = base + 2;
        out[3] = base; out[4] = base + 2; out[5] = base + 3;
    }
    NvMeshData data = {.vertices = vertices, .vertex_count = 24, .indices = indices, .index_count = 36};
    return nv_renderer_add_mesh(renderer, &data);
}

internal void build_scene(App* app)
{
    NvScene* scene = app->scene;
    NvNodeId none = {0};

    NvNodeId camera = nv_scene_add_node(scene, none, "camera");
    NvNode* camera_node = nv_scene_get(scene, camera);
    camera_node->position = nv_vec3(0.0f, 2.5f, 7.0f);
    camera_node->rotation = nv_quat_axis_angle(nv_vec3(1, 0, 0), -0.32f);
    camera_node->camera = (NvCamera){
        .projection = NV_PROJECTION_PERSPECTIVE,
        .fov_y = 55.0f * NV_PI / 180.0f,
        .near_z = 0.1f,
        .far_z = 100.0f,
    };
    scene->active_camera = camera;

    NvNodeId sun = nv_scene_add_node(scene, none, "sun");
    NvNode* sun_node = nv_scene_get(scene, sun);
    sun_node->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), 0.7f),
                                     nv_quat_axis_angle(nv_vec3(1, 0, 0), -0.9f));
    sun_node->light = (NvLight){
        .type = NV_LIGHT_DIRECTIONAL,
        .color = nv_vec3(1.0f, 0.95f, 0.85f),
        .intensity = 1.1f,
    };

    NvNodeId ground = nv_scene_add_node(scene, none, "ground");
    NvNode* ground_node = nv_scene_get(scene, ground);
    ground_node->position = nv_vec3(0.0f, -1.2f, 0.0f);
    ground_node->scale = nv_vec3(9.0f, 0.1f, 9.0f);
    ground_node->mesh = app->cube;
    ground_node->material = app->ground_material;

    app->planet = nv_scene_add_node(scene, none, "planet");
    NvNode* planet_node = nv_scene_get(scene, app->planet);
    planet_node->mesh = app->cube;
    planet_node->material = app->planet_material;

    app->moon = nv_scene_add_node(scene, app->planet, "moon");
    NvNode* moon_node = nv_scene_get(scene, app->moon);
    moon_node->position = nv_vec3(2.4f, 0.0f, 0.0f);
    moon_node->scale = nv_vec3(0.45f, 0.45f, 0.45f);
    moon_node->mesh = app->cube;
    moon_node->material = app->moon_material;
}

internal void node_tree(App* app, u32 index)
{
    NvScene* scene = app->scene;
    NvNode* node = &scene->nodes[index];
    NvNodeId id = {index, node->gen};

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (!node->first_child)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (app->selected.index == id.index && app->selected.gen == id.gen)
        flags |= ImGuiTreeNodeFlags_Selected;

    b32 open = igTreeNodeEx_Ptr((void*)(umm)index, flags, "%s", node->name);
    if (igIsItemClicked(ImGuiMouseButton_Left))
        app->selected = id;
    if (open && node->first_child) {
        for (u32 child = node->first_child; child; child = scene->nodes[child].next_sibling)
            node_tree(app, child);
        igTreePop();
    }
}

internal void build_ui(App* app, NvRect panel)
{
    ImGuiIO* io = igGetIO_Nil();
    if (nv_imgui_begin_panel(&app->imgui, "Scene", panel)) {
        igText("%.0f FPS (%.2f ms)", io->Framerate, 1000.0f / io->Framerate);
        igSliderFloat("Orbit", &app->orbit_speed, -3.0f, 3.0f, "%.2f rad/s", 0);

        igSeparator();
        for (u32 root = app->scene->first_root; root; root = app->scene->nodes[root].next_sibling)
            node_tree(app, root);

        igSeparator();
        if (app->selected.index) {
            NvNode* node = nv_scene_get(app->scene, app->selected);
            igInputText("Name", node->name, sizeof(node->name), 0, NULL, NULL);
            igDragFloat3("Position", &node->position.x, 0.02f, 0.0f, 0.0f, "%.2f", 0);
            igDragFloat3("Scale", &node->scale.x, 0.01f, 0.01f, 100.0f, "%.2f", 0);
        } else {
            igTextDisabled("Select a node to edit it.");
        }
    }
    igEnd();
}

internal void frame(void* userdata)
{
    App* app = userdata;
    f64 now = nv_time_seconds();
    f32 dt = (f32)(now - app->last_time);
    app->last_time = now;

    WGPUTextureView target = nv_gpu_begin_frame(&app->gpu);
    if (!target)
        return;

    NvEditorLayout layout = nv_editor_layout(&app->gpu, NV_EDITOR_VIEWPORT_FRACTION);
    nv_imgui_new_frame(&app->imgui, dt);
    build_ui(app, layout.panel);

    // The moon is the planet's child, so spinning the planet carries the moon around it.
    app->orbit_angle += app->orbit_speed * dt;
    nv_scene_get(app->scene, app->planet)->rotation = nv_quat_axis_angle(nv_vec3(0, 1, 0), app->orbit_angle);
    nv_scene_get(app->scene, app->moon)->rotation =
        nv_quat_axis_angle(nv_vec3_normalize(nv_vec3(1, 1, 0)), (f32)now * 2.0f);
    nv_scene_update(app->scene);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->gpu.device, NULL);
    nv_renderer_draw(&app->renderer, app->scene, NULL, layout.viewport, encoder, target);
    nv_imgui_render(&app->imgui, encoder, target);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(app->gpu.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    nv_gpu_end_frame(&app->gpu);
}

int main(void)
{
    App* app = &app_state;
    nv_arena_init(&app->arena, permanent_memory, sizeof(permanent_memory));
    app->scene = NV_PUSH_STRUCT(&app->arena, NvScene);

    nv_window_create(&app->window, "nv - scene");
    if (!nv_gpu_create(&app->gpu, &app->window)) {
        fprintf(stderr, "fatal: failed to initialize WebGPU\n");
        return 1;
    }

    nv_renderer_init(&app->renderer, &app->gpu, &app->arena);
    nv_imgui_init(&app->imgui, &app->gpu, &app->window, &app->arena);
    app->cube = create_cube_mesh(&app->renderer);
    app->ground_material = nv_renderer_add_material(&app->renderer, &(NvMaterialDesc){.base_color = {0.60f, 0.62f, 0.66f, 1.0f}});
    app->planet_material = nv_renderer_add_material(&app->renderer, &(NvMaterialDesc){.base_color = {0.95f, 0.55f, 0.25f, 1.0f}});
    app->moon_material = nv_renderer_add_material(&app->renderer, &(NvMaterialDesc){.base_color = {0.55f, 0.70f, 0.95f, 1.0f}});

    build_scene(app);
    app->orbit_speed = 0.7f;
    app->selected = app->moon;
    app->last_time = nv_time_seconds();
    nv_window_run(&app->window, frame, app);
    return 0;
}
