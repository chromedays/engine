// Drawing the battle (docs/specs/battle.md, "Executable"): the field, units and shells as nodes, effects for the
// battle's events, the camera, deployment input and the Battle panel. The rules are battle.c; nothing here changes them
// but through battle_place, battle_remove, battle_start and battle_retry.

#include "game.h"

#include "nv_version.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RGBA(r, g, b, a) {r, g, b, a}

// Effects

internal NvVfxEffectId make_explosion(NvVfx* vfx)
{
    NvVfxEffectDesc effect = {.name = "Explosion", .emitter_count = 4};
    effect.emitters[0] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 0.2f, .life_max = 0.2f, .size_start = 0.4f, .size_end = 1.8f,
        .colors = {RGBA(7.0f, 4.5f, 1.5f, 1.0f), RGBA(3.0f, 1.4f, 0.4f, 0.8f), RGBA(1.0f, 0.3f, 0.1f, 0.0f)},
        .shape = NV_VFX_SHAPE_DISC, .blend = NV_VFX_BLEND_ADD};
    effect.emitters[1] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 0.4f, .life_max = 0.4f, .size_start = 0.2f, .size_end = 2.6f,
        .colors = {RGBA(1.6f, 1.3f, 0.9f, 0.7f), RGBA(0.6f, 0.45f, 0.3f, 0.4f), RGBA(0.1f, 0.1f, 0.1f, 0.0f)},
        .shape = NV_VFX_SHAPE_RING, .blend = NV_VFX_BLEND_ADD};
    effect.emitters[2] = (NvVfxEmitterDesc){
        .count = 40, .life_min = 0.4f, .life_max = 1.0f, .speed_min = 3.0f, .speed_max = 10.0f, .cone = NV_PI,
        .gravity = 9.0f, .drag = 1.1f, .use_ground = 1, .ground = 0.02f, .restitution = 0.4f, .friction = 0.3f,
        .size_start = 0.06f, .size_end = 0.02f, .stretch = 0.3f,
        .colors = {RGBA(9.0f, 5.5f, 1.6f, 1.0f), RGBA(4.0f, 1.5f, 0.3f, 1.0f), RGBA(0.7f, 0.1f, 0.02f, 0.0f)},
        .shape = NV_VFX_SHAPE_STREAK, .blend = NV_VFX_BLEND_ADD};
    effect.emitters[3] = (NvVfxEmitterDesc){
        .count = 8, .delay = 0.05f, .life_min = 1.0f, .life_max = 2.0f, .speed_min = 0.5f, .speed_max = 2.0f, .cone = 1.2f,
        .gravity = -0.6f, .drag = 0.8f, .turbulence = 1.0f, .turbulence_scale = 0.7f,
        .size_start = 0.3f, .size_end = 1.0f, .spin = 1.0f,
        .colors = {RGBA(0.9f, 0.55f, 0.25f, 0.8f), RGBA(0.22f, 0.2f, 0.18f, 0.6f), RGBA(0.05f, 0.05f, 0.05f, 0.0f)},
        .shape = NV_VFX_SHAPE_PUFF, .blend = NV_VFX_BLEND_ALPHA};
    return nv_vfx_add_effect(vfx, &effect);
}

// Blue sparks where a shell hit a shield.
internal NvVfxEffectId make_shield_hit(NvVfx* vfx)
{
    NvVfxEffectDesc effect = {.name = "Shield hit", .emitter_count = 2};
    effect.emitters[0] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 0.15f, .life_max = 0.15f, .size_start = 0.2f, .size_end = 1.0f,
        .colors = {RGBA(1.0f, 3.0f, 8.0f, 0.9f), RGBA(0.3f, 1.0f, 3.0f, 0.5f), RGBA(0.1f, 0.3f, 1.0f, 0.0f)},
        .shape = NV_VFX_SHAPE_RING, .blend = NV_VFX_BLEND_ADD};
    effect.emitters[1] = (NvVfxEmitterDesc){
        .count = 24, .life_min = 0.2f, .life_max = 0.6f, .speed_min = 2.0f, .speed_max = 7.0f, .cone = 1.2f,
        .gravity = 6.0f, .drag = 0.8f, .size_start = 0.05f, .size_end = 0.02f, .stretch = 0.3f,
        .colors = {RGBA(2.0f, 5.0f, 10.0f, 1.0f), RGBA(0.6f, 1.6f, 5.0f, 1.0f), RGBA(0.1f, 0.3f, 1.0f, 0.0f)},
        .shape = NV_VFX_SHAPE_STREAK, .blend = NV_VFX_BLEND_ADD};
    return nv_vfx_add_effect(vfx, &effect);
}

// A small flash at the muzzle.
internal NvVfxEffectId make_muzzle(NvVfx* vfx)
{
    NvVfxEffectDesc effect = {.name = "Muzzle", .emitter_count = 2};
    effect.emitters[0] = (NvVfxEmitterDesc){
        .count = 1, .life_min = 0.1f, .life_max = 0.1f, .size_start = 0.3f, .size_end = 0.1f,
        .colors = {RGBA(8.0f, 6.0f, 2.0f, 1.0f), RGBA(3.0f, 1.5f, 0.4f, 0.8f), RGBA(1.0f, 0.3f, 0.1f, 0.0f)},
        .shape = NV_VFX_SHAPE_DISC, .blend = NV_VFX_BLEND_ADD};
    effect.emitters[1] = (NvVfxEmitterDesc){
        .count = 6, .life_min = 0.3f, .life_max = 0.8f, .speed_min = 1.0f, .speed_max = 4.0f, .cone = 0.5f,
        .gravity = -0.3f, .drag = 0.6f, .size_start = 0.15f, .size_end = 0.5f,
        .colors = {RGBA(0.7f, 0.65f, 0.6f, 0.4f), RGBA(0.35f, 0.34f, 0.33f, 0.3f), RGBA(0.1f, 0.1f, 0.1f, 0.0f)},
        .shape = NV_VFX_SHAPE_PUFF, .blend = NV_VFX_BLEND_ALPHA};
    return nv_vfx_add_effect(vfx, &effect);
}

internal void make_effects(Game* game)
{
    Effects* effects = &game->effects;
    nv_vfx_init(&game->vfx, &game->gpu, (NvVfxCapacity){.particles = 262144, .segments = 16384, .decals = 2048}, &game->permanent);
    effects->explosion = make_explosion(&game->vfx);
    effects->shield_hit = make_shield_hit(&game->vfx);
    effects->muzzle = make_muzzle(&game->vfx);
    effects->trail = (NvVfxLineStyle){
        .life = 0.35f, .width_start = 0.12f, .width_end = 0.0f,
        .colors = {RGBA(4.0f, 2.4f, 0.8f, 0.9f), RGBA(0.6f, 0.2f, 0.05f, 0.0f)}};
    effects->scorch = (NvVfxDecalStyle){
        .life = 20.0f, .fade = 0.4f, .color = RGBA(0.02f, 0.015f, 0.01f, 0.8f), .shape = NV_VFX_SHAPE_PUFF};
    game->renderer.vfx = &game->vfx;
}

// The battle's events become effects, and each shell leaves a trail for the stretch it moved. Called after every tick, so
// that no tick's events or stretch are missed when several run in a frame.
void view_on_tick(Game* game)
{
    Battle* battle = &game->battle;
    NvVfx* vfx = &game->vfx;
    const Effects* effects = &game->effects;
    for (u32 i = 0; i < battle->event_count; ++i) {
        const BattleEvent* event = &battle->events[i];
        switch (event->kind) {
        case BATTLE_EVENT_FIRE:
            nv_vfx_burst(vfx, effects->muzzle, event->position, event->direction, 1.0f);
            break;
        case BATTLE_EVENT_HIT:
            nv_vfx_burst(vfx, effects->explosion, event->position, nv_vec3(0.0f, 1.0f, 0.0f), 0.5f);
            nv_vfx_decal(vfx, &effects->scorch, nv_vec3(event->position.x, 0.02f, event->position.z), (f32)(battle->tick % 64) * 0.1f, 1.0f);
            break;
        case BATTLE_EVENT_SHIELD_HIT:
            nv_vfx_burst(vfx, effects->shield_hit, event->position, nv_vec3_scale(event->direction, -1.0f), 1.0f);
            break;
        case BATTLE_EVENT_DEATH:
            nv_vfx_burst(vfx, effects->explosion, nv_vec3_add(event->position, nv_vec3(0.0f, 0.3f, 0.0f)), nv_vec3(0.0f, 1.0f, 0.0f), 1.0f);
            break;
        }
    }
    battle->event_count = 0;
    battle->events_dropped = 0;
    for (u32 i = 0; i < battle->projectile_count; ++i)
        nv_vfx_trail(vfx, &effects->trail, battle->projectiles[i].previous_position, battle->projectiles[i].position);
}

// Meshes and the field

internal NvMaterialId add_color(Game* game, f32 r, f32 g, f32 b)
{
    NvMaterialDesc desc = {.base_color = {r, g, b, 1.0f}};
    return nv_renderer_add_material(&game->renderer, &desc);
}

// A unit's mesh: a capsule body, and a cone in front for its facing (+Z in the unit's space). The body is as wide as the unit
// but no wider than it is tall, so a squat unit is a rounded blob.
internal void set_unit_mesh(Game* game, u32 index, const UnitDef* def)
{
    NvVertex vertices[NV_MESH_CAPSULE_VERTICES(16, 6) + NV_MESH_CONE_VERTICES(12)];
    u32 indices[NV_MESH_CAPSULE_INDICES(16, 6) + NV_MESH_CONE_INDICES(12)];
    NvMeshBuilder mesh = {.vertices = vertices, .vertex_capacity = NV_ARRAY_COUNT(vertices), .indices = indices,
                          .index_capacity = NV_ARRAY_COUNT(indices)};
    f32 radius = fminf(def->radius, def->height * 0.5f);
    f32 half_height = fmaxf(0.0f, def->height * 0.5f - radius);
    nv_mesh_append_capsule(&mesh, nv_vec3(0.0f, def->height * 0.5f, 0.0f), radius, half_height, 16, 6);

    // The nose: a cone stands along +Y, so turn it to +Z (y becomes z, z becomes -y) and move it in front of the body.
    f32 length = radius * 0.8f;
    u32 first = mesh.vertex_count;
    nv_mesh_append_cone(&mesh, nv_vec3(0.0f, 0.0f, 0.0f), radius * 0.5f, length * 0.5f, 12);
    NvVec3 offset = nv_vec3(0.0f, def->height * 0.5f, radius * 0.85f + length * 0.5f);
    for (u32 i = first; i < mesh.vertex_count; ++i) {
        NvVertex* v = &mesh.vertices[i];
        f32 y = v->position[1], z = v->position[2];
        v->position[1] = -z + offset.y;
        v->position[2] = y + offset.z;
        v->position[0] += offset.x;
        f32 ny = v->normal[1], nz = v->normal[2];
        v->normal[1] = -nz;
        v->normal[2] = ny;
    }
    NvMeshData data = nv_mesh_builder_data(&mesh);
    // A project loaded again remakes the meshes in the slots it has (nv_renderer_replace_mesh), so loads use up none.
    if (index < game->unit_meshes_made) {
        nv_renderer_replace_mesh(&game->renderer, game->unit_meshes[index], &data);
    } else {
        game->unit_meshes[index] = nv_renderer_add_mesh(&game->renderer, &data);
        game->unit_meshes_made = index + 1;
    }
}

// The plane `field` (FIELD_GROUND, FIELD_PLAYER_ZONE, FIELD_ENEMY_ZONE) at its size and place, its mesh remade in its slot.
internal void set_plane(Game* game, u32 field, f32 half_x, f32 half_z, NvVec3 position)
{
    NvVertex vertices[NV_MESH_PLANE_VERTICES];
    u32 indices[NV_MESH_PLANE_INDICES];
    NvMeshBuilder mesh = {.vertices = vertices, .vertex_capacity = NV_ARRAY_COUNT(vertices), .indices = indices,
                          .index_capacity = NV_ARRAY_COUNT(indices)};
    nv_mesh_append_plane(&mesh, half_x, half_z);
    NvMeshData data = nv_mesh_builder_data(&mesh);
    NvNode* node = nv_scene_get(game->scene, game->field_nodes[field]);
    if (game->field_meshes[field].index)
        nv_renderer_replace_mesh(&game->renderer, game->field_meshes[field], &data);
    else
        game->field_meshes[field] = nv_renderer_add_mesh(&game->renderer, &data);
    node->mesh = game->field_meshes[field];
    node->position = position;
}

internal void add_plane_node(Game* game, u32 field, const char* name, NvMaterialId material)
{
    game->field_nodes[field] = nv_scene_add_node(game->scene, (NvNodeId){0}, name);
    nv_scene_get(game->scene, game->field_nodes[field])->material = material;
}

// The field's size and planes from the rules (the project file's `grid`, `cell_size` and `zone_rows`); the camera starts over
// (above and behind the player's side, -Z, looking at the middle) when `restart_camera` or the field's size changed.
internal void apply_field(Game* game, b32 restart_camera)
{
    const BattleRules* rules = &game->defs.rules;
    f32 field_width = (f32)rules->grid_width * rules->cell_size;
    f32 field_length = (f32)rules->grid_length * rules->cell_size;
    restart_camera = restart_camera || field_width != game->field_width || field_length != game->field_length;
    game->field_width = field_width;
    game->field_length = field_length;
    if (restart_camera) {
        // Far enough to see all of it: the distance is that of the camera's default for a field 96 m long, in proportion to
        // the field's length.
        f32 start_distance = field_length * (CAMERA_START_DISTANCE / CAMERA_REFERENCE_LENGTH);
        f32 max_distance = fmaxf(CAMERA_MAX_DISTANCE, start_distance * 1.35f);
        game->orbit = (NvOrbitCamera){
            .target = {field_width * 0.5f, 0.0f, field_length * 0.4f}, .yaw = NV_PI, .pitch = 62.0f * NV_PI / 180.0f,
            .distance = start_distance, .min_pitch = CAMERA_MIN_PITCH, .max_pitch = CAMERA_MAX_PITCH,
            .min_distance = CAMERA_MIN_DISTANCE, .max_distance = max_distance};
        nv_scene_get(game->scene, game->camera)->camera.far_z = fmaxf(CAMERA_FAR_Z, 2.0f * max_distance);
    }

    // The ground, and a tint on each side's deployment zone (a little above it, so the two do not fight over depth).
    f32 zone_half = (f32)rules->zone_rows * rules->cell_size * 0.5f;
    set_plane(game, FIELD_GROUND, field_width * 0.5f, field_length * 0.5f, nv_vec3(field_width * 0.5f, 0.0f, field_length * 0.5f));
    set_plane(game, FIELD_PLAYER_ZONE, field_width * 0.5f, zone_half, nv_vec3(field_width * 0.5f, 0.01f, zone_half));
    set_plane(game, FIELD_ENEMY_ZONE, field_width * 0.5f, zone_half, nv_vec3(field_width * 0.5f, 0.01f, field_length - zone_half));
    game->hover_cell_x = game->hover_cell_row = -1;
    nv_vfx_clear(&game->vfx);
    game->accumulator = 0.0f;
}

void view_apply_project(Game* game)
{
    apply_field(game, true);
    for (u32 i = 0; i < game->defs.unit_count; ++i)
        set_unit_mesh(game, i, &game->defs.units[i]);
    if (game->selected_def >= game->defs.unit_count)
        game->selected_def = 0;
}

void view_apply_edit(Game* game)
{
    apply_field(game, false);
    for (u32 i = 0; i < game->defs.unit_count; ++i)
        set_unit_mesh(game, i, &game->defs.units[i]);
}

void view_build(Game* game)
{
    NvScene* scene = game->scene;
    NvNodeId none = {0};

    game->camera = nv_scene_add_node(scene, none, "camera");
    nv_scene_get(scene, game->camera)->camera = (NvCamera){
        .projection = NV_PROJECTION_PERSPECTIVE, .fov_y = 45.0f * NV_PI / 180.0f, .near_z = 0.5f, .far_z = 400.0f};
    scene->active_camera = game->camera;

    NvNodeId sun = nv_scene_add_node(scene, none, "sun");
    NvNode* sun_node = nv_scene_get(scene, sun);
    sun_node->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), 0.5f), nv_quat_axis_angle(nv_vec3(1, 0, 0), -0.9f));
    sun_node->light = (NvLight){.type = NV_LIGHT_DIRECTIONAL, .color = nv_vec3(1.0f, 0.96f, 0.9f), .intensity = 1.3f};

    // The field's planes; their size comes with the project (view_apply_project).
    NvMaterialId ground = add_color(game, 0.07f, 0.08f, 0.09f);
    add_plane_node(game, FIELD_GROUND, "ground", ground);
    add_plane_node(game, FIELD_PLAYER_ZONE, "player zone", add_color(game, 0.04f, 0.09f, 0.2f));
    add_plane_node(game, FIELD_ENEMY_ZONE, "enemy zone", add_color(game, 0.2f, 0.06f, 0.05f));

    game->team_materials[TEAM_PLAYER] = add_color(game, 0.1f, 0.35f, 1.0f);
    game->team_materials[TEAM_ENEMY] = add_color(game, 1.0f, 0.18f, 0.12f);

    // Units mode's unit and ground (docs/specs/abproj.md, "Editing units"); their meshes are set while that mode is shown.
    game->preview_node = nv_scene_add_node(scene, none, "unit preview");
    nv_scene_get(scene, game->preview_node)->material = game->team_materials[TEAM_PLAYER];
    game->preview_ground = nv_scene_add_node(scene, none, "unit preview ground");
    nv_scene_get(scene, game->preview_ground)->material = ground;
    NvVertex ground_vertices[NV_MESH_PLANE_VERTICES];
    u32 ground_indices[NV_MESH_PLANE_INDICES];
    NvMeshBuilder ground_mesh = {.vertices = ground_vertices, .vertex_capacity = NV_ARRAY_COUNT(ground_vertices),
                                 .indices = ground_indices, .index_capacity = NV_ARRAY_COUNT(ground_indices)};
    nv_mesh_append_plane(&ground_mesh, UNIT_GROUND_HALF, UNIT_GROUND_HALF);
    NvMeshData ground_data = nv_mesh_builder_data(&ground_mesh);
    game->preview_ground_mesh = nv_renderer_add_mesh(&game->renderer, &ground_data);
    game->shell_material = add_color(game, 1.0f, 0.75f, 0.25f);
    NvVertex vertices[NV_MESH_SPHERE_VERTICES(8, 6)];
    u32 indices[NV_MESH_SPHERE_INDICES(8, 6)];
    NvMeshBuilder mesh = {.vertices = vertices, .vertex_capacity = NV_ARRAY_COUNT(vertices), .indices = indices,
                          .index_capacity = NV_ARRAY_COUNT(indices)};
    nv_mesh_append_sphere(&mesh, nv_vec3(0.0f, 0.0f, 0.0f), 0.15f, 8, 6);
    NvMeshData data = nv_mesh_builder_data(&mesh);
    game->shell_mesh = nv_renderer_add_mesh(&game->renderer, &data);

    make_effects(game);
    game->hover_cell_x = game->hover_cell_row = -1;
}

// Input

typedef struct PlayerCell {
    b32 ok;
    s32 x, row;
} PlayerCell;

// The ground cell a point of the viewport (CSS pixels) shows, if it is in the player's zone.
internal PlayerCell player_cell_at(Game* game, f32 x, f32 y)
{
    NvTapRay tap = nv_renderer_tap_ray(game->scene, game->layout.scene, x, y, nv_window_pixel_ratio(&game->window));
    NvRay ray = tap.ray;
    if (!tap.ok || ray.direction.y >= -1e-4f)
        return (PlayerCell){0};
    f32 distance = -ray.origin.y / ray.direction.y;
    f32 ground_x = ray.origin.x + ray.direction.x * distance, ground_z = ray.origin.z + ray.direction.z * distance;
    if (ground_x < 0.0f || ground_z < 0.0f)
        return (PlayerCell){0};
    const BattleRules* rules = &game->defs.rules;
    s32 cell_x = (s32)(ground_x / rules->cell_size), cell_row = (s32)(ground_z / rules->cell_size);
    if (cell_x >= (s32)rules->grid_width || cell_row >= (s32)rules->zone_rows)
        return (PlayerCell){0};
    return (PlayerCell){.ok = true, .x = cell_x, .row = cell_row};
}

// A left drag or one finger moves the view over the ground; a right or middle drag or two fingers turn it; the wheel and
// pinch zoom. A tap places the chosen unit in a cell of the player's zone, or removes the one standing there.
void view_input(Game* game)
{
    const NvViewInput* in = &game->imgui.view;
    b32 units = game->mode == MODE_UNITS;
    NvOrbitCamera* orbit = units ? &game->unit_orbit : &game->orbit;
    f32 ratio = nv_window_pixel_ratio(&game->window);
    const NvSceneOutput* scene_output = &game->layout.scene;

    nv_orbit_camera_turn(orbit, -in->pan_x * CAMERA_RADIANS_PER_PIXEL, in->pan_y * CAMERA_RADIANS_PER_PIXEL, in->dolly);
    if (in->orbit_x != 0.0f || in->orbit_y != 0.0f) {
        // The ground under the finger follows it. The image's height is in CSS pixels, like the drag.
        f32 image_height_css = (f32)scene_output->height * scene_output->pixel_height / ratio;
        NvVec3 move = nv_orbit_camera_pan_ground(orbit, nv_scene_get(game->scene, game->camera), image_height_css, in->orbit_x, in->orbit_y);
        orbit->target = nv_vec3_add(orbit->target, move);
    }
    if (units) { // the unit editor keeps its height (the unit's middle) and its ground
        orbit->target.x = nv_clamp_f32(orbit->target.x, -UNIT_GROUND_HALF, UNIT_GROUND_HALF);
        orbit->target.z = nv_clamp_f32(orbit->target.z, -UNIT_GROUND_HALF, UNIT_GROUND_HALF);
        return; // no cells to point at or tap
    }
    orbit->target.x = nv_clamp_f32(orbit->target.x, 0.0f, game->field_width);
    orbit->target.z = nv_clamp_f32(orbit->target.z, 0.0f, game->field_length);
    orbit->target.y = 0.0f;

    // The cell under the mouse pointer (a touch screen has none).
    game->hover_cell_x = game->hover_cell_row = -1;
    if (game->imgui.ui_scale <= 1.0f && game->battle.phase == BATTLE_DEPLOY) {
        ImVec2_c mouse = igGetIO_Nil()->MousePos;
        const NvRect* v = &game->layout.viewport;
        if (mouse.x * ratio >= (f32)v->x && mouse.x * ratio < (f32)(v->x + v->width) && mouse.y * ratio >= (f32)v->y &&
            mouse.y * ratio < (f32)(v->y + v->height)) {
            PlayerCell cell = player_cell_at(game, mouse.x, mouse.y);
            if (cell.ok) {
                game->hover_cell_x = cell.x;
                game->hover_cell_row = cell.row;
            }
        }
    }

    if (in->tapped && game->defs_ok && game->battle.phase == BATTLE_DEPLOY) {
        PlayerCell cell = player_cell_at(game, in->tap_x, in->tap_y);
        if (cell.ok && !battle_remove(&game->battle, cell.x, cell.row))
            battle_place(&game->battle, game->selected_def, cell.x, cell.row);
    }
}

// Updating the nodes and lines

internal NvVec3 cell_corner(const BattleRules* rules, u32 x, u32 row, f32 y)
{
    return nv_vec3((f32)x * rules->cell_size, y, (f32)row * rules->cell_size);
}

// The lines of a zone's cells: rows `first` to `first + zone_rows`.
internal void draw_zone_grid(NvRenderer* renderer, const BattleRules* rules, u32 first_row, NvVec3 color)
{
    for (u32 r = 0; r <= rules->zone_rows; ++r)
        nv_renderer_debug_line(renderer, cell_corner(rules, 0, first_row + r, 0.03f), cell_corner(rules, rules->grid_width, first_row + r, 0.03f), color);
    for (u32 x = 0; x <= rules->grid_width; ++x)
        nv_renderer_debug_line(renderer, cell_corner(rules, x, first_row, 0.03f), cell_corner(rules, x, first_row + rules->zone_rows, 0.03f), color);
}

internal void draw_ring(NvRenderer* renderer, NvVec3 center, NvVec3 u, NvVec3 v, f32 radius, NvVec3 color, u32 segments)
{
    NvVec3 previous = nv_vec3_add(center, nv_vec3_scale(u, radius));
    for (u32 i = 1; i <= segments; ++i) {
        f32 angle = (f32)i * (2.0f * NV_PI / (f32)segments);
        NvVec3 point = nv_vec3_add(center, nv_vec3_add(nv_vec3_scale(u, cosf(angle) * radius), nv_vec3_scale(v, sinf(angle) * radius)));
        nv_renderer_debug_line(renderer, previous, point, color);
        previous = point;
    }
}

// A unit's health: a dark bar with a green to red one over it, above its head and level with the screen.
internal void draw_health_bar(NvRenderer* renderer, NvVec3 above, NvVec3 screen_right, f32 fraction)
{
    const f32 half_width = 0.6f;
    NvVec3 color = nv_vec3(1.0f - fraction, 0.2f + 0.8f * fraction, 0.15f);
    for (u32 i = 0; i < 3; ++i) { // a few lines side by side make a bar
        NvVec3 row = nv_vec3_add(above, nv_vec3(0.0f, 0.04f * (f32)i, 0.0f));
        nv_renderer_debug_line(renderer, nv_vec3_sub(row, nv_vec3_scale(screen_right, half_width)),
                               nv_vec3_add(row, nv_vec3_scale(screen_right, half_width)), nv_vec3(0.05f, 0.05f, 0.05f));
        nv_renderer_debug_line(renderer, nv_vec3_sub(row, nv_vec3_scale(screen_right, half_width)),
                               nv_vec3_add(row, nv_vec3_scale(screen_right, half_width * (2.0f * fraction - 1.0f))), color);
    }
}

internal f32 lerp(f32 a, f32 b, f32 t)
{
    return a + (b - a) * t;
}

// The angle halfway from `a` to `b` going the short way round.
internal f32 lerp_angle(f32 a, f32 b, f32 t)
{
    f32 delta = fmodf(b - a + NV_PI, 2.0f * NV_PI);
    if (delta < 0.0f)
        delta += 2.0f * NV_PI;
    return a + (delta - NV_PI) * t;
}

internal NvNode* unit_node(Game* game, u32 slot)
{
    while (game->unit_nodes_made < slot)
        game->unit_nodes[++game->unit_nodes_made] = nv_scene_add_node(game->scene, (NvNodeId){0}, "unit");
    return nv_scene_get(game->scene, game->unit_nodes[slot]);
}

internal NvNode* shell_node(Game* game, u32 index)
{
    while (game->shell_nodes_made <= index) {
        NvNodeId id = nv_scene_add_node(game->scene, (NvNodeId){0}, "shell");
        NvNode* node = nv_scene_get(game->scene, id);
        node->material = game->shell_material;
        game->shell_nodes[game->shell_nodes_made++] = id;
    }
    return nv_scene_get(game->scene, game->shell_nodes[index]);
}

// Every frame: the nodes follow the battle (between two ticks while it runs), and the lines, effects' clock, the scene's
// matrices and the camera are brought up to date.
// Units mode's view (docs/specs/abproj.md, "Editing units"): the chosen unit alone at the origin, facing +Z, with its sizes,
// its range, its muzzle and a shell's arc to the end of its range, and its shield.
internal void draw_unit_preview(Game* game)
{
    NvRenderer* renderer = &game->renderer;
    const UnitDef* def = &game->defs.units[game->selected_def];
    const WeaponDef* weapon = &def->weapon;
    const BattleRules* rules = &game->defs.rules;
    NvNode* node = nv_scene_get(game->scene, game->preview_node);
    node->mesh = game->unit_meshes[game->selected_def];
    nv_scene_get(game->scene, game->preview_ground)->mesh = game->preview_ground_mesh;
    if (!game->unit_orbit_set) {
        game->unit_orbit = (NvOrbitCamera){
            // Behind the unit and to its side (yaw pi looks along +Z, as the unit faces), looking past it at the middle of its
            // shot, far enough for most of its range's circle.
            .target = {0.0f, def->height * 0.5f, weapon->range * 0.4f}, .yaw = NV_PI + 0.7f, .pitch = 35.0f * NV_PI / 180.0f,
            .distance = fmaxf(4.0f, weapon->range * 1.1f), .min_pitch = 5.0f * NV_PI / 180.0f,
            .max_pitch = CAMERA_MAX_PITCH, .min_distance = UNIT_CAMERA_MIN_DISTANCE, .max_distance = UNIT_CAMERA_MAX_DISTANCE};
        game->unit_orbit_set = true;
    }

    NvVec3 x = nv_vec3(1.0f, 0.0f, 0.0f), y = nv_vec3(0.0f, 1.0f, 0.0f), z = nv_vec3(0.0f, 0.0f, 1.0f);
    NvVec3 ground = nv_vec3(0.0f, 0.03f, 0.0f);
    draw_ring(renderer, ground, x, z, def->radius, nv_vec3(0.9f, 0.9f, 0.9f), 32);
    draw_ring(renderer, ground, x, z, weapon->range, nv_vec3(1.0f, 0.55f, 0.15f), 96);
    draw_ring(renderer, ground, x, z, weapon->range * rules->stop_fraction, nv_vec3(0.45f, 0.22f, 0.06f), 96);

    // The muzzle in the world: facing +Z, the unit's right is -X (battle.c, fire_shell).
    NvVec3 muzzle = nv_vec3(-weapon->muzzle.x, weapon->muzzle.y, weapon->muzzle.z);
    NvVec3 yellow = nv_vec3(1.0f, 0.85f, 0.3f);
    const f32 cross = 0.12f;
    nv_renderer_debug_line(renderer, nv_vec3_sub(muzzle, nv_vec3_scale(x, cross)), nv_vec3_add(muzzle, nv_vec3_scale(x, cross)), yellow);
    nv_renderer_debug_line(renderer, nv_vec3_sub(muzzle, nv_vec3_scale(y, cross)), nv_vec3_add(muzzle, nv_vec3_scale(y, cross)), yellow);
    nv_renderer_debug_line(renderer, nv_vec3_sub(muzzle, nv_vec3_scale(z, cross)), nv_vec3_add(muzzle, nv_vec3_scale(z, cross)), yellow);

    // A shell to the ground at the end of the range, as fire_shell aims it: it climbs distance * tan(angle) over the way, and
    // the time follows from that rise and gravity.
    NvVec3 aim = nv_vec3(0.0f, 0.0f, weapon->range);
    f32 dx = aim.x - muzzle.x, dz = aim.z - muzzle.z;
    f32 distance = sqrtf(dx * dx + dz * dz);
    f32 tan_angle = tanf(weapon->launch_angle);
    f32 rise = distance * tan_angle + muzzle.y;
    if (rise > 0.0f && rules->gravity > 0.0f) {
        f32 time = sqrtf(2.0f * rise / rules->gravity);
        NvVec3 velocity = nv_vec3(dx / time, tan_angle * distance / time, dz / time);
        const u32 segments = 40;
        NvVec3 previous = muzzle;
        for (u32 i = 1; i <= segments; ++i) {
            f32 t = time * (f32)i / (f32)segments;
            NvVec3 point = nv_vec3_add(muzzle, nv_vec3_scale(velocity, t));
            point.y -= 0.5f * rules->gravity * t * t;
            nv_renderer_debug_line(renderer, previous, point, yellow);
            previous = point;
        }
    }

    if (def->ability.kind == ABILITY_SHIELD) {
        NvVec3 center = nv_vec3(0.0f, def->height * 0.5f, 0.0f);
        NvVec3 blue = nv_vec3(0.3f, 0.8f, 1.0f);
        f32 radius = def->ability.shield.radius;
        draw_ring(renderer, center, x, z, radius, blue, 32);
        draw_ring(renderer, center, x, y, radius, blue, 32);
        draw_ring(renderer, center, y, z, radius, blue, 32);
    }
}

// The battle's nodes from slot or index `first` on (units and shells) show nothing.
internal void hide_battle_nodes(Game* game, u32 first_unit_slot, u32 first_shell)
{
    for (u32 slot = first_unit_slot; slot <= game->unit_nodes_made; ++slot)
        nv_scene_get(game->scene, game->unit_nodes[slot])->mesh = (NvMeshId){0};
    for (u32 i = first_shell; i < game->shell_nodes_made; ++i)
        nv_scene_get(game->scene, game->shell_nodes[i])->mesh = (NvMeshId){0};
}

void view_update(Game* game, f32 game_dt)
{
    const Battle* battle = &game->battle;
    NvRenderer* renderer = &game->renderer;
    NvNode* camera = nv_scene_get(game->scene, game->camera);
    b32 units = game->mode == MODE_UNITS && game->defs_ok && game->defs.unit_count > 0;
    for (u32 i = 0; i < FIELD_PLANE_COUNT; ++i)
        nv_scene_get(game->scene, game->field_nodes[i])->mesh = units ? (NvMeshId){0} : game->field_meshes[i];
    nv_scene_get(game->scene, game->preview_node)->mesh = (NvMeshId){0};
    nv_scene_get(game->scene, game->preview_ground)->mesh = (NvMeshId){0};
    if (units) {
        hide_battle_nodes(game, 1, 0);
        draw_unit_preview(game);
        nv_vfx_update(&game->vfx, game_dt);
        nv_scene_update(game->scene);
        nv_orbit_camera_place(&game->unit_orbit, camera);
        renderer->shadows.distance = game->unit_orbit.distance * SHADOW_DISTANCE_PER_CAMERA_DISTANCE;
        return;
    }
    // Between the last two ticks while it runs; paused (and stepping), exactly at the last tick.
    f32 alpha = battle->phase == BATTLE_FIGHT && !game->paused ? nv_clamp_f32(game->accumulator / BATTLE_TICK_SECONDS, 0.0f, 1.0f) : 1.0f;
    NvVec3 screen_right = nv_quat_rotate(camera->rotation, nv_vec3(1.0f, 0.0f, 0.0f));

    for (u32 slot = 1; slot <= battle->unit_count; ++slot) {
        const Unit* unit = &battle->units[slot];
        const UnitDef* def = &battle->defs->units[unit->def];
        NvNode* node = unit_node(game, slot);
        b32 alive = !(unit->flags & UNIT_DEAD);
        node->mesh = alive ? game->unit_meshes[unit->def] : (NvMeshId){0};
        node->material = game->team_materials[unit->team];
        NvVec3 position = nv_vec3(lerp(unit->previous_position.x, unit->position.x, alpha), unit->position.y,
                                  lerp(unit->previous_position.z, unit->position.z, alpha));
        node->position = position;
        node->rotation = nv_quat_axis_angle(nv_vec3(0.0f, 1.0f, 0.0f), lerp_angle(unit->previous_yaw, unit->yaw, alpha));
        if (!alive)
            continue;
        if (battle->phase != BATTLE_DEPLOY) {
            draw_health_bar(renderer, nv_vec3_add(position, nv_vec3(0.0f, def->height + 0.4f, 0.0f)), screen_right, unit->health / def->health);
            if (def->ability.kind == ABILITY_SHIELD && unit->ability.shield.energy > 0.0f) {
                f32 share = unit->ability.shield.energy / def->ability.shield.capacity;
                NvVec3 color = nv_vec3_scale(nv_vec3(0.3f, 0.8f, 1.0f), 0.3f + 0.7f * share);
                NvVec3 center = nv_vec3_add(position, nv_vec3(0.0f, def->height * 0.5f, 0.0f));
                f32 radius = def->ability.shield.radius;
                draw_ring(renderer, center, nv_vec3(1, 0, 0), nv_vec3(0, 0, 1), radius, color, 20);
                draw_ring(renderer, center, nv_vec3(1, 0, 0), nv_vec3(0, 1, 0), radius, color, 20);
                draw_ring(renderer, center, nv_vec3(0, 1, 0), nv_vec3(0, 0, 1), radius, color, 20);
            }
        }
    }

    for (u32 i = 0; i < battle->projectile_count; ++i) {
        const Projectile* shell = &battle->projectiles[i];
        NvNode* node = shell_node(game, i);
        node->mesh = game->shell_mesh;
        node->position = nv_vec3(lerp(shell->previous_position.x, shell->position.x, alpha),
                                 lerp(shell->previous_position.y, shell->position.y, alpha),
                                 lerp(shell->previous_position.z, shell->position.z, alpha));
    }
    hide_battle_nodes(game, battle->unit_count + 1, battle->projectile_count);

    if (battle->phase == BATTLE_DEPLOY && game->defs_ok) {
        const BattleRules* rules = &battle->defs->rules;
        draw_zone_grid(renderer, rules, 0, nv_vec3(0.25f, 0.42f, 0.75f));
        draw_zone_grid(renderer, rules, battle_enemy_first_row(rules), nv_vec3(0.6f, 0.25f, 0.2f));
        if (game->hover_cell_x >= 0) {
            b32 ok = battle_can_place(battle, game->selected_def, game->hover_cell_x, game->hover_cell_row) ||
                     battle->placed[game->hover_cell_row][game->hover_cell_x]; // a placed unit can be taken away
            NvVec3 color = ok ? nv_vec3(0.2f, 1.0f, 0.3f) : nv_vec3(1.0f, 0.2f, 0.2f);
            u32 x = (u32)game->hover_cell_x, row = (u32)game->hover_cell_row;
            for (u32 inset = 0; inset < 2; ++inset) {
                f32 pad = 0.05f + 0.08f * (f32)inset;
                NvVec3 a = nv_vec3_add(cell_corner(rules, x, row, 0.05f), nv_vec3(pad, 0.0f, pad));
                NvVec3 b = nv_vec3_add(cell_corner(rules, x + 1, row, 0.05f), nv_vec3(-pad, 0.0f, pad));
                NvVec3 c = nv_vec3_add(cell_corner(rules, x + 1, row + 1, 0.05f), nv_vec3(-pad, 0.0f, -pad));
                NvVec3 d = nv_vec3_add(cell_corner(rules, x, row + 1, 0.05f), nv_vec3(pad, 0.0f, -pad));
                nv_renderer_debug_line(renderer, a, b, color);
                nv_renderer_debug_line(renderer, b, c, color);
                nv_renderer_debug_line(renderer, c, d, color);
                nv_renderer_debug_line(renderer, d, a, color);
            }
        }
    }

    nv_vfx_update(&game->vfx, game_dt); // the game clock: a pause freezes the effects, a speed-up hurries them
    nv_scene_update(game->scene);
    nv_orbit_camera_place(&game->orbit, camera);
    // The shadow map covers the view up to this distance; panning and turning keep it, so shadows do not shimmer then.
    renderer->shadows.distance = game->orbit.distance * SHADOW_DISTANCE_PER_CAMERA_DISTANCE;
}

// The panel and the build label

// What the page's top-left corner of the viewport says: the build type, the commit, what the page downloaded and the commit's
// subject.
void view_draw_build_label(Game* game)
{
    char text[160];
    snprintf(text, sizeof(text), "%s build %s%s%s", NV_BUILD_NAME, NV_GIT_COMMIT, game->download_text[0] ? " \xC2\xB7 " : "",
             game->download_text);
    nv_imgui_draw_build_label(&game->imgui, game->layout.viewport, text, 0.0f, "Commit: " NV_GIT_SUBJECT);
}

// A button that acts once when clicked and once a frame while held past STEP_HOLD_SECONDS. Not ImGui's ButtonRepeat: that
// acts on the press and again after a delay counted in frame time, so a click spanning a slow frame or two steps twice.
#define STEP_HOLD_SECONDS 0.4f
internal b32 step_button(const char* label)
{
    b32 clicked = igButton(label, (ImVec2_c){0, 0});
    b32 held = igIsItemActive() && igGetIO_Nil()->MouseDownDuration[0] > STEP_HOLD_SECONDS;
    return clicked || held;
}

// A value's widget in the Rules section or Units mode (docs/specs/abproj.md, "Editing the rules", "Editing units"): it shows the
// value the project's text gives and writes a change back there (game_set_value). The widget's range only guides the drag; the
// project's reader judges.
internal void value_number(Game* game, DefsKey where, const char* label, f32 speed, f32 min, f32 max, const char* format)
{
    f64 values[DEFS_MAX_VALUES];
    if (defs_value_get(game->project_text, game->project_size, where, values) != 1)
        return;
    f32 value = (f32)values[0];
    if (igDragFloat(label, &value, speed, min, max, format, ImGuiSliderFlags_AlwaysClamp)) {
        f64 changed = value;
        game_set_value(game, where, &changed, 1);
    }
}

internal void value_vector(Game* game, DefsKey where, const char* label, f32 speed, f32 min, f32 max, const char* format)
{
    f64 values[DEFS_MAX_VALUES];
    if (defs_value_get(game->project_text, game->project_size, where, values) != 3)
        return;
    f32 value[3] = {(f32)values[0], (f32)values[1], (f32)values[2]};
    if (igDragFloat3(label, value, speed, min, max, format, ImGuiSliderFlags_AlwaysClamp)) {
        f64 changed[3] = {value[0], value[1], value[2]};
        game_set_value(game, where, changed, 3);
    }
}

internal void value_integers(Game* game, DefsKey where, const char* label, u32 count, f32 speed, s32 min, s32 max)
{
    f64 values[DEFS_MAX_VALUES];
    if (defs_value_get(game->project_text, game->project_size, where, values) != count)
        return;
    s32 value[2] = {(s32)values[0], count > 1 ? (s32)values[1] : 0};
    b32 changed = count > 1 ? igDragInt2(label, value, speed, min, max, "%d", ImGuiSliderFlags_AlwaysClamp)
                            : igDragInt(label, value, speed, min, max, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (changed) {
        f64 next[2] = {value[0], value[1]};
        game_set_value(game, where, next, count);
    }
}

#define RULE(key) ((DefsKey){DEFS_RULES, NULL, key})

// The rules of the project in place, editable before a round. A change is put in place at once; Save writes it to the file.
internal void rules_section(Game* game)
{
    if (!game->project_size || !igCollapsingHeader_TreeNodeFlags(TL("Rules"), 0))
        return;
    igBeginDisabled(!game_values_editable(game));
    igPushItemWidth(igGetFontSize() * 8.0f);
    value_number(game, RULE("cell_size"), TL("Cell size (m)"), 0.01f, 0.1f, 10.0f, "%.2f");
    value_integers(game, RULE("grid"), TL("Grid (across, long)"), 2, 0.1f, 1, BATTLE_MAX_GRID_LENGTH);
    value_integers(game, RULE("zone_rows"), TL("Zone rows"), 1, 0.1f, 1, BATTLE_MAX_ZONE_ROWS);
    value_number(game, RULE("round_time"), TL("Round time (s)"), 0.5f, 1.0f, 600.0f, "%.1f");
    value_number(game, RULE("gravity"), TL("Gravity (m/s²)"), 0.05f, 0.1f, 100.0f, "%.2f");
    value_number(game, RULE("retarget_interval"), TL("Retarget interval (s)"), 0.01f, 0.01f, 10.0f, "%.2f");
    value_number(game, RULE("stop_fraction"), TL("Stop fraction"), 0.005f, 0.01f, 1.0f, "%.3f");
    value_number(game, RULE("min_damage_fraction"), TL("Min. damage fraction"), 0.005f, 0.0f, 1.0f, "%.3f");
    igPopItemWidth();
    igEndDisabled();
    if (game->battle.phase != BATTLE_DEPLOY) {
        igPushStyleColor_Vec4(ImGuiCol_Text, igGetStyle()->Colors[ImGuiCol_TextDisabled]);
        igTextWrapped("%s", T("Rules change only in deployment (Retry goes back to it)."));
        igPopStyleColor(1);
    }
}

// Units mode's panel (docs/specs/abproj.md, "Editing units"): the chosen unit's values, its weapon's and its shield's.
internal void units_panel(Game* game)
{
    const BattleDefs* defs = &game->defs;
    igSeparator();
    igSetNextItemWidth(igGetFontSize() * 10.0f);
    if (igBeginCombo(TL("Unit"), defs->units[game->selected_def].name, 0)) {
        for (u32 i = 0; i < defs->unit_count; ++i) {
            igPushID_Int((int)i);
            if (igSelectable_Bool(defs->units[i].name, game->selected_def == i, 0, (ImVec2_c){0, 0}))
                game->selected_def = i;
            igPopID();
        }
        igEndCombo();
    }
    // The defs are replaced by every change, so the names the keys point at are copies.
    const UnitDef* def = &defs->units[game->selected_def];
    char unit[BATTLE_NAME_SIZE], weapon[BATTLE_NAME_SIZE];
    memcpy(unit, def->name, sizeof(unit));
    memcpy(weapon, def->weapon.name, sizeof(weapon));
    b32 shield = def->ability.kind == ABILITY_SHIELD;

    igBeginDisabled(!game_values_editable(game));
    igPushItemWidth(igGetFontSize() * 8.0f);
    igPushID_Str("unit");
    igSeparatorText(T("Unit"));
    value_integers(game, (DefsKey){DEFS_UNIT, unit, "cost"}, TL("Cost"), 1, 1.0f, 1, 10000);
    value_number(game, (DefsKey){DEFS_UNIT, unit, "health"}, TL("Health"), 0.5f, 0.1f, 100000.0f, "%.1f");
    value_number(game, (DefsKey){DEFS_UNIT, unit, "armor"}, TL("Armor"), 0.1f, 0.0f, 1000.0f, "%.1f");
    value_number(game, (DefsKey){DEFS_UNIT, unit, "speed"}, TL("Speed (m/s)"), 0.05f, 0.0f, 100.0f, "%.2f");
    value_number(game, (DefsKey){DEFS_UNIT, unit, "radius"}, TL("Radius (m)"), 0.01f, 0.05f, 20.0f, "%.2f");
    value_number(game, (DefsKey){DEFS_UNIT, unit, "height"}, TL("Height (m)"), 0.01f, 0.05f, 20.0f, "%.2f");
    igPopID();

    igPushID_Str("weapon");
    char heading[64];
    snprintf(heading, sizeof(heading), T("Weapon: %s"), weapon);
    igSeparatorText(heading);
    value_number(game, (DefsKey){DEFS_WEAPON, unit, "range"}, TL("Range (m)"), 0.1f, 0.1f, 500.0f, "%.1f");
    value_number(game, (DefsKey){DEFS_WEAPON, unit, "damage"}, TL("Damage"), 0.5f, 0.0f, 100000.0f, "%.1f");
    value_number(game, (DefsKey){DEFS_WEAPON, unit, "cooldown"}, TL("Cooldown (s)"), 0.01f, 0.01f, 60.0f, "%.2f");
    value_number(game, (DefsKey){DEFS_WEAPON, unit, "launch_angle"}, TL("Launch angle (°)"), 0.5f, 0.5f, 90.0f, "%.1f");
    value_number(game, (DefsKey){DEFS_WEAPON, unit, "spread"}, TL("Spread (m)"), 0.01f, 0.0f, 50.0f, "%.2f");
    value_vector(game, (DefsKey){DEFS_WEAPON, unit, "muzzle"}, TL("Muzzle (m)"), 0.01f, -10.0f, 10.0f, "%.2f");
    igPopID();

    igPushID_Str("ability");
    if (shield) {
        igSeparatorText(T("Ability: shield"));
        value_number(game, (DefsKey){DEFS_ABILITY, unit, "radius"}, TL("Radius (m)"), 0.01f, 0.05f, 50.0f, "%.2f");
        value_number(game, (DefsKey){DEFS_ABILITY, unit, "capacity"}, TL("Capacity"), 0.5f, 0.1f, 100000.0f, "%.1f");
        value_number(game, (DefsKey){DEFS_ABILITY, unit, "regen"}, TL("Regen (per s)"), 0.1f, 0.0f, 10000.0f, "%.1f");
        value_number(game, (DefsKey){DEFS_ABILITY, unit, "regen_delay"}, TL("Regen delay (s)"), 0.05f, 0.0f, 600.0f, "%.2f");
    } else {
        igSeparatorText(T("Ability"));
        igTextDisabled("%s", T("No ability"));
    }
    igPopID();
    igPopItemWidth();
    igEndDisabled();
    igSeparator();
    igTextWrapped("%s", T("Drag a value, or double-click it and type. The view shows the unit's range, where it stops, its muzzle, a shell's arc to the end of its range and its shield. Save writes the changes to the file."));
}

internal void start_over(Game* game)
{
    nv_vfx_clear(&game->vfx);
    game->accumulator = 0.0f;
}

void view_panel(Game* game)
{
    Battle* battle = &game->battle;
    if (nv_imgui_begin_panel(&game->imgui, "Battle", game->layout.panel)) {
        // The mode: the battle, or the unit editor (only in deployment, so no round runs behind it).
        const char* phases[3] = {T("Deployment"), T("Fight"), T("Result")};
        const char* modes[2] = {TL("Battle"), TL("Units")};
        for (u32 i = 0; i < NV_ARRAY_COUNT(modes); ++i) {
            if (i)
                igSameLine(0.0f, -1.0f);
            b32 lit = game->mode == (GameMode)i;
            igBeginDisabled(i == MODE_UNITS && !lit && (!game->defs_ok || battle->phase != BATTLE_DEPLOY));
            if (lit)
                igPushStyleColor_Vec4(ImGuiCol_Button, igGetStyle()->Colors[ImGuiCol_ButtonActive]);
            if (igButton(modes[i], (ImVec2_c){0, 0}))
                game_set_mode(game, (GameMode)i);
            if (lit)
                igPopStyleColor(1);
            igEndDisabled();
        }
        igSameLine(0.0f, -1.0f);
        igTextDisabled("%s", phases[battle->phase]);

        const char* languages[NV_LANGUAGE_COUNT] = {"English", "한국어"};
        s32 language = (s32)nv_strings_language();
        igSetNextItemWidth(igGetFontSize() * 8.0f);
        if (igCombo_Str_arr(TL("Language"), &language, languages, NV_LANGUAGE_COUNT, -1))
            nv_strings_set_language((NvLanguage)language);

        // The project: where the rules, units and stage came from, and opening and saving it as a file on this computer
        // (docs/specs/abproj.md, "Loading and saving"). Save writes the text it was read from, comments and all.
        igSeparator();
        if (game->project_edited)
            igTextWrapped(T("Project: %s (changed)"), game->project_name);
        else
            igTextWrapped(T("Project: %s"), game->project_name);
        const char* labels[4] = {TL("Open..."), TL("Reload"), TL("Save"), TL("Save as...")};
        for (u32 i = 0; i < NV_ARRAY_COUNT(labels); ++i) {
            if (i)
                igSameLine(0.0f, -1.0f);
            igBeginDisabled(!game_project_action_allowed(game, (ProjectAction)i));
            if (igButton(labels[i], (ImVec2_c){0, 0}))
                game_project_action(game, (ProjectAction)i);
            igEndDisabled();
        }
        if (game->project_message[0]) {
            if (game->project_message_bad)
                igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){1.0f, 0.4f, 0.35f, 1.0f});
            igTextWrapped("%s", game->project_message);
            if (game->project_message_bad)
                igPopStyleColor(1);
        }
        if (game->mode == MODE_BATTLE)
            rules_section(game);

        if (!game->defs_ok) {
            igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){1.0f, 0.4f, 0.35f, 1.0f});
            igTextWrapped("%s", T("Definitions could not be loaded"));
            igPopStyleColor(1);
            igTextWrapped("%s", game->defs.first_error); // the file's own text, so English
            igEnd();
            return;
        }
        if (game->mode == MODE_UNITS) {
            units_panel(game);
            igEnd();
            return;
        }

        igSeparator();
        // The unit types: one button each, the chosen one lit.
        igBeginDisabled(battle->phase != BATTLE_DEPLOY);
        for (u32 i = 0; i < battle->defs->unit_count; ++i) {
            char label[BATTLE_NAME_SIZE + 24];
            snprintf(label, sizeof(label), "%s (%u)###unit%u", battle->defs->units[i].name, battle->defs->units[i].cost, i);
            b32 chosen = game->selected_def == i;
            if (chosen)
                igPushStyleColor_Vec4(ImGuiCol_Button, igGetStyle()->Colors[ImGuiCol_ButtonActive]);
            if (igButton(label, (ImVec2_c){0, 0}))
                game->selected_def = i;
            if (chosen)
                igPopStyleColor(1);
            igSameLine(0.0f, -1.0f);
        }
        igNewLine();
        igEndDisabled();

        b32 can_start = battle->phase == BATTLE_DEPLOY && battle_alive_count(battle, TEAM_PLAYER) > 0;
        igBeginDisabled(!can_start);
        if (igButton(TL("Start"), (ImVec2_c){0, 0})) {
            start_over(game);
            game->paused = false; // a paused clock would make Start look broken
            battle_start(battle);
        }
        igEndDisabled();
        igSameLine(0.0f, -1.0f);
        igBeginDisabled(battle->phase == BATTLE_DEPLOY);
        if (igButton(TL("Retry"), (ImVec2_c){0, 0})) {
            start_over(game);
            battle_retry(battle);
        }
        igEndDisabled();
        igSameLine(0.0f, -1.0f);
        if (igButton(TL("Reset"), (ImVec2_c){0, 0})) {
            start_over(game);
            battle_retry(battle);
            battle_clear_placement(battle);
        }

        // The game clock's speed: the rules run the same ticks at any speed, only more or fewer a frame.
        b32 paused = game->paused;
        if (paused)
            igPushStyleColor_Vec4(ImGuiCol_Button, igGetStyle()->Colors[ImGuiCol_ButtonActive]);
        if (igButton(TL("Pause"), (ImVec2_c){0, 0}))
            game->paused = !game->paused;
        if (paused)
            igPopStyleColor(1);
        const f32 speeds[4] = {0.5f, 1.0f, 2.0f, 4.0f};
        const char* speed_labels[4] = {"0.5x###speed0", "1x###speed1", "2x###speed2", "4x###speed3"};
        for (u32 i = 0; i < NV_ARRAY_COUNT(speeds); ++i) {
            igSameLine(0.0f, -1.0f);
            b32 lit = !game->paused && game->speed == speeds[i];
            if (lit)
                igPushStyleColor_Vec4(ImGuiCol_Button, igGetStyle()->Colors[ImGuiCol_ButtonActive]);
            if (igButton(speed_labels[i], (ImVec2_c){0, 0})) {
                game->speed = speeds[i];
                game->paused = false;
            }
            if (lit)
                igPopStyleColor(1);
        }

        // One tick back or forward, paused; holding a button runs through the ticks.
        igBeginDisabled(battle->phase == BATTLE_DEPLOY || battle->tick == 0);
        if (step_button(TL("< Tick"))) {
            game->step = -1;
            game->paused = true;
        }
        igEndDisabled();
        igSameLine(0.0f, -1.0f);
        igBeginDisabled(battle->phase != BATTLE_FIGHT);
        if (step_button(TL("Tick >"))) {
            game->step = 1;
            game->paused = true;
        }
        igEndDisabled();

        igSeparator();
        igText(T("Supply: %u / %u"), battle_supply_left(battle), battle->defs->supply);
        igText(T("Player: %u alive"), battle_alive_count(battle, TEAM_PLAYER));
        igText(T("Enemy: %u alive"), battle_alive_count(battle, TEAM_ENEMY));
        // The rules count ticks; the seconds are that count over the tick rate, so they slow down with the game on a slow device.
        u32 round_ticks = battle->defs->rules.round_ticks;
        igText(T("Time: %.1f / %.0f s (%u / %u ticks)"), (f32)battle->tick * BATTLE_TICK_SECONDS,
               (f32)round_ticks * BATTLE_TICK_SECONDS, battle->tick, round_ticks);

        if (battle->phase == BATTLE_RESULT) {
            const char* outcomes[4] = {"", T("Victory"), T("Defeat"), T("Draw")};
            igPushStyleColor_Vec4(ImGuiCol_Text, battle->outcome == OUTCOME_PLAYER ? (ImVec4_c){0.4f, 1.0f, 0.5f, 1.0f}
                                                 : battle->outcome == OUTCOME_ENEMY ? (ImVec4_c){1.0f, 0.4f, 0.35f, 1.0f}
                                                                                    : (ImVec4_c){1.0f, 0.9f, 0.4f, 1.0f});
            igText("%s", outcomes[battle->outcome]);
            igPopStyleColor(1);
            igText(T("Value left: %.0f against %.0f"), battle_remaining_value(battle, TEAM_PLAYER), battle_remaining_value(battle, TEAM_ENEMY));
        }
        igSeparator();
        igTextWrapped("%s", T("Tap a cell of your zone (blue) to place the chosen unit; tap a placed unit to remove it. Drag to move the view, drag with the right button or two fingers to turn it, and use the wheel or pinch to zoom."));
    }
    igEnd();
}
