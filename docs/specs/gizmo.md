# Transform gizmo spec

Status: draft (2026-09-28). Changes to this spec are agreed first.

## Goal

Move, rotate and scale the selected node directly in the scene viewport, by mouse or touch,
instead of typing numbers into the inspector. Follows the viewport spec (`viewport.md`), which
named ImGuizmo as the candidate for this step.

## Third-party candidates

| Candidate | What it is | Language, license | Fit | Trade-offs |
|---|---|---|---|---|
| **ImGuizmo** through **cimguizmo** (recommended) | Translate, rotate and scale gizmos drawn with ImGui draw lists; input from ImGui IO | C++ with a generated C API, MIT | We already use Dear ImGui through cimgui. It needs no renderer work, and cimguizmo keeps us at one C++ file (`anim.cpp`) | Built for the mouse, so touch needs our input routing (below) and bigger handles. cimguizmo must build against our Dear ImGui 1.92.9b |
| im3d | Immediate-mode 3D gizmos and debug drawing that outputs vertices | C++, MIT | Renderer agnostic | We would render its triangles (a new pipeline) and wrap its C++ API ourselves (a second C++ file) |
| Write it ourselves | Arrows, rings and boxes hit by `nv_renderer_view_ray`, drawn as debug lines or with the ImGui draw list | C | Full control of touch behaviour and no dependency | Roughly 800+ lines to reach ImGuizmo's quality (rotation rings, plane handles, snapping); debug lines are 1 px wide |

Recommendation: ImGuizmo through cimguizmo. If cimguizmo does not build against our Dear ImGui
version, we fall back to writing it ourselves rather than adding a second C++ file.

## Decisions

| Topic | Decision |
|---|---|
| Operations | Translate, rotate, scale; one at a time |
| Space | World or local, for translate and rotate (scale is always local) |
| Snap | Off by default; when on: 0.5 m, 15°, 0.1 |
| Controls | In the Inspector, above the transform fields: an operation radio (Move / Rotate / Scale), a Local checkbox and a Snap checkbox. On desktop, W / E / R also switch the operation while the viewport has the pointer |
| Which nodes | The selected node, unless it is the active camera (the orbit camera owns it). Characters work: root motion keeps adding to the moved position |
| Parents and joints | The gizmo edits the world matrix. The result goes back to `local` through the inverse of parent world × `attach.joint_model` |
| Write-back | Our own `nv_mat4_decompose` (translation, quaternion, scale), not ImGuizmo's Euler-degree decomposition. Shear from non-uniform scale under a rotated parent is dropped |
| Viewport UI | The gizmo draws in the viewport; it is the second exception to "editor UI stays in the panel", after the build label |
| Touch | Handle sizes scale with `NvImgui.ui_scale` |
| Undo | Not now |

## Engine changes

- **Input routing (`nv/imgui.h`).** Today every press that starts in `view_rect` goes to
  `NvImgui.view`, so ImGui never sees it. A new hook, `NvImgui.view_grab`, is a function that the
  app sets. On mouse down or touch start in the viewport, the engine moves ImGui's mouse there and
  asks the hook whether that point is on a gizmo handle (the app calls ImGuizmo's `IsOver`).
  - When the hook says yes, the press goes to ImGui as a plain left button: no orbit, no pick, and
    for touch no scroll-or-press wait. It stays with ImGui until it is released.
  - Otherwise the press goes to `NvImgui.view` as today.
- **Math (`nv/math.h`).**
  - `nv_mat4_decompose` returns translation, rotation (quaternion) and scale.
  - `nv_mat4_inverse` returns the general inverse, unless the renderer already has one to move
    here.
- **Build.** cimguizmo is fetched like cimgui and built into the `cimgui` library, so it shares
  that library's ImGui context.

## App changes

- After `update_camera`, the app passes the camera's view and projection matrices and the
  viewport rect (in ImGui coordinates) to ImGuizmo, then calls `Manipulate` on the selected node's
  world matrix. When the gizmo reports a change, the app writes it back to `local`.
- `pick` skips a tap that the gizmo used.
- Risk: our projection maps depth to 0..1 (WebGPU), while ImGuizmo expects OpenGL's -1..1. Only
  depth differs, and the gizmo uses x and y; phase 1 checks this.

## Phases

1. **Move:** the dependency, input routing, the translate gizmo in world space, and write-back
   through parents and joints.
2. **Rotate and scale:** both operations, local and world space, snapping, the Inspector controls
   and W / E / R.
3. **Touch and docs:** handle sizes, phone-size checks, `AGENTS.md` and the Dependencies section
   of `docs/CODING_STANDARD.md`.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size. Orbit, pan, zoom, picking and panel input must keep working.
