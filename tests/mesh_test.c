// The mesh primitives in engine/mesh.h: a box's vertices, normals and winding, a plane, and appending one after another.
#include <engine/mesh.h>

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            printf("FAILED line %d: %s\n", __LINE__, #condition);                 \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static NvVec3 position_of(const NvVertex* v) { return nv_vec3(v->position[0], v->position[1], v->position[2]); }
static NvVec3 normal_of(const NvVertex* v) { return nv_vec3(v->normal[0], v->normal[1], v->normal[2]); }

// Every triangle winds counter-clockwise seen from the side its vertex normals point to.
static void check_winding(const NvMeshBuilder* mesh, u32 first_index, u32 index_count)
{
    for (u32 i = first_index; i < first_index + index_count; i += 3) {
        const NvVertex* a = &mesh->vertices[mesh->indices[i]];
        const NvVertex* b = &mesh->vertices[mesh->indices[i + 1]];
        const NvVertex* c = &mesh->vertices[mesh->indices[i + 2]];
        NvVec3 ab = nv_vec3_sub(position_of(b), position_of(a));
        NvVec3 ac = nv_vec3_sub(position_of(c), position_of(a));
        NvVec3 face = nv_vec3_cross(ab, ac);
        CHECK(nv_vec3_dot(face, normal_of(a)) > 0.0f);
        CHECK(nv_vec3_dot(face, normal_of(b)) > 0.0f);
        CHECK(nv_vec3_dot(face, normal_of(c)) > 0.0f);
    }
}

// Every edge, by position, is shared by exactly two triangles that run it in opposite directions: the surface is closed and
// every triangle faces the same way as its neighbors. Seams and poles double vertices, so edges are matched by position.
static b32 same_point(const NvVertex* a, const NvVertex* b)
{
    return fabsf(a->position[0] - b->position[0]) < 1e-4f && fabsf(a->position[1] - b->position[1]) < 1e-4f &&
           fabsf(a->position[2] - b->position[2]) < 1e-4f;
}

static void check_closed(const NvMeshBuilder* mesh, u32 first_index, u32 index_count)
{
    const u32* idx = &mesh->indices[first_index];
    for (u32 t = 0; t < index_count; t += 3) {
        for (u32 e = 0; e < 3; ++e) {
            const NvVertex* from = &mesh->vertices[idx[t + e]];
            const NvVertex* to = &mesh->vertices[idx[t + (e + 1) % 3]];
            if (same_point(from, to)) {
                CHECK(!"a degenerate edge");
                continue;
            }
            u32 reverse = 0, same = 0;
            for (u32 u = 0; u < index_count; u += 3) {
                for (u32 f = 0; f < 3; ++f) {
                    const NvVertex* a = &mesh->vertices[idx[u + f]];
                    const NvVertex* b = &mesh->vertices[idx[u + (f + 1) % 3]];
                    reverse += same_point(a, to) && same_point(b, from);
                    same += same_point(a, from) && same_point(b, to);
                }
            }
            CHECK(reverse == 1 && same == 1);
        }
    }
}

static void check_normals(const NvMeshBuilder* mesh, u32 first_vertex, u32 vertex_count)
{
    for (u32 v = first_vertex; v < first_vertex + vertex_count; ++v) {
        NvVec3 n = normal_of(&mesh->vertices[v]);
        CHECK(fabsf(nv_vec3_dot(n, n) - 1.0f) < 1e-4f);
        CHECK(mesh->vertices[v].uv[0] >= 0.0f && mesh->vertices[v].uv[0] <= 1.0f);
        CHECK(mesh->vertices[v].uv[1] >= 0.0f && mesh->vertices[v].uv[1] <= 1.0f);
    }
}

// Appends with `append` into a fresh builder and checks the counts against the macros, the normals, the winding and that the
// surface is closed.
static NvVertex shape_vertices[8192];
static u32 shape_indices[16384];

static NvMeshBuilder fresh(void)
{
    return (NvMeshBuilder){.vertices = shape_vertices, .vertex_capacity = NV_ARRAY_COUNT(shape_vertices), .indices = shape_indices,
                           .index_capacity = NV_ARRAY_COUNT(shape_indices)};
}

static void check_shape(const NvMeshBuilder* mesh, u32 vertices, u32 indices)
{
    CHECK(mesh->vertex_count == vertices && mesh->index_count == indices);
    check_normals(mesh, 0, mesh->vertex_count);
    check_winding(mesh, 0, mesh->index_count);
    check_closed(mesh, 0, mesh->index_count);
}

static void shapes(void)
{
    NvVec3 c = nv_vec3(1, 2, 3);

    // Sphere: every vertex is `radius` from the center, its normal pointing straight out.
    for (u32 segments = 3; segments <= 24; segments += 7) {
        for (u32 rings = 2; rings <= 12; rings += 5) {
            NvMeshBuilder mesh = fresh();
            nv_mesh_append_sphere(&mesh, c, 0.75f, segments, rings);
            check_shape(&mesh, NV_MESH_SPHERE_VERTICES(segments, rings), NV_MESH_SPHERE_INDICES(segments, rings));
            for (u32 v = 0; v < mesh.vertex_count; ++v) {
                NvVec3 out = nv_vec3_sub(position_of(&mesh.vertices[v]), c);
                CHECK(fabsf(sqrtf(nv_vec3_dot(out, out)) - 0.75f) < 1e-4f);
                CHECK(nv_vec3_dot(nv_vec3_scale(out, 1.0f / 0.75f), normal_of(&mesh.vertices[v])) > 0.9999f);
            }
        }
    }

    // Cylinder: the side is `radius` from the axis with a level normal; the caps are at +-half_height facing up and down.
    for (u32 segments = 3; segments <= 24; segments += 7) {
        NvMeshBuilder mesh = fresh();
        nv_mesh_append_cylinder(&mesh, c, 0.5f, 1.25f, segments);
        check_shape(&mesh, NV_MESH_CYLINDER_VERTICES(segments), NV_MESH_CYLINDER_INDICES(segments));
        for (u32 v = 0; v < mesh.vertex_count; ++v) {
            NvVec3 p = nv_vec3_sub(position_of(&mesh.vertices[v]), c), n = normal_of(&mesh.vertices[v]);
            f32 across = sqrtf(p.x * p.x + p.z * p.z);
            CHECK(fabsf(p.y) <= 1.25f + 1e-5f && across <= 0.5f + 1e-5f);
            if (fabsf(n.y) < 1e-6f)
                CHECK(fabsf(across - 0.5f) < 1e-5f && fabsf(fabsf(p.y) - 1.25f) < 1e-5f);
            else
                CHECK(fabsf(p.y - n.y * 1.25f) < 1e-5f);
        }
    }

    // Cone: the base at -half_height facing down, every tip at +half_height; side normals lean up and out.
    for (u32 segments = 3; segments <= 24; segments += 7) {
        NvMeshBuilder mesh = fresh();
        nv_mesh_append_cone(&mesh, c, 0.5f, 1.0f, segments);
        check_shape(&mesh, NV_MESH_CONE_VERTICES(segments), NV_MESH_CONE_INDICES(segments));
        u32 tips = 0;
        for (u32 v = 0; v < mesh.vertex_count; ++v) {
            NvVec3 p = nv_vec3_sub(position_of(&mesh.vertices[v]), c), n = normal_of(&mesh.vertices[v]);
            if (fabsf(p.y - 1.0f) < 1e-5f) {
                ++tips;
                CHECK(fabsf(p.x) < 1e-5f && fabsf(p.z) < 1e-5f && n.y > 0.0f);
            } else {
                CHECK(fabsf(p.y + 1.0f) < 1e-5f);
            }
            if (n.y > 0.0f)
                CHECK(fabsf(n.y - 0.5f / sqrtf(0.25f + 4.0f)) < 1e-5f); // the slope: radius over the side's length
        }
        CHECK(tips == segments);
    }

    // Capsule: every vertex is `radius` from the segment between the hemispheres' centers, its normal pointing out.
    for (u32 segments = 3; segments <= 24; segments += 7) {
        for (u32 rings = 1; rings <= 7; rings += 3) {
            NvMeshBuilder mesh = fresh();
            nv_mesh_append_capsule(&mesh, c, 0.4f, 0.6f, segments, rings);
            check_shape(&mesh, NV_MESH_CAPSULE_VERTICES(segments, rings), NV_MESH_CAPSULE_INDICES(segments, rings));
            for (u32 v = 0; v < mesh.vertex_count; ++v) {
                NvVec3 p = nv_vec3_sub(position_of(&mesh.vertices[v]), c);
                NvVec3 axis = nv_vec3(0.0f, nv_clamp_f32(p.y, -0.6f, 0.6f), 0.0f);
                NvVec3 out = nv_vec3_sub(p, axis);
                CHECK(fabsf(sqrtf(nv_vec3_dot(out, out)) - 0.4f) < 1e-4f);
                CHECK(nv_vec3_dot(nv_vec3_scale(out, 1.0f / 0.4f), normal_of(&mesh.vertices[v])) > 0.999f);
            }
            // Its full height: from -(half_height + radius) to +(half_height + radius).
            f32 lowest = 1e9f, highest = -1e9f;
            for (u32 v = 0; v < mesh.vertex_count; ++v) {
                lowest = fminf(lowest, mesh.vertices[v].position[1] - c.y);
                highest = fmaxf(highest, mesh.vertices[v].position[1] - c.y);
            }
            CHECK(fabsf(lowest + 1.0f) < 1e-5f && fabsf(highest - 1.0f) < 1e-5f);
        }
    }

    // Torus: every vertex is `minor_radius` from the tube's middle circle, its normal pointing away from it.
    for (u32 segments = 3; segments <= 24; segments += 7) {
        for (u32 tube = 3; tube <= 12; tube += 4) {
            NvMeshBuilder mesh = fresh();
            nv_mesh_append_torus(&mesh, c, 1.0f, 0.25f, segments, tube);
            check_shape(&mesh, NV_MESH_TORUS_VERTICES(segments, tube), NV_MESH_TORUS_INDICES(segments, tube));
            for (u32 v = 0; v < mesh.vertex_count; ++v) {
                NvVec3 p = nv_vec3_sub(position_of(&mesh.vertices[v]), c);
                f32 across = sqrtf(p.x * p.x + p.z * p.z);
                NvVec3 middle = nv_vec3(p.x / across * 1.0f, 0.0f, p.z / across * 1.0f); // the major radius is 1
                NvVec3 out = nv_vec3_sub(p, middle);
                CHECK(fabsf(sqrtf(nv_vec3_dot(out, out)) - 0.25f) < 1e-4f);
                CHECK(nv_vec3_dot(nv_vec3_scale(out, 1.0f / 0.25f), normal_of(&mesh.vertices[v])) > 0.999f);
            }
        }
    }

    // The box is closed too.
    NvMeshBuilder box = fresh();
    nv_mesh_append_box(&box, c, nv_vec3(1, 2, 3));
    check_shape(&box, NV_MESH_BOX_VERTICES, NV_MESH_BOX_INDICES);
}

int main(void)
{
    shapes();

    NvVertex vertices[24 * 2 + 4];
    u32 indices[36 * 2 + 6];
    NvMeshBuilder mesh = {.vertices = vertices, .vertex_capacity = NV_ARRAY_COUNT(vertices), .indices = indices,
                          .index_capacity = NV_ARRAY_COUNT(indices)};

    // A box: 24 vertices, 36 indices, the first face is +X.
    nv_mesh_append_box(&mesh, nv_vec3(1, 2, 3), nv_vec3(0.5f, 1.0f, 1.5f));
    CHECK(mesh.vertex_count == 24 && mesh.index_count == 36);
    const f32 face_x[4][3] = {{1.5f, 1.0f, 1.5f}, {1.5f, 3.0f, 1.5f}, {1.5f, 3.0f, 4.5f}, {1.5f, 1.0f, 4.5f}};
    for (u32 v = 0; v < 4; ++v) {
        for (u32 axis = 0; axis < 3; ++axis)
            CHECK(fabsf(vertices[v].position[axis] - face_x[v][axis]) < 1e-6f);
        CHECK(vertices[v].normal[0] == 1.0f && vertices[v].normal[1] == 0.0f && vertices[v].normal[2] == 0.0f);
        CHECK(vertices[v].uv[0] == 0.0f && vertices[v].uv[1] == 0.0f);
    }
    CHECK(indices[0] == 0 && indices[1] == 1 && indices[2] == 2 && indices[3] == 0 && indices[4] == 2 && indices[5] == 3);

    // Every vertex is on the box, every normal has length 1 and each face has one of the six axis normals.
    u32 faces_seen[6] = {0};
    for (u32 v = 0; v < 24; ++v) {
        NvVec3 p = position_of(&vertices[v]), n = normal_of(&vertices[v]);
        CHECK(fabsf(fabsf(p.x - 1.0f) - 0.5f) < 1e-6f);
        CHECK(fabsf(fabsf(p.y - 2.0f) - 1.0f) < 1e-6f);
        CHECK(fabsf(fabsf(p.z - 3.0f) - 1.5f) < 1e-6f);
        CHECK(fabsf(nv_vec3_dot(n, n) - 1.0f) < 1e-6f);
        u32 face = n.x > 0 ? 0 : n.x < 0 ? 1 : n.y > 0 ? 2 : n.y < 0 ? 3 : n.z > 0 ? 4 : 5;
        ++faces_seen[face];
        // The vertex is on the face its normal names.
        CHECK(fabsf(nv_vec3_dot(nv_vec3_sub(p, nv_vec3(1, 2, 3)), n) - (fabsf(n.x) * 0.5f + fabsf(n.y) * 1.0f + fabsf(n.z) * 1.5f)) < 1e-6f);
    }
    for (u32 face = 0; face < 6; ++face)
        CHECK(faces_seen[face] == 4);
    check_winding(&mesh, 0, 36);

    // A second box goes after the first, with its indices offset.
    nv_mesh_append_box(&mesh, nv_vec3(0, 0, 0), nv_vec3(1, 1, 1));
    CHECK(mesh.vertex_count == 48 && mesh.index_count == 72);
    CHECK(indices[36] == 24 && indices[37] == 25 && indices[38] == 26);
    for (u32 i = 36; i < 72; ++i)
        CHECK(indices[i] >= 24 && indices[i] < 48);
    check_winding(&mesh, 36, 36);

    // A plane: the corners, the UVs, and counter-clockwise seen from +Y.
    nv_mesh_append_plane(&mesh, 30.0f, 20.0f);
    CHECK(mesh.vertex_count == 52 && mesh.index_count == 78);
    const NvVertex* plane = &vertices[48];
    CHECK(plane[0].position[0] == -30.0f && plane[0].position[2] == -20.0f && plane[0].uv[0] == 0.0f && plane[0].uv[1] == 0.0f);
    CHECK(plane[1].position[0] == -30.0f && plane[1].position[2] == 20.0f && plane[1].uv[0] == 0.0f && plane[1].uv[1] == 1.0f);
    CHECK(plane[2].position[0] == 30.0f && plane[2].position[2] == 20.0f && plane[2].uv[0] == 1.0f && plane[2].uv[1] == 1.0f);
    CHECK(plane[3].position[0] == 30.0f && plane[3].position[2] == -20.0f && plane[3].uv[0] == 1.0f && plane[3].uv[1] == 0.0f);
    for (u32 v = 0; v < 4; ++v)
        CHECK(plane[v].position[1] == 0.0f && plane[v].normal[1] == 1.0f);
    CHECK(indices[72] == 48 && indices[73] == 49 && indices[74] == 50 && indices[75] == 48 && indices[76] == 50 && indices[77] == 51);
    check_winding(&mesh, 72, 6);

    // What the renderer takes.
    NvMeshData data = nv_mesh_builder_data(&mesh);
    CHECK(data.vertices == vertices && data.skinned_vertices == NULL && data.vertex_count == 52);
    CHECK(data.indices == indices && data.index_count == 78);

    // NOTE: Running out of room is an assert (a trap), so it cannot be tested in-process.
    if (failures == 0)
        printf("mesh_test: all passed\n");
    return failures ? 1 : 0;
}
