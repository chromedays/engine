#include "engine/mesh.h"

#include <math.h>

void nv_mesh_append_box(NvMeshBuilder* mesh, NvVec3 center, NvVec3 half)
{
    NV_ASSERT(mesh->vertex_count + 24 <= mesh->vertex_capacity);
    NV_ASSERT(mesh->index_count + 36 <= mesh->index_capacity);
    // Each face: normal n and in-plane axes u, v with u x v = n (counter-clockwise from outside).
    local_persist const f32 faces[6][3][3] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    local_persist const f32 corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const f32 c[3] = {center.x, center.y, center.z};
    const f32 h[3] = {half.x, half.y, half.z};
    for (u32 face = 0; face < 6; ++face) {
        u32 base = mesh->vertex_count;
        for (u32 corner = 0; corner < 4; ++corner) {
            NvVertex* vertex = &mesh->vertices[mesh->vertex_count++];
            *vertex = (NvVertex){0};
            for (u32 axis = 0; axis < 3; ++axis) {
                f32 p = faces[face][0][axis] + corners[corner][0] * faces[face][1][axis] + corners[corner][1] * faces[face][2][axis];
                vertex->position[axis] = c[axis] + p * h[axis];
                vertex->normal[axis] = faces[face][0][axis];
            }
        }
        u32* out = &mesh->indices[mesh->index_count];
        out[0] = base; out[1] = base + 1; out[2] = base + 2;
        out[3] = base; out[4] = base + 2; out[5] = base + 3;
        mesh->index_count += 6;
    }
}

void nv_mesh_append_plane(NvMeshBuilder* mesh, f32 half_x, f32 half_z)
{
    NV_ASSERT(mesh->vertex_count + 4 <= mesh->vertex_capacity);
    NV_ASSERT(mesh->index_count + 6 <= mesh->index_capacity);
    u32 base = mesh->vertex_count;
    mesh->vertices[base + 0] = (NvVertex){{-half_x, 0, -half_z}, {0, 1, 0}, {0, 0}};
    mesh->vertices[base + 1] = (NvVertex){{-half_x, 0, half_z}, {0, 1, 0}, {0, 1}};
    mesh->vertices[base + 2] = (NvVertex){{half_x, 0, half_z}, {0, 1, 0}, {1, 1}};
    mesh->vertices[base + 3] = (NvVertex){{half_x, 0, -half_z}, {0, 1, 0}, {1, 0}};
    mesh->vertex_count += 4;
    u32* out = &mesh->indices[mesh->index_count];
    out[0] = base; out[1] = base + 1; out[2] = base + 2;
    out[3] = base; out[4] = base + 2; out[5] = base + 3;
    mesh->index_count += 6;
}

internal void set_vertex(NvVertex* vertex, NvVec3 position, NvVec3 normal, f32 u, f32 v)
{
    *vertex = (NvVertex){{position.x, position.y, position.z}, {normal.x, normal.y, normal.z}, {u, v}};
}

internal void push_triangle(NvMeshBuilder* mesh, u32 a, u32 b, u32 c)
{
    u32* out = &mesh->indices[mesh->index_count];
    out[0] = a;
    out[1] = b;
    out[2] = c;
    mesh->index_count += 3;
}

// Rows of `segments + 1` vertices from `base` on, each row further down (or along) the surface than the one before: the two
// triangles of each quad between row r and r + 1, counter-clockwise seen from outside. A row that is a single point (a
// pole) gets one triangle per segment instead of two.
internal void push_rows(NvMeshBuilder* mesh, u32 base, u32 segments, u32 rows, b32 first_is_pole, b32 last_is_pole)
{
    for (u32 r = 0; r + 1 < rows; ++r) {
        for (u32 s = 0; s < segments; ++s) {
            u32 a = base + r * (segments + 1) + s;     // this row
            u32 b = a + segments + 1;                  // the next row
            u32 c = b + 1, d = a + 1;
            if (!(first_is_pole && r == 0))
                push_triangle(mesh, a, d, c);
            if (!(last_is_pole && r + 2 == rows))
                push_triangle(mesh, a, c, b);
        }
    }
}

// The direction at polar angle `theta` from +Y and angle `phi` around Y.
internal NvVec3 polar(f32 theta, f32 phi)
{
    f32 s = sinf(theta);
    return nv_vec3(s * cosf(phi), cosf(theta), s * sinf(phi));
}

void nv_mesh_append_sphere(NvMeshBuilder* mesh, NvVec3 center, f32 radius, u32 segments, u32 rings)
{
    NV_ASSERT(segments >= 3 && rings >= 2);
    NV_ASSERT(mesh->vertex_count + NV_MESH_SPHERE_VERTICES(segments, rings) <= mesh->vertex_capacity);
    NV_ASSERT(mesh->index_count + NV_MESH_SPHERE_INDICES(segments, rings) <= mesh->index_capacity);
    u32 base = mesh->vertex_count;
    for (u32 r = 0; r <= rings; ++r) {
        for (u32 s = 0; s <= segments; ++s) {
            NvVec3 n = polar(NV_PI * (f32)r / (f32)rings, 2.0f * NV_PI * (f32)s / (f32)segments);
            set_vertex(&mesh->vertices[mesh->vertex_count++], nv_vec3_add(center, nv_vec3_scale(n, radius)), n,
                       (f32)s / (f32)segments, (f32)r / (f32)rings);
        }
    }
    push_rows(mesh, base, segments, rings + 1, 1, 1);
}

// A flat disk at height `y` facing +Y (up) or -Y: its center and `segments` rim vertices.
internal void push_cap(NvMeshBuilder* mesh, NvVec3 center, f32 radius, f32 y, u32 segments, b32 up)
{
    NvVec3 normal = nv_vec3(0.0f, up ? 1.0f : -1.0f, 0.0f);
    u32 middle = mesh->vertex_count;
    set_vertex(&mesh->vertices[mesh->vertex_count++], nv_vec3(center.x, center.y + y, center.z), normal, 0.5f, 0.5f);
    for (u32 s = 0; s < segments; ++s) {
        f32 phi = 2.0f * NV_PI * (f32)s / (f32)segments;
        f32 c = cosf(phi), z = sinf(phi);
        set_vertex(&mesh->vertices[mesh->vertex_count++], nv_vec3(center.x + c * radius, center.y + y, center.z + z * radius),
                   normal, 0.5f + 0.5f * c, 0.5f + 0.5f * z);
    }
    for (u32 s = 0; s < segments; ++s) {
        u32 rim = middle + 1 + s, next = middle + 1 + (s + 1) % segments;
        if (up)
            push_triangle(mesh, middle, next, rim);
        else
            push_triangle(mesh, middle, rim, next);
    }
}

void nv_mesh_append_cylinder(NvMeshBuilder* mesh, NvVec3 center, f32 radius, f32 half_height, u32 segments)
{
    NV_ASSERT(segments >= 3);
    NV_ASSERT(mesh->vertex_count + NV_MESH_CYLINDER_VERTICES(segments) <= mesh->vertex_capacity);
    NV_ASSERT(mesh->index_count + NV_MESH_CYLINDER_INDICES(segments) <= mesh->index_capacity);
    u32 base = mesh->vertex_count;
    for (u32 r = 0; r < 2; ++r) {
        f32 y = r == 0 ? half_height : -half_height;
        for (u32 s = 0; s <= segments; ++s) {
            f32 phi = 2.0f * NV_PI * (f32)s / (f32)segments;
            NvVec3 n = nv_vec3(cosf(phi), 0.0f, sinf(phi)); // level, not polar(): cosf(pi / 2) is not quite 0
            set_vertex(&mesh->vertices[mesh->vertex_count++],
                       nv_vec3(center.x + n.x * radius, center.y + y, center.z + n.z * radius), n, (f32)s / (f32)segments,
                       (f32)r);
        }
    }
    push_rows(mesh, base, segments, 2, 0, 0);
    push_cap(mesh, center, radius, half_height, segments, 1);
    push_cap(mesh, center, radius, -half_height, segments, 0);
}

void nv_mesh_append_cone(NvMeshBuilder* mesh, NvVec3 center, f32 radius, f32 half_height, u32 segments)
{
    NV_ASSERT(segments >= 3);
    NV_ASSERT(mesh->vertex_count + NV_MESH_CONE_VERTICES(segments) <= mesh->vertex_capacity);
    NV_ASSERT(mesh->index_count + NV_MESH_CONE_INDICES(segments) <= mesh->index_capacity);
    // The side's normal leans up by the slope: across (2 * half_height) for every `radius` in.
    f32 height = 2.0f * half_height;
    f32 length = sqrtf(height * height + radius * radius);
    f32 across = height / length, up = radius / length;
    // One tip per segment, its normal halfway between the segment's edges, then the rim.
    u32 tips = mesh->vertex_count;
    for (u32 s = 0; s < segments; ++s) {
        f32 phi = 2.0f * NV_PI * ((f32)s + 0.5f) / (f32)segments;
        set_vertex(&mesh->vertices[mesh->vertex_count++], nv_vec3(center.x, center.y + half_height, center.z),
                   nv_vec3(cosf(phi) * across, up, sinf(phi) * across), ((f32)s + 0.5f) / (f32)segments, 0.0f);
    }
    u32 rim = mesh->vertex_count;
    for (u32 s = 0; s <= segments; ++s) {
        f32 phi = 2.0f * NV_PI * (f32)s / (f32)segments;
        f32 c = cosf(phi), z = sinf(phi);
        set_vertex(&mesh->vertices[mesh->vertex_count++],
                   nv_vec3(center.x + c * radius, center.y - half_height, center.z + z * radius),
                   nv_vec3(c * across, up, z * across), (f32)s / (f32)segments, 1.0f);
    }
    for (u32 s = 0; s < segments; ++s)
        push_triangle(mesh, tips + s, rim + s + 1, rim + s);
    push_cap(mesh, center, radius, -half_height, segments, 0);
}

void nv_mesh_append_capsule(NvMeshBuilder* mesh, NvVec3 center, f32 radius, f32 half_height, u32 segments, u32 rings)
{
    NV_ASSERT(segments >= 3 && rings >= 1);
    NV_ASSERT(mesh->vertex_count + NV_MESH_CAPSULE_VERTICES(segments, rings) <= mesh->vertex_capacity);
    NV_ASSERT(mesh->index_count + NV_MESH_CAPSULE_INDICES(segments, rings) <= mesh->index_capacity);
    // A sphere cut at the equator, its top half raised by half_height and its bottom half lowered: the equator's two rows
    // make the cylinder between them.
    u32 base = mesh->vertex_count;
    u32 rows = 2 * rings + 2;
    for (u32 r = 0; r < rows; ++r) {
        b32 top = r <= rings;
        f32 theta = top ? 0.5f * NV_PI * (f32)r / (f32)rings : 0.5f * NV_PI + 0.5f * NV_PI * (f32)(r - rings - 1) / (f32)rings;
        f32 y = top ? half_height : -half_height;
        for (u32 s = 0; s <= segments; ++s) {
            NvVec3 n = polar(theta, 2.0f * NV_PI * (f32)s / (f32)segments);
            NvVec3 p = nv_vec3_add(center, nv_vec3_add(nv_vec3_scale(n, radius), nv_vec3(0.0f, y, 0.0f)));
            set_vertex(&mesh->vertices[mesh->vertex_count++], p, n, (f32)s / (f32)segments, (f32)r / (f32)(rows - 1));
        }
    }
    push_rows(mesh, base, segments, rows, 1, 1);
}

void nv_mesh_append_torus(NvMeshBuilder* mesh, NvVec3 center, f32 major_radius, f32 minor_radius, u32 segments,
                          u32 tube_segments)
{
    NV_ASSERT(segments >= 3 && tube_segments >= 3);
    NV_ASSERT(mesh->vertex_count + NV_MESH_TORUS_VERTICES(segments, tube_segments) <= mesh->vertex_capacity);
    NV_ASSERT(mesh->index_count + NV_MESH_TORUS_INDICES(segments, tube_segments) <= mesh->index_capacity);
    // Rows go around the tube, from its outer equator over the top; each row goes once around Y.
    u32 base = mesh->vertex_count;
    for (u32 t = 0; t <= tube_segments; ++t) {
        f32 psi = 2.0f * NV_PI * (f32)t / (f32)tube_segments;
        f32 out = cosf(psi), lift = sinf(psi);
        for (u32 s = 0; s <= segments; ++s) {
            f32 phi = 2.0f * NV_PI * (f32)s / (f32)segments;
            f32 c = cosf(phi), z = sinf(phi);
            f32 reach = major_radius + minor_radius * out;
            set_vertex(&mesh->vertices[mesh->vertex_count++],
                       nv_vec3(center.x + c * reach, center.y + minor_radius * lift, center.z + z * reach),
                       nv_vec3(c * out, lift, z * out), (f32)s / (f32)segments, (f32)t / (f32)tube_segments);
        }
    }
    // Going around the tube runs upward on the outside, the opposite of a sphere's rows, so the quads turn the other way.
    for (u32 t = 0; t < tube_segments; ++t) {
        for (u32 s = 0; s < segments; ++s) {
            u32 a = base + t * (segments + 1) + s, b = a + segments + 1, c = b + 1, d = a + 1;
            push_triangle(mesh, a, b, c);
            push_triangle(mesh, a, c, d);
        }
    }
}
