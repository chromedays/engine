#include <nv/gpu.h>
#include <nv/window.h>

#include <stdio.h>

/*
 * Vertex positions and colors are generated from the vertex index, so no vertex buffer is needed.
 * `u.scale` squeezes the shorter screen axis so the triangle keeps its shape at any aspect ratio.
 */
static const char* k_shader =
    "struct Uniforms {\n"
    "    scale: vec2f,\n"
    "};\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "\n"
    "struct VsOut {\n"
    "    @builtin(position) position: vec4f,\n"
    "    @location(0) color: vec3f,\n"
    "};\n"
    "\n"
    "@vertex\n"
    "fn vs_main(@builtin(vertex_index) i: u32) -> VsOut {\n"
    "    var positions = array<vec2f, 3>(\n"
    "        vec2f( 0.0,  0.5),\n"
    "        vec2f(-0.5, -0.5),\n"
    "        vec2f( 0.5, -0.5),\n"
    "    );\n"
    "    var colors = array<vec3f, 3>(\n"
    "        vec3f(1.0, 0.0, 0.0),\n"
    "        vec3f(0.0, 1.0, 0.0),\n"
    "        vec3f(0.0, 0.0, 1.0),\n"
    "    );\n"
    "    var out: VsOut;\n"
    "    out.position = vec4f(positions[i] * u.scale, 0.0, 1.0);\n"
    "    out.color = colors[i];\n"
    "    return out;\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_main(in: VsOut) -> @location(0) vec4f {\n"
    "    return vec4f(in.color, 1.0);\n"
    "}\n";

static WGPURenderPipeline create_pipeline(WGPUDevice device, WGPUTextureFormat format)
{
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){k_shader, WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    module_desc.nextInChain = &wgsl.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &module_desc);

    WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
    color_target.format = format;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = (WGPUStringView){"fs_main", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"triangle", WGPU_STRLEN};
    desc.vertex.module = module;
    desc.vertex.entryPoint = (WGPUStringView){"vs_main", WGPU_STRLEN};
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.fragment = &fragment;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
    wgpuShaderModuleRelease(module);
    return pipeline;
}

typedef struct Uniforms {
    float scale[2];
    float _pad[2]; /* uniform buffers are sized in 16-byte multiples */
} Uniforms;

typedef struct App {
    NvWindow window;
    NvGpu gpu;
    WGPURenderPipeline pipeline;
    WGPUBuffer uniform_buffer;
    WGPUBindGroup bind_group;
} App;

static void create_uniforms(App* app)
{
    WGPUBufferDescriptor buffer_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer_desc.label = (WGPUStringView){"triangle uniforms", WGPU_STRLEN};
    buffer_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    buffer_desc.size = sizeof(Uniforms);
    app->uniform_buffer = wgpuDeviceCreateBuffer(app->gpu.device, &buffer_desc);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = app->uniform_buffer;
    entry.size = sizeof(Uniforms);

    /* The pipeline was created with an automatic layout; take group 0's layout from it. */
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(app->pipeline, 0);
    WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group_desc.layout = layout;
    group_desc.entryCount = 1;
    group_desc.entries = &entry;
    app->bind_group = wgpuDeviceCreateBindGroup(app->gpu.device, &group_desc);
    wgpuBindGroupLayoutRelease(layout);
}

static void update_uniforms(App* app)
{
    const float w = (float)app->gpu.width;
    const float h = (float)app->gpu.height;
    Uniforms u = {.scale = {1.0f, 1.0f}};
    if (w > h)
        u.scale[0] = h / w;
    else
        u.scale[1] = w / h;
    wgpuQueueWriteBuffer(app->gpu.queue, app->uniform_buffer, 0, &u, sizeof(u));
}

static void draw_frame(App* app, WGPUTextureView target)
{
    NvGpu* gpu = &app->gpu;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = target;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){0.05, 0.05, 0.08, 1.0};

    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, app->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, app->bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

/* Static rather than on main's stack: on the web, main returns before the first frame runs. */
static App g_app;

static void frame(void* userdata)
{
    App* app = userdata;
    WGPUTextureView target = nv_gpu_begin_frame(&app->gpu);
    if (target) {
        update_uniforms(app);
        draw_frame(app, target);
        nv_gpu_end_frame(&app->gpu);
    }
}

int main(void)
{
    App* app = &g_app;
    if (!nv_window_create(&app->window, "engine - triangle", 1280, 720)) {
        fprintf(stderr, "fatal: failed to create window\n");
        return 1;
    }

    if (!nv_gpu_create(&app->gpu, &app->window)) {
        fprintf(stderr, "fatal: failed to initialize WebGPU\n");
        nv_window_destroy(&app->window);
        return 1;
    }

    app->pipeline = create_pipeline(app->gpu.device, app->gpu.surface_format);
    create_uniforms(app);

    nv_window_run(&app->window, frame, app);

    wgpuBindGroupRelease(app->bind_group);
    wgpuBufferRelease(app->uniform_buffer);
    wgpuRenderPipelineRelease(app->pipeline);
    nv_gpu_destroy(&app->gpu);
    nv_window_destroy(&app->window);
    return 0;
}
