#include <nv/gpu.h>
#include <nv/imgui.h>
#include <nv/scene.h>
#include <nv/window.h>

#include <stdio.h>

// Every mesh node becomes one instance: the vertex shader looks up its model matrix and color in
// `objects` with the instance index.
global const char* scene_shader =
    "struct Frame {\n"
    "    view_proj: mat4x4f,\n"
    "    light_dir: vec4f,\n"
    "    light_color: vec4f,\n"
    "    ambient: vec4f,\n"
    "};\n"
    "struct Object {\n"
    "    model: mat4x4f,\n"
    "    color: vec4f,\n"
    "};\n"
    "@group(0) @binding(0) var<uniform> frame: Frame;\n"
    "@group(0) @binding(1) var<storage, read> objects: array<Object>;\n"
    "\n"
    "struct VsIn {\n"
    "    @location(0) position: vec3f,\n"
    "    @location(1) normal: vec3f,\n"
    "};\n"
    "struct VsOut {\n"
    "    @builtin(position) clip: vec4f,\n"
    "    @location(0) normal: vec3f,\n"
    "    @location(1) color: vec3f,\n"
    "};\n"
    "\n"
    "@vertex\n"
    "fn vs_main(in: VsIn, @builtin(instance_index) i: u32) -> VsOut {\n"
    "    let object = objects[i];\n"
    "    var out: VsOut;\n"
    "    out.clip = frame.view_proj * object.model * vec4f(in.position, 1.0);\n"
    "    out.normal = (object.model * vec4f(in.normal, 0.0)).xyz;\n"
    "    out.color = object.color.rgb;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_main(in: VsOut) -> @location(0) vec4f {\n"
    "    let n = normalize(in.normal);\n"
    "    let diffuse = max(dot(n, -frame.light_dir.xyz), 0.0);\n"
    "    let light = frame.ambient.rgb + frame.light_color.rgb * diffuse;\n"
    "    return vec4f(in.color * light, 1.0);\n"
    "}\n";

typedef struct Vertex {
    f32 position[3];
    f32 normal[3];
} Vertex;

typedef struct Mesh {
    WGPUBuffer vertices;
    WGPUBuffer indices;
    u32 index_count;
} Mesh;

// Layouts match the WGSL structs above.
typedef struct FrameUniforms {
    NvMat4 view_proj;
    f32 light_dir[4];
    f32 light_color[4];
    f32 ambient[4];
} FrameUniforms;

typedef struct ObjectData {
    NvMat4 model;
    f32 color[4];
} ObjectData;

enum { MESH_CUBE = 1, MESH_COUNT };
enum { MATERIAL_DEFAULT, MATERIAL_PLANET, MATERIAL_MOON, MATERIAL_COUNT };

global const f32 material_colors[MATERIAL_COUNT][4] = {
    [MATERIAL_DEFAULT] = {0.60f, 0.62f, 0.66f, 1.0f},
    [MATERIAL_PLANET] = {0.95f, 0.55f, 0.25f, 1.0f},
    [MATERIAL_MOON] = {0.55f, 0.70f, 0.95f, 1.0f},
};

typedef struct App {
    NvWindow window;
    NvGpu gpu;
    NvArena arena;
    NvScene* scene;
    NvNodeId planet;
    NvNodeId moon;

    NvImgui imgui;
    f64 last_time;
    f32 orbit_angle;
    f32 orbit_speed;   // radians per second
    NvNodeId selected; // node shown in the inspector
    bool show_demo;    // bool because ImGui writes it through a bool*

    Mesh meshes[MESH_COUNT]; // [0] unused: mesh id 0 means "no mesh"
    WGPURenderPipeline pipeline;
    WGPUBuffer frame_buffer;
    WGPUBuffer object_buffer;
    WGPUBindGroup bind_group;

    WGPUTexture depth_texture;
    WGPUTextureView depth_view;
    u32 depth_width;
    u32 depth_height;

    ObjectData* objects; // [NV_MAX_NODES], filled every frame
    u32* object_meshes;  // [NV_MAX_NODES], mesh index per object
} App;

// IMPORTANT: Global rather than on main's stack: main returns before the first frame runs.
global App app_state;
global u8 permanent_memory[NV_MEGABYTES(6)];

internal WGPUBuffer create_buffer(NvGpu* gpu, WGPUBufferUsage usage, const void* data, umm size)
{
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.usage = usage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu->device, &desc);
    if (data)
        wgpuQueueWriteBuffer(gpu->queue, buffer, 0, data, size);
    return buffer;
}

internal Mesh create_cube_mesh(NvGpu* gpu)
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

    Vertex vertices[24];
    u16 indices[36];
    for (u32 face = 0; face < 6; ++face) {
        const f32* n = faces[face][0];
        const f32* u = faces[face][1];
        const f32* v = faces[face][2];
        for (u32 corner = 0; corner < 4; ++corner) {
            Vertex* vertex = &vertices[face * 4 + corner];
            for (u32 axis = 0; axis < 3; ++axis) {
                vertex->position[axis] = 0.5f * (n[axis] + corners[corner][0] * u[axis] + corners[corner][1] * v[axis]);
                vertex->normal[axis] = n[axis];
            }
        }
        u16 base = (u16)(face * 4);
        u16* out = &indices[face * 6];
        out[0] = base; out[1] = base + 1; out[2] = base + 2;
        out[3] = base; out[4] = base + 2; out[5] = base + 3;
    }

    Mesh mesh = {0};
    mesh.vertices = create_buffer(gpu, WGPUBufferUsage_Vertex, vertices, sizeof(vertices));
    mesh.indices = create_buffer(gpu, WGPUBufferUsage_Index, indices, sizeof(indices));
    mesh.index_count = NV_ARRAY_COUNT(indices);
    return mesh;
}

internal WGPURenderPipeline create_pipeline(WGPUDevice device, WGPUTextureFormat color_format)
{
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){scene_shader, WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    module_desc.nextInChain = &wgsl.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &module_desc);

    WGPUVertexAttribute attributes[2] = {
        {.format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x3, .offset = 3 * sizeof(f32), .shaderLocation = 1},
    };
    WGPUVertexBufferLayout vertex_layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertex_layout.stepMode = WGPUVertexStepMode_Vertex;
    vertex_layout.arrayStride = sizeof(Vertex);
    vertex_layout.attributeCount = NV_ARRAY_COUNT(attributes);
    vertex_layout.attributes = attributes;

    WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
    color_target.format = color_format;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = (WGPUStringView){"fs_main", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth24Plus;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Less;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"scene", WGPU_STRLEN};
    desc.vertex.module = module;
    desc.vertex.entryPoint = (WGPUStringView){"vs_main", WGPU_STRLEN};
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex_layout;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.primitive.frontFace = WGPUFrontFace_CCW;
    desc.primitive.cullMode = WGPUCullMode_Back;
    desc.depthStencil = &depth;
    desc.fragment = &fragment;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
    wgpuShaderModuleRelease(module);
    return pipeline;
}

internal void create_gpu_resources(App* app)
{
    WGPUDevice device = app->gpu.device;
    app->meshes[MESH_CUBE] = create_cube_mesh(&app->gpu);
    app->pipeline = create_pipeline(device, app->gpu.surface_format);
    app->frame_buffer = create_buffer(&app->gpu, WGPUBufferUsage_Uniform, NULL, sizeof(FrameUniforms));
    app->object_buffer = create_buffer(&app->gpu, WGPUBufferUsage_Storage, NULL, NV_MAX_NODES * sizeof(ObjectData));

    WGPUBindGroupEntry entries[2] = {
        {.binding = 0, .buffer = app->frame_buffer, .size = sizeof(FrameUniforms)},
        {.binding = 1, .buffer = app->object_buffer, .size = NV_MAX_NODES * sizeof(ObjectData)},
    };
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(app->pipeline, 0);
    WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group_desc.layout = layout;
    group_desc.entryCount = NV_ARRAY_COUNT(entries);
    group_desc.entries = entries;
    app->bind_group = wgpuDeviceCreateBindGroup(device, &group_desc);
    wgpuBindGroupLayoutRelease(layout);
}

// Recreates the depth buffer whenever the canvas size changes.
internal void update_depth_buffer(App* app)
{
    if (app->depth_texture && app->depth_width == app->gpu.width && app->depth_height == app->gpu.height)
        return;
    if (app->depth_texture) {
        wgpuTextureViewRelease(app->depth_view);
        wgpuTextureRelease(app->depth_texture);
    }

    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    desc.size = (WGPUExtent3D){app->gpu.width, app->gpu.height, 1};
    desc.format = WGPUTextureFormat_Depth24Plus;
    app->depth_texture = wgpuDeviceCreateTexture(app->gpu.device, &desc);
    app->depth_view = wgpuTextureCreateView(app->depth_texture, NULL);
    app->depth_width = app->gpu.width;
    app->depth_height = app->gpu.height;
}

internal NvMat4 camera_view_proj(NvNode* camera_node, f32 aspect)
{
    NvCamera* camera = &camera_node->camera;
    NvMat4 proj = {0};
    switch (camera->projection) {
    case NV_PROJECTION_PERSPECTIVE:
        proj = nv_mat4_perspective(camera->fov_y, aspect, camera->near_z, camera->far_z);
        break;
    case NV_PROJECTION_ORTHOGRAPHIC:
        proj = nv_mat4_orthographic(camera->ortho_height, aspect, camera->near_z, camera->far_z);
        break;
    default:
        NV_INVALID_CODE_PATH;
    }
    return nv_mat4_mul(proj, nv_mat4_inverse(camera_node->world));
}

internal void render_scene(App* app, WGPUCommandEncoder encoder, WGPUTextureView target)
{
    NvScene* scene = app->scene;
    FrameUniforms uniforms = {0};
    uniforms.view_proj = camera_view_proj(nv_scene_get(scene, scene->active_camera),
                                          (f32)app->gpu.width / (f32)app->gpu.height);
    uniforms.ambient[0] = uniforms.ambient[1] = uniforms.ambient[2] = 0.12f;

    u32 object_count = 0;
    for (u32 index = 1; index <= scene->node_count; ++index) {
        NvNode* node = &scene->nodes[index];
        if (!(node->gen & 1))
            continue;

        // TODO: Only the first directional light is used until lighting gets its own pass.
        if (node->light.type == NV_LIGHT_DIRECTIONAL && uniforms.light_color[3] == 0.0f) {
            NvVec3 dir = nv_mat4_forward(node->world);
            NvVec3 color = nv_vec3_scale(node->light.color, node->light.intensity);
            uniforms.light_dir[0] = dir.x;
            uniforms.light_dir[1] = dir.y;
            uniforms.light_dir[2] = dir.z;
            uniforms.light_color[0] = color.x;
            uniforms.light_color[1] = color.y;
            uniforms.light_color[2] = color.z;
            uniforms.light_color[3] = 1.0f; // marks the light as taken
        }

        if (node->mesh.index) {
            NV_ASSERT(node->mesh.index < MESH_COUNT && node->material.index < MATERIAL_COUNT);
            ObjectData* object = &app->objects[object_count];
            object->model = node->world;
            for (u32 i = 0; i < 4; ++i)
                object->color[i] = material_colors[node->material.index][i];
            app->object_meshes[object_count] = node->mesh.index;
            ++object_count;
        }
    }

    WGPUQueue queue = app->gpu.queue;
    wgpuQueueWriteBuffer(queue, app->frame_buffer, 0, &uniforms, sizeof(uniforms));
    if (object_count)
        wgpuQueueWriteBuffer(queue, app->object_buffer, 0, app->objects, object_count * sizeof(ObjectData));

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = target;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){0.02, 0.02, 0.035, 1.0};

    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = app->depth_view;
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Discard;
    depth.depthClearValue = 1.0f;

    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;
    pass_desc.depthStencilAttachment = &depth;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, app->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, app->bind_group, 0, NULL);

    u32 bound_mesh = 0;
    for (u32 i = 0; i < object_count; ++i) {
        u32 mesh_index = app->object_meshes[i];
        Mesh* mesh = &app->meshes[mesh_index];
        if (mesh_index != bound_mesh) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh->vertices, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(pass, mesh->indices, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
            bound_mesh = mesh_index;
        }
        // The first-instance offset makes instance_index equal the object's slot in `objects`.
        wgpuRenderPassEncoderDrawIndexed(pass, mesh->index_count, 1, 0, 0, i);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
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
    ground_node->mesh = (NvMeshId){MESH_CUBE};

    app->planet = nv_scene_add_node(scene, none, "planet");
    NvNode* planet_node = nv_scene_get(scene, app->planet);
    planet_node->mesh = (NvMeshId){MESH_CUBE};
    planet_node->material = (NvMaterialId){MATERIAL_PLANET};

    app->moon = nv_scene_add_node(scene, app->planet, "moon");
    NvNode* moon_node = nv_scene_get(scene, app->moon);
    moon_node->position = nv_vec3(2.4f, 0.0f, 0.0f);
    moon_node->scale = nv_vec3(0.45f, 0.45f, 0.45f);
    moon_node->mesh = (NvMeshId){MESH_CUBE};
    moon_node->material = (NvMaterialId){MATERIAL_MOON};
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

internal void build_ui(App* app)
{
    ImGuiIO* io = igGetIO_Nil();
    f32 width = 270.0f * app->imgui.ui_scale;
    if (width > io->DisplaySize.x - 20.0f)
        width = io->DisplaySize.x - 20.0f;
    igSetNextWindowPos((ImVec2_c){10.0f, 10.0f}, ImGuiCond_FirstUseEver, (ImVec2_c){0.0f, 0.0f});
    igSetNextWindowSize((ImVec2_c){width, 0.0f}, ImGuiCond_FirstUseEver);
    if (igBegin("Scene", NULL, 0)) {
        igText("%.0f FPS (%.2f ms)", io->Framerate, 1000.0f / io->Framerate);
        igSliderFloat("Orbit", &app->orbit_speed, -3.0f, 3.0f, "%.2f rad/s", 0);
        igCheckbox("ImGui demo", &app->show_demo);

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

    if (app->show_demo)
        igShowDemoWindow(&app->show_demo);
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

    nv_imgui_new_frame(&app->imgui, dt);
    build_ui(app);

    // The moon is the planet's child, so spinning the planet carries the moon around it.
    app->orbit_angle += app->orbit_speed * dt;
    nv_scene_get(app->scene, app->planet)->rotation = nv_quat_axis_angle(nv_vec3(0, 1, 0), app->orbit_angle);
    nv_scene_get(app->scene, app->moon)->rotation =
        nv_quat_axis_angle(nv_vec3_normalize(nv_vec3(1, 1, 0)), (f32)now * 2.0f);
    nv_scene_update(app->scene);

    update_depth_buffer(app);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->gpu.device, NULL);
    render_scene(app, encoder, target);
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
    app->objects = NV_PUSH_ARRAY(&app->arena, NV_MAX_NODES, ObjectData);
    app->object_meshes = NV_PUSH_ARRAY(&app->arena, NV_MAX_NODES, u32);

    nv_window_create(&app->window, "nv - scene");
    if (!nv_gpu_create(&app->gpu, &app->window)) {
        fprintf(stderr, "fatal: failed to initialize WebGPU\n");
        return 1;
    }

    create_gpu_resources(app);
    nv_imgui_init(&app->imgui, &app->gpu, &app->window, &app->arena);
    build_scene(app);
    app->orbit_speed = 0.7f;
    app->selected = app->moon;
    app->last_time = nv_time_seconds();
    nv_window_run(&app->window, frame, app);
    return 0;
}
