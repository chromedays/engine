# Animation spec

Status: implemented (phases 1–4, 2026-09-26). Changes to this spec are agreed first.

## Goal

Skeletal animation for characters loaded from glTF: play clips, blend and crossfade between them,
and control them from ImGui. Everything runs in the browser like the rest of nv.

## Decisions

| Topic | Decision |
|---|---|
| glTF parsing | [cgltf](https://github.com/jkuhlmann/cgltf) (single-header C, MIT), used from C |
| Animation runtime | [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) (C++17, MIT) behind our own C API; the wrapper is the one C++ file of ours (see `docs/CODING_STANDARD.md`) |
| Character | Quaternius Universal Base Characters (Standard, free), CC0: `Superhero_Male_FullBody` only, to start |
| Animations | Quaternius Universal Animation Library (Standard, free), CC0: 8 clips (below) |
| Textures | Base color only, downscaled to 1024 px; normal and roughness maps wait for PBR shading |
| Binary assets | Git LFS (`.gitattributes`), under `assets/` |
| Asset trimming | gltf-transform through `npx` from `tools/trim_assets.sh`; the tool is not added to the repo |

## Source assets

Both packs come from itch.io ("No thanks, just take me to the downloads"):

- <https://quaternius.itch.io/universal-base-characters> →
  `Universal Base Characters[Standard].zip` (122 MB)
- <https://quaternius.itch.io/universal-animation-library> →
  `Universal Animation Library[Standard].zip` (15 MB)

What they contain (checked 2026-09-26):

- **Characters:** `Base Characters/Godot - UE/Superhero_{Male,Female}_FullBody.gltf` (+ `.bin`,
  PNG textures). One skin with **65 joints**; 3 meshes (body, face, eyes) with `JOINTS_0` /
  `WEIGHTS_0`; 3 materials with base color, normal and roughness textures (PNG, 1–4 MB each).
- **Animations:** `Unreal-Godot/UAL1_Standard.glb` (7.6 MB), a mannequin on the same rig with
  **43 clips**. `UAL1_Standard_RM.glb` is the same with root motion baked in.
- **The 65 joint names match exactly** between the characters and the animation file, so clips
  bind to a character by joint name with no retargeting.

Clips in `UAL1_Standard.glb`: A_TPose, Crouch_Fwd_Loop, Crouch_Idle_Loop, Dance_Loop, Death01,
Driving_Loop, Fixing_Kneeling, Hit_Chest, Hit_Head, Idle_Loop, Idle_Talking_Loop, Idle_Torch_Loop,
Interact, Jog_Fwd_Loop, Jump_Land, Jump_Loop, Jump_Start, PickUp_Table, Pistol_Aim_Down,
Pistol_Aim_Neutral, Pistol_Aim_Up, Pistol_Idle_Loop, Pistol_Reload, Pistol_Shoot, Punch_Cross,
Punch_Jab, Push_Loop, Roll, Sitting_Enter, Sitting_Exit, Sitting_Idle_Loop, Sitting_Talking_Loop,
Spell_Simple_Enter, Spell_Simple_Exit, Spell_Simple_Idle_Loop, Spell_Simple_Shoot, Sprint_Loop,
Swim_Fwd_Loop, Swim_Idle_Loop, Sword_Attack, Sword_Idle, Walk_Formal_Loop, Walk_Loop.

### What goes into the repository

The full packs are too large for a web download, so `assets/quaternius/` holds a trimmed selection:

- `character.glb`: `Superhero_Male_FullBody` with its 3 meshes, the skin, and base color textures
  downscaled to 1024 px (normal and roughness textures dropped).
- `clips.glb`: the rig from `UAL1_Standard.glb` with these 8 clips and no mesh:
  - Idle_Loop, Walk_Loop, Jog_Fwd_Loop, Sprint_Loop (locomotion, for crossfades),
  - Jump_Start, Jump_Loop, Jump_Land (a chained move),
  - Dance_Loop (a long loop).
- `LICENSE.txt`: the CC0 text and the source URLs above.

The female character and more clips can be added later the same way; both use the same rig.

Trimming uses [gltf-transform](https://gltf-transform.dev/) (Node CLI, MIT), run with `npx` from
`tools/trim_assets.sh`. The script records which files and clips were kept so the assets can be
rebuilt; the tool itself is not added to the repository, the build or CI.

## Architecture

Three new modules. cgltf stays on the C side, ozz stays inside the C++ wrapper, and the two meet
through plain C structs.

### `nv/renderer.h` (C)

The mesh, material and draw code that lives in `examples/scene` today moves into the engine, now
that glTF loading is a second user of it.

- Mesh registry with two vertex formats: static (position, normal, uv) and skinned (position,
  normal, uv, 4 joint indices, 4 weights).
- Materials: base color factor and texture to start.
- Two pipelines (static, skinned); the renderer owns the depth buffer.
- Per-object data (model matrix, material, offset of its joint matrices) in one storage buffer;
  every character's skinning matrices concatenated in another, uploaded once per frame.

### `nv/gltf.h` (C, cgltf)

- Loads meshes, materials and textures into the renderer.
- Creates a root scene node per character. Joints are not scene nodes; the ozz skeleton owns them.
- Reads the skin (joint list, inverse bind matrices) and fills `NvJointDesc` for the skeleton.
- Reads clips from a clip file and fills `NvTrackDesc`s, matching glTF channels to skeleton joints
  by name.

### `nv/anim.h` (C API) + `engine/src/anim.cpp` (ozz wrapper, C++)

```c
#define NV_MAX_SKELETONS   8
#define NV_MAX_CLIPS       64
#define NV_MAX_ANIMATORS   64
#define NV_MAX_JOINTS      128   // the Quaternius rig has 65
#define NV_MAX_ANIM_LAYERS 4

typedef struct NvSkeletonId { u32 index; } NvSkeletonId; // 0 = none
typedef struct NvClipId     { u32 index; } NvClipId;     // 0 = none
typedef struct NvAnimatorId { u32 index; } NvAnimatorId; // 0 = none

// Inputs, filled on the C side (by the glTF loader).
typedef struct NvJointDesc {
    char name[NV_NODE_NAME_MAX];
    s32 parent;      // -1 for the root
    NvVec3 position; // rest pose
    NvQuat rotation;
    NvVec3 scale;
} NvJointDesc;

typedef struct NvTrackDesc { // keyframes for one joint
    u32 joint;
    u32 translation_count, rotation_count, scale_count;
    const f32 *translation_times, *rotation_times, *scale_times;
    const NvVec3* translations;
    const NvQuat* rotations;
    const NvVec3* scales;
} NvTrackDesc;

// Playback state: plain public structs. Layers blend by weight.
typedef struct NvAnimLayer {
    NvClipId clip;   // 0 = empty layer
    f32 time;
    f32 speed;
    f32 weight;
    b32 loop;
} NvAnimLayer;

typedef struct NvAnimator {
    NvSkeletonId skeleton;
    NvAnimLayer layers[NV_MAX_ANIM_LAYERS];
    f32 fade_duration;
    f32 fade_elapsed;
    NvMat4* joint_model; // [joint count] model-space joint matrices, written by nv_anim_update
} NvAnimator;

void         nv_anim_init(NvArena* permanent); // routes ozz allocations to the arena
NvSkeletonId nv_anim_create_skeleton(const NvJointDesc* joints, u32 joint_count);
NvClipId     nv_anim_create_clip(NvSkeletonId skeleton, const char* name, f32 duration,
                                 const NvTrackDesc* tracks, u32 track_count);
NvAnimatorId nv_anim_create_animator(NvSkeletonId skeleton);
NvAnimator*  nv_anim_get(NvAnimatorId id);
void nv_anim_play(NvAnimator* animator, NvClipId clip, f32 fade_seconds); // crossfades
void nv_anim_update(NvAnimator* animator, f32 dt); // advance, sample, blend, local-to-model
s32  nv_anim_find_joint(NvSkeletonId skeleton, const char* name);        // for attachments
```

- ozz `Skeleton` / `Animation` objects and sampling contexts live in fixed tables inside the
  wrapper; C code only holds ids.
- ozz data is built at runtime from the C descriptions (`RawSkeleton` / `RawAnimation` and their
  builders), which avoids a native `gltf2ozz` step in the web build. If load time becomes a
  problem, switch to offline conversion to `.ozz` files (already tracked by LFS).
- ozz's allocator is routed to the permanent arena; frees are ignored because everything is
  allocated once at load.

### Scene integration

- `NvNode` gets an `NvAnimatorId animator` component (0 = none).
- Skinning matrix per joint = node world × joint model matrix × inverse bind matrix, computed by
  the renderer.
- Frame order: update animators → `nv_scene_update` → render (upload skinning matrices, draw).

### Asset delivery on the web

Assets are packaged with Emscripten's `--preload-file` and read with plain `fopen`. The package
downloads before `main` runs.

## Phases

1. **Renderer and static glTF.** Extract `nv/renderer.h`, move `examples/scene` onto it, load the
   character with cgltf and draw it in its bind pose.
2. **ozz wrapper and skinning.** Build the skeleton and clips, play one clip, skin on the GPU. The
   character walks.
3. **Blending and tools.** Crossfades and layer weights; ImGui panel for the clip list, speed,
   fade time and weights; a bone debug view.
4. **Optional.** Joint attachments, root motion (ozz motion extraction, the `_RM` clips), IK (ozz
   two-bone and aim IK).

Each phase is checked in headless Chromium (captures a few moments apart to see the pose change)
in both Release and Debug builds, and deployed.

## Implementation notes

What shipped, and where it differs from the plan above:

- **Assets:** `clips_rm.glb` holds the root-motion versions of Walk_Loop, Jog_Fwd_Loop and
  Sprint_Loop alongside the 8 clips in `clips.glb`. Totals: character 685 KB, clips 439 KB,
  root-motion clips 166 KB.
- **Textures** are decoded by the browser (`createImageBitmap`) rather than a C image library, and
  get mipmaps from a box filter at load.
- **Renderer:** skinned draws read their matrices from `nv_anim_skins()`, indexed by
  `NvNode.animator`; a skinned node without an animator draws in its bind pose.
- **Root motion** uses ozz's `MotionExtractor` (horizontal root translation of the `root` joint)
  and a `Float3Track`; `nv_anim_update` accumulates the model-space motion in
  `NvAnimator.root_motion`. Since the app spec (`docs/specs/app.md`), `nv_anim_update_scene`
  moves the animator's `owner` node by it; the app only adds turning.
- **Crossfades** use layers 0 (fading in) and 1 (fading out); the app's manual blend uses
  layer 2, time-synchronized to layer 0.
- **Aim IK** corrects one joint (`Head`) with ozz's `IKAimJob`. The joint's forward and up axes are
  found from the rest pose instead of being hard-coded.
- **Attachment:** the sword's offset from `hand_r` is computed once from the rest pose ("grip in the
  fist, blade forward"). The sword node's `attach` component keeps it on the animated hand.
- **Memory:** ozz allocates from its own arena (about 1 MB after loading everything).

## Resolved questions

- Asset trimming: gltf-transform through `npx`, from a kept script (`tools/trim_assets.sh`).
- Character: the male character only, to start.
- Clips: the 8 listed above.
- Textures: base color only, 1024 px.
