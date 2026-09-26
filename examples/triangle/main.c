#include <engine/gpu.h>
#include <engine/window.h>

#include <stdio.h>

/* Vertex positions and colors are generated from the vertex index, so no vertex buffer is needed. */
static const char* k_shader =
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
    "    out.position = vec4f(positions[i], 0.0, 1.0);\n"
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

static void draw_frame(EngGpu* gpu, WGPURenderPipeline pipeline, WGPUTextureView target)
{
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
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

int main(void)
{
    EngWindow window;
    if (!eng_window_create(&window, "engine - triangle", 1280, 720)) {
        fprintf(stderr, "fatal: failed to create window\n");
        return 1;
    }

    EngGpu gpu;
    if (!eng_gpu_create(&gpu, &window)) {
        fprintf(stderr, "fatal: failed to initialize WebGPU\n");
        eng_window_destroy(&window);
        return 1;
    }

    WGPURenderPipeline pipeline = create_pipeline(gpu.device, gpu.surface_format);

    while (!eng_window_should_close(&window)) {
        eng_window_poll_events(&window);
        WGPUTextureView target = eng_gpu_begin_frame(&gpu);
        if (target) {
            draw_frame(&gpu, pipeline, target);
            eng_gpu_end_frame(&gpu);
        }
    }

    wgpuRenderPipelineRelease(pipeline);
    eng_gpu_destroy(&gpu);
    eng_window_destroy(&window);
    return 0;
}
