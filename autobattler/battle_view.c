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
internal NvMeshId make_unit_mesh(Game* game, const UnitDef* def)
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
    return nv_renderer_add_mesh(&game->renderer, &data);
}

internal NvMeshId make_plane(Game* game, f32 half_x, f32 half_z)
{
    NvVertex vertices[NV_MESH_PLANE_VERTICES];
    u32 indices[NV_MESH_PLANE_INDICES];
    NvMeshBuilder mesh = {.vertices = vertices, .vertex_capacity = NV_ARRAY_COUNT(vertices), .indices = indices,
                          .index_capacity = NV_ARRAY_COUNT(indices)};
    nv_mesh_append_plane(&mesh, half_x, half_z);
    NvMeshData data = nv_mesh_builder_data(&mesh);
    return nv_renderer_add_mesh(&game->renderer, &data);
}

internal NvNodeId add_plane(Game* game, const char* name, f32 half_x, f32 half_z, NvVec3 position, NvMaterialId material)
{
    NvNodeId id = nv_scene_add_node(game->scene, (NvNodeId){0}, name);
    NvNode* node = nv_scene_get(game->scene, id);
    node->mesh = make_plane(game, half_x, half_z);
    node->material = material;
    node->position = position;
    return id;
}

void view_build(Game* game)
{
    NvScene* scene = game->scene;
    NvNodeId none = {0};

    game->camera = nv_scene_add_node(scene, none, "camera");
    nv_scene_get(scene, game->camera)->camera = (NvCamera){
        .projection = NV_PROJECTION_PERSPECTIVE, .fov_y = 45.0f * NV_PI / 180.0f, .near_z = 0.5f, .far_z = 400.0f};
    scene->active_camera = game->camera;
    // Above and behind the player's side (-Z), looking at the middle of the field, far enough to see all of it.
    game->orbit = (NvOrbitCamera){
        .target = {FIELD_WIDTH * 0.5f, 0.0f, FIELD_LENGTH * 0.4f}, .yaw = NV_PI, .pitch = 62.0f * NV_PI / 180.0f,
        .distance = 104.0f, .min_pitch = CAMERA_MIN_PITCH, .max_pitch = CAMERA_MAX_PITCH,
        .min_distance = CAMERA_MIN_DISTANCE, .max_distance = CAMERA_MAX_DISTANCE};

    NvNodeId sun = nv_scene_add_node(scene, none, "sun");
    NvNode* sun_node = nv_scene_get(scene, sun);
    sun_node->rotation = nv_quat_mul(nv_quat_axis_angle(nv_vec3(0, 1, 0), 0.5f), nv_quat_axis_angle(nv_vec3(1, 0, 0), -0.9f));
    sun_node->light = (NvLight){.type = NV_LIGHT_DIRECTIONAL, .color = nv_vec3(1.0f, 0.96f, 0.9f), .intensity = 1.3f};

    // The ground, and a tint on each side's deployment zone (a little above it, so the two do not fight over depth).
    f32 zone_half = BATTLE_ZONE_ROWS * BATTLE_CELL_SIZE * 0.5f;
    add_plane(game, "ground", FIELD_WIDTH * 0.5f, FIELD_LENGTH * 0.5f, nv_vec3(FIELD_WIDTH * 0.5f, 0.0f, FIELD_LENGTH * 0.5f),
              add_color(game, 0.07f, 0.08f, 0.09f));
    add_plane(game, "player zone", FIELD_WIDTH * 0.5f, zone_half, nv_vec3(FIELD_WIDTH * 0.5f, 0.01f, zone_half),
              add_color(game, 0.04f, 0.09f, 0.2f));
    add_plane(game, "enemy zone", FIELD_WIDTH * 0.5f, zone_half, nv_vec3(FIELD_WIDTH * 0.5f, 0.01f, FIELD_LENGTH - zone_half),
              add_color(game, 0.2f, 0.06f, 0.05f));

    game->team_materials[TEAM_PLAYER] = add_color(game, 0.1f, 0.35f, 1.0f);
    game->team_materials[TEAM_ENEMY] = add_color(game, 1.0f, 0.18f, 0.12f);
    game->shell_material = add_color(game, 1.0f, 0.75f, 0.25f);
    NvVertex vertices[NV_MESH_SPHERE_VERTICES(8, 6)];
    u32 indices[NV_MESH_SPHERE_INDICES(8, 6)];
    NvMeshBuilder mesh = {.vertices = vertices, .vertex_capacity = NV_ARRAY_COUNT(vertices), .indices = indices,
                          .index_capacity = NV_ARRAY_COUNT(indices)};
    nv_mesh_append_sphere(&mesh, nv_vec3(0.0f, 0.0f, 0.0f), 0.15f, 8, 6);
    NvMeshData data = nv_mesh_builder_data(&mesh);
    game->shell_mesh = nv_renderer_add_mesh(&game->renderer, &data);
    for (u32 i = 0; i < game->defs.unit_count; ++i)
        game->unit_meshes[i] = make_unit_mesh(game, &game->defs.units[i]);

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
    s32 cell_x = (s32)(ground_x / BATTLE_CELL_SIZE), cell_row = (s32)(ground_z / BATTLE_CELL_SIZE);
    if (cell_x >= BATTLE_GRID_WIDTH || cell_row >= BATTLE_ZONE_ROWS)
        return (PlayerCell){0};
    return (PlayerCell){.ok = true, .x = cell_x, .row = cell_row};
}

// A left drag or one finger moves the view over the ground; a right or middle drag or two fingers turn it; the wheel and
// pinch zoom. A tap places the chosen unit in a cell of the player's zone, or removes the one standing there.
void view_input(Game* game)
{
    const NvViewInput* in = &game->imgui.view;
    NvOrbitCamera* orbit = &game->orbit;
    f32 ratio = nv_window_pixel_ratio(&game->window);
    const NvSceneOutput* scene_output = &game->layout.scene;

    nv_orbit_camera_turn(orbit, -in->pan_x * CAMERA_RADIANS_PER_PIXEL, in->pan_y * CAMERA_RADIANS_PER_PIXEL, in->dolly);
    if (in->orbit_x != 0.0f || in->orbit_y != 0.0f) {
        // The ground under the finger follows it. The image's height is in CSS pixels, like the drag.
        f32 image_height_css = (f32)scene_output->height * scene_output->pixel_height / ratio;
        NvVec3 move = nv_orbit_camera_pan_ground(orbit, nv_scene_get(game->scene, game->camera), image_height_css, in->orbit_x, in->orbit_y);
        orbit->target = nv_vec3_add(orbit->target, move);
    }
    orbit->target.x = nv_clamp_f32(orbit->target.x, 0.0f, FIELD_WIDTH);
    orbit->target.z = nv_clamp_f32(orbit->target.z, 0.0f, FIELD_LENGTH);
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

internal NvVec3 cell_corner(u32 x, u32 row, f32 y)
{
    return nv_vec3((f32)x * BATTLE_CELL_SIZE, y, (f32)row * BATTLE_CELL_SIZE);
}

// The lines of a zone's cells: rows `first` to `first + BATTLE_ZONE_ROWS`.
internal void draw_zone_grid(NvRenderer* renderer, u32 first_row, NvVec3 color)
{
    for (u32 r = 0; r <= BATTLE_ZONE_ROWS; ++r)
        nv_renderer_debug_line(renderer, cell_corner(0, first_row + r, 0.03f), cell_corner(BATTLE_GRID_WIDTH, first_row + r, 0.03f), color);
    for (u32 x = 0; x <= BATTLE_GRID_WIDTH; ++x)
        nv_renderer_debug_line(renderer, cell_corner(x, first_row, 0.03f), cell_corner(x, first_row + BATTLE_ZONE_ROWS, 0.03f), color);
}

internal void draw_ring(NvRenderer* renderer, NvVec3 center, NvVec3 u, NvVec3 v, f32 radius, NvVec3 color)
{
    const u32 segments = 20;
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
void view_update(Game* game, f32 game_dt)
{
    const Battle* battle = &game->battle;
    NvRenderer* renderer = &game->renderer;
    NvNode* camera = nv_scene_get(game->scene, game->camera);
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
                draw_ring(renderer, center, nv_vec3(1, 0, 0), nv_vec3(0, 0, 1), radius, color);
                draw_ring(renderer, center, nv_vec3(1, 0, 0), nv_vec3(0, 1, 0), radius, color);
                draw_ring(renderer, center, nv_vec3(0, 1, 0), nv_vec3(0, 0, 1), radius, color);
            }
        }
    }
    for (u32 slot = battle->unit_count + 1; slot <= game->unit_nodes_made; ++slot)
        nv_scene_get(game->scene, game->unit_nodes[slot])->mesh = (NvMeshId){0};

    for (u32 i = 0; i < battle->projectile_count; ++i) {
        const Projectile* shell = &battle->projectiles[i];
        NvNode* node = shell_node(game, i);
        node->mesh = game->shell_mesh;
        node->position = nv_vec3(lerp(shell->previous_position.x, shell->position.x, alpha),
                                 lerp(shell->previous_position.y, shell->position.y, alpha),
                                 lerp(shell->previous_position.z, shell->position.z, alpha));
    }
    for (u32 i = battle->projectile_count; i < game->shell_nodes_made; ++i)
        nv_scene_get(game->scene, game->shell_nodes[i])->mesh = (NvMeshId){0};

    if (battle->phase == BATTLE_DEPLOY && game->defs_ok) {
        draw_zone_grid(renderer, 0, nv_vec3(0.25f, 0.42f, 0.75f));
        draw_zone_grid(renderer, BATTLE_ENEMY_FIRST_ROW, nv_vec3(0.6f, 0.25f, 0.2f));
        if (game->hover_cell_x >= 0) {
            b32 ok = battle_can_place(battle, game->selected_def, game->hover_cell_x, game->hover_cell_row) ||
                     battle->placed[game->hover_cell_row][game->hover_cell_x]; // a placed unit can be taken away
            NvVec3 color = ok ? nv_vec3(0.2f, 1.0f, 0.3f) : nv_vec3(1.0f, 0.2f, 0.2f);
            u32 x = (u32)game->hover_cell_x, row = (u32)game->hover_cell_row;
            for (u32 inset = 0; inset < 2; ++inset) {
                f32 pad = 0.05f + 0.08f * (f32)inset;
                NvVec3 a = nv_vec3_add(cell_corner(x, row, 0.05f), nv_vec3(pad, 0.0f, pad));
                NvVec3 b = nv_vec3_add(cell_corner(x + 1, row, 0.05f), nv_vec3(-pad, 0.0f, pad));
                NvVec3 c = nv_vec3_add(cell_corner(x + 1, row + 1, 0.05f), nv_vec3(-pad, 0.0f, -pad));
                NvVec3 d = nv_vec3_add(cell_corner(x, row + 1, 0.05f), nv_vec3(pad, 0.0f, -pad));
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
}

// The panel and the build label

// What the page's top-left corner of the viewport says: the build type, the commit, what the page downloaded and the commit's
// subject.
void view_build_label(Game* game)
{
    char text[160];
    snprintf(text, sizeof(text), "%s build %s%s%s", NV_BUILD_NAME, NV_GIT_COMMIT, game->download_text[0] ? " \xC2\xB7 " : "",
             game->download_text);
    nv_imgui_build_label(&game->imgui, game->layout.viewport, text, 0.0f, "Commit: " NV_GIT_SUBJECT, NULL);
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

internal void start_over(Game* game)
{
    nv_vfx_clear(&game->vfx);
    game->accumulator = 0.0f;
}

void view_panel(Game* game)
{
    Battle* battle = &game->battle;
    if (nv_imgui_begin_panel(&game->imgui, "Battle", game->layout.panel)) {
        const char* phases[3] = {T("Deployment"), T("Fight"), T("Result")};
        igText("%s", T("Battle"));
        igSameLine(0.0f, -1.0f);
        igTextDisabled("%s", phases[battle->phase]);

        const char* languages[NV_LANGUAGE_COUNT] = {"English", "한국어"};
        s32 language = (s32)nv_strings_language();
        igSetNextItemWidth(igGetFontSize() * 8.0f);
        if (igCombo_Str_arr(TL("Language"), &language, languages, NV_LANGUAGE_COUNT, -1))
            nv_strings_set_language((NvLanguage)language);

        if (!game->defs_ok) {
            igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4_c){1.0f, 0.4f, 0.35f, 1.0f});
            igTextWrapped("%s", T("Definitions could not be loaded"));
            igPopStyleColor(1);
            igTextWrapped("%s", game->defs.first_error); // the file's own text, so English
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
        igText(T("Time: %.1f / %.0f s (%u / %u ticks)"), (f32)battle->tick * BATTLE_TICK_SECONDS,
               (f32)BATTLE_MAX_TICKS * BATTLE_TICK_SECONDS, battle->tick, (u32)BATTLE_MAX_TICKS);

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
