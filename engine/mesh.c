#include "engine/mesh.h"

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
