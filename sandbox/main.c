#include "sandbox.h"

#include <cimguizmo.h>
#include <emscripten/emscripten.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// IMPORTANT: Global rather than on main's stack: main returns before the first frame runs.
global Sandbox sandbox_state;
global u8 permanent_memory[NV_MEGABYTES(48)];
global u8 scratch_memory[NV_MEGABYTES(128)]; // holds a whole glb while it loads (chess.glb is 43 MB)
global u8 anim_memory[NV_MEGABYTES(48)];

//
// Scene
//

NvMeshId sandbox_box_mesh(Sandbox* sandbox, NvVec3 half)
{
    NvVertex vertices[24];
    u32 indices[36];
    NvMeshBuilder mesh = {.vertices = vertices, .vertex_capacity = NV_ARRAY_COUNT(vertices), .indices = indices,
                          .index_capacity = NV_ARRAY_COUNT(indices)};
    nv_mesh_append_box(&mesh, nv_vec3(0, 0, 0), half);
    NvMeshData data = nv_mesh_builder_data(&mesh);
    return nv_renderer_add_mesh(&sandbox->renderer, &data);
}

NvAnimatorId sandbox_node_animator(NvScene* scene, NvNodeId id)
{
    NvNode* node = nv_scene_get(scene, id);
    if (node->animator.index)
        return node->animator;
    for (u32 child = node->first_child; child; child = scene->nodes[child].next_sibling) {
        if (scene->nodes[child].animator.index)
            return scene->nodes[child].animator;
    }
    return (NvAnimatorId){0};
}

SceneView* sandbox_view(Sandbox* sandbox)
{
    return &sandbox->docs[sandbox->shown].view;
}

void sandbox_show_scene(Sandbox* sandbox, u32 index)
{
    if (index >= sandbox->scene_count)
        return;
    SceneDoc* doc = &sandbox->docs[index];
    if (!doc->built) {
        nv_log(NV_LOG_ERROR, "scene", "cannot show %s: %s", doc->file, doc->error);
        return;
    }
    if (index == sandbox->shown)
        return;
    // Play belongs to the scene on screen: switching ends it, so a scene that is not shown is always in Edit mode.
    sandbox_stop_playing(sandbox);
    sandbox->shown = index;
    effects_clear(sandbox); // what the other scene fired does not carry over
    if (stress_tab_shown(sandbox))
        sandbox->open_stress = 1;
}

//
// Frame
//

#define ORBIT_RADIANS_PER_PIXEL 0.008f

// Mouse and touch input on the viewport: drags orbit and pan, the wheel and pinches zoom.
internal void apply_view_input(Sandbox* sandbox, SceneView* view, const NvSceneOutput* scene_output)
{
    const NvViewInput* in = &sandbox->imgui.view;
    nv_orbit_camera_turn(&view->orbit, -in->orbit_x * ORBIT_RADIANS_PER_PIXEL, in->orbit_y * ORBIT_RADIANS_PER_PIXEL, in->dolly);

    if (view->selected.index != view->panned_for.index || view->selected.gen != view->panned_for.gen) {
        view->pan = nv_vec3(0, 0, 0);
        view->panned_for = view->selected;
    }
    if (in->pan_x != 0.0f || in->pan_y != 0.0f) {
        // Move the orbit point so the scene follows the finger. The image's height is in CSS pixels: the scene's height
        // times the screen pixels each of its pixels takes.
        f32 height = (f32)scene_output->height * scene_output->pixel_height / nv_window_pixel_ratio(&sandbox->window);
        NvVec3 move = nv_orbit_camera_pan(&view->orbit, nv_scene_get(view->scene, view->camera), height, in->pan_x, in->pan_y);
        if (view->follow_selection)
            view->pan = nv_vec3_add(view->pan, move);
        else
            view->orbit.target = nv_vec3_add(view->orbit.target, move);
    }
}

// A tap in the viewport selects the mesh under it, or clears the selection. With Ctrl or Shift held,
// or the phone's Multi toggle on, it adds the mesh or removes it, and empty space changes nothing
// (docs/specs/selection.md). A character's meshes stand for the character, so they select its root.
// A tap on a bar around a fixed-size image (docs/specs/resolution.md) does nothing.
internal void pick(Sandbox* sandbox, const NvSceneOutput* scene_output)
{
    const NvViewInput* in = &sandbox->imgui.view;
    if (!in->tapped)
        return;
    // A tap on the build label's badge opens the Console tab and leaves the selection alone.
    const f32* badge = sandbox->badge_box;
    if (badge[2] > badge[0] && in->tap_x >= badge[0] && in->tap_x < badge[2] && in->tap_y >= badge[1] &&
        in->tap_y < badge[3]) {
        sandbox->open_console = 1;
        return;
    }
    SceneView* view = sandbox_view(sandbox);
    NvTapRay tap = nv_renderer_tap_ray(view->scene, *scene_output, in->tap_x, in->tap_y, nv_window_pixel_ratio(&sandbox->window));
    if (!tap.ok)
        return;
    NvNodeId hit = nv_renderer_pick(&sandbox->renderer, view->scene, nv_anim_skins(), tap.ray);
    if (hit.index) {
        NvNode* node = nv_scene_get(view->scene, hit);
        if (node->animator.index && node->parent)
            hit = (NvNodeId){node->parent, view->scene->nodes[node->parent].gen};
        sandbox->open_inspector = 1;
    }
    if (sandbox->multi_select || (in->tap_mods & (ImGuiMod_Ctrl | ImGuiMod_Shift)))
        selection_toggle(sandbox, view, hit);
    else
        selection_set(view, hit);
}

// A node's mesh boxes, and those of its mesh children (a character's meshes).
internal void draw_node_boxes(Sandbox* sandbox, NvScene* scene, NvNodeId id, NvVec3 color)
{
    NvNode* selected = nv_scene_get(scene, id);
    u32 nodes[17];
    u32 count = 0;
    nodes[count++] = id.index;
    if (!selected->mesh.index) {
        for (u32 child = selected->first_child; child && count < NV_ARRAY_COUNT(nodes); child = scene->nodes[child].next_sibling)
            nodes[count++] = child;
    }
    for (u32 n = 0; n < count; ++n) {
        NvNode* node = &scene->nodes[nodes[n]];
        if (!node->mesh.index)
            continue;
        const NvSkin* skin = node->animator.index ? &nv_anim_skins()[node->animator.index] : NULL;
        NvBox box = nv_renderer_mesh_bounds(&sandbox->renderer, node->mesh, skin);
        if (nv_box_is_empty(box))
            continue;
        NvVec3 lo = box.min;
        NvVec3 hi = box.max;
        NvVec3 corners[8];
        for (u32 c = 0; c < 8; ++c)
            corners[c] = nv_mat4_transform_point(node->world, nv_vec3(c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z));
        for (u32 c = 0; c < 8; ++c) {
            for (u32 bit = 1; bit < 8; bit <<= 1) {
                if (!(c & bit))
                    nv_renderer_debug_line(&sandbox->renderer, corners[c], corners[c | bit], color);
            }
        }
    }
}

// Every selected node's boxes: the primary's cyan, the others' a darker blue.
internal void draw_selection(Sandbox* sandbox)
{
    SceneView* view = sandbox_view(sandbox);
    for (u32 i = 0; i < view->other_count; ++i)
        draw_node_boxes(sandbox, view->scene, view->others[i], nv_vec3(0.15f, 0.4f, 0.85f));
    if (view->selected.index)
        draw_node_boxes(sandbox, view->scene, view->selected, nv_vec3(0.3f, 0.85f, 1.0f));
}

// Where the camera orbits when it follows: the selected node, or the view's focus. Cameras and
// lights are not worth orbiting, so they fall back to the focus. A character's origin is at its
// feet, so its focus is raised.
internal NvVec3 view_focus_point(SceneView* view)
{
    NvScene* scene = view->scene;
    NvNodeId focus = view->selected;
    if (!focus.index || nv_scene_get(scene, focus)->camera.projection || nv_scene_get(scene, focus)->light.type)
        focus = view->focus;
    NvVec3 point = nv_vec3(0, 0, 0);
    if (focus.index) {
        point = nv_mat4_translation(nv_scene_get(scene, focus)->world);
        if (sandbox_node_animator(scene, focus).index)
            point.y += 0.92f;
    }
    return point;
}

void sandbox_set_home(SceneView* view)
{
    view->home = view->orbit;
}

void sandbox_focus_selection(Sandbox* sandbox)
{
    SceneView* view = sandbox_view(sandbox);
    if (!view->selected.index)
        return;
    // Following, the orbit point is the selection plus the pan, so an empty pan is what centers it.
    view->pan = nv_vec3(0, 0, 0);
    view->orbit.target = view_focus_point(view);
}

internal void update_camera(Sandbox* sandbox, const NvSceneOutput* scene_output)
{
    SceneView* view = sandbox_view(sandbox);
    NvScene* scene = view->scene;
    apply_view_input(sandbox, view, scene_output);
    // Following, the camera orbits the selection (or the view's focus). Not following, it stays
    // where it was and only moves by panning, whatever gets selected.
    // NOTE: While the gizmo drags the selection, the camera holds still: following it would move
    // the pointer's ray with the node, and the drag would run away.
    if (view->follow_selection && !ImGuizmo_IsUsingAny())
        view->orbit.target = nv_vec3_add(view_focus_point(view), view->pan);

    // The camera is a top-level node and nv_scene_update already ran this frame.
    nv_orbit_camera_place(&view->orbit, nv_scene_get(scene, view->camera));
}

internal OPERATION gizmo_imguizmo_operation(Sandbox* sandbox)
{
    switch (sandbox->gizmo_operation) {
    case GIZMO_MOVE: return TRANSLATE;
    case GIZMO_ROTATE: return ROTATE_X | ROTATE_Y | ROTATE_Z; // no screen-space ring
    case GIZMO_SCALE: return SCALE;
    }
    NV_INVALID_CODE_PATH;
    return TRANSLATE;
}

// NvImgui.view_grab: a press on a gizmo handle drags the gizmo instead of the camera.
internal b32 gizmo_grab(void* data)
{
    Sandbox* sandbox = data;
    return sandbox->gizmo_shown && ImGuizmo_IsOver_OPERATION(gizmo_imguizmo_operation(sandbox));
}

// Puts a world matrix back into the node's position, rotation and scale, which nv_scene_update turns
// into `world` next frame: world = parent world * joint (for attached nodes) * local.
internal void set_node_world(NvScene* scene, NvNode* node, NvMat4 world)
{
    NvMat4 parent = node->parent ? scene->nodes[node->parent].world : nv_mat4_identity();
    if (node->attach.animator.index)
        parent = nv_mat4_mul(parent, node->attach.joint_model);
    NvMat4 local = nv_mat4_mul(nv_mat4_inverse(parent), world);
    NvVec3 position, scale;
    NvQuat rotation;
    // A scale dragged to (almost) nothing cannot be split back into a rotation.
    f32 smallest = 1.0e-4f;
    for (u32 column = 0; column < 3; ++column) {
        const f32* c = &local.e[column * 4];
        if (c[0] * c[0] + c[1] * c[1] + c[2] * c[2] < smallest * smallest)
            return;
    }
    nv_mat4_decompose(local, &position, &rotation, &scale);
    node->position = position;
    node->rotation = rotation;
    node->scale = scale;
}

// The nodes the gizmo moves: the selected ones, without the active camera (the orbit camera's to
// move), without those under a selected node, which already move with it, and without what a generator made.
internal u32 gizmo_targets(Sandbox* sandbox, SceneView* view, NvNodeId* out)
{
    NvScene* scene = view->scene;
    SceneDoc* doc = sandbox_doc(sandbox);
    u32 count = 0;
    for (u32 i = 0; i < selection_count(view); ++i) {
        NvNodeId id = selection_get(view, i);
        if (id.index == scene->active_camera.index || !doc_authored(doc, id.index))
            continue;
        b32 under_selected = 0;
        for (u32 p = scene->nodes[id.index].parent; p && !under_selected; p = scene->nodes[p].parent)
            under_selected = selection_has(view, (NvNodeId){p, scene->nodes[p].gen});
        if (!under_selected)
            out[count++] = id;
    }
    return count;
}

// Where the gizmo sits: on the node itself when it moves one node; for several, at their average
// position, with world axes or the primary's rotation (docs/specs/selection.md).
internal NvMat4 gizmo_pivot(Sandbox* sandbox, NvScene* scene, const NvNodeId* nodes, u32 count)
{
    if (count == 1)
        return nv_scene_get(scene, nodes[0])->world;
    NvVec3 center = nv_vec3(0, 0, 0);
    for (u32 i = 0; i < count; ++i)
        center = nv_vec3_add(center, nv_mat4_translation(nv_scene_get(scene, nodes[i])->world));
    center = nv_vec3_scale(center, 1.0f / (f32)count);
    NvQuat rotation = nv_quat_identity();
    if (sandbox->gizmo_local || sandbox->gizmo_operation == GIZMO_SCALE) {
        // The primary's rotation, or the first moving node's when the primary is the camera.
        NvNodeId primary = sandbox_view(sandbox)->selected;
        NvNodeId axes = primary.index != scene->active_camera.index ? primary : nodes[0];
        NvVec3 position, scale;
        nv_mat4_decompose(nv_scene_get(scene, axes)->world, &position, &rotation, &scale);
    }
    return nv_mat4_trs(center, rotation, nv_vec3(1, 1, 1));
}

// The transform gizmo on the selection. ImGuizmo edits a world matrix: for one node its own, for
// several the pivot's, whose change since the drag started moves every node.
internal void draw_gizmo(Sandbox* sandbox, const NvSceneOutput* scene_output)
{
    SceneView* view = sandbox_view(sandbox);
    NvScene* scene = view->scene;
    sandbox->gizmo_shown = 0;
    // While a drag runs, the nodes it started with stay its nodes; otherwise the selection's.
    if (!ImGuizmo_IsUsingAny()) {
        sandbox->gizmo_node_count = gizmo_targets(sandbox, view, sandbox->gizmo_nodes);
        if (!sandbox->gizmo_node_count)
            return;
        for (u32 i = 0; i < sandbox->gizmo_node_count; ++i)
            sandbox->gizmo_start_world[i] = nv_scene_get(scene, sandbox->gizmo_nodes[i])->world;
        sandbox->gizmo_start = gizmo_pivot(sandbox, scene, sandbox->gizmo_nodes, sandbox->gizmo_node_count);
        sandbox->gizmo_matrix = sandbox->gizmo_start;
    }
    if (!sandbox->gizmo_node_count)
        return;

    NvMat4 view_matrix, projection;
    nv_renderer_camera_matrices(scene, *scene_output, &view_matrix, &projection);
    // ImGui works in CSS pixels; the image is in framebuffer pixels. The rectangle is the whole
    // scene at its screen size, which may reach past the image where the last scene pixels are cut.
    f32 ratio = igGetIO_Nil()->DisplayFramebufferScale.x;
    f32 image_width = (f32)scene_output->width * scene_output->pixel_width / ratio;
    f32 image_height = (f32)scene_output->height * scene_output->pixel_height / ratio;
    ImGuizmo_SetRect((f32)scene_output->image.x / ratio, (f32)scene_output->image.y / ratio, image_width, image_height);
    ImGuizmo_SetOrthographic(nv_scene_get(scene, scene->active_camera)->camera.projection == NV_PROJECTION_ORTHOGRAPHIC);
    // NOTE: ImGuizmo sizes the gizmo as a fraction of the viewport width, which leaves it tiny on a
    // phone. Its handles are hit within fixed pixel distances, so it is sized in pixels instead:
    // about 64 CSS pixels long, larger for fingers.
    f32 length = 64.0f * sandbox->imgui.ui_scale;
    ImGuizmo_SetGizmoSizeClipSpace(length / (image_width * 0.5f));
    sandbox->gizmo_shown = 1;

    // (W, E and R pick the operation: desktop shortcuts, sandbox/shortcuts.c.) Ctrl held turns the Snap
    // box around for as long as it is held, so a drag can snap once or move freely once.
    ImGuiIO* io = igGetIO_Nil();
    b32 snap_now = sandbox->gizmo_snap != (io->KeyCtrl != 0);

    // Snap steps: half a meter, 15 degrees, a tenth of the scale.
    f32 snap[3] = {0.5f, 0.5f, 0.5f};
    if (sandbox->gizmo_operation == GIZMO_ROTATE)
        snap[0] = 15.0f;
    else if (sandbox->gizmo_operation == GIZMO_SCALE)
        snap[0] = 0.1f;

    MODE mode = (sandbox->gizmo_local || sandbox->gizmo_operation == GIZMO_SCALE) ? LOCAL : WORLD;
    if (!ImGuizmo_Manipulate(view_matrix.e, projection.e, gizmo_imguizmo_operation(sandbox), mode, sandbox->gizmo_matrix.e,
                             NULL, snap_now ? snap : NULL, NULL, NULL))
        return;

    // One node takes the gizmo's matrix as it is. Several take its change since the drag started,
    // applied to where each was then, so nothing drifts over a long drag.
    if (sandbox->gizmo_node_count == 1) {
        NvNodeId id = sandbox->gizmo_nodes[0];
        if (nv_scene_alive(scene, id))
            set_node_world(scene, nv_scene_get(scene, id), sandbox->gizmo_matrix);
        return;
    }
    NvMat4 change = nv_mat4_mul(sandbox->gizmo_matrix, nv_mat4_inverse(sandbox->gizmo_start));
    for (u32 i = 0; i < sandbox->gizmo_node_count; ++i) {
        NvNodeId id = sandbox->gizmo_nodes[i];
        if (nv_scene_alive(scene, id))
            set_node_world(scene, nv_scene_get(scene, id), nv_mat4_mul(change, sandbox->gizmo_start_world[i]));
    }
}

internal f64 now_ms(void)
{
    return nv_time_seconds() * 1000.0;
}

f64 sandbox_load(const FrameTimes* t)
{
    f64 cpu = t->anim + t->scene + t->draw + t->ui;
    f64 gpu = t->gpu + t->gpu_shadow + t->gpu_upscale + t->gpu_bloom + t->gpu_particles;
    f64 busy = cpu > gpu ? cpu : gpu;
    return t->frame > 0.0 ? busy / t->frame * 100.0 : 0.0;
}

// Averages the frame times over windows of about a second, for the stats and the benchmark.
internal void accumulate_times(Sandbox* sandbox, f64 now)
{
    FrameTimes* t = &sandbox->times;
    FrameTimes* sum = &sandbox->window_sum;
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
    if (t->frame > sandbox->window_worst_frame)
        sandbox->window_worst_frame = t->frame;
    ++sandbox->window_frames;
    if (now - sandbox->window_start < 1.0)
        return;
    f64 n = (f64)sandbox->window_frames;
    sandbox->shown_average = (FrameTimes){sum->frame / n, sum->anim / n, sum->scene / n, sum->draw / n, sum->ui / n, sum->gpu / n,
                                      sum->gpu_shadow / n, sum->gpu_upscale / n,
                                      sum->gpu_bloom / n, sum->gpu_particles / n};
    sandbox->shown_worst_frame = sandbox->window_worst_frame;
    *sum = (FrameTimes){0};
    sandbox->window_worst_frame = 0.0;
    sandbox->window_frames = 0;
    sandbox->window_start = now;
}

void sandbox_start_playing(Sandbox* sandbox)
{
    if (sandbox->playing)
        return;
    NvChunkWritten snapshot = save_write(sandbox, sandbox->play_snapshot, SAVE_MAX_SIZE);
    NV_ASSERT(snapshot.ok);
    sandbox->play_snapshot_size = snapshot.size;
    sandbox->play_time = 0.0f;
    behaviors_play_start(sandbox, sandbox_doc(sandbox));
    effects_clear(sandbox);
    sandbox->playing = 1;
    behaviors_restart_clips(sandbox, sandbox_doc(sandbox));
}

void sandbox_stop_playing(Sandbox* sandbox)
{
    if (!sandbox->playing)
        return;
    sandbox->playing = 0;
    benchmark_end(sandbox); // a run ends with the Play it ran in
    // The scene as it was at Play: nodes, behaviors, scene settings. The view, the selection and the editor settings stay as they
    // are now.
    SaveLoad load = save_load_parts(sandbox, sandbox->play_snapshot, sandbox->play_snapshot_size, SAVE_PART_SCENE | SAVE_PART_SHOWN);
    NV_ASSERT(load.ok); // written by save_write at Play
    (void)load;
    effects_clear(sandbox);
    behaviors_stop(sandbox, sandbox_doc(sandbox));
}

internal void frame(void* userdata)
{
    Sandbox* sandbox = userdata;
    nv_log_pump();
    f64 now = nv_time_seconds();
    f32 dt = (f32)(now - sandbox->last_time);
    sandbox->last_time = now;
    sandbox->time = now;

    NvGpuFrame gpu_frame = nv_gpu_begin_frame(&sandbox->gpu);
    if (!gpu_frame.ok)
        return;
    WGPUTextureView target = gpu_frame.view;

    FrameTimes* times = &sandbox->times;
    times->frame = (f64)dt * 1000.0;
    f64 t = now_ms();
    sandbox_layout(sandbox);
    const Layout* layout = &sandbox->layout;
    sandbox->imgui.view_rect = layout->viewport;
    NvRect* logged = &sandbox->logged_viewport;
    // Not on every frame of a splitter drag: it would fill the log ring with viewport sizes.
    b32 moved = logged->width != layout->viewport.width || logged->height != layout->viewport.height ||
                logged->x != layout->viewport.x || logged->y != layout->viewport.y;
    if (moved && !sandbox->docks.dragging) {
        *logged = layout->viewport;
        nv_log(NV_LOG_INFO, "sandbox", "%s UI: scene viewport %ux%u at (%u, %u) of the %ux%u color target",
               sandbox->ui_mode == UI_PHONE ? "phone" : "desktop", layout->viewport.width, layout->viewport.height,
               layout->viewport.x, layout->viewport.y, sandbox->gpu.width, sandbox->gpu.height);
    }
    nv_imgui_new_frame(&sandbox->imgui, dt);
    // IMPORTANT: Before the panel, so ImGuizmo's full-screen window (created on the first frame)
    // stays behind the panel.
    ImGuizmo_BeginFrame();
    selection_prune(sandbox_view(sandbox));
    sandbox_build_ui(sandbox);
    times->ui = now_ms() - t;

    SceneDoc* doc = sandbox_doc(sandbox);
    NvScene* scene = doc->scene;
    behaviors_update(sandbox, doc, dt);
    selection_prune(sandbox_view(sandbox)); // a generator may have removed selected nodes

    effects_update(sandbox, dt);
    t = now_ms();
    nv_anim_update_scene(scene, dt);
    times->anim = now_ms() - t;
    t = now_ms();
    nv_scene_update(scene);
    times->scene = now_ms() - t;
    pick(sandbox, &layout->scene);
    update_camera(sandbox, &layout->scene);
    draw_gizmo(sandbox, &layout->scene);
    undo_update(sandbox);
    draw_selection(sandbox);
    if (doc->show_bones)
        behaviors_draw_bones(sandbox, doc);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(sandbox->gpu.device, NULL);
    t = now_ms();
    // The depth target is samplable while the Textures tab shows it (docs/specs/textures.md).
    sandbox->renderer.depth_sampled = sandbox->textures.shown_now;
    nv_renderer_draw(&sandbox->renderer, scene, nv_anim_skins(), layout->scene, encoder, target);
    times->draw = now_ms() - t;
    times->gpu = sandbox->renderer.gpu_ms;
    times->gpu_shadow = sandbox->renderer.shadows.size ? sandbox->renderer.gpu_shadow_ms : 0.0;
    times->gpu_upscale = sandbox->renderer.gpu_upscale_ms;
    times->gpu_bloom = sandbox->renderer.post.bloom ? sandbox->renderer.gpu_bloom_ms : 0.0;
    times->gpu_particles = sandbox->renderer.gpu_particles_ms;
    t = now_ms();
    nv_imgui_render(&sandbox->imgui, encoder, target);
    times->ui += now_ms() - t;
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(sandbox->gpu.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    nv_renderer_end_frame(&sandbox->renderer);
    nv_gpu_end_frame(&sandbox->gpu);

    accumulate_times(sandbox, now);
    stress_after_frame(sandbox);
    save_update(sandbox);
}

#if !defined(NDEBUG)
// For tests: Module._sandbox_debug_layout(region, component) is a region's rectangle in CSS pixels
// (region 0 viewport, 1 top bar, 2 left dock, 3 right dock, 4 bottom dock, 5 phone panel, 6 the
// Play button; component 0 x, 1 y, 2 width, 3 height). Module._sandbox_debug_ui_mode() is 0 for the
// desktop UI and 1 for the phone UI, and Module._sandbox_debug_dock(n) the wanted sizes and state
// (0 left width, 1 right width, 2 bottom height, 3 bottom open).
EMSCRIPTEN_KEEPALIVE float sandbox_debug_layout(int region, int component)
{
    Sandbox* sandbox = &sandbox_state;
    f32 ratio = nv_window_pixel_ratio(&sandbox->window);
    const Layout* layout = &sandbox->layout;
    const NvRect* rects[] = {&layout->viewport, &layout->top_bar, &layout->left, &layout->right, &layout->bottom, &layout->panel};
    if (region == 6) {
        const f32* box = sandbox->play_box;
        f32 values[4] = {box[0], box[1], box[2] - box[0], box[3] - box[1]};
        return values[component];
    }
    const NvRect* r = rects[region];
    f32 values[4] = {(f32)r->x / ratio, (f32)r->y / ratio, (f32)r->width / ratio, (f32)r->height / ratio};
    return values[component];
}

// The scene's resolution: 0 width, 1 height, 2 to 5 the image's x, y, width, height (framebuffer
// pixels), 6 screen pixels per scene pixel across, 7 and 8 the allocated targets' width and height,
// 9 screen pixels per scene pixel down.
EMSCRIPTEN_KEEPALIVE float sandbox_debug_scene(int which)
{
    const NvSceneOutput* out = &sandbox_state.layout.scene;
    f32 values[10] = {(f32)out->width, (f32)out->height, (f32)out->image.x, (f32)out->image.y, (f32)out->image.width,
                      (f32)out->image.height, out->pixel_width, (f32)sandbox_state.renderer.target_width,
                      (f32)sandbox_state.renderer.target_height, out->pixel_height};
    return values[which];
}

// Sets the resolution without the UI: mode 0 Scale (a is the divisor) or 1 Fixed (a, b the size).
EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_fit(int fit)
{
    sandbox_state.resolution.fixed_fit = (NvFixedFit)fit;
}

EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_resolution(int mode, int a, int b)
{
    NvResolution* res = &sandbox_state.resolution;
    res->mode = mode ? NV_RESOLUTION_FIXED : NV_RESOLUTION_SCALE;
    if (mode) {
        res->fixed_width = (u32)a;
        res->fixed_height = (u32)b;
    } else {
        res->divisor = (u32)a;
    }
}

// Where a world point is on screen, in CSS pixels (axis 0 x, 1 y), seen by the shown scene's camera
// as drawn now: for tests that press on the gizmo or a node.
EMSCRIPTEN_KEEPALIVE float sandbox_debug_project(float x, float y, float z, int axis)
{
    Sandbox* sandbox = &sandbox_state;
    SceneView* view = sandbox_view(sandbox);
    NvMat4 view_matrix, projection;
    nv_renderer_camera_matrices(view->scene, sandbox->layout.scene, &view_matrix, &projection);
    NvMat4 view_projection = nv_mat4_mul(projection, view_matrix);
    f32 w = view_projection.e[3] * x + view_projection.e[7] * y + view_projection.e[11] * z + view_projection.e[15];
    NvVec3 p = nv_mat4_transform_point(view_projection, nv_vec3(x, y, z));
    f32 ndc_x = p.x / w, ndc_y = p.y / w;
    const NvSceneOutput* out = &sandbox->layout.scene;
    f32 ratio = nv_window_pixel_ratio(&sandbox->window);
    f32 screen_x = (f32)out->image.x + (ndc_x * 0.5f + 0.5f) * (f32)out->width * out->pixel_width;
    f32 screen_y = (f32)out->image.y + (0.5f - ndc_y * 0.5f) * (f32)out->height * out->pixel_height;
    return (axis ? screen_y : screen_x) / ratio;
}

// The renderer's sample count (1 or 4); the setter is for tests that switch it without the UI.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_msaa(void)
{
    return (int)sandbox_state.renderer.msaa;
}

EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_msaa(int samples)
{
    sandbox_state.renderer.msaa = (u32)samples;
}

// The post-processing settings: 0 tone mapper, 1 exposure x1000, 2 bloom on, 3 bloom intensity x1000; the setter is
// for tests that change them without the UI.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_post(int which)
{
    const NvPostSettings* post = &sandbox_state.renderer.post;
    int values[4] = {(int)post->tone, (int)(post->exposure * 1000.0f + 0.5f), post->bloom, (int)(post->bloom_intensity * 1000.0f + 0.5f)};
    return values[which];
}

EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_post(int tone, float exposure, int bloom, float intensity)
{
    sandbox_state.renderer.post = (NvPostSettings){.tone = (NvToneMap)tone, .exposure = exposure, .bloom = bloom != 0, .bloom_intensity = intensity};
}

// Sets the intensity of the shown scene's first directional light, to put values above 1 on screen.
EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_sun(float intensity)
{
    NvScene* scene = sandbox_view(&sandbox_state)->scene;
    for (u32 index = 1; index <= scene->node_count; ++index) {
        NvNode* node = &scene->nodes[index];
        if ((node->gen & 1) && node->light.type == NV_LIGHT_DIRECTIONAL) {
            node->light.intensity = intensity;
            return;
        }
    }
}

// The particle system: 0 alive, 1 visible, 2 dropped spawns, 3 effects registered; the second fires a test
// effect (0 explosion, 1 sparks, 2 smoke, 3 missile) beside the orbit point.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_vfx(int which)
{
    NvVfxStats stats = nv_vfx_stats(&sandbox_state.vfx);
    int values[6] = {(int)stats.alive, (int)stats.visible, (int)stats.dropped, (int)sandbox_state.vfx.effect_count - 1,
                     (int)stats.segments, (int)stats.decals};
    return values[which];
}

// The first behavior of a kind in the shown scene, or NULL.
internal Behavior* first_generator(BehaviorKind kind)
{
    SceneDoc* doc = sandbox_doc(&sandbox_state);
    for (u32 i = 0; i < doc->behavior_count; ++i)
        if (doc->behaviors[i].desc.kind == kind)
            return &doc->behaviors[i];
    return NULL;
}

// Sets the shown scene's first `effects` behavior: particles kept, explosions a second, missiles, beams, decals a second.
EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_effects(int particles, int explosions, int missiles, int beams, int decals)
{
    Behavior* behavior = first_generator(BEHAVIOR_EFFECTS);
    if (!behavior)
        return;
    EffectsDesc* effects = &behavior->desc.effects;
    effects->on = particles || explosions || missiles || beams || decals;
    effects->particles = (u32)particles;
    effects->explosions = (u32)explosions;
    effects->missiles = (u32)missiles;
    effects->beams = (u32)beams;
    effects->decals = (u32)decals;
}

EMSCRIPTEN_KEEPALIVE void sandbox_debug_vfx_fire(int effect)
{
    effects_fire(&sandbox_state, (u32)effect);
}

// The gizmo: 0 operation (0 move, 1 rotate, 2 scale), 1 local axes, 2 snap.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_gizmo(int which)
{
    int values[3] = {(int)sandbox_state.gizmo_operation, sandbox_state.gizmo_local, sandbox_state.gizmo_snap};
    return values[which];
}

// The shown view: 0 selected node index, 1 follows selection, 2 keyboard shortcuts window open,
// 3 to 5 the Scene, Inspector and Console docks shown.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_state(int which)
{
    const Sandbox* sandbox = &sandbox_state;
    int values[6] = {(int)sandbox_view(&sandbox_state)->selected.index, sandbox_view(&sandbox_state)->follow_selection, sandbox->show_shortcuts,
                     sandbox->docks.show_left, sandbox->docks.show_right, sandbox->docks.show_bottom};
    return values[which];
}

// The selected node's position (0 x, 1 y, 2 z).
EMSCRIPTEN_KEEPALIVE float sandbox_debug_selected_position(int axis)
{
    SceneView* view = sandbox_view(&sandbox_state);
    if (!view->selected.index)
        return 0.0f;
    NvVec3 p = nv_scene_get(view->scene, view->selected)->position;
    f32 values[3] = {p.x, p.y, p.z};
    return values[axis];
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_playing(void)
{
    return sandbox_state.playing;
}

// The shown view: 0 yaw, 1 pitch, 2 distance, 3 to 5 orbit point x, y, z.
EMSCRIPTEN_KEEPALIVE float sandbox_debug_view(int which)
{
    const SceneView* view = sandbox_view(&sandbox_state);
    f32 values[6] = {view->orbit.yaw, view->orbit.pitch, view->orbit.distance, view->orbit.target.x, view->orbit.target.y, view->orbit.target.z};
    return values[which];
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_ui_mode(void)
{
    return (int)sandbox_state.ui_mode;
}

EMSCRIPTEN_KEEPALIVE float sandbox_debug_dock(int which)
{
    const Docks* docks = &sandbox_state.docks;
    f32 values[4] = {docks->left_width, docks->right_width, docks->bottom_height, (f32)docks->bottom_open};
    return values[which];
}

// For tests: Module._sandbox_debug_save_round_trip() checks the save round trip at any moment.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_save_round_trip(void)
{
    return save_round_trip_matches(&sandbox_state);
}

// For tests: Module._sandbox_debug_undo_steps() is the steps held, and _sandbox_debug_undo_done() how many
// of them are applied.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_undo_steps(void)
{
    return (int)sandbox_state.undo.count;
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_undo_done(void)
{
    return (int)sandbox_state.undo.done;
}

// The bytes the undo's Node scope holds for the selected nodes, for checking UNDO_NODE_BYTES.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_undo_node_bytes(void)
{
    return (int)sandbox_state.undo.committed_size[SAVE_SCOPE_NODE];
}

// For tests, the Console tab (docs/specs/console.md). Module._sandbox_debug_log(level, n) adds an "sandbox"
// message "test message n", and _sandbox_debug_log_lines(level, n) one of three lines; strings from the page
// go through Module.nvLog. _sandbox_debug_wgpu_error() makes WebGPU report a validation error.
EMSCRIPTEN_KEEPALIVE void sandbox_debug_log(int level, int number)
{
    nv_log((NvLogLevel)level, "sandbox", "test message %d", number);
}

EMSCRIPTEN_KEEPALIVE void sandbox_debug_log_lines(int level, int number)
{
    nv_log((NvLogLevel)level, "sandbox", "first line %d\nsecond line\nthird line", number);
}

EMSCRIPTEN_KEEPALIVE void sandbox_debug_wgpu_error(void)
{
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = 16; // no usage: not a valid buffer
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(sandbox_state.gpu.device, &descriptor);
    if (buffer)
        wgpuBufferRelease(buffer);
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_log_count(void)
{
    return (int)nv_log_ring.count;
}

// The i-th message held (0 = the oldest): its repeat count, and its level.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_log_repeat(int i)
{
    return (int)nv_log_message((u32)i)->repeat;
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_log_level(int i)
{
    return (int)nv_log_message((u32)i)->level;
}

// The build label's tap box (component 0..3 is x0, y0, x1, y1), all zero when it has no badge.
EMSCRIPTEN_KEEPALIVE float sandbox_debug_badge_box(int component)
{
    return sandbox_state.badge_box[component];
}

// The average CPU time of building and recording the UI, in milliseconds.
EMSCRIPTEN_KEEPALIVE float sandbox_debug_ui_ms(void)
{
    return (float)sandbox_state.shown_average.ui;
}

// The shown scene's camera yaw, in radians.
EMSCRIPTEN_KEEPALIVE float sandbox_debug_camera_yaw(void)
{
    return sandbox_view(&sandbox_state)->orbit.yaw;
}

// Warnings and errors not yet seen (0 while the Console tab is shown).
EMSCRIPTEN_KEEPALIVE int sandbox_debug_console_unseen(void)
{
    return (int)console_unseen(&sandbox_state, NULL);
}

// Rows the Console tab's filters let through.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_console_rows(void)
{
    return (int)sandbox_state.console.rows;
}

// Where an item of the Console tab was drawn last frame, in CSS pixels: id is a ConsoleRect,
// component 0..3 is x0, y0, x1, y1.
EMSCRIPTEN_KEEPALIVE float sandbox_debug_console_rect(int id, int component)
{
    return sandbox_state.console.rects[id][component];
}

// The message list's scroll (0) and the end of its range (1).
EMSCRIPTEN_KEEPALIVE float sandbox_debug_console_scroll(int which)
{
    return which ? sandbox_state.console.scroll_max : sandbox_state.console.scroll_y;
}

// The number of the selected message + 1; 0 = none.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_console_selected(void)
{
    return (int)sandbox_state.console.selected;
}

// For tests, the Textures tab (docs/specs/textures.md): where an item was drawn last frame (id is a
// TexturesRect, component 0..3 is x0, y0, x1, y1, CSS pixels), the rows listed, the pick
// (kind * 1000 + index, -1 = none), the preview slots in use, and whether the depth target is samplable.
EMSCRIPTEN_KEEPALIVE float sandbox_debug_textures_rect(int id, int component)
{
    return sandbox_state.textures.rects[id][component];
}

// Where the k-th listed row was drawn last frame (component 0..3); zero when it was not.
EMSCRIPTEN_KEEPALIVE float sandbox_debug_textures_row(int k, int component)
{
    return sandbox_state.textures.row_rects[k][component];
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_textures_listed(void)
{
    return (int)sandbox_state.textures.listed;
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_textures_selected(void)
{
    TextureViewer* viewer = &sandbox_state.textures;
    return viewer->has_selection ? (int)viewer->selected_kind * 1000 + (int)viewer->selected_index : -1;
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_textures_mip(void)
{
    return (int)sandbox_state.textures.mip;
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_preview_slots(void)
{
    return (int)nv_imgui_preview_count(&sandbox_state.imgui);
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_depth_sampled(void)
{
    return sandbox_state.renderer.depth_texture_sampled;
}

// Changes the shadow settings (size 0 = off; format 0 depth32float, 1 depth16unorm), as the View tab does.
EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_shadows(int size, int format)
{
    sandbox_state.renderer.shadows.size = (u32)size;
    sandbox_state.renderer.shadows.format = (NvShadowFormat)format;
}

// Shows scene `index` of the list (0 is the first), as the View tab's picker does.
EMSCRIPTEN_KEEPALIVE void sandbox_debug_show_scene(int index)
{
    sandbox_show_scene(&sandbox_state, (u32)index);
}

// Renderer textures: how many, and the i-th's width, height and mip count (component 0, 1, 2).
EMSCRIPTEN_KEEPALIVE int sandbox_debug_texture_count(void)
{
    return (int)sandbox_state.renderer.texture_count;
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_texture_info(int i, int component)
{
    WGPUTexture texture = sandbox_state.renderer.textures[i].texture;
    return component == 0 ? (int)wgpuTextureGetWidth(texture)
         : component == 1 ? (int)wgpuTextureGetHeight(texture)
                          : (int)wgpuTextureGetMipLevelCount(texture);
}

// The k-th byte of the i-th renderer texture's name; 0 past its end.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_texture_name(int i, int k)
{
    const char* name = sandbox_state.renderer.textures[i].name;
    return k < (int)strlen(name) ? (u8)name[k] : 0;
}

// For tests, search (docs/specs/search.md).
// A buffer the test writes a query into (stringToUTF8), then _sandbox_debug_search_set(panel) copies it
// to a panel's box (0 to 4) or, for 5, to the palette.
EMSCRIPTEN_KEEPALIVE char* sandbox_debug_search_buffer(void)
{
    return sandbox_state.search.debug_buffer;
}

EMSCRIPTEN_KEEPALIVE void sandbox_debug_search_set(int panel)
{
    Search* s = &sandbox_state.search;
    if (panel == SEARCH_PANEL_COUNT) {
        snprintf(s->palette_query, SEARCH_QUERY_MAX, "%s", s->debug_buffer);
        s->highlight = 0;
    } else {
        search_set_query(&sandbox_state, (SearchPanel)panel, s->debug_buffer);
    }
}

// Rows a panel drew last frame.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_search_rows(int panel)
{
    return (int)sandbox_state.search.rows[panel];
}

// The palette: 0 open, 1 result count, 2 highlighted index, 3 kind of the highlighted row (-1 none),
// 4 settings collected, 5 recent actions held.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_palette(int which)
{
    const Search* s = &sandbox_state.search;
    switch (which) {
    case 0: return s->palette_open;
    case 1: return (int)s->result_count;
    case 2: return s->highlight;
    case 3: return s->result_count ? (int)s->results[s->highlight].kind : -1;
    case 4: return (int)s->setting_count;
    default: return (int)s->recent_count;
    }
}

// The k-th byte of a panel's search query; 0 past its end.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_search_query(int panel, int k)
{
    const char* q = sandbox_state.search.queries[panel];
    return k < (int)strlen(q) ? (u8)q[k] : 0;
}

// The k-th byte of the palette's query; 0 past its end.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_palette_query(int k)
{
    const char* q = sandbox_state.search.palette_query;
    return k < (int)strlen(q) ? (u8)q[k] : 0;
}

// Result i's kind (0 action, 1 setting, 2 node, 3 more) and what it names: component 0 kind, 1 the
// k-th byte of its name.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_palette_result(int i, int k)
{
    const Search* s = &sandbox_state.search;
    if (i < 0 || i >= (int)s->result_count)
        return -1;
    const PaletteResult* r = &s->results[i];
    if (k < 0)
        return r->kind;
    const char* name = r->kind == PALETTE_ACTION ? command_name(&sandbox_state, r->index)
                       : r->kind == PALETTE_SETTING ? s->settings[r->index].label
                       : r->kind == PALETTE_NODE ? sandbox_view(&sandbox_state)->scene->nodes[r->index].name
                                                 : "";
    return k < (int)strlen(name) ? (u8)name[k] : 0;
}

// The UI language: 0 English, 1 Korean; _sandbox_debug_set_language changes it.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_language(void)
{
    return (int)nv_strings_language();
}

EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_language(int language)
{
    nv_strings_set_language((NvLanguage)language);
}

// The k-th byte of the selected node's name; 0 past its end.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_selected_name(int k)
{
    SceneView* view = sandbox_view(&sandbox_state);
    if (!view->selected.index)
        return 0;
    const char* name = nv_scene_get(view->scene, view->selected)->name;
    return k < (int)strlen(name) ? (u8)name[k] : 0;
}

// Renames the selected node to the text in the search buffer (as typed names arrive: cut at a character).
EMSCRIPTEN_KEEPALIVE void sandbox_debug_rename_selected(void)
{
    SceneView* view = sandbox_view(&sandbox_state);
    if (!view->selected.index)
        return;
    NvNode* node = nv_scene_get(view->scene, view->selected);
    const char* text = sandbox_state.search.debug_buffer;
    u32 length = nv_utf8_fit(text, NV_NODE_NAME_MAX - 1);
    memcpy(node->name, text, length);
    node->name[length] = 0;
}

// Turns the shown scene's first `chain` on with `links` links (0 = off).
EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_chain(int links)
{
    Behavior* behavior = first_generator(BEHAVIOR_CHAIN);
    if (!behavior)
        return;
    behavior->desc.chain.on = links > 0;
    if (links > 0)
        behavior->desc.chain.count = (u32)links;
}

// Sets the shown scene's first `grid` to `cubes` cubes (0 = off).
EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_grid(int cubes)
{
    Behavior* behavior = first_generator(BEHAVIOR_GRID);
    if (!behavior)
        return;
    behavior->desc.grid.on = cubes > 0;
    if (cubes > 0)
        behavior->desc.grid.count = (u32)cubes;
}

// Sets the shown scene's first `crowd` to `characters` characters (0 = off).
EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_crowd(int characters)
{
    Behavior* behavior = first_generator(BEHAVIOR_CROWD);
    if (!behavior)
        return;
    behavior->desc.crowd.on = characters > 0;
    if (characters > 0)
        behavior->desc.crowd.count = (u32)characters;
}

// What the shown scene's generators have made: 0 cubes, 1 links, 2 characters in use, 3 characters made.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_built(int which)
{
    Behavior* grid = first_generator(BEHAVIOR_GRID);
    Behavior* chain = first_generator(BEHAVIOR_CHAIN);
    Behavior* crowd = first_generator(BEHAVIOR_CROWD);
    int values[4] = {grid ? (int)grid->grid.built : 0, chain ? (int)chain->chain.built : 0, crowd ? (int)crowd->crowd.active : 0,
                     crowd ? (int)crowd->crowd.created : 0};
    return values[which];
}

// The benchmark: 0 running, 1 the step, 2 results held, 3 the run started Play. _sandbox_debug_benchmark_start runs it, as the
// button does.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_benchmark(int which)
{
    const Benchmark* benchmark = &sandbox_state.benchmark;
    int values[4] = {benchmark->running, (int)benchmark->step, (int)benchmark->result_count, benchmark->started_play};
    return values[which];
}

EMSCRIPTEN_KEEPALIVE void sandbox_debug_benchmark_start(void)
{
    benchmark_start(&sandbox_state);
}

// The scenes: 0 the shown scene's index, 1 how many there are, 2 whether scene `a` was built.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_scenes(int which, int a)
{
    switch (which) {
    case 0: return (int)sandbox_state.shown;
    case 1: return (int)sandbox_state.scene_count;
    default: return a >= 0 && a < (int)sandbox_state.scene_count && sandbox_state.docs[a].built;
    }
}

// The selected node's index in the shown scene; 0 = none.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_selected_node(void)
{
    return (int)sandbox_view(&sandbox_state)->selected.index;
}

// The selection (docs/specs/selection.md): how many nodes, and the i-th one's index (0 = the primary).
EMSCRIPTEN_KEEPALIVE int sandbox_debug_selection_count(void)
{
    return (int)selection_count(sandbox_view(&sandbox_state));
}

EMSCRIPTEN_KEEPALIVE int sandbox_debug_selection(int i)
{
    SceneView* view = sandbox_view(&sandbox_state);
    return i >= 0 && (u32)i < selection_count(view) ? (int)selection_get(view, (u32)i).index : 0;
}

// Selects node `index` of the shown scene: mode 0 only it (0 clears), 1 adds or removes it.
EMSCRIPTEN_KEEPALIVE void sandbox_debug_select(int index, int mode)
{
    SceneView* view = sandbox_view(&sandbox_state);
    NvNodeId id = index > 0 ? (NvNodeId){(u32)index, view->scene->nodes[index].gen} : (NvNodeId){0};
    if (mode)
        selection_toggle(&sandbox_state, view, id);
    else
        selection_set(view, id);
}

EMSCRIPTEN_KEEPALIVE void sandbox_debug_set_multi(int on)
{
    sandbox_state.multi_select = on != 0;
}

// The index of the shown scene's first live node named as the search buffer's text; 0 = none.
EMSCRIPTEN_KEEPALIVE int sandbox_debug_find_node(void)
{
    NvScene* scene = sandbox_view(&sandbox_state)->scene;
    for (u32 i = 1; i <= scene->node_count; ++i) {
        if ((scene->nodes[i].gen & 1) && strcmp(scene->nodes[i].name, sandbox_state.search.debug_buffer) == 0)
            return (int)i;
    }
    return 0;
}

// The Scene tab's k-th row last frame: component 0 its node index, 1 to 4 its rect (x0, y0, x1, y1).
EMSCRIPTEN_KEEPALIVE float sandbox_debug_tree_row(int k, int component)
{
    if (k < 0 || (u32)k >= sandbox_state.tree_row_count[0] || k >= (int)NV_ARRAY_COUNT(sandbox_state.tree_row_rects))
        return 0.0f;
    return component ? sandbox_state.tree_row_rects[k][component - 1] : (f32)sandbox_state.tree_rows[0][k];
}

// Node `index` of the shown scene: which 0 to 2 its world position, 3 to 5 its local position.
EMSCRIPTEN_KEEPALIVE float sandbox_debug_node(int index, int which)
{
    NvNode* node = &sandbox_view(&sandbox_state)->scene->nodes[index];
    NvVec3 world = nv_mat4_translation(node->world);
    f32 values[6] = {world.x, world.y, world.z, node->position.x, node->position.y, node->position.z};
    return values[which];
}

// For tests: Module._sandbox_debug_save_crc() is a CRC-32 of the save the state would write now, so a
// test can compare the state at two moments (before Play and after Stop).
EMSCRIPTEN_KEEPALIVE unsigned sandbox_debug_save_crc(void)
{
    Sandbox* sandbox = &sandbox_state;
    umm mark = sandbox->scratch.used;
    u8* bytes = NV_PUSH_ARRAY(&sandbox->scratch, SAVE_MAX_SIZE, u8);
    u32 crc = nv_crc32(bytes, save_write(sandbox, bytes, SAVE_MAX_SIZE).size);
    sandbox->scratch.used = mark;
    return crc;
}
#endif

int main(void)
{
    Sandbox* sandbox = &sandbox_state;
    nv_arena_init(&sandbox->permanent, permanent_memory, sizeof(permanent_memory));
    nv_arena_init(&sandbox->scratch, scratch_memory, sizeof(scratch_memory));
    nv_arena_init(&sandbox->anim_memory, anim_memory, sizeof(anim_memory));
    sandbox_strings_init();

    nv_window_create(&sandbox->window, "nv");
    if (!nv_gpu_create(&sandbox->gpu, &sandbox->window)) {
        nv_log(NV_LOG_ERROR, "sandbox", "fatal: failed to initialize WebGPU");
        return 1;
    }
    nv_renderer_init(&sandbox->renderer, &sandbox->gpu, &sandbox->permanent);
    nv_imgui_init(&sandbox->imgui, &sandbox->gpu, &sandbox->window, &sandbox->permanent);
    nv_imgui_load_ui_font(&sandbox->imgui, &sandbox->permanent);
    nv_window_download_text(sandbox->download_text, sizeof(sandbox->download_text));
    nv_strings_set_language(nv_strings_browser_language()); // a save may change it (the LANG tag)
    // Shadows (docs/specs/shadows.md): lighter on touch screens, where the GPU is the limit.
    b32 touch = sandbox->imgui.ui_scale > 1.0f;
    // One UI per device (docs/specs/layout.md): touch gets the phone UI, everything else the desktop's.
    sandbox->ui_mode = touch ? UI_PHONE : UI_DESKTOP;
    sandbox->docks = (Docks){
        .left_width = 260.0f,
        .right_width = 340.0f,
        .bottom_height = 220.0f,
        .bottom_open = 1,
        .show_left = 1,
        .show_right = 1,
        .show_bottom = 1,
    };
    sandbox->renderer.shadows = (NvShadowSettings){
        .size = touch ? 1024 : 2048,
        .format = NV_SHADOW_FORMAT_DEPTH32F,
        .filter = touch ? NV_SHADOW_FILTER_LOW : NV_SHADOW_FILTER_HIGH,
        .distance = 30.0f,
    };
    // Anti-aliasing (docs/specs/msaa.md): 4x MSAA everywhere; the View tab turns it off.
    sandbox->renderer.msaa = 4;
    // Tone mapping and bloom (docs/specs/vfx.md); the View tab changes them.
    sandbox->renderer.post = (NvPostSettings){.tone = NV_TONE_PBR_NEUTRAL, .exposure = 1.0f, .bloom = 1, .bloom_intensity = 0.04f};
    // The phone shows the scene at half the pixels by default: its GPU is the limit.
    sandbox->resolution = (NvResolution){.mode = NV_RESOLUTION_SCALE, .divisor = touch ? 2 : 1, .fixed_width = 1280, .fixed_height = 720};
    sandbox->imgui.view_grab = gizmo_grab;
    sandbox->imgui.view_grab_data = sandbox;
    // Keys the desktop UI binds are not the browser's (Ctrl+S would open "Save page").
    sandbox->imgui.claims_key = shortcuts_claim;
    sandbox->imgui.claims_key_data = sandbox;
    // Thicker gizmo lines on touch screens, like the rest of the UI.
    Style* gizmo_style = ImGuizmo_GetStyle();
    f32 ui_scale = sandbox->imgui.ui_scale;
    gizmo_style->TranslationLineThickness *= ui_scale;
    gizmo_style->TranslationLineArrowSize *= ui_scale;
    gizmo_style->RotationLineThickness *= ui_scale;
    gizmo_style->RotationOuterLineThickness *= ui_scale;
    gizmo_style->ScaleLineThickness *= ui_scale;
    gizmo_style->ScaleLineCircleSize *= ui_scale;
    gizmo_style->HatchedAxisLineThickness *= ui_scale;
    gizmo_style->CenterCircleSize *= ui_scale;
    nv_anim_init(&sandbox->anim_memory);

    // Every scene is built from its file. One that cannot be built cannot be shown, but the first is where the sandbox starts.
    if (!scenes_init(sandbox)) {
        nv_log(NV_LOG_ERROR, "sandbox", "fatal: failed to build the first scene: %s", sandbox->docs[0].error);
        return 1;
    }

    effects_init(sandbox);
    sandbox->autosave = true;
    sandbox->console.auto_scroll = true;
    sandbox->play_snapshot = NV_PUSH_ARRAY(&sandbox->permanent, SAVE_MAX_SIZE, u8);
    sandbox->window_start = nv_time_seconds();
    NV_ASSERT(save_round_trip_matches(sandbox));
    save_init(sandbox);
    undo_init(sandbox);
    sandbox->last_time = nv_time_seconds();
    nv_window_run(&sandbox->window, frame, sandbox);
    return 0;
}
