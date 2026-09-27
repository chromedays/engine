# One app, one scene spec

Status: draft (2026-09-27). Changes to this spec are agreed first.

## Goal

Replace the three examples with one app that shows one scene: the scene example's planet and
moon and the character example's animated character, sword and target, side by side. The editor
panel shows the node tree and the selected node's components. Animation becomes part of the
scene, so the app no longer moves nodes by hand to follow the animation.

## Decisions

| Topic | Decision |
|---|---|
| Examples | `triangle` is deleted. `scene` and `character` merge into one app |
| Location | `app/main.c` (scene setup, frame) and `app/ui.c` (editor panel); `examples/` is removed |
| Address | Pages root, `https://chromedays.github.io/engine/`; `web/landing.html` is removed |
| Old addresses | `/engine/character/`, `/engine/scene/` and `/engine/triangle/` become tiny pages that redirect to `/engine/` |
| Assets | One preloaded `app.data` with `assets/quaternius/` (1.3 MB) |
| Animation in the scene | New `attach` node component, animator `owner`, look-at `target_node`, and `nv_anim_update_scene` (below) |
| glTF loading | The loader creates the skeleton and animator when the file has a skin |
| `scene.c` | Still knows nothing about anim or the renderer; it only multiplies by a matrix anim writes |
| Third-party | None needed |

## Structure changes

`+` is added, `-` removed, unmarked lines stay.

```diff
 NvScene  (4096 slots)
 ├─ nodes[1..]    NvNode
 │   ├─ parent / first_child / next_sibling
 │   ├─ position, rotation, scale
 │   ├─ world     ← nv_scene_update
+│   │              (× attach.joint_model if attached)
 │   └─ components (0 = none)
 │       ├─ mesh, material ─────────► NvRenderer
 │       ├─ animator NvAnimatorId ────► nv_anim
+│       ├─ attach   NvJointAttach
+│       │    ├─ animator ───────────► nv_anim
+│       │    ├─ joint
+│       │    └─ joint_model  ◄── written by anim
 │       ├─ camera
 │       └─ light
 └─ active_camera
```

```diff
 nv_anim
 └─ animators[64] NvAnimator
     ├─ skeleton
+    ├─ owner  NvNodeId ──► e.g. the character root
     ├─ layers[4]
     ├─ look_at
+    │   └─ target_node NvNodeId (0 = use target)
     ├─ root_motion
     └─ joint_model[]

 NvGltfModel
 ├─ root, mesh_nodes[]
 ├─ joints, inverse_bind
+├─ skeleton   (created by the loader)
+└─ animator   (created by the loader, owner = root)
```

```diff
 Frame
 1  app: play / blend / demo motion ─► NvAnimator
-2  app: nv_anim_update(animator)
-3  app: apply_root_motion ─► root node
-4  app: update_sword ─► sword node
-5  app: update_look_at ─► look_at.target
+2  nv_anim_update_scene(scene, dt)
+     ├─ look_at.target_node ─► look_at.target
+     ├─ every animator ─► joint_model[]
+     ├─ root_motion ─► owner node
+     └─ every attach ─► attach.joint_model
 6  nv_scene_update ─► world
 7  nv_renderer_draw(scene, skins)
```

```diff
-scene example          character example
-├─ camera              ├─ camera
-├─ sun                 ├─ sun
-├─ ground              ├─ ground
-└─ planet              ├─ character
-   └─ moon             │  ├─ mesh ×3  animator=#1
-                       │  └─ sword    (app moves it)
-                       └─ target
+one scene
+├─ camera
+├─ sun
+├─ ground
+├─ planet
+│  └─ moon
+├─ character   owner of animator #1
+│  ├─ mesh ×3  animator = #1
+│  └─ sword    attach = {#1, hand_r}
+└─ target      look_at.target_node of #1
```

## Engine API changes

- **`nv/scene.h`**
  - `NvJointAttach { NvAnimatorId animator; u32 joint; NvMat4 joint_model; }` and
    `NvNode.attach`.
  - An attached node's parent should be its animator's owner. `nv_scene_update` computes
    `world = parent.world × attach.joint_model × local`. Nothing else in `scene.c` changes.
- **`nv/anim.h`**
  - `NvAnimator.owner` (`NvNodeId`) and `NvLookAt.target_node` (`NvNodeId`).
  - `nv_anim_create_animator(skeleton, owner)` replaces `nv_anim_create_animator(skeleton)`.
  - `nv_anim_update_scene(NvScene* scene, f32 dt)`. For every animator:
    - It turns `look_at.target_node`'s world position into the owner's model space. It uses
      last frame's world matrices, so it lags one frame, which is not visible.
    - It calls `nv_anim_update`.
    - It moves the owner by `root_motion` (rotated by the owner's rotation), then clears it.
  - After that, it copies `joint_model[joint]` into every node's `attach`.
  - `nv_anim_update` stays for callers that do not use a scene.
  - `nv_anim_clip_skeleton(NvClipId)` and `nv_anim_clip_count()`, so UI can list a skeleton's
    clips without the app keeping its own table.
- **`nv/gltf.h`**
  - `NvGltfModel.skeleton` and `NvGltfModel.animator`. When the file has a skin, the loader
    creates both, with `owner` set to the model root, and sets `animator` on every skinned mesh
    node.

## App

- **Scene layout:**
  - The character stands at the origin, and the planet and moon sit about 3 m to one side.
  - The camera orbits a focus node: the selected node, or the character when nothing is selected.
    It keeps following the character's root motion.
- **Behavior that stays in the app:**
  - The jump chain (Jump_Start → Jump_Loop → Jump_Land).
  - The blend helper.
  - Choosing root-motion or in-place clips.
  - The turn rate.
  - Moving the look-at target.
  - The planet's orbit.
  - The bone overlay.
- **Editor panel (`nv_imgui_begin_panel`), tabs:**
  - **Scene:** the node tree; tapping a node selects it.
  - **Inspector:** the selected node.
    - Its name and transform, as today.
    - One section per component it has: Mesh (material color), Camera, Light (color,
      intensity), Attach (joint).
    - Animator: today's character controls, which are clips, Jump, speed, fade, blend, layer
      bars, root motion, turn and look at.
  - **View:** FPS, camera yaw and distance, show bones, and planet orbit speed.

## Build and deploy

- `app/CMakeLists.txt`: `add_executable(app main.c ui.c)` and
  `nv_setup_executable(app ASSETS assets/quaternius)`.
- `nv_setup_executable` gains an option to install at the package root instead of a subfolder.
- `web/redirect.html.in` produces the three old-address pages.
- CI keeps the same steps; only the install layout changes. The `cp web/landing.html` step goes.

## Phases

1. **Engine:** `attach`, `owner`, `target_node`, `nv_anim_update_scene`, clip queries, and the
   loader creating animators. Port the character example to them, with its behavior unchanged,
   to check the engine change on its own.
2. **App:**
   - Create `app/` with the merged scene and the Scene / Inspector / View panel.
   - Delete `examples/`.
3. **Deploy and docs:**
   - Install at the root, add the old-address redirects and update CI.
   - Update `AGENTS.md`, `README.md` and the implementation notes in `docs/specs/animation.md`.

Every phase is checked in Release and Debug in headless Chromium at desktop and phone size:

- all clips, jump, blend, sword, root motion and look at still work;
- the planet orbits;
- selecting nodes and editing transforms works;
- the panel scrolls by touch.

## Open questions

1. Directory and target name: `app` (proposed) or something else?
2. Keep redirect pages for the old addresses (proposed), or drop them?
