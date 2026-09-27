#include "app.h"

#include <stdio.h>

//
// Scene tab
//

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
    if (igIsItemClicked(ImGuiMouseButton_Left) && !igIsItemToggledOpen()) {
        app->selected = id;
        app->open_inspector = 1;
    }
    if (open && node->first_child) {
        for (u32 child = node->first_child; child; child = scene->nodes[child].next_sibling)
            node_tree(app, child);
        igTreePop();
    }
}

internal void scene_tab(App* app)
{
    for (u32 root = app->scene->first_root; root; root = app->scene->nodes[root].next_sibling)
        node_tree(app, root);
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
    if (node == nv_scene_get(app->scene, app->sword))
        igCheckbox("Visible", &app->show_sword);
}

internal void inspector_tab(App* app)
{
    if (!app->selected.index) {
        igTextDisabled("Select a node in the Scene tab.");
        return;
    }
    NvNode* node = nv_scene_get(app->scene, app->selected);
    igInputText("Name", node->name, sizeof(node->name), 0, NULL, NULL);
    igDragFloat3("Position", &node->position.x, 0.02f, 0.0f, 0.0f, "%.2f", 0);
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
    NvAnimatorId animator = app_node_animator(app, app->selected);
    if (animator.index)
        animator_section(app, nv_anim_get(animator));
}

//
// View tab
//

internal void view_tab(App* app)
{
    ImGuiIO* io = igGetIO_Nil();
    igText("%.0f FPS (%.2f ms)", io->Framerate, 1000.0f / io->Framerate);
    igSliderAngle("Camera yaw", &app->camera_yaw, -180.0f, 180.0f, "%.0f deg", 0);
    igSliderAngle("Camera pitch", &app->camera_pitch, -10.0f, 80.0f, "%.0f deg", 0);
    igSliderFloat("Distance", &app->camera_distance, 1.0f, 20.0f, "%.1f m", 0);
    igCheckbox("Show bones", &app->show_bones);
    igSliderFloat("Planet orbit", &app->orbit_speed, -3.0f, 3.0f, "%.2f rad/s", 0);
}

void app_build_ui(App* app, NvRect panel)
{
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
        igEndTabBar();
    }
    igEnd();
}
