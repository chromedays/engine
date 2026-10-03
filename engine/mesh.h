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

// The primitives (docs/specs/mesh.md). Every one is closed (but the plane), wound counter-clockwise seen from outside, and
// round ones stand along +Y. `segments` go around Y (at least 3); `rings` go from a pole to the other (sphere, at least 2)
// or from a pole to the equator (capsule, at least 1). A seam's vertices are doubled so UVs can run from 0 to 1, and a
// sphere's poles have one vertex per segment. The macros give the vertices and indices each one appends, for sizing arrays.
#define NV_MESH_BOX_VERTICES 24
#define NV_MESH_BOX_INDICES 36
#define NV_MESH_PLANE_VERTICES 4
#define NV_MESH_PLANE_INDICES 6
#define NV_MESH_SPHERE_VERTICES(segments, rings) (((segments) + 1) * ((rings) + 1))
#define NV_MESH_SPHERE_INDICES(segments, rings) (6 * (segments) * ((rings) - 1))
#define NV_MESH_CYLINDER_VERTICES(segments) (4 * ((segments) + 1))
#define NV_MESH_CYLINDER_INDICES(segments) (12 * (segments))
#define NV_MESH_CONE_VERTICES(segments) (3 * (segments) + 2)
#define NV_MESH_CONE_INDICES(segments) (6 * (segments))
#define NV_MESH_CAPSULE_VERTICES(segments, rings) (((segments) + 1) * (2 * (rings) + 2))
#define NV_MESH_CAPSULE_INDICES(segments, rings) (12 * (segments) * (rings))
#define NV_MESH_TORUS_VERTICES(segments, tube_segments) (((segments) + 1) * ((tube_segments) + 1))
#define NV_MESH_TORUS_INDICES(segments, tube_segments) (6 * (segments) * (tube_segments))

// An axis-aligned box: four vertices per face, so each face has its own normal.
void nv_mesh_append_box(NvMeshBuilder* mesh, NvVec3 center, NvVec3 half);

// A flat rectangle at y = 0 facing +Y. UVs run from (0, 0) at (-half_x, -half_z) to (1, 1).
void nv_mesh_append_plane(NvMeshBuilder* mesh, f32 half_x, f32 half_z);

// A UV sphere with smooth normals.
void nv_mesh_append_sphere(NvMeshBuilder* mesh, NvVec3 center, f32 radius, u32 segments, u32 rings);

// A cylinder from center.y - half_height to center.y + half_height: a smooth side and two flat caps.
void nv_mesh_append_cylinder(NvMeshBuilder* mesh, NvVec3 center, f32 radius, f32 half_height, u32 segments);

// A cone with its base at center.y - half_height and its tip at center.y + half_height: a smooth side and a flat base.
void nv_mesh_append_cone(NvMeshBuilder* mesh, NvVec3 center, f32 radius, f32 half_height, u32 segments);

// A capsule: a cylinder of 2 * half_height with a hemisphere of `radius` on each end, so it is
// 2 * (half_height + radius) tall. Smooth normals.
void nv_mesh_append_capsule(NvMeshBuilder* mesh, NvVec3 center, f32 radius, f32 half_height, u32 segments, u32 rings);

// A torus lying in the XZ plane: `major_radius` from the center to the middle of the tube, `minor_radius` the tube's.
// `segments` go around Y, `tube_segments` around the tube (at least 3). Smooth normals.
void nv_mesh_append_torus(NvMeshBuilder* mesh, NvVec3 center, f32 major_radius, f32 minor_radius, u32 segments,
                          u32 tube_segments);
