# Fighter rig and animation (`fighter.glb`)

One articulated training fighter (about 1.75 m, stylised, low poly) with 47 in-place animation clips, generated
entirely by script from an empty Blender scene. Gameplay code moves the character; the animation is presentation only.

Original design: wrap top with a V collar and cloth sash, loose trousers tucked into wrapped shins, wrapped
forearms and hands, soft shoes, short hair cap. Nothing here references any existing franchise.

## Files

| file | what |
|---|---|
| `tools/blender/build_fighter.py` | entry point: builds everything below from a factory-empty scene |
| `tools/blender/fighter_skeleton.py`, `fighter_mesh.py`, `fighter_scene.py` | rest skeleton, procedural mesh + explicit skin weights, scene/material setup |
| `tools/blender/fighter_pose.py`, `fighter_dsl.py`, `fighter_clips.py` | pose engine (FK + analytic 2-bone IK), key/ease DSL, the clip catalogue |
| `tools/blender/validate_fighter.py`, `validate_fighter.gd`, `godot_preview_fighter.gd` | validation (Blender side, Godot side) and an engine-render lineup |
| `assets_src/fighter.blend` | armature + skinned mesh + all 47 actions (fake-user, LINEAR keys, 30 fps) |
| `game/assets/characters/fighter.glb` | glTF binary: skin + 47 animations (789 KiB) |
| `game/assets/characters/fighter_clips.json` | `{clip: {duration, loop, contact, notes, frames}}` |
| `game/assets/characters/fighter_post_import.gd` + `fighter.glb.import` | sets `loop_mode` from the json at import (glTF cannot carry loop flags) |
| `assets_src/fighter_contact_sheet.jpg`, `fighter_deform_check.jpg` | validation renders (PNG originals are produced by the validator) |
| `assets_src/fighter_foot_report.json`, `fighter_expected_bones.json` | foot planting numbers, Blender reference poses for the Godot cross-check |

## Regenerate

```
cd /home/user/ClaudeToken
/home/user/tools/bpyenv/bin/python tools/blender/build_fighter.py            # blend + glb + json (~2 s)
tools/scripts/godot.sh --headless --import                                    # import into the project
# optional validation
/home/user/tools/bpyenv/bin/python tools/blender/validate_fighter.py [--png-dir /some/dir]
tools/scripts/godot.sh --headless -s tools/blender/validate_fighter.gd -- $PWD/assets_src/fighter_expected_bones.json
tools/scripts/godot.sh --render -s tools/blender/godot_preview_fighter.gd -- /tmp/fighter_godot.png
```

`build_fighter.py --out-dir D` writes the three outputs somewhere else. The build script also keeps
`fighter.glb.import` pointing at the post-import script; on a checkout without that `.import` file run the
Godot import once and rebuild (or just copy the committed `.import`).

## Character

* 2406 vertices, **4354 triangles**, one skinned mesh, five material slots with these exact names and plain base colours:
  `skin` (0.78,0.55,0.40), `cloth_main` (0.72,0.74,0.78: top and trousers), `cloth_accent` (0.78,0.22,0.16: collar trim,
  sash, shoes; the only double-sided material), `wraps` (0.86,0.80,0.66: shin/forearm/hand wraps), `hair` (0.09,0.07,0.06).
  Godot gives surface `i` the material whose `resource_name` is the slot name, so a runtime recolour (player vs opponent) is
  "duplicate the surface material and set `albedo_color` where `resource_name == "cloth_main"`" (see `godot_preview_fighter.gd`).
* No modifiers (geometry is generated final), smooth shading with hard edges above 62 degrees, at most 4 bone influences per
  vertex (Mobile-renderer friendly), no UVs/textures. Object transforms are identity (scale 1, rotation 0).
* Proportions: head 0.233 m (7.5 heads), top of the hair 1.758 m, shoulder joints at x = +-0.175, hip joints at x = +-0.09.

## Rig spec

* Blender: Z up, **character faces -Y**, left = +X (`.L` bones at +X), 1 unit = 1 m, feet soles at Z = 0 in the rest pose.
  glTF/Godot (exporter converts to +Y up): the character **faces +Z**, left is +X, floor at Y = 0. Blender (x, y, z) becomes
  Godot (x, z, -y). Verified in Godot: toes of `foot.L` are at larger Z than the ankle, `hand.L` x = +0.335.
* 22 bones, all `use_deform`, exact names, parents before children. The table lists rest positions in metres as
  (x left, y forward, z up); in Godot read them as (x, z, y).

| bone | parent | head (x,y,z) m | tail (x,y,z) m | length m |
|---|---|---|---|---|
| `root` | - | (0.000, 0.000, 0.000) | (0.000, 0.150, 0.000) | 0.150 |
| `hips` | `root` | (0.000, 0.000, 0.900) | (0.000, 0.000, 1.020) | 0.120 |
| `spine` | `hips` | (0.000, 0.000, 1.020) | (0.000, 0.000, 1.210) | 0.190 |
| `chest` | `spine` | (0.000, 0.000, 1.210) | (0.000, 0.000, 1.430) | 0.220 |
| `neck` | `chest` | (0.000, 0.000, 1.430) | (0.000, 0.000, 1.575) | 0.145 |
| `head` | `neck` | (0.000, 0.000, 1.575) | (0.000, 0.000, 1.750) | 0.175 |
| `shoulder.L` | `chest` | (0.035, 0.015, 1.415) | (0.175, 0.000, 1.390) | 0.143 |
| `upper_arm.L` | `shoulder.L` | (0.175, 0.000, 1.390) | (0.278, 0.010, 1.108) | 0.300 |
| `forearm.L` | `upper_arm.L` | (0.278, 0.010, 1.108) | (0.335, 0.088, 0.866) | 0.261 |
| `hand.L` | `forearm.L` | (0.335, 0.088, 0.866) | (0.366, 0.122, 0.703) | 0.169 |
| `shoulder.R` | `chest` | (-0.035, 0.015, 1.415) | (-0.175, 0.000, 1.390) | 0.143 |
| `upper_arm.R` | `shoulder.R` | (-0.175, 0.000, 1.390) | (-0.278, 0.010, 1.108) | 0.300 |
| `forearm.R` | `upper_arm.R` | (-0.278, 0.010, 1.108) | (-0.335, 0.088, 0.866) | 0.261 |
| `hand.R` | `forearm.R` | (-0.335, 0.088, 0.866) | (-0.366, 0.122, 0.703) | 0.169 |
| `thigh.L` | `hips` | (0.090, 0.000, 0.900) | (0.090, 0.045, 0.495) | 0.407 |
| `shin.L` | `thigh.L` | (0.090, 0.045, 0.495) | (0.090, -0.005, 0.085) | 0.413 |
| `foot.L` | `shin.L` | (0.090, -0.005, 0.085) | (0.090, 0.130, 0.040) | 0.142 |
| `toe.L` | `foot.L` | (0.090, 0.130, 0.040) | (0.090, 0.215, 0.030) | 0.086 |
| `thigh.R` | `hips` | (-0.090, 0.000, 0.900) | (-0.090, 0.045, 0.495) | 0.407 |
| `shin.R` | `thigh.R` | (-0.090, 0.045, 0.495) | (-0.090, -0.005, 0.085) | 0.413 |
| `foot.R` | `shin.R` | (-0.090, -0.005, 0.085) | (-0.090, 0.130, 0.040) | 0.142 |
| `toe.R` | `foot.R` | (-0.090, 0.130, 0.040) | (-0.090, 0.215, 0.030) | 0.086 |

* Rest pose: relaxed A-pose, arms about 20 degrees off the body with a 17 degree elbow bend, **knees flexed about 13 degrees**
  (knee 4.5 cm forward of the hip-ankle line, so a two-bone IK has an unambiguous bend plane), feet flat. Ankle (foot bone
  head) is 0.085 m above the floor, the ball of the foot (toe bone head) 0.04 m, the toe tip 0.215 m ahead of the hip line.
* Bone roll: local Z of body/limb bones faces the character's front, foot/toe/root Z faces up. The exporter bakes the Blender
  bone axes into node rest transforms, so `root` imports with a non-identity rest rotation; this is expected and harmless
  (it is never animated).
* Godot scene after import: `fighter (Node3D) > FighterArmature > Skeleton3D > FighterMesh`, plus a sibling `AnimationPlayer`.
  Bone names keep their dots (`upper_arm.L`).
* **Runtime IK** (`TwoBoneIK3D`): legs `thigh.X > shin.X > foot.X` with the pole target in front of the knee (+Z, slightly
  outward); arms `upper_arm.X > forearm.X > hand.X` with the pole behind/outside the elbow. To plant a flat foot put the
  ankle target 0.085 m above the floor; the sole of a flat foot spans 0.08 m behind to 0.23 m ahead of the ankle.
  `shoulder.X` is a real clavicle bone (lift / protract).

## Animation conventions

* **30 fps**, every key on an integer frame, LINEAR keys on every frame (baked from the solver), so no sampling issues.
  Durations are therefore multiples of 1/30 s: a requested 0.35 s clip is 11 frames = 0.3667 s, 0.45 s is 0.4667 s; the
  json and the Godot `Animation.length` carry the real values. Contacts are snapped to the nearest frame the same way.
* Loops: the last key repeats the first (frame N == frame 0) and the length is N/30, so wrapping is seamless.
  Loop flags live in `fighter_clips.json` and are applied at import by `fighter_post_import.gd` (verified: 20 looping clips
  import with `LOOP_LINEAR`, the other 27 with `LOOP_NONE`).
* **Root policy**: `root` has no tracks at all, in no clip. `hips` has rotation + local position (bob/shift/lowering); every
  other deform bone has a rotation track. Every clip has exactly 22 tracks (21 rotation + `hips` position), so no pose
  leaks from one clip into the next.
* `contact` (json) = time of the strike / impact / release / touchdown that gameplay should align to; `null` for loops and
  for `getup`.
* Authoring: arms and legs are IK-solved in the script (hand targets in shoulder space, feet as ground pivots), then baked to
  FK rotations. Planted feet are constant by construction. Strikes use eased keys (`in3` into the contact, `out`/`ovs` after
  it), spline keys for water/loops, neck/head lag the torso by 1/2 frames, shoulder bones aim 30 percent toward the hand, the
  forearm takes half of the wrist roll.
* Attacks start and end in the matching element pose, so cross-fade from/to the stance clips:
  earth_* from `stance_earth`; water_* from `stance_water`; fire_*/`lightning_*` from `stance_fire`; `heat_draw` uses its own
  rooted stance, `magma_hold`/`pour` the wide magma stance; air_* from `stance_air`; evades/jump/reactions from the `idle`
  ready pose; `guard`/`deflect` from the guard pose.
* Gait clips (`walk`, `run`, `strafe_l/r`, `walk_back`) are generated per frame: stance footprints move backwards at exactly
  the design ground speed, so feet do not skate when the root moves at that speed (scale playback by actual / design speed).
  Hips are lowered automatically wherever a planted leg would overstretch.

## Clips (47)

`frames` x 1/30 = duration. "spec" shows the requested value where 30 fps quantisation changes it.

| clip | frames | duration s | loop | contact s | description |
|---|---|---|---|---|---|
| `idle` | 60 | 2.000 | loop | - | Relaxed ready stance, breathing; feet planted |
| `stance_earth` | 60 | 2.000 | loop | - | Wide low horse stance; slow heavy breathing |
| `stance_water` | 72 | 2.400 | loop | - | Bladed stance, fluid hip weight shift, flowing hands |
| `stance_fire` | 36 | 1.200 | loop | - | Forward pressure, fists up, light bounce (two per loop) |
| `stance_air` | 60 | 2.000 | loop | - | Light on the balls of the feet, open palms, slow sway |
| `walk` | 27 | 0.900 | loop | - | Natural walk, 1.4 m/s, **stride 1.26 m per cycle** (two 0.63 m steps) |
| `run` | 18 | 0.600 | loop | - | Forward run, 5.5 m/s, **stride 3.30 m per cycle** (two 1.65 m steps), 12 deg lean; hips dip 0.07 to 0.13 m |
| `strafe_l` | 27 | 0.900 | loop | - | Combat strafe left, guard up, 1.1 m/s, stride 0.99 m per cycle |
| `strafe_r` | 27 | 0.900 | loop | - | Combat strafe right, 1.1 m/s, stride 0.99 m per cycle |
| `walk_back` | 27 | 0.900 | loop | - | Backpedal with guard up, toe-first, 1.0 m/s, stride 0.90 m per cycle |
| `evade_l` | 14 | 0.467 (spec 0.45) | once | 0.200 | Low sidestep/hop left; contact = apex of the dodge |
| `evade_r` | 14 | 0.467 (spec 0.45) | once | 0.200 | Mirror of `evade_l` |
| `evade_back` | 14 | 0.467 (spec 0.45) | once | 0.200 | Backward hop with lean away, low silhouette |
| `evade_fwd` | 14 | 0.467 (spec 0.45) | once | 0.200 | Forward dive-step, head low |
| `jump` | 9 | 0.300 | once | 0.167 | Crouch, explosive extension, feet leave the ground at contact; ends rising |
| `fall` | 24 | 0.800 | loop | - | Airborne descent, arms up/out, legs staggered |
| `land` | 9 | 0.300 | once | 0.067 | Touchdown at contact, knees absorb, rise to ready; feet planted from contact |
| `glide` | 48 | 1.600 | loop | - | Arms spread wide, body pitched forward, legs trailing, wing flutter |
| `air_dash` | 9 | 0.300 | once | 0.133 | Tuck then streamline forward (superman); hold the last pose |
| `guard` | 48 | 1.600 | loop | - | Held block, forearms up, knees bent, slight tension breathing |
| `deflect` | 11 | 0.367 (spec 0.35) | once | 0.067 (spec ~0.08) | Cross-body parry with hip turn |
| `earth_wall` | 12 | 0.400 | once | 0.167 (spec ~0.18) | Stomp (contact) then both palms rise |
| `hit_front` | 11 | 0.367 (spec 0.35) | once | 0.067 | Chest and head snap back, arms jerk up |
| `hit_back` | 11 | 0.367 (spec 0.35) | once | 0.067 | Arches back, head thrown up, arms swing back |
| `hit_heavy` | 18 | 0.600 | once | 0.100 | Thrown back, front foot leaves the ground, stumbles and recovers |
| `knockdown` | 21 | 0.700 | once | 0.067 | Falls backward, slams the ground at about 0.57 s, ends lying on the back |
| `getup` | 24 | 0.800 | once | - | Lying > heels in > sit-up > squat > stand, ends in the idle pose |
| `stagger` | 15 | 0.500 | once | 0.100 | Off-balance wobble in place, one foot re-plants |
| `earth_lift` | 12 | 0.400 | once | 0.200 | Stomp at 0.1 s then hands pull a stone up; contact = stone pops |
| `earth_throw` | 14 | 0.467 (spec 0.45) | once | 0.167 (spec ~0.15) | Driving straight punch/push, right hand |
| `earth_heavy` | 24 | 0.800 | once | 0.467 (spec ~0.45) | Scoop low, heave overhead, drive forward-down |
| `earth_hold` | 36 | 1.200 | loop | - | Both hands cupped around a mass at chest height, strained tremble |
| `water_draw` | 15 | 0.500 | once | 0.367 (spec ~0.35) | Flowing pull from low-back to high-forward |
| `water_hold` | 48 | 1.600 | loop | - | Both hands circling a sphere of water, continuous, opposite phase |
| `water_whip` | 15 | 0.500 | once | 0.233 (spec ~0.22) | Wide horizontal arc with the right arm, torso whips |
| `water_freeze` | 12 | 0.400 | once | 0.267 (spec ~0.25) | Reach out open, clench both fists with a shoulder squeeze |
| `water_shield` | 48 | 1.600 | loop | - | Arms rounded like a shield, hands slowly orbiting, low stance |
| `fire_jab` | 9 | 0.300 | once | 0.100 (spec ~0.09) | Quick lead-hand jab with a hip/shoulder snap |
| `fire_charge` | 24 | 0.800 | loop | - | Fists chambered at the hips, coiled forward, tense vibration |
| `fire_release` | 12 | 0.400 | once | 0.100 | Double palm strike from the charge pose, ends in fire stance |
| `heat_draw` | 48 | 1.600 | loop | - | Rooted stance, open palms pull toward the chest then push out |
| `magma_hold` | 24 | 0.800 | loop | - | Cupped hands around a heavy mass, hunched, fast tremble |
| `pour` | 15 | 0.500 | once | 0.267 (spec ~0.25) | Raise, push the mass down, sweep it forward along the ground |
| `lightning_charge` | 36 | 1.200 | loop | - | Arms trace a 0.25 m ring in front of the chest, fingers extended |
| `lightning_release` | 11 | 0.367 (spec 0.35) | once | 0.067 (spec ~0.08) | Right-hand two-finger thrust |
| `air_push` | 11 | 0.367 (spec 0.35) | once | 0.133 (spec ~0.12) | Two-hand open palm push with a small hip pivot |
| `air_gust` | 18 | 0.600 | once | 0.367 (spec ~0.35) | Big push: torso coils ~105 deg left over 0.23 s (feet planted), unwinds through the front into a two-palm drive, settles to air stance; total yaw change 105 to -10 deg |

Simplified clips (kept deliberately simple, mostly pose-to-pose): `fall`, `glide`, `air_dash`, `evade_*` (hop is a
stylised lean-and-lift, not a physical step), `stagger`, `water_freeze`, `earth_hold`, `magma_hold`. `knockdown`/`getup`
use the whole-body lying pose (root stays put; the body then extends about 0.9 m behind and 0.7 m ahead of the root).

## Validation results

Run on the final files (`validate_fighter.py`, `validate_fighter.gd`).

1. **Visual** (`assets_src/fighter_contact_sheet.jpg`: rest + 19 key poses, front and side; `fighter_deform_check.jpg`: elbows
   folded, arms overhead / across / back, deep squat, 90 degree knee lift, back kick, twist+bend). Cycles CPU previews,
   because Workbench needs libEGL, which the headless box lacks. Reviewed by eye and fixed: weights that followed the wrong
   bone below the hips, hair/head crown shape, collar trim gap, hand/pose reach, backwards-trailing knee flips. No joint
   collapse at elbows, knees, shoulders or hips in the check poses; remaining artefacts are listed under weaknesses.
2. **Foot planting** (`assets_src/fighter_foot_report.json`). Sole skate = horizontal drift of any shoe-sole vertex while it
   is in ground contact (z < 8 mm for 3+ frames), measured on the evaluated, skinned mesh in Blender:

   | group | worst result |
   |---|---|
   | idle, 4 stances, guard, deflect, hit_*, stagger, all earth/water/fire/lightning clips, air_gust, getup, evades | **0.00 cm** sole skate and **0.00 cm** ankle/ball bone variance |
   | `fire_release` (rear heel lifts) | 0.35 cm skate, right ankle bone variance 0.91 cm |
   | `air_push` (heel lift) | 0.29 cm skate, ankle 0.61 cm |
   | `lightning_release` | 0.11 cm skate, 0.17 cm bone variance |
   | `land` | 0.76 cm skate, 0.14 cm bone variance |
   | `walk`, `run`, `strafe_l/r`, `walk_back` (ground speed compensated) | 1.36, 0.74, 0.74, 0.74, 1.13 cm |
   | `jump` (push-off), `knockdown` (fall) | 0.11 cm, 1.61 cm (feet leaving the ground; the heel rises 9.8 cm in `jump`, ball fixed) |

   All requested planted cases are under the 1.5 cm target. Floor check over all frames of all clips: no skinned vertex goes
   more than 0.9 cm below the floor (`run` heel, `knockdown` fall frame). IK reach: no planted leg is ever clamped in any clip except 1.8 cm
   on the trailing leg at one frame of `air_dash`; arm targets are clamped by at most 3.7 cm (`water_draw`) and 1.4 cm
   (`earth_heavy`).
3. **Godot import** (`tools/scripts/godot.sh --headless --import`, then `validate_fighter.gd`): 22 bones with the exact names,
   1 skinned `MeshInstance3D` with 5 surfaces named `skin, cloth_main, cloth_accent, wraps, hair`, 47 animations whose lengths
   all match the json, 22 tracks each, loop modes match the json. Global mesh AABB height **1.757 m** (y from 0.001 to 1.758,
   including hair); character **faces +Z**; left hand at +X. Cross-check of seven clip/time samples (idle, fire_jab, earth_heavy,
   walk, knockdown, air_gust, run) of nine bone positions against Blender's evaluation: **max error 0.01 mm**.
4. **Engine render** (`godot_preview_fighter.gd`, Forward Mobile, lavapipe): six fighters in different clip poses with recoloured
   `cloth_main`/`cloth_accent`; materials, skinning and poses look right.

## Known weaknesses

* Timing is quantised to 30 fps (up to 17 ms off the requested durations/contacts, listed above).
* The run is stylised: reaching 5.5 m/s with an 0.82 m leg forces a long flight phase, a deep hip dip (0.07 to 0.13 m) and a
  3.3 m stride; it reads as a fast lunging run, not a sprint.
* Mitten hands (no finger bones), faceless head, no cloth simulation: sash tails are rigid strips weighted to hips/thigh and
  can poke through the thigh on very high kicks.
* Hand-authored linear-blend skinning: at wide leg splits the pelvis band shows a flap/crease between the thighs and a jagged
  intersection line where it meets the thigh tubes; arms raised high behind the back pinch the shoulder caps; wrist twist
  beyond about 90 degrees candy-wraps the forearm (forearm takes half of the twist). Elbow/knee/hip bends up to the tested
  extremes are clean.
* `air_gust` keeps the feet planted and twists the legs up to about 30 degrees against them during the coil; the unwind
  peaks near 30 degrees of body yaw per frame, so it reads as a hard whip. `air_dash`/`earth_lift` start with a fast wrist
  roll (100+ degrees per frame).
* `evade_*` and `jump` are short stylised hops (feet planted until take-off, then airborne). The evade/jump contact is
  "apex/take-off", not an impact.
* Asymmetries: the left foot is always the lead foot in stances; `earth_throw`, `water_whip`, `lightning_release` use the
  right arm, `fire_jab` the left. Only the evades and strafes come as left/right pairs (mirrors).
* Lying poses (`knockdown`, `getup`) extend far from the root (0.9 m behind, 0.7 m ahead); the game must place the root
  accordingly. In the lying pose the lowest skinned vertices are within 1 cm of the floor (soles -0.4 cm).
* `fighter.glb.import` is generated by Godot; if the post-import script path is lost (for example after "Reimport" with
  changed defaults), loops come out as `LOOP_NONE` and must be set from `fighter_clips.json` at load time instead.
