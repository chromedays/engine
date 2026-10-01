# Single-player auto-battler: what the engine needs

Status: reference (2026-10-01). Not a spec: each item gets its own spec in `docs/specs/` before it
is built.

## Goal

A single-player game in the style of Mechabellum, and the list of what this engine and app must
gain to make it. Online play is out of scope for now.

## The reference game

Mechabellum (Game River, published by Paradox Arc, released 2024) is a turn-based auto-battler.
This summary is from memory, not checked against the game.

- **Deployment phase**: the player spends supply on units and places them on their half of a grid,
  and picks unit upgrades, tech, specialists and reinforcement cards.
- **Battle phase**: hundreds of units, from swarms of small Crawlers to giant mechs such as the
  Fortress and the Overlord, fight with no player input. Surviving units take HP off the loser's
  base.
- Online 1v1 and 2v2, replays and spectating.

## What single-player changes

| Topic | Effect |
|---|---|
| Networking, matchmaking, spectating | Dropped |
| Determinism | Not needed to check matches, still useful for replays, retries and tests. WebAssembly float math is deterministic within one build, so a fixed tick, seeded random numbers and a stable iteration order are enough; no fixed-point math |
| AI opponent, campaign, difficulty, progression | Become the core: with no human opponent, the fun comes from here |
| Time controls | Free to add: pause, speed-up, slow motion |

## What the engine has today

WebGPU renderer, scene graph, glTF loading, ozz-animation skeletal animation, one shadow map,
MSAA (multisample anti-aliasing), a scene resolution of its own, the ImGui editor, IndexedDB saves,
undo, and the stress scene with its benchmark. Limits: `NV_MAX_NODES` 16384, `NV_MAX_ANIMATORS` 256.
Every mesh node is one draw call, nothing is culled, and there is no audio, particle system or
game UI.

## Features needed

In priority order. "Where" follows the engine/app criterion in `AGENTS.md`: would another app use
it?

| # | Feature | Contents | Where |
|---|---|---|---|
| 1 | Simulation core | Fixed tick; unit stats, targeting, movement, range, projectiles, damage; round state machine (deploy, battle, resolve) | app |
| 2 | Spatial queries | Uniform grid or spatial hash for enemies in range and area damage (separate from picking) | engine |
| 3 | Crowd movement | Flow field with separation and avoidance; the battlefield is flat, so no navmesh | engine |
| 4 | Data-driven definitions | Units, tech, cards and stages in data files, so balancing needs no rebuild | app |
| 5 | AI opponent | Heuristics for deploying, buying and upgrading; parameters per difficulty; counters to the player's deployment | app |
| 6 | Mass rendering | Instanced batches per mesh, frustum culling, LOD (level of detail) | engine |
| 7 | Mass animation | Rigid per-joint animation or VAT (vertex animation textures) for mechs, past the 256-animator limit | engine |
| 8 | VFX (visual effects) | GPU particles (compute shaders), missile trails, beams, additive blending, bloom, decals | engine |
| 9 | Time controls | Pause, 2x and 4x speed, slow motion: the number of ticks per frame changes | app |
| 10 | Deployment and game UI | Grid-snapped placement, drag and rotate, shop, cards, health bars, round results; a game UI apart from the ImGui editor | engine (UI base), app (screens) |
| 11 | Audio | Web Audio API wrapper, positional sound, a cap on simultaneous sounds, music | engine |
| 12 | Campaign and progression | Stage list, win conditions or stars, unit and tech unlocks, difficulty; progress saved with new tags in `app/save.c` | app |
| 13 | Replays and retries | Store the initial state and each round's deployment; replay a lost round with a changed deployment | app |
| 14 | Tutorial | Step-by-step guidance, highlights, limited input | app |
| 15 | Camera and battlefield | RTS (real-time strategy) camera with zoom, pan and bounds; terrain; base objects; following units in battle | engine in part, app |

## Suggested order

1. **Battle prototype** (1, 2, 3, 4): drawn with today's cubes and debug lines. A ctest test checks
   that the same deployment gives the same result hash.
2. **Round loop and AI** (5, 9, the least of 10): this is where the game proves fun or not.
3. **Mass rendering and animation** (6, 7): measured with a stress scene workload of about 1,000
   units and 5,000 projectiles.
4. **Presentation** (8, 11, 15).
5. **Content** (12, 13, 14).

Steps 1 and 2 need nothing new from the engine. Each step's spec compares third-party library
candidates first (spatial hash, flow field, data file format, audio), as `AGENTS.md` requires.
