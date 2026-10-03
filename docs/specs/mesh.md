# Mesh primitives spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: 구현됨 (2026-10-03). 이 스펙의 변경은 먼저 합의한다.

### 목표

코드로 만드는 기본 도형을 엔진에 둔다. 상자와 평면(`docs/specs/shared.md`에서 옮김)에 구, 원기둥, 원뿔, 캡슐, 토러스를 더한다.
`battle.md`의 프로토타입이 모델 없이 유닛, 포탄, 표시를 그릴 수 있게 하고, 다른 앱도 쓸 수 있다. 서드파티는 없다.

### 결정

| 주제 | 결정 |
|---|---|
| 위치 | `engine/mesh.h`, `engine/mesh.c`. GPU 없음, ctest로 검사한다 |
| 방식 | 모두 호출하는 쪽의 배열에 덧붙인다(`NvMeshBuilder`). 용량이 모자라면 assert. `nv_mesh_builder_data`로 `nv_renderer_add_mesh`에 넘긴다 |
| 크기 매크로 | 도형마다 덧붙이는 꼭짓점과 인덱스 수: `NV_MESH_SPHERE_VERTICES(segments, rings)`, `NV_MESH_SPHERE_INDICES(...)` 등. 배열 크기를 정할 때 쓴다 |
| 방향 | 모두 닫힌 도형이고(평면만 빼고) 바깥에서 볼 때 반시계로 감긴다. 둥근 도형은 +Y를 따라 선다. `segments`는 Y축을 도는 나눔(3 이상) |
| 법선 | 상자와 평면, 원기둥과 원뿔의 뚜껑은 면마다 평평한 법선. 구, 원기둥과 원뿔의 옆면, 캡슐, 토러스는 부드러운 법선 |
| UV | 0에서 1. 이음매의 꼭짓점은 두 번 두어 UV가 끊기지 않는다. 뚜껑은 원판 위에 평면으로 펼친다 |
| 극 | 구와 캡슐의 극에는 segment마다 꼭짓점이 하나씩 있고, 극의 띠는 퇴화 삼각형 없이 segment마다 삼각형 하나다 |
| 원뿔 끝 | segment마다 끝 꼭짓점이 하나씩이고, 법선은 그 segment 가운데 방향이다(끝이 한 점으로 뭉개진 그늘을 피함) |

### API

```c
void nv_mesh_append_box(NvMeshBuilder* mesh, NvVec3 center, NvVec3 half);
void nv_mesh_append_plane(NvMeshBuilder* mesh, f32 half_x, f32 half_z);
void nv_mesh_append_sphere(NvMeshBuilder* mesh, NvVec3 center, f32 radius, u32 segments, u32 rings);            // rings >= 2, pole to pole
void nv_mesh_append_cylinder(NvMeshBuilder* mesh, NvVec3 center, f32 radius, f32 half_height, u32 segments);
void nv_mesh_append_cone(NvMeshBuilder* mesh, NvVec3 center, f32 radius, f32 half_height, u32 segments);         // tip up
void nv_mesh_append_capsule(NvMeshBuilder* mesh, NvVec3 center, f32 radius, f32 half_height, u32 segments,
                            u32 rings);                                                                         // rings >= 1, per hemisphere
void nv_mesh_append_torus(NvMeshBuilder* mesh, NvVec3 center, f32 major_radius, f32 minor_radius, u32 segments,
                          u32 tube_segments);                                                                   // in the XZ plane
```

| 도형 | 꼭짓점 | 인덱스 |
|---|---|---|
| 상자 | 24 | 36 |
| 평면 | 4 | 6 |
| 구 | (segments + 1)(rings + 1) | 6 · segments · (rings − 1) |
| 원기둥 | 4 (segments + 1) | 12 · segments |
| 원뿔 | 3 · segments + 2 | 6 · segments |
| 캡슐 | (segments + 1)(2 · rings + 2) | 12 · segments · rings |
| 토러스 | (segments + 1)(tube_segments + 1) | 6 · segments · tube_segments |

캡슐의 전체 높이는 2 · (half_height + radius)다: 원기둥 부분이 2 · half_height이고 양 끝에 반구가 붙는다.

### 테스트

`tests/mesh_test.c`(ctest, Node)가 도형마다 여러 segment와 ring 수로:

- 덧붙인 수가 매크로와 같다.
- 법선은 길이 1이고 UV는 0에서 1 사이다.
- 꼭짓점이 도형의 면 위에 있다(구는 중심에서 반지름, 원기둥은 축에서 반지름과 높이 안, 캡슐은 축 선분에서 반지름, 토러스는
  관의 중심 원에서 관 반지름). 부드러운 법선은 그 면의 바깥 방향과 같다.
- 모든 삼각형이 바깥에서 볼 때 반시계다(면의 기하 법선과 꼭짓점 법선이 같은 쪽).
- 닫혀 있다: 위치로 맞춘 모든 변이 정확히 두 삼각형에 반대 방향으로 쓰인다.

거꾸로 감긴 토러스, 거꾸로 감긴 원뿔 옆면, 빠진 원기둥 뚜껑을 일부러 만들어 테스트가 잡는 것도 확인했다.

## English

Status: built (2026-10-03). Changes to this spec are agreed first.

### Goal

Shapes made in code, in the engine. The box and plane (moved in `docs/specs/shared.md`) gain a sphere, cylinder, cone, capsule and
torus, so the prototype of `battle.md` can draw units, shells and markers without models, and other apps can use them too. No
third-party code.

### Decisions

| Topic | Decision |
|---|---|
| Where | `engine/mesh.h`, `engine/mesh.c`. No GPU; checked by ctest |
| How | Each appends to arrays the caller owns (`NvMeshBuilder`) and asserts that it fits. `nv_mesh_builder_data` hands the result to `nv_renderer_add_mesh` |
| Size macros | The vertices and indices each shape appends: `NV_MESH_SPHERE_VERTICES(segments, rings)`, `NV_MESH_SPHERE_INDICES(...)` and so on, for sizing arrays |
| Orientation | All are closed (but the plane) and wound counter-clockwise seen from outside. Round shapes stand along +Y. `segments` divide the turn around Y (at least 3) |
| Normals | Flat per face for the box, the plane, and the caps of cylinders and cones. Smooth for the sphere, the sides of cylinders and cones, the capsule and the torus |
| UVs | 0 to 1. A seam's vertices are doubled so UVs do not wrap. Caps are mapped flat over their disk |
| Poles | Sphere and capsule poles have one vertex per segment, and a pole band is one triangle per segment, with no degenerate triangles |
| Cone tip | One tip vertex per segment, its normal pointing to the middle of that segment (so the tip is not shaded as one pinched point) |

### API

See the code block above.

| Shape | Vertices | Indices |
|---|---|---|
| Box | 24 | 36 |
| Plane | 4 | 6 |
| Sphere | (segments + 1)(rings + 1) | 6 · segments · (rings − 1) |
| Cylinder | 4 (segments + 1) | 12 · segments |
| Cone | 3 · segments + 2 | 6 · segments |
| Capsule | (segments + 1)(2 · rings + 2) | 12 · segments · rings |
| Torus | (segments + 1)(tube_segments + 1) | 6 · segments · tube_segments |

A capsule is 2 · (half_height + radius) tall: its cylinder part is 2 · half_height, with a hemisphere on each end.

### Tests

`tests/mesh_test.c` (ctest, Node), for each shape at several segment and ring counts:

- The counts appended equal the macros.
- Normals have length 1, and UVs are between 0 and 1.
- Vertices are on the shape's surface (a sphere's radius from its center, a cylinder's radius from its axis and within its height,
  a capsule's radius from its axis segment, a torus's tube radius from the tube's middle circle), and smooth normals point the
  way that surface faces.
- Every triangle winds counter-clockwise seen from outside (its geometric normal and its vertex normals agree).
- It is closed: every edge, matched by position, is used by exactly two triangles in opposite directions.

Deliberately broken versions (a torus wound backward, a cone side wound backward, a missing cylinder cap) were checked to fail the
tests.
