# Fighter rig and animation (`fighter.glb`)

One articulated training fighter (about 1.75 m, stylised, 13.7k triangles, PBR textures) with **59 in-place animation clips**
(47 game-spec clips + 12 generic `mv_*` moveset clips), generated entirely by script from an empty Blender scene. Gameplay
code moves the character; the animation is presentation only.

Original design: short-sleeved wrap top with a V collar, lapel piping and a double-wound cloth sash with a back knot and two
tails, loose patched trousers tucked into wrapped shins, wrapped forearms and hands (bare fingers), soft shoes with rubber
soles, a modelled face (brows, eyes, nose, lips, ears) and layered hair with a top knot. Nothing here references any existing
franchise.

## Files

| file | what |
|---|---|
| `tools/blender/build_fighter.py` | entry point: builds everything below from a factory-empty scene |
| `tools/blender/fighter_skeleton.py`, `fighter_mesh.py`, `fighter_scene.py` | rest skeleton (22 + 20 added bones), mesh infrastructure (lofts, explicit skin weights, UV atlas packer), scene/material setup (wires the baked textures into the glTF materials) |
| `tools/blender/fighter_character.py`, `fighter_body.py`, `fighter_garments.py`, `fighter_hands.py` | the character: head/face/hair/neck/torso skin, garments (top, sash, trousers, wraps, shoes), arms + sleeves + articulated hands |
| `tools/blender/fighter_textures.py` | numpy PBR bake: albedo / normal / ORM at 1024 x 1024 per material slot |
| `tools/blender/fighter_pose.py`, `fighter_dsl.py`, `fighter_clips.py`, `fighter_grips.py` | pose engine (FK + analytic 2-bone IK + finger solver), key/ease DSL, the clip catalogue (incl. `mv_*`), hand shapes per clip |
| `tools/blender/preview_cli.py`, `preview_pose.py`, `godot_preview_closeup.gd` | iteration previews: Cycles views of the rest pose / any clip frame, Godot close-ups (full, face, hands, fists, waist, feet, back) |
| `tools/blender/validate_fighter.py`, `validate_fighter.gd`, `godot_preview_fighter.gd` | validation (Blender side, Godot side) and an engine-render lineup |
| `assets_src/fighter.blend` | armature + skinned mesh + all 59 actions (fake-user, LINEAR keys, 30 fps) |
| `game/assets/characters/fighter.glb` | glTF binary: skin + 59 animations + 15 embedded textures (7.8 MiB). Godot's glTF importer also extracts the textures to `fighter_<slot>_<albedo|normal|orm>.png` next to it (project default `embedded_image_handling`); they are generated files |
| `assets_src/fighter_shots/*.jpg` | before/after and close-up Godot renders (see the section at the end) |
| `game/assets/characters/fighter_clips.json` | `{clip: {duration, loop, contact, notes, frames}}` (59 entries) |
| `game/assets/characters/fighter_post_import.gd` + `fighter.glb.import` | sets `loop_mode` from the json at import (glTF cannot carry loop flags) |
| `assets_src/fighter_contact_sheet.jpg`, `fighter_deform_check.jpg` | validation renders (PNG originals are produced by the validator) |
| `assets_src/fighter_foot_report.json`, `fighter_expected_bones.json` | foot planting numbers, Blender reference poses for the Godot cross-check |

## Regenerate

```
cd /home/user/ClaudeToken
/home/user/tools/bpyenv/bin/python tools/blender/build_fighter.py            # blend + glb + json + textures (~20 s)
tools/scripts/godot.sh --headless --import                                    # import into the project
# preview while iterating (Cycles CPU): any rest-pose view or any clip frame; FIGHTER_NO_TEXTURES=1 skips the texture bake
/home/user/tools/bpyenv/bin/python tools/blender/preview_cli.py /tmp/prev --views front,face,hands
FIGHTER_NO_TEXTURES=1 /home/user/tools/bpyenv/bin/python tools/blender/preview_pose.py /tmp/pose mv_spin:12,fire_jab:c --views q,side
tools/scripts/godot.sh --render --resolution 1000x700 -s $PWD/tools/blender/godot_preview_closeup.gd -- /tmp/cu "fire_jab@c" "full,face,fists"
# optional validation
/home/user/tools/bpyenv/bin/python tools/blender/validate_fighter.py [--png-dir /some/dir]
tools/scripts/godot.sh --headless -s $PWD/tools/blender/validate_fighter.gd -- $PWD/assets_src/fighter_expected_bones.json
tools/scripts/godot.sh --render -s $PWD/tools/blender/godot_preview_fighter.gd -- /tmp/fighter_godot.png
```

`build_fighter.py --out-dir D` writes the three outputs somewhere else. The build script also keeps
`fighter.glb.import` pointing at the post-import script; on a checkout without that `.import` file run the
Godot import once and rebuild (or just copy the committed `.import`).

## Character

* **7335 vertices, 13698 triangles** (budget 8k to 14k), one skinned mesh with five material slots with these exact names:
  `skin`, `cloth_main` (top, sleeves, trousers), `cloth_accent` (collar, lapel piping, sleeve trim, sash, hair tie, shoes; the
  only double-sided material), `wraps` (shin / forearm / hand wraps, rubber soles and the two eyeballs), `hair`. Base colour
  factors are unchanged (`skin` 0.78,0.55,0.40; `cloth_main` 0.72,0.74,0.78; `cloth_accent` 0.78,0.22,0.16; `wraps`
  0.86,0.80,0.66; `hair` 0.09,0.07,0.06). Godot gives surface `i` the material whose `resource_name` is the slot name, so a
  runtime recolour (player vs opponent) is "duplicate the surface material and set `albedo_color` where `resource_name == "cloth_main"`"
  (see `godot_preview_fighter.gd`, `fighter_view.gd`).
* **Textures** (`fighter_textures.py`, baked at build time, no external files): per slot a 1024 x 1024 **albedo**, a
  tangent-space **normal** map (OpenGL / +Y, as glTF and Godot expect; the importer generates tangents) and an **ORM** image
  (R ambient occlusion, G roughness, B metallic = 0) that glTF shares between `occlusionTexture` and `metallicRoughnessTexture`.
  Because the game multiplies albedo by `albedo_color` (and sets `roughness` 0.6 for skin / 0.82 otherwise), the albedo maps are
  *tint-relative* (all values <= 1, cloth / wraps / hair near neutral) and the roughness map is stored relative to that
  reference value. Painted detail: face (brows with a thin scar on the left one, lash lines, lips, nostrils, blush), iris and
  pupil on eyeball meshes that use the `wraps` slot (so the whites are not skin-tinted), nails, knuckle scuffs, cloth weave and
  dirt, stitched seams (centre back, side, outer trouser seam, hems), a stitched knee patch, leather grain and welt stitching on the
  shoes, rubber soles, linen wraps, hair strands. Normal maps carry weave, strands, lip lines, brow hair, seams.
  Bake: per-vertex AO (BVH ray casts) + numpy rasterisation; the whole build takes about 20 s and is deterministic.
* Smooth shading with hard edges above 62 degrees, at most 4 bone influences per vertex (Mobile-renderer friendly), one UV set
  (per-slot shelf-packed atlas, 7 px margins, bled), no modifiers (geometry is generated final), object transforms identity.
  `FIGHTER_NO_TEXTURES=1` skips the bake (fast geometry iteration).
* **Anatomy / silhouette**: 7.5 head figure with a modelled face (brow ridge, eye sockets with lids, nose, lips, chin, ears),
  neck, shaped torso (pectorals, waist), V-collar wrap top with a lapel crossing, cloth folds (low amplitude sines that follow the
  body), short sleeves with an accent hem band, tapered upper arms and forearms, a hand with palm, thenar pad, four fingers and
  a thumb, loose trousers with pleats and a knee crease, shin wraps built from overlapping tilted bands, shoes with toe box,
  heel counter, welt and rubber sole with toe spring. Hair: a cap with per-meridian clump offsets, ~40 shingle tufts along the
  hairline and fringe, side locks and a top knot (3 tails) with a tie band.
* Proportions: head 0.233 m, top of the hair 1.758 m (top knot to 1.792 m), shoulder joints at x = +-0.175, hip joints at
  x = +-0.09.

## Rig spec

* Blender: Z up, **character faces -Y**, left = +X (`.L` bones at +X), 1 unit = 1 m, feet soles at Z = 0 in the rest pose.
  glTF/Godot (exporter converts to +Y up): the character **faces +Z**, left is +X, floor at Y = 0. Blender (x, y, z) becomes
  Godot (x, z, -y). Verified in Godot: toes of `foot.L` are at larger Z than the ankle, `hand.L` x = +0.335.
* **22 original bones** (unchanged names, rest pose, scale, +Z forward, in-place root) plus **20 added bones**, 42 in all, all
  `use_deform`, parents before children. The table lists the 22 original rest positions in metres as (x left, y forward, z up);
  in Godot read them as (x, z, y). Added bones are listed below the table.

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

* **Added bones** (rest transforms of the 22 above are untouched):

  | bones | parent | use |
  |---|---|---|
  | `thumb_1.X`, `thumb_2.X` | `hand.X` / `thumb_1.X` | thumb base and tip phalanx (animated) |
  | `finger_im_1.X`, `finger_im_2.X` | `hand.X` / `finger_im_1.X` | index + middle finger group (proximal, middle+distal) (animated) |
  | `finger_rp_1.X`, `finger_rp_2.X` | `hand.X` / `finger_rp_1.X` | ring + pinky group (animated) |
  | `sash_tail.L.001..003`, `sash_tail.R.001..003` | `hips` then chained | the two sash tails, **not animated**: runtime springs (`SpringBoneSimulator3D`, see `game/presentation/anim/fighter_secondary_motion.gd`) |
  | `hair_top.001`, `hair_top.002` | `head` then chained | top knot and its tails, **not animated**: runtime springs |

  The hand bone still ends at the middle fingertip; finger bones live inside it. Finger bone roll: local Z faces forward.
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
  other animated bone has a rotation track. Every clip has exactly **34 tracks** (21 body rotations + `hips` position + 12 finger
  rotations), so no pose leaks from one clip into the next. The secondary bones (sash tails, hair) have no tracks in any clip
  (Godot's importer additionally drops tracks that never change, `remove_immutable_tracks`).
* **Hands** (`fighter_grips.py`): every clip carries a hand shape per hand, a 5-tuple (thumb tuck, thumb tip curl, index+middle
  curl, ring+pinky curl, spread) that `RigModel.finger_quats` turns into rotations of the 12 finger bones. Presets: `relaxed`,
  `open`, `open_spread`, `blade` (flat knife hand), `fist`, `fist_loose`, `claw`, `flow` (soft, splayed: water), `cup`,
  `cup_claw`, `two_finger` (index + middle extended, others curled: lightning). Keys are smoothstep-blended, fists close two
  to three frames before the strike contact and open on release (`fire_release`, `air_push`), stances breathe (finger curl
  follows the chest), `fire_charge` trembles. For loops the first and last shape match.
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
  Hips are lowered automatically wherever a planted leg would overstretch. Heel contact uses the pivot `pv_heel` = -0.092 m
  (the back of the sole), so the heel does not slide at heel strike.

## Clips (59)

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

### Generic moveset clips (`mv_*`)

Expressive building blocks for a larger moveset (appended after the 47 game-spec clips; names, durations and contacts are
stable like the rest). Same conventions: in place, start and end on a stance pose (base noted in `fighter_clips.json` notes),
`contact` = impact / release / completion, planted feet do not slide. `mv_spin` pivots on the ball of the left foot (ankle
circles it) and uses the new `pivot_hips` option of the clip DSL (keeps the hips over the support foot while the body turns); its
final pose is the idle ready pose with a yaw of exactly 360 degrees (identical rotation).

| clip | frames | duration s | loop | contact s | description |
|---|---|---|---|---|---|
| `mv_push_two_hand` | 15 | 0.500 | once | 0.200 | Wide low stance; palms chambered at the ribs, body lunges, both palms drive out at chest height |
| `mv_uppercut_lift` | 15 | 0.500 | once | 0.200 | Sink and coil on the rear hip, rear fist drives from hip to chin height while the body rises |
| `mv_stomp` | 17 | 0.567 | once | 0.233 | Knee up with raised hands, then the front foot slams flat (same spot) while both palms press down |
| `mv_sweep_low` | 21 | 0.700 | once | 0.333 | Crouch on the left foot, right leg whips around along the floor in a half circle (ankle ~4 cm up) |
| `mv_spin` | 30 | 1.000 | once | 0.600 | Wind-up, then a full 360 degree turn on the ball of the left foot (hips over the pivot), right arm trailing wide, chest lags the hips |
| `mv_palm_thrust` | 14 | 0.467 | once | 0.167 | Coil on the rear hip, rear palm thrusts straight out with a hip drive |
| `mv_overhead_slam` | 21 | 0.700 | once | 0.367 | Rise and arch back with both hands overhead, drop into a deep lunge and crash both fists to knee height |
| `mv_wide_draw` | 24 | 0.800 | once | 0.633 | Arms sweep wide, arc up overhead, gather in front of the chest with a small sink |
| `mv_ground_slap` | 21 | 0.700 | once | 0.267 | Sink low, wind the right hand up, slap the floor in front of the lead foot |
| `mv_rising_guard` | 15 | 0.500 | once | 0.200 | From a deep crouch with hands low, rise explosively with both forearms crossing in front of the face |
| `mv_roundhouse` | 20 | 0.667 | once | 0.267 | Chamber the right knee across the body, pivot on the left foot, whip the leg around at chest height |
| `mv_front_kick` | 17 | 0.567 | once | 0.200 | Drive the lead knee up, snap the foot out at stomach height (leaning back a little) |


Simplified clips (kept deliberately simple, mostly pose-to-pose): `fall`, `glide`, `air_dash`, `evade_*` (hop is a
stylised lean-and-lift, not a physical step), `stagger`, `water_freeze`, `earth_hold`, `magma_hold`. `knockdown`/`getup`
use the whole-body lying pose (root stays put; the body then extends about 0.9 m behind and 0.7 m ahead of the root).

## Validation results

Run on the final files (`validate_fighter.py`, `validate_fighter.gd`).

1. **Visual** (`assets_src/fighter_contact_sheet.jpg`: rest + 19 key poses, front and side; `fighter_deform_check.jpg`: elbows
   folded, arms overhead / across / back, deep squat, 90 degree knee lift, back kick, twist+bend). Cycles CPU previews,
   because Workbench needs libEGL, which the headless box lacks. In the check poses sleeves, shoulders, elbows, knees and hips
   deform cleanly (the cap sleeve was shortened so it no longer balloons when the arm is raised); remaining artefacts are listed
   under weaknesses. Engine renders (Godot Forward Mobile on lavapipe) are in `assets_src/fighter_shots/`.
2. **Foot planting** (`assets_src/fighter_foot_report.json`). Sole skate = horizontal drift of any shoe-sole vertex while it
   is in ground contact (z < 8 mm for 3+ frames), measured on the evaluated, skinned mesh in Blender:

   | group | worst result |
   |---|---|
   | idle, 4 stances, guard, deflect, hit_*, stagger, all earth/water/fire/lightning clips, air_gust, getup, evades, `mv_push_two_hand`, `mv_stomp`, `mv_sweep_low`, `mv_overhead_slam`, `mv_wide_draw`, `mv_ground_slap`, `mv_rising_guard`, `mv_front_kick` | **0.00 cm** sole skate and **0.00 cm** ankle/ball bone variance |
   | `fire_release` (rear heel lifts) | 0.26 cm skate, right ankle bone variance 0.91 cm |
   | `air_push` (heel lift) | 0.21 cm skate, ankle 0.61 cm |
   | `lightning_release`, `mv_uppercut_lift`, `mv_palm_thrust` (rear heel lifts) | 0.11, 0.57, 0.57 cm skate |
   | `land` | 0.92 cm skate, 0.14 cm bone variance |
   | `walk`, `run`, `strafe_l/r`, `walk_back` (ground speed compensated) | 1.36, 0.97, 0.76, 0.76, 1.30 cm |
   | `jump` (push-off), `knockdown` (fall) | 1.20 cm, 1.61 cm (feet leaving the ground; the heel rises 9.8 cm in `jump`, ball fixed) |
   | `mv_spin`, `mv_roundhouse` (pivots) | the supporting **ball of the foot** stays put (1.1 cm and 0.3 cm bone variance); the ankle and heel swing around it (17 / 7 cm) as in a real ball pivot, so the sole-vertex metric reads 16 / 7 cm |

   Floor check over all frames of all 59 clips: no skinned vertex goes more than 1.8 cm below the floor (`knockdown` and
   `getup` lying frames: soles and the sash knot; `run` heel 1.1 cm). IK reach: no planted leg is clamped by more than 1.8 cm
   in any clip (`air_dash`, one frame; `mv_spin` 1.2 cm); arm targets are clamped by at most 3.7 cm (`water_draw`) and 1.4 cm
   (`earth_heavy`); every `mv_*` hand target is reachable.
3. **Godot import** (`tools/scripts/godot.sh --headless --import`, then `validate_fighter.gd`): the 22 original bones plus the 20
   added ones (42), 1 skinned `MeshInstance3D` with 5 surfaces named `skin, cloth_main, cloth_accent, wraps, hair` (albedo,
   normal and ORM textures on each), 59 animations whose lengths all match the json, 34 tracks each, loop modes match the json.
   Mesh AABB height **1.792 m** including the top knot (the body without the knot is 1.758 m); character **faces +Z**; left
   hand at +X. Cross-check of seven clip/time samples (idle, fire_jab, earth_heavy, walk, knockdown, air_gust, run) of nine bone
   positions against Blender's evaluation: **max error 0.01 mm**. `run_tests.gd` (188), `run_ui_tests.gd` (50) and
   `check_scripts.gd` pass with the new asset.
4. **Engine render**: `godot_preview_closeup.gd` (full / face / hands / fists / waist / feet / back with the game's player
   palette) and in-game flagship / show_fire / show_earth autoplay movies at 1280 x 592 (quality 2).
   `assets_src/fighter_shots/before_after_idle.jpg`, `before_after_fire_jab.jpg` compare with the previous mannequin;
   `closeups_after.jpg`, `mv_poses_engine.jpg`, `ingame_flagship.jpg`, `ingame_player_crops.jpg` show the new asset.

## Known weaknesses

* Timing is quantised to 30 fps (up to 17 ms off the requested durations/contacts, listed above).
* The run is stylised: reaching 5.5 m/s with an 0.82 m leg forces a long flight phase, a deep hip dip (0.07 to 0.13 m) and a
  3.3 m stride; it reads as a fast lunging run, not a sprint.
* Face: simple and slightly stern; the nose is a blocky wedge at close range, the head is long (7.5 heads) and the top-of-
  forehead hair fringe is a set of flat shingles. The fingers are straight tubes (two bones per grouped pair, middle and distal
  phalanx share one bone), so a clenched fist is a blunt blob rather than four curled fingers. Hand wraps and shin wraps are
  stacked single-sided bands: at very close range their edges look like torn paper. The tunic hem under the sash has a notched
  outline (side slits sampled by 32 segments).
* No cloth simulation in the asset: the sash tails and the top knot are weighted to the secondary bones and need the runtime
  spring chains (`fighter_secondary_motion.gd`); without them they are rigid and can poke through the thigh on very high kicks.
* Textures: 15 images (5 slots x albedo / normal / ORM) at 1024 x 1024 are embedded in the glb (7.8 MiB) and, after the Godot
  import, also exist as extracted PNGs (+ `.import`) in `game/assets/characters/`; with VRAM compression that is roughly 20 MB of
  texture memory per fighter model (shared by all fighters). If that is too much on device, reduce `SIZE` in `fighter_textures.py`
  to 512 or drop the hair normal map. GPU cost, thermals and the look of the weave on a real phone screen are unverified.
* The game's `fighter_view.gd` sets `roughness` per slot; Godot multiplies it with the roughness texture, so the maps store
  roughness relative to 0.6 (skin) / 0.82 (others). If those constants change, update `ROUGH_REF` in `fighter_textures.py`.
* Hand-authored linear-blend skinning: at wide leg splits the pelvis band shows a flap/crease between the thighs and a jagged
  intersection line where it meets the thigh tubes; arms raised high behind the back pinch the shoulder caps; wrist twist
  beyond about 90 degrees candy-wraps the forearm (forearm takes half of the twist). Elbow/knee/hip bends up to the tested
  extremes are clean.
* Vertex ambient occlusion on the large neck / chest cap triangles gives a faint jagged shading line at the neckline.
* `air_gust` keeps the feet planted and twists the legs up to about 30 degrees against them during the coil; the unwind
  peaks near 30 degrees of body yaw per frame, so it reads as a hard whip. `air_dash`/`earth_lift` start with a fast wrist
  roll (100+ degrees per frame). `mv_spin` turns 27 degrees per frame at its peak (smoothstep over 20 frames).
* `evade_*` and `jump` are short stylised hops (feet planted until take-off, then airborne). The evade/jump contact is
  "apex/take-off", not an impact.
* Asymmetries: the left foot is always the lead foot in stances; `earth_throw`, `water_whip`, `lightning_release`,
  `mv_uppercut_lift`, `mv_palm_thrust`, `mv_ground_slap` use the right arm, `fire_jab` the left. Only the evades and strafes come
  as left/right pairs (mirrors); `mirror_clipdef` in `fighter_dsl.py` mirrors any other clip (its hand shapes are looked up by the new clip name in `fighter_grips.py`).
* Lying poses (`knockdown`, `getup`) extend far from the root (0.9 m behind, 0.7 m ahead); the game must place the root
  accordingly. In the lying pose the lowest skinned vertices are within 1 cm of the floor (soles -0.9 cm).
* `fighter.glb.import` is generated by Godot; if the post-import script path is lost (for example after "Reimport" with
  changed defaults), loops come out as `LOOP_NONE` and must be set from `fighter_clips.json` at load time instead.
