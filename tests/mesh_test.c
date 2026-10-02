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

int main(void)
{
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
