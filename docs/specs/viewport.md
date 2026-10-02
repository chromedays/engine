# Viewport interaction spec

Status: implemented (2026-09-28). Changes to this spec are agreed first.

## Goal

Work in the scene viewport directly: orbit, zoom and pan the camera, and pick nodes, by mouse or
touch. Rotation becomes editable in the inspector.

## Decisions

| Topic | Decision |
|---|---|
| Orbit | Left drag / one-finger drag |
| Zoom | Wheel / two-finger pinch |
| Pan | Right or middle drag / two-finger drag; moves the orbit point off the focused node |
| Follow | The camera orbits the selection; a per-scene "Camera follows selection" checkbox in the View tab (off by default) turns it on; while off, the camera stays put and panning moves its orbit point |
| Select | Click / tap: a ray from the camera picks the nearest mesh node. Ctrl / Shift+click, or the phone's Multi toggle, add and remove instead (`selection.md`) |
| Deselect | Click / tap where no mesh is hit |
| Characters | Picking a skinned mesh selects its parent, the character root |
| Selection outline | The picked node's mesh boxes, drawn as debug lines (every selected node's since `selection.md`) |
| Rotation | Inspector edits it as Euler angles in degrees (yaw, pitch, roll) |
| Gizmo | A later step: see `gizmo.md` |
| Third-party | None |

## Engine changes

- **Input routing (`nv/imgui.h`).**
  - Since `resolution.md` the scene may be rendered at a resolution of its own and shown in an
    image rectangle inside the viewport (`App.layout.scene`): picking and panning use that image
    (`nv_renderer_view_ray` takes an `NvSceneOutput`), and a tap on the bars around a fixed-size
    image does nothing. Orbit, pan and zoom still work anywhere in the viewport.
  - `NvImgui.view_rect` is the viewport (set each frame by the app). Mouse and touch input that
    starts inside it, while no ImGui popup is open, skips the ImGui gesture handling and fills
    `NvImgui.view` (`NvViewInput`):
    - the orbit drag;
    - the pan drag;
    - a zoom factor (1 = none);
    - a tap position.
  - Everything that starts elsewhere works as before.
- **Mesh bounds (`nv/renderer.h`).**
  - `NvRenderMesh` keeps its local bounding box.
  - Skinned meshes also keep one bind-pose box per joint, around every vertex that joint has weight
    on. `nv_renderer_mesh_bounds` moves those boxes by the current skinning matrices and joins
    them, which bounds the posed mesh. A skinned vertex is a weighted average of its joints'
    moves, so it stays inside that union.
  - Picking and the selection outline use the posed box. (First shipped with the bind-pose box,
    which kept a T-pose width while animated.)
- **Picking (`nv/renderer.h`).**
  - `nv_renderer_view_ray` turns a viewport position into a world ray through the scene's active
    camera.
  - `nv_renderer_pick` returns the nearest mesh node the ray hits, using each node's box in its own
    space.
- **Math (`nv/math.h`).** Transforming a direction, and quaternion ↔ Euler (YXZ order).

## Phases

1. **Camera control:** input routing, then orbit, zoom and pan in the app.
2. **Selection:** mesh bounds, picking, tap to select or deselect, the selection outline.
3. **Rotation editing:** Euler angles in the inspector.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size. Panel input (scrolling, taps, sliders) must keep working.
