#include "app.h"

#include <stdio.h>

//
// Scene tab
//

// Deeper than this, the tree stops and says how much is hidden (the stress chain is 1000 deep).
#define TREE_MAX_DEPTH 24

internal void node_tree(App* app, SceneView* view, u32 index, u32 depth)
{
    NvScene* scene = view->scene;
    NvNode* node = &scene->nodes[index];
    NvNodeId id = {index, node->gen};
    if (depth >= TREE_MAX_DEPTH) {
        u32 hidden = 0;
        for (u32 i = index; i; i = scene->nodes[i].first_child)
            ++hidden;
        igTextDisabled("... %u more levels", hidden);
        return;
    }

    // Big groups start closed: thousands of rows would cost more than what they display.
    u32 children = 0;
    for (u32 child = node->first_child; child; child = scene->nodes[child].next_sibling)
        ++children;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children <= 16 && depth < 2)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (!children)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (view->selected.index == id.index && view->selected.gen == id.gen)
        flags |= ImGuiTreeNodeFlags_Selected;

    b32 open = children > 16 ? igTreeNodeEx_Ptr((void*)(umm)index, flags, "%s (%u)", node->name, children)
                             : igTreeNodeEx_Ptr((void*)(umm)index, flags, "%s", node->name);
    if (igIsItemClicked(ImGuiMouseButton_Left) && !igIsItemToggledOpen()) {
        view->selected = id;
        app->open_inspector = 1;
    }
    if (open && children) {
        for (u32 child = node->first_child; child; child = scene->nodes[child].next_sibling)
            node_tree(app, view, child, depth + 1);
        igTreePop();
    }
}

internal void scene_tab(App* app)
{
    SceneView* view = app_view(app);
    for (u32 root = view->scene->first_root; root; root = view->scene->nodes[root].next_sibling)
        node_tree(app, view, root, 0);
}

//
// Inspector tab
//

internal void animator_section(App* app, NvAnimator* animator)
{
    igSeparatorText("Animator");

    // Clips of this skeleton, without the root-motion copies (app_play picks those).
    NvClipId current = animator->layers[0].clip.index ? app_regular_clip(app, animator->layers[0].clip) : (NvClipId){0};
    u32 clip_count = nv_anim_clip_count();
    for (u32 c = 1; c <= clip_count; ++c) {
        NvClipId clip = {c};
        if (nv_anim_clip_skeleton(clip).index != animator->skeleton.index || nv_anim_clip_has_root_motion(clip))
            continue;
        if (igSelectable_Bool(nv_anim_clip_name(clip), clip.index == current.index, 0, (ImVec2_c){0, 0}))
            app_play(app, clip);
    }
    if (igButton("Jump", (ImVec2_c){-1.0f, 0.0f}))
        app_jump(app);

    igSliderFloat("Speed", &animator->layers[0].speed, 0.0f, 2.0f, "%.2fx", 0);
    igSliderFloat("Fade", &app->fade_seconds, 0.0f, 1.0f, "%.2f s", 0);
    const char* names[APP_MAX_CLIPS];
    for (u32 i = 0; i < app->clip_count; ++i)
        names[i] = nv_anim_clip_name(app->clips[i]);
    igCombo_Str_arr("Blend", &app->blend_clip, names, (int)app->clip_count, -1);
    igSliderFloat("Weight", &app->blend_weight, 0.0f, 1.0f, "%.2f", 0);

    f32 total = 0.0f;
    for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
        if (animator->layers[l].clip.index)
            total += animator->layers[l].weight;
    }
    for (u32 l = 0; l < NV_MAX_ANIM_LAYERS; ++l) {
        NvAnimLayer* layer = &animator->layers[l];
        if (!layer->clip.index)
            continue;
        char overlay[64];
        snprintf(overlay, sizeof(overlay), "%s %.2f", nv_anim_clip_name(layer->clip), layer->weight);
        igProgressBar(total > 0.0f ? layer->weight / total : 0.0f, (ImVec2_c){-1.0f, 0.0f}, overlay);
    }

    if (igCheckbox("Root motion", &app->root_motion) && app->jump == JUMP_NONE)
        app_play(app, app_regular_clip(app, animator->layers[0].clip));
    igBeginDisabled(!app->root_motion);
    igSliderFloat("Turn", &app->turn_rate, -1.5f, 1.5f, "%.2f rad/s", 0);
    if (igButton("Back to center", (ImVec2_c){-1.0f, 0.0f}))
        app_back_to_center(app);
    igEndDisabled();
    igCheckbox("Look at target", &app->look_at);
}

internal void attach_section(App* app, NvNode* node)
{
    igSeparatorText("Attach");
    NvAnimator* animator = nv_anim_get(node->attach.animator);
    const NvJointDesc* joints = nv_anim_joints(animator->skeleton);
    if (igBeginCombo("Joint", joints[node->attach.joint].name, 0)) {
        for (u32 j = 0; j < animator->joint_count; ++j) {
            bool current = j == node->attach.joint;
            if (igSelectable_Bool(joints[j].name, current, 0, (ImVec2_c){0, 0}))
                node->attach.joint = j;
            if (current)
                igSetItemDefaultFocus();
        }
        igEndCombo();
    }
    if (app->shown == SCENE_SHOWCASE && node == nv_scene_get(app->scene, app->sword))
        igCheckbox("Visible", &app->show_sword);
}

internal void inspector_tab(App* app)
{
    SceneView* view = app_view(app);
    if (!view->selected.index) {
        igTextDisabled("Select a node in the Scene tab.");
        return;
    }
    NvNode* node = nv_scene_get(view->scene, view->selected);
    igInputText("Name", node->name, sizeof(node->name), 0, NULL, NULL);
    if (view->selected.index != view->scene->active_camera.index) {
        // The gizmo in the viewport; W, E and R switch the operation there too.
        s32* operation = (s32*)&app->gizmo_operation;
        igRadioButton_IntPtr("Move", operation, GIZMO_MOVE);
        igSameLine(0.0f, -1.0f);
        igRadioButton_IntPtr("Rotate", operation, GIZMO_ROTATE);
        igSameLine(0.0f, -1.0f);
        igRadioButton_IntPtr("Scale", operation, GIZMO_SCALE);
        igSameLine(0.0f, -1.0f);
        igBeginDisabled(app->gizmo_operation == GIZMO_SCALE);
        igCheckbox("Local", &app->gizmo_local);
        igEndDisabled();
        igSameLine(0.0f, -1.0f);
        igCheckbox("Snap", &app->gizmo_snap);
    }
    igDragFloat3("Position", &node->position.x, 0.02f, 0.0f, 0.0f, "%.2f", 0);
    // Rotation as pitch (X), yaw (Y) and roll (Z) in degrees; the quaternion is only rewritten when
    // edited, so looking at a node never changes it.
    NvVec3 euler = nv_quat_to_euler(node->rotation);
    f32 degrees[3] = {euler.x * 180.0f / NV_PI, euler.y * 180.0f / NV_PI, euler.z * 180.0f / NV_PI};
    // Tiny negatives (float noise, -0) would show as "-0.0".
    for (u32 i = 0; i < 3; ++i) {
        if (fabsf(degrees[i]) < 0.05f)
            degrees[i] = 0.0f;
    }
    if (igDragFloat3("Rotation", degrees, 0.5f, 0.0f, 0.0f, "%.1f", 0))
        node->rotation = nv_quat_from_euler(nv_vec3(degrees[0] * NV_PI / 180.0f, degrees[1] * NV_PI / 180.0f, degrees[2] * NV_PI / 180.0f));
    igDragFloat3("Scale", &node->scale.x, 0.01f, 0.01f, 100.0f, "%.2f", 0);

    if (node->mesh.index) {
        igSeparatorText("Mesh");
        f32 color[4];
        for (u32 i = 0; i < 4; ++i)
            color[i] = app->renderer.materials[node->material.index].desc.base_color[i];
        if (igColorEdit4("Color", color, ImGuiColorEditFlags_Float))
            nv_renderer_set_material_color(&app->renderer, node->material, color);
        if (app->renderer.materials[node->material.index].desc.base_color_texture.index)
            igTextDisabled("Multiplied with a texture.");
    }
    if (node->camera.projection) {
        igSeparatorText("Camera");
        igSliderAngle("Field of view", &node->camera.fov_y, 20.0f, 100.0f, "%.0f deg", 0);
    }
    if (node->light.type) {
        igSeparatorText("Light");
        igColorEdit3("Color##light", &node->light.color.x, ImGuiColorEditFlags_Float);
        igSliderFloat("Intensity", &node->light.intensity, 0.0f, 3.0f, "%.2f", 0);
    }
    if (node->attach.animator.index)
        attach_section(app, node);
    // The full animator controls drive the showcase character; others show what they play.
    NvAnimatorId animator = app_node_animator(view->scene, view->selected);
    if (animator.index == app->animator.index) {
        animator_section(app, nv_anim_get(animator));
    } else if (animator.index) {
        igSeparatorText("Animator");
        NvAnimLayer* layer = &nv_anim_get(animator)->layers[0];
        if (layer->clip.index)
            igText("%s  %.2f / %.2f s", nv_anim_clip_name(layer->clip), layer->time, nv_anim_clip_duration(layer->clip));
    }
}

//
// View tab
//

internal void view_tab(App* app)
{
    ImGuiIO* io = igGetIO_Nil();
    SceneView* view = app_view(app);
    local_persist const char* scenes[SCENE_COUNT] = {"Showcase", "Stress"};
    int shown = (int)app->shown;
    if (igCombo_Str_arr("Scene", &shown, scenes, SCENE_COUNT, -1))
        app_show_scene(app, (SceneKind)shown);
    igText("%.0f FPS (%.2f ms)", io->Framerate, 1000.0f / io->Framerate);
    igTextDisabled("%s build, commit %s", NV_BUILD_NAME, NV_GIT_COMMIT);
    igSliderAngle("Camera yaw", &view->camera_yaw, -180.0f, 180.0f, "%.0f deg", 0);
    igSliderAngle("Camera pitch", &view->camera_pitch, -10.0f, 80.0f, "%.0f deg", 0);
    igSliderFloat("Distance", &view->camera_distance, 1.0f, 100.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
    igCheckbox("Camera follows selection", &view->follow_selection);
    if (app->shown == SCENE_SHOWCASE) {
        igCheckbox("Show bones", &app->show_bones);
        igSliderFloat("Planet orbit", &app->orbit_speed, -3.0f, 3.0f, "%.2f rad/s", 0);
    }
}

// The build type in the viewport's top-left corner, so a Debug page is never mistaken for Release.
internal void build_label(void)
{
    char text[64];
    snprintf(text, sizeof(text), "%s build %s", NV_BUILD_NAME, NV_GIT_COMMIT);
    ImDrawList* draw = igGetForegroundDrawList_ViewportPtr(NULL);
    ImVec2_c size = igCalcTextSize(text, NULL, false, -1.0f);
    ImVec2_c pos = {6.0f, 6.0f};
    ImDrawList_AddRectFilled(draw, (ImVec2_c){pos.x - 4.0f, pos.y - 2.0f}, (ImVec2_c){pos.x + size.x + 4.0f, pos.y + size.y + 2.0f},
                             0x99000000u, 3.0f, 0);
    ImDrawList_AddText_Vec2(draw, pos, 0xFFFFFFFFu, text, NULL);
}

void app_build_ui(App* app, NvRect panel)
{
    build_label();
    if (nv_imgui_begin_panel(&app->imgui, "Editor", panel) && igBeginTabBar("tabs", 0)) {
        if (igBeginTabItem("Scene", NULL, 0)) {
            scene_tab(app);
            igEndTabItem();
        }
        // NOTE: Picking a node in the Scene tab jumps here, since that is where it is edited.
        ImGuiTabItemFlags inspector_flags = app->open_inspector ? ImGuiTabItemFlags_SetSelected : 0;
        app->open_inspector = 0;
        if (igBeginTabItem("Inspector", NULL, inspector_flags)) {
            inspector_tab(app);
            igEndTabItem();
        }
        if (igBeginTabItem("View", NULL, 0)) {
            view_tab(app);
            igEndTabItem();
        }
        if (app->shown == SCENE_STRESS) {
            ImGuiTabItemFlags stress_flags = app->open_stress ? ImGuiTabItemFlags_SetSelected : 0;
            app->open_stress = 0;
            if (igBeginTabItem("Stress", NULL, stress_flags)) {
                stress_ui(app);
                igEndTabItem();
            }
        }
        igEndTabBar();
    }
    igEnd();
}
