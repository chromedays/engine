# Virtual cameras spec

Status: draft (2026-09-30). Changes to this spec are agreed first.

## Goal

Cameras for Play mode that frame the scene by themselves, as Unity's Cinemachine does: many
**virtual cameras** each describe a shot (what to follow, what to look at, how softly), and one
**brain** picks the live one by priority and blends from the previous one. The orbit camera stays
the editor's camera.

- **Edit mode:** the viewport shows the orbit camera, as today. Virtual cameras are nodes that can
  be placed, selected and edited like any other, and one can be previewed.
- **Play mode:** the viewport looks through the brain. A shot change cuts or blends. A drag in the
  viewport steers an orbital camera while it is live.

Out of scope: a timeline or shot sequencer (Unity's Timeline and Sequences), impulse shakes from
events, target groups (framing several nodes at once), state-driven and clear-shot cameras, and
per-pair custom blends. Each can come later on top of this spec.

## How Cinemachine does it

| Part | What it does | Here |
|---|---|---|
| Brain (`CinemachineBrain`) | On the real camera. Picks the active virtual camera with the highest priority (the most recently activated wins a tie), and blends to it with a curve over a time, or cuts | Phase 1 |
| Virtual camera (`CinemachineCamera`) | Renders nothing: holds a shot's settings. Each frame a pipeline computes its pose: Body (position), Aim (rotation), Noise, then Extensions | Phase 1 |
| Body | Follow (an offset from the target, with damping), Orbital Follow (yaw, pitch, radius around the target; the old FreeLook), Third Person Follow, Spline Dolly (a point on a path), Position Composer | Follow and Orbital (1, 2), Spline Dolly (3) |
| Aim | Hard Look At, Rotation Composer (keeps the target inside screen zones, with damping), Pan Tilt | Look At (1), Composer (2) |
| Noise | Perlin noise on position and rotation: a handheld look | Phase 3 |
| Extensions | Deoccluder (moves the camera in front of what hides the target), Confiner (keeps it in a volume), and more | Deoccluder (3) |
| Damping | Framerate-independent exponential decay: a damping time *T* means 99% of the way in *T* seconds | Phase 2 |

## Approaches

| Candidate | What it is (language, license) | Fit | Trade-offs |
|---|---|---|---|
| **Write it ourselves** (recommended) | A small C module: rigs, the brain, damping, a composer, Catmull-Rom paths, 1D gradient noise | C17, fixed tables, no dependency. Each part is short (the damping is one line, the noise about 40, a path about 30) | Ours to write and tune; Cinemachine's behavior is the reference |
| Cinemachine | The real thing (C#, Unity Companion License) | None: C#, and the license allows use only with Unity | Reference only: its documentation and behavior |
| Phantom Camera | Cinemachine-like addon for Godot (GDScript, MIT) | Priorities and tweened blends match ours closely | GDScript tied to Godot's nodes: a port, not a library. A design reference |
| Unreal Gameplay Cameras | Unreal Engine's camera rig system (C++, Unreal EULA) | None: part of Unreal | Reference only |
| stb_perlin.h | Perlin noise (single-header C, public domain or MIT) | Would do the noise | 400 lines for what 1D noise needs in 40 |
| TinySpline | B-splines and NURBS (C, MIT) | Would do the dolly path | Far more than a Catmull-Rom path through a few points |

Recommendation: write it ourselves. No third-party library.

## Where it lives

An engine module, `nv/vcam.h` and `engine/src/vcam.c`, used like `nv/anim.h`: a fixed table of
rigs, and one update over the scene each frame.

- `NvNode.vcam` (`NvVcamId`, 0 = none) marks a node as a virtual camera; its `NvCamera` holds its
  lens (field of view). `nv_vcam_create(scene, node)` adds a rig, `nv_vcam_get(id)` returns it.
- `NvVcamBrain` belongs to a scene: its output node, the live and the previous rig, the blend in
  progress, and the default blend. `nv_vcam_update_scene(scene, brain, viewport, dt)` runs after
  `nv_scene_update` (targets' world matrices are current), computes every active rig, picks the
  live one, blends, and writes the output node's transform and field of view. The output node is
  top-level, so its world matrix is written directly (as `update_camera` does for the orbit).
- The viewport goes in because the composer and the orbital drag work in screen space.

## Rig

Every field is zero-initialized to something usable (a zeroed rig is a still camera where its
node is, looking along -Z).

| Field | Meaning |
|---|---|
| `priority` | s32; the highest active rig is live |
| `active` | b32; an inactive rig is never live (Cinemachine's enabled) |
| `follow`, `look_at` | node ids; 0 = none. A node attached to a joint (`NvNode.attach`) works as a target, e.g. the character's head |
| `body` | `NV_VCAM_BODY_NONE` (the node's own position), `FOLLOW`, `ORBITAL`, `DOLLY` |
| `follow_offset` | Follow: meters from the target, in the target's space or the world's (`follow_world`) |
| `orbit_yaw`, `orbit_pitch`, `orbit_radius` | Orbital: authored start, changed by the drag while playing (not saved while playing) |
| `path` | Dolly: a node whose children are the path's points, in order; the camera takes the point nearest the follow target, or `dolly_position` (0..1) without one |
| `body_damping` | seconds per axis (x, y, z in the camera's space); 0 = none |
| `aim` | `NV_VCAM_AIM_NONE` (the node's own rotation), `LOOK_AT`, `COMPOSER` |
| `screen`, `dead_zone`, `soft_zone` | Composer: where the target sits (-1..1 on each axis) and the zones' sizes |
| `aim_damping` | seconds; how softly the composer turns inside the soft zone |
| `noise_amplitude`, `noise_frequency` | position (meters) and rotation (radians) amplitude, and Hz |
| `deocclude` | b32; move toward the target, in front of the first mesh box the ray from the target hits |

Damping moves a value by `(target - value) * (1 - exp(-dt * 4.605 / T))`: 99% of the way in *T*
seconds at any frame rate, as Cinemachine's damper.

The composer projects the look target into the viewport. Inside the dead zone it does not turn;
between the dead and the soft zone it turns toward the dead zone's edge, damped; at the soft
zone's edge it turns at once, so the target never leaves the soft zone.

## Decisions

| Topic | Decision |
|---|---|
| Edit mode | Rigs are not computed: nothing may move the scene by itself (`play.md`). Virtual camera nodes stay where they are placed |
| Preview in Edit mode | The Inspector's Virtual camera section has **Look through**: the viewport shows that rig's pose computed once, without damping or noise, and without writing its node (a pure function of the targets, so nothing moves). Orbit input is ignored while it is on; a tap elsewhere in the viewport still picks. Esc or the button again ends it |
| Play | The brain's output node becomes `scene->active_camera`. The live rig at Play is shown with no blend. Stop makes the orbit camera active again |
| Free camera while playing | A View tab checkbox: the viewport shows the orbit camera while playing, the brain keeps running (for looking at a shot from outside). Not saved |
| Output node | A new top-level node, **game camera**, with a perspective `NvCamera`. In Edit mode it stays where it is; while playing the brain moves it, and Stop puts it back like any node |
| Priority ties | The most recently activated rig wins, as in Cinemachine |
| Blend | Scene setting: default 2 s, ease in and out (smoothstep); 0 cuts. Position and field of view lerp, rotation slerps. A new live rig during a blend blends from the current blended pose |
| Viewport input while playing | An orbital rig that is live takes the orbit drag and the zoom; otherwise the drag does nothing. Taps still pick |
| Composer guides | While a rig is previewed or live, its dead zone (clear) and soft zone (tinted) are drawn over the viewport, with a View tab checkbox (off by default). A third exception to "editor UI stays in the docks", after the build label and the gizmo |
| Inspector | A **Virtual camera** section on rig nodes: Active, Priority, Body and its fields, Aim and its fields, Noise, Deocclude, Look through. Targets are picked from a combo of node names |
| View tab | A **Cameras** section: every rig with its priority, the live one marked; **Go live** raises its priority above the others (an edit, so lost on Stop while playing); the blend time; Free camera; Guides |
| Showcase | Three rigs under a **cameras** group: **follow cam** (Follow behind the character, Composer on the head, priority 10), **orbit cam** (Orbital around the character, Look At, 5), **dolly cam** (Dolly along four points past the planet, Composer on the character, 0) |
| Save | Rig fields are saved in the node's `NODE` as a `VCAM` container (below); the blend time in `SCNE`. The new nodes change the showcase's layout number, so node edits in older saves are skipped once (`save.md`); the view and settings still load |
| Undo | Rig fields are part of the node scope, so they are undoable with no extra code (`undo.md`). The output node is not driven in Edit mode, so it needs no `driven_fields` entry |
| Tests | Debug builds export `Module._app_debug_vcam_live()` (the live rig's node index, 0 = none), `_app_debug_vcam_blend()` (0..1, 1 = no blend) and `_app_debug_vcam_target_screen(axis)` (the live rig's look target in viewport -1..1) |
| Third-party | None |

### Save tags (`VCAM`, in a node's `NODE`)

| Tag | Type | Saved from | Missing |
|---|---|---|---|
| `VACT` | u32 | `active` | 1 |
| `VPRI` | f32 | `priority` (whole numbers) | as built |
| `VFOL` | u32[] | the follow target's path; empty = none | as built |
| `VLOK` | u32[] | the look target's path; empty = none | as built |
| `VBDY` | u32 | `body`: 0 none, 1 follow, 2 orbital, 3 dolly | as built |
| `VOFS` | f32[3] | `follow_offset` | as built |
| `VOFW` | u32 | `follow_world` | 0 |
| `VORB` | f32[3] | `orbit_yaw`, `orbit_pitch` (radians), `orbit_radius` (meters) | as built |
| `VPTH` | u32[] | the path node's path; empty = none | as built |
| `VDPO` | f32 | `dolly_position` | 0 |
| `VBDA` | f32[3] | `body_damping` | (0, 0, 0) |
| `VAIM` | u32 | `aim`: 0 none, 1 look at, 2 composer | as built |
| `VCMP` | f32[6] | `screen` (x, y), `dead_zone` (w, h), `soft_zone` (w, h) | as built |
| `VADA` | f32 | `aim_damping` | 0 |
| `VNOI` | f32[3] | noise: position amplitude, rotation amplitude, frequency | (0, 0, 0) |
| `VDOC` | u32 | `deocclude` | 0 |

In `SCNE`: `BLEN` (f32), the blend time in seconds, clamped to 0..10 on load; missing: 2.

## Changes

- **Engine.** `nv/vcam.h`, `engine/src/vcam.c`; `NvNode.vcam`; `nv/math.h` gains lerp, quaternion
  slerp and look-rotation, smoothstep, Catmull-Rom and 1D gradient noise (none are there yet).
- **App (`app/main.c`).** The cameras group, the three rigs and the game camera in the showcase;
  `nv_vcam_update_scene` in the frame after `nv_scene_update`, and `active_camera` switched at Play
  and Stop; viewport input routed to a live orbital rig; Look through.
- **UI (`app/ui.c`).** The Inspector's Virtual camera section, the View tab's Cameras section, the
  guides.
- **Save (`app/save.c`).** The tags above. **Specs.** `save.md`, `undo.md`, `play.md`, `layout.md`
  (the guides exception), and `AGENTS.md` once implemented.

## Phases

1. **Brain and hard rigs:** the module, priorities, cut and blend, Follow and Look At without
   damping, the game camera, the showcase's follow cam and orbit cam, Play and Stop switching the
   active camera, Free camera, the View tab section, save tags. Checked: the live rig changes with
   Go live and blends over the set time; Play then Stop gives equal save CRCs.
2. **Soft framing:** damping, the Composer, the Orbital drag while playing, Look through, the
   Inspector section, the guides. Checked: while the character walks (root motion), its head stays
   inside the soft zone; framerate-independent damping (the same pose after 1 s at 30 and 60 Hz,
   within a small tolerance).
3. **Path, noise, deocclusion:** Dolly on a path of child nodes, the dolly cam, noise, deocclude
   against mesh boxes. Checked: with the planet between the camera and the character, deocclude
   moves the camera in front of it.
4. **Edge cases and docs:** a rig's target removed or deactivated; the live rig turned inactive
   during a blend; Go live during a blend; the stress scene while playing and back; the phone
   layout (Inspector section, Look through, orbital drag with one finger, zoom with a pinch);
   `AGENTS.md`, README and the other specs.

Every phase is checked in Release and Debug in headless Chromium, with the mouse at desktop size
and with touch at phone size.

## Open questions

- Should the brain also run in Edit mode with damping off (Cinemachine updates in the editor), so
  virtual camera nodes show their solved pose while targets are edited? That writes their nodes,
  which `play.md` rules out today; Look through covers the need without it.
- Engine module now, or in `app/` until a second user (the stress scene, a game) needs it? The
  coding standard says pull out on the second use; the rigs are engine-shaped (scene nodes, like
  animation), so this draft puts them in the engine from the start.
