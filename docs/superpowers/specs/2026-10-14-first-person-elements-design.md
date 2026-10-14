# First-person element control, Jungle Ruins map, motion / physics / materials pass — design

Date: 2026-10-14 · Branch: claude/loving-dirac-0gtvnt · Engine: Unreal 5.8.3 (unreal/) · Status: approved in chat, awaiting spec review

IP rule (absolute, NEXT_SESSION.md): 100 % original. The well-known animated elemental martial-arts series is a *feel*
reference only. No franchise names or terms (including the series' word for element control) in code, assets, docs,
UI or commits. Words used here: *element control*, *raise*, *hold*, *steer*, *release*, *guide*.

## 1. Goal and owner decisions

The owner wants to feel physically connected to the elements: raise matter with the body, hold it at the hands, steer it,
throw it, and see it react with weight — in first person — with super nice motion, physics and materials.

Decisions taken in the 2026-10-14 brainstorm:

| # | Question | Decision |
|---|---|---|
| D1 | First person vs third person | First person becomes the default way to play (Lab, duels, open world, new map). One key / HUD button toggles back to the existing third-person camera. Nothing existing is removed. |
| D2 | Device priority | **iPad touch first** (Paddycaken, iPad Pro 13 M5, cabled and available). Mac mouse + keys in the same design, tuned second. |
| D3 | Map | **Intel Sample Library "Jungle Ruins"** (CC BY 4.0, JungleRuins_1_0_1b.zip, 4.25 GB, direct download, no login). Credit: Cristiano Siqueira, Charlene Teets / Intel Sample Library. |
| D4 | Architecture | **Approach A, sim-driven** ("best quality, most crisp"): look / steer / momentum enter the deterministic 60 Hz sim as new default-off inputs, so what you steer is exactly what hits. |
| D5 | Scope | Four sub-projects, one program: S1 first-person core, S2 element feel, S3 Jungle Ruins map, S4 motion / physics / materials polish. |

Non-goals: VR; replacing the counter matrix or move set; new moves (the 160 existing moves are made first-person
capable); Godot parity for the new first-person paths (they are Unreal-only additive features, labelled like Grand,
leap / cloud step and the open world, each with its own CoreTests).

## 2. Constraints that bind every part

- `FourfoldCore/Public/ff/*.h` are FROZEN: add fields and methods only, never change existing declarations.
  `Session::Step(const InputFrame&, float camera_yaw)` stays; first-person data rides in new `InputFrame` fields.
- Every new field defaults to legacy behaviour. With first person off, every existing test (551 CoreTests incl. the
  13,300-row golden matrix, 68 game logic tests, 50 FX logic tests, the FX soak) must stay bit-identical.
- Determinism: same seed + inputs → same state hash; float32 `Vec3` / double scalar split and operation order kept;
  no NaN control flow; deterministic tie-breaks (actor / body id) in every new ray or pick.
- Ledgers: steering, guidance and momentum only redirect or rescale velocity; any heat / mass change goes through
  `heat_body` / `decay_body` / `split_body` / `merge_bodies`.
- Logic islands (`Fourfold/Private/Logic`, `FourfoldFX/Private/Logic`) stay engine-free and unit-tested
  (-Werror -Wshadow -Wconversion, UE macro poison).
- Assets are produced by scripts and never hand-edited; large downloads live in git-ignored folders; licences are
  recorded (new `unreal/docs/CREDITS.md`).
- iPad: Mobile forward renderer, no Lumen / Nanite / VSM; 60 fps target. Mac: desktop renderer, 60 fps at 1600x900.
- FX GPU budget ≤ 2 ms on Mac; ≤ 4 shadowless FX lights; ≤ 32 particles per one-shot; quality 0 = no Niagara.
- Feel changes are logged in `docs/TUNING_LOG.md`.
- Shared checkout protocol (memory: parallel-sessions-coordination): before any Unreal build / run check
  `pgrep -fl "UnrealEditor|UnrealBuildTool"`; commit own paths with `git commit -o -- <paths>`.

## 3. S1 — First-person core

### 3.1 Sim (FourfoldCore), Unreal-only, default off

New input (`ff/Input.h`, additive):

```cpp
Vec3 look_dir{0.f, 0.f, 1.f};  // unit view forward in sim space (incl. pitch)
bool look_active = false;       // true only while the local player plays in first person
Vec2 steer{};                   // control-window offset of the held element, each axis in [-1, 1] (x right, y up)
bool steer_active = false;      // a held element is being steered this tick
```

`MergeFrom` takes look / steer from whichever frame has the flag set. `ClearEdges` is unchanged. The UE producer
quantises `look_dir` to 1e-4 for replay stability.

Intent / state (`Private/Sim/ActorState.h`): `ActorIntent` gains `aim3`, `aim3_active`, `steer`, `steer_active`;
`ActorState` gains `look`, `look_active`, `steer`, `swing_vel`, `aim3_point` and `eye()` = pos + 1.62 m up.
`PlayerController::build` fills them only when `look_active`; the AI path (`frame_from_intent`) is untouched.

Behaviour when the owner has `look_active` (one choke point each):

| Topic | Rule | Site |
|---|---|---|
| Facing | flat(look) unless the running action sets a `face` override, so the body strafes under the view | `CombatWorldMove.cpp` `_move_actor` facing |
| Aim direction | `aim_dir` stays flat = flat(look) (cones, `find_body`, `actors_in_cone` keep their flat dot rules) | `aim_dir` |
| Aim point | 3D ray from `eye()` along `look`: arena boxes (`segment_hit`), terrain (heightfield march), actor capsules; first hit within 30 m and inside the bubble, else the 30 m point | `aim_point`, new `ArenaMap::ray_hit` |
| Aim assist | if the lock target's chest is within 6° of the ray, the aim point bends to it (magnetism); never more | same |
| Lock target | the enemy closest to the crosshair within 25° (ties by id); otherwise keep the current one while it stays within 40°; otherwise none (modules already fall back to `forward()`) | `_auto_target` / `_process_intent` |
| Launch | `target_point` / `_throw_target` return `aim3_point`; new `launch_vel3` keeps the move's total speed (not horizontal speed) so steep aims do not hit the 30 m/s clamp | `Verbs.cpp`, `ActEarth.cpp` |
| Held element | target = `eye()` + view-rotated hand anchor (0.62 m forward, 0.28 m down, centre) + steer offset (±40° yaw, ±25° pitch around the ray at the hold distance); critically damped spring with half-life `h = 0.07 s · sqrt(m / 5 kg)`, clamped 0.04–0.25 s, acceleration-limited; `swing_vel` = smoothed anchor velocity | `CombatWorldBodies.cpp` held block |
| Release | launch velocity + 0.25 · `swing_vel` (|v| ≤ 30) | `release_body` |
| Guide | while `residual_authority > 0`, the body's velocity rotates toward `aim3_point` at up to 240°/s · authority (speed preserved); waves steer toward the flat aim point with their existing per-move rates | new `_guide` next to `_home`; `_update_wave` |
| Source spawn | moves that hold their element (grip / seize / draw / charge) spawn the body at its source — earth at the ground point under the reticle (≤ 3 m ahead), water at the nearest water cell — and the hold spring carries it to the hands | `ActEarth`, `ActWater`, `VerbGrip` hold paths |

Snapshot (`ff/Snapshot.h`, additive): `ActorView.look`, `look_active`, `aim3_point`, `hold_anchor`;
`BodyView.guided`.

Tests: new `unreal/CoreTests/tests/test_first_person.cpp` — legacy equivalence (flag off → identical state hash over a
soak), pitch shot reaches a ledge target, crosshair lock pick, steer moves a held body inside the window, heavier body
lags more, release adds swing momentum, guide curves a shot only inside the residual window, ledgers balance,
determinism (two runs, same hash). The whole suite (551 + new) must pass.

### 3.2 Camera (game module)

- `ffg::CameraLogic` gets a `first_person` mode (engine-free, unit-tested in `logic_tests.cpp`):
  eye = interpolated player position + 1.62 m, stabilised (not locked to the head bone); yaw / pitch from look input,
  pitch ±80°; procedural layer: gait bob (≤ 1.2 cm vertical, phase from the gait cycle), landing dip spring
  (≤ 6 cm), stance dip while charging (3–5 cm by tier), stomp dip 4–6 cm; base horizontal FOV 90° (setting),
  release FOV punch +3–5° over 0.08 s, back over 0.25 s; camera pitch kick 0.5 / 1 / 1.5 / 2° by tier, spring
  half-life 0.06 s; trauma shake scaled ×0.35 with no roll; reduced-motion setting removes bob, punch and kick.
- Near clip 5 cm. No see-through list and no boom collision in first person.
- Knockdown, ragdoll, get-up and being grabbed: the camera eases out to a 2.2 m over-shoulder view and back when
  control returns (anti-sickness).
- `AFourfoldCameraRig` places the UE camera from the logic; HUD projection keeps using the rig camera.
- Toggle: `V` (desktop), a HUD button (touch); the choice is saved in settings (`CameraMode`). First person is the
  default.

### 3.3 Input

Touch (iPad first; extends `ffg::TouchControls`, unit-tested):
- Left half: floating move stick (unchanged). Right half: one-finger drag = look (unchanged gain in rad/mm), optional
  gyro look only while a right-half finger is down.
- ATTACK pad: a quick flick from touch-down (≥ 6 mm in 0.25 s) = thrust / ground / sweep exactly as today; a tap =
  strike; a hold ≥ 0.18 s = raise / charge; while held, the thumb's offset from its press point (15 mm = full
  window) is `steer`; pushing past the window edge turns the view at up to 2.4 rad/s; release = launch at the
  crosshair, and the thumb velocity over the last 100 ms becomes extra swing momentum (a flick curves the throw).
- GUARD, TECH, element petals, LEAP, EVADE unchanged.

Desktop: mouse captured in play; mouse = look; LMB tap strike / hold raise; while LMB held the mouse moves `steer`
(window edge turns the view); release flick from mouse velocity; RMB tech; existing keys unchanged; `V` toggles.

### 3.4 Body and arms

- The local player's full MetaHuman body stays visible; the face part and grooms stop rendering in the main pass
  (shadows kept), so you see arms, chest and legs on stomps and kicks. The rival is unchanged.
- New arm-IK step in `FFourfoldAnimProxy::Procedural` (cache lowerarm / hand / twist bones, `ffg::SolveTwoBone` with an
  elbow pole, roll spread to the twist bones). The engine-free `AnimDirector` decides per phase: startup reaches to the
  source, charge / hold cups around the element (water, air: palms facing; earth: palms under; fire: fists), active
  pushes along the aim, recovery fades. Spine and arms follow view pitch. Hold poses are lifted so the hands sit
  25–35 cm below the eye, inside the frame.
- Hand tension blends relaxed → formed shape with charge.

### 3.5 HUD

Centre dot; it becomes a ring when locked; a faint control-window frame shows while steering; the counter strip and
callouts stay; the player's own ground arcs / charge rims are hidden in first person (the hands show the charge).

## 4. S2 — Elements in first person

Same 160 moves, made physical and close-up. Presentation reads only snapshot state and events (FX rule unchanged).

- FX frame gains a *viewer actor id*: head-pair lightning arcs, pelvis status shells, pelvis / feet flames and the cast
  ring billboard are suppressed or replaced for the first-person viewer.
- Water: draw from river, lake or puddle — a tube peels off the surface and flies to the hands; held water stays a
  ribbon (no Blob swap) rooted at the palms, 16 Catmull-Rom-subdivided nodes, lag rising from head to tail; the whip
  cracks at peak speed; jets start at the hands; freezing spreads from the hands; spilled water persists as puddles.
- Earth: the stomp dips the camera with 2 frames of hit-stop; the block tears out of the ground at the reticle and
  leaves a scar; a held rock bobs ±2 cm on its stiff spring and sheds dust; the punch launches it tumbling (spin from
  velocity); impacts fracture into Chaos debris with dust trails.
- Fire: inhale sound, embers gathering at the fists and a real (shadowless) light on the hands; alternating fist jets;
  palm streams; kick arcs; heat haze on the Mac high tier; lightning charges as arcs between the fingers.
- Air: gathering swirl at the palms pulls dust and leaves inward; gusts show through grass, trees (MPC wind), dust and
  water ripples; an air ball between the hands.
- Shared: per-stage sounds (raise, hold loop that tracks speed, release, impact); iPad haptics (continuous hold texture,
  transients on release and impact, maximum only for T3); hit-stop freezes the element and its target only; hands react
  (wet sheen, frost, soot, dust) through the status materials (S4).

## 5. S3 — Jungle Ruins map (L_Ruins)

Pipeline (all scripted, regenerable):

1. `unreal/Tools/ruins/fetch_ruins.py` downloads the zip into `unreal/SourceArt/OpenWorld/JungleRuins/` (git-ignored),
   verifies size, extracts only the needed USD / textures, copies `credits_license.txt`, and writes `ruins.json`
   (source URL, licence, credit, file list).
2. `unreal/Tools/blender/ruins/prep_ruins.py` (Blender 5.0 headless, `--disable-autoexec`): opens the USD, cuts a
   playable core of about 250 x 250 m around the pyramid, river and creek from the cinematic terrain tile, splits the
   terrain into static-mesh chunks with LODs, gives the pyramid / ruin stonework LODs, re-scatters a thinned jungle at
   iPad-safe density (instances + LODs), bakes textures to 2k (4k for hero stone on Mac), exports FBX + `layout.json`.
   The exact core rectangle is chosen from the USD layout and recorded in the script.
3. Setup part `ruins` (`unreal/Content/Python/fourfold/ruins/`): imports via Interchange, builds materials on our
   masters, places HISMs, river / creek surfaces with a flowing water material, our lighting rig tuned for a humid
   jungle (sun through canopy, height fog, sky light), writes `/Game/Fourfold/Maps/L_Ruins`.
4. Bake (`unreal/Tools/ruins/bake_ruins.py`, run from the editor after import): line traces on a 2 m grid → heightfield,
   tagged blockers (pyramid steps / walls, big trunks) → solid boxes, river / creek → water-height layer, spawn,
   2–3 encounter sites, a shrine → `unreal/Content/Fourfold/Data/ruins/world.json` + `heights.f32` + `water.f32`.

Core additions (with tests): per-map world dataset (the open-world actor takes a data directory; the static cache keys
on it), configurable world-edge margin, and an optional per-cell water-height layer (`WorldDef`) so the sloped river is
real water — wading depth, water draw sources and splashes come from it; the single-level lake remains the default.

L_Ruins becomes the start map (`GameDefaultMap`); L_World and L_Lab stay. Credits: `unreal/docs/CREDITS.md` and the
in-game credits line. Targets: Mac 60 fps at 1600x900; iPad 60 fps measured on device (CSV via `ios.sh`).

## 6. S4 — Motion, physics and materials polish

Motion:
- Fix the C++ movement-constant drift (`CombatWorld.h`: TURN_RATE 14 → 9, RUN_SPEED 5.5 → 5.0, RUN_MIN 4.0 → 3.6,
  WALK_MAX 1.8 → 1.6, matching Godot and `sim.json`), re-run kit tests and re-tune any test that encoded the drift.
- First-person locomotion: view-driven facing with 8-direction walk / run (import about 12 more Game Animation Sample
  clips already on disk via `animation/mocap.py`, add `LocoRole` diagonals and run strafes); feet stay planted through
  element stances (revisit `lock_weight` for held / charge phases).

Physics (only bodies owned by a first-person actor, tagged at spawn, so legacy tests stay identical):
- held mass inertia and swing momentum (S1); air drag on light matter (water, fire, air, sand); small bounce
  (restitution 0.25) and tumble for rock chunks.
- Visual: debris dust trails, directional fracture from the hit direction, droplets shed from fast water.

Materials:
- FX light direction from a map-fed parameter (replace the hard-coded Lab sun in `FFLighting.ush`).
- Mac high tier: water refraction (scene colour + normal offset) and depth fade, soft particles (`FFSoftFade`),
  heat haze on fire; the dead `softParticles` / `distortion` quality flags are wired. iPad keeps the fake-lit path.
- Hero rocks: 4k textures + detail normal; fresh-cut faces kept.
- Status materials on the MetaHuman: wet / frost / burn / glow parameters on the body, pants and sash material
  instances, so your visible arms react.
- Map materials: wet stone, moss, river water with flow and foam.

Sound and haptics: per-stage layers from the synth pipeline (`Tools/audio`), iOS haptics through the engine's
force-feedback path.

## 7. Order of work (milestones)

| M | Content | Proof |
|---|---|---|
| M1 | S1 sim fields + behaviour + `test_first_person.cpp` | CoreTests green, legacy hash identical |
| M2 | Camera, input (touch + desktop), toggle, HUD dot, head hidden | logic tests; Mac capture in L_Lab / L_World; iPad deploy |
| M3 | Arm IK + held poses + FX from the hands + viewer suppression | logic + FX tests; close-up captures per element |
| M4 | S3 map: fetch → Blender prep → import → bake → water layer → lighting → default map | CoreTests (water layer), captures, iPad CSV |
| M5 | S2 element feel + S4 physics, motion and materials | tests; per-element captures; Mac GPU CSV |
| M6 | Sound, haptics, perf pass, docs (`NEXT_SESSION.md`, `CREDITS.md`, `TUNING_LOG.md`, stream PROGRESS files) | owner plays on iPad |

M1 and the offline part of M4 (download, Blender prep) can run in parallel; Unreal builds and runs are serialised.

## 8. Risks

- Disk: about 29 GB free; the zip (4.25 GB) plus extraction and cooked content are tight. Extract selectively, delete
  the zip after extraction is verified.
- Jungle Ruins is research-scale (8K terrain, over a trillion instanced triangles, GScatter in the .blend). Only the
  USD core is used; trees are re-scattered at mobile density.
- Motion sickness: stabilised eye, no roll, reduced-motion setting, over-shoulder fallback on knockdowns.
- Touch conflicts (look vs steer vs flick): steer lives on the ATTACK thumb only; look stays on the right-half drag.
- First-person rendering bug UE-250355 (first-person scale missing from the depth prepass): we do not use first-person
  scale; the body is the real world-space body.
- Balance: crosshair aiming makes hitting a moving rival harder; the 6° magnetism and the 25° lock cone are the tuning
  knobs (logged in TUNING_LOG).
- 3D speeds change live interaction power (K = mass·|v|/20) for first-person throws only; the golden matrix is
  unaffected (it never reads intents).
