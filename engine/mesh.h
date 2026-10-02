#pragma once

#include "engine/math.h"

// Mesh vertex data and the primitives that fill it. No GPU here: engine/renderer.h uploads what is built, and the tests
// run these functions under Node.

typedef struct NvVertex {
    f32 position[3];
    f32 normal[3];
    f32 uv[2];
} NvVertex;

typedef struct NvSkinnedVertex {
    f32 position[3];
    f32 normal[3];
    f32 uv[2];
    u16 joints[4];
    f32 weights[4];
} NvSkinnedVertex;

// Mesh data to upload. Exactly one of `vertices` / `skinned_vertices` is set.
typedef struct NvMeshData {
    const NvVertex* vertices;
    const NvSkinnedVertex* skinned_vertices;
    u32 vertex_count;
    const u32* indices;
    u32 index_count;
} NvMeshData;

// A mesh being built in arrays the caller owns (on the stack or in an arena); the primitives append to it and assert that
// they fit. The counts start at zero.
typedef struct NvMeshBuilder {
    NvVertex* vertices;
    u32 vertex_count, vertex_capacity;
    u32* indices;
    u32 index_count, index_capacity;
} NvMeshBuilder;

// What nv_renderer_add_mesh takes, over the arrays built so far.
static inline NvMeshData nv_mesh_builder_data(const NvMeshBuilder* mesh)
{
    return (NvMeshData){.vertices = mesh->vertices, .skinned_vertices = NULL, .vertex_count = mesh->vertex_count,
                        .indices = mesh->indices, .index_count = mesh->index_count};
}

// An axis-aligned box: 24 vertices (four per face, so each face has its own normal) and 36 indices, counter-clockwise
// seen from outside.
void nv_mesh_append_box(NvMeshBuilder* mesh, NvVec3 center, NvVec3 half);

// A flat rectangle at y = 0 facing +Y: 4 vertices and 6 indices. UVs run from (0, 0) at (-half_x, -half_z) to (1, 1).
void nv_mesh_append_plane(NvMeshBuilder* mesh, f32 half_x, f32 half_z);
