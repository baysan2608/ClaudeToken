# Stream `animation` - hand-keyed martial-arts clips on the shared rig

131 clips (P0 66 · P1 51 · P2 14) authored by script on the frozen UE5-Mannequin-compatible rig, every one validated
and exported as `SourceArt/Animation/A_<clip>.fbx`; `clips.json` + `anim_map.json` map all 160 sim moves (+ the 5 chained actions `lightning`, `pour`, `vent`, `gust_grip`, `flare_dash` the sim switches to mid-move) to their
ideal clips (no stand-ins); the Unreal import script passes the mock dry run and a scripted fake-editor test.

Related: [CLIPS.md](CLIPS.md) (every clip: frames, contacts, technique, which moves use it - generated) ·
[COVERAGE.md](COVERAGE.md) (generated) · [API_NOTES.md](API_NOTES.md) · [REQUESTS.md](REQUESTS.md) ·
[PROGRESS.md](PROGRESS.md) (work log / checkpoint).

## What is here
| Path | What |
|---|---|
| `Tools/blender/animation/ffa_rig.py` | rest frames of the frozen rig (rebuilt exactly as Blender builds them) + numpy FK |
| `ffa_solver.py` | body-space pose -> local rotations: pelvis / spine chain (15/20/25/25/15 %), neck + gaze, clavicle follow, two-bone arm / leg IK with elbow / knee poles (bend plane from the pole, continuous through straight limbs), foot pivots (heel / ankle / ball / toe) with toe-floor clamp, forearm roll split onto the twist bones (`lowerarm_twist_02/01` 30 / 60 %, upper-arm counter-roll), soft forearm range, wrist swing limit, finger shapes |
| `ffa_dsl.py` | authoring DSL: `Clip(...).k(frame, ease, pel=, spine=, hand_l=HW(...), foot_r=F(...), fing=...)`; eases (`io in in3 out ovs snap acc sp` ...), Catmull-Rom (`sp`), hand paths (`arc`), per-channel overlap offsets (waist leads, head / free hand lag), breath and tremble layers, foot locking (feet interpolate about the end key's pivot, planted ranges auto-detected), auto hip drop for gaits, anatomical forearm-roll channel, hand-roll / joint speed limiters (no 1-frame pops) |
| `ffa_hands.py` | the ten hand shapes of MARTIAL_ARTS §1.4 as finger-curl presets (solved thumb) |
| `ffa_validate.py` | per-clip checks (below); errors block the export |
| `ffa_proxy.py`, `ffa_render.py` | proxy mannequin (tapered limbs, nose, mittens + finger tubes, left / right tinted) and a numpy / PIL software renderer for contact sheets and MP4s |
| `ffa_export.py` | bakes every frame onto `ff_rig_spec.build_armature()` (validated before every export) and writes the FBX through the frozen `ff_fbx_export.export_animation_fbx`; `--check-fbx` re-imports and compares (<= 0.25 deg, 1 mm, frame 0 first, 103 bones) |
| `clips/` | the catalogue: `bases.py` (stance poses), `shared.py` (idle, guard, gaits), `reactions.py` (evades, air, hits, knockdown / getup, modes), `earth.py`, `water.py`, `fire.py` (+ lightning / combustion), `air.py`, `p2.py`, `hand_shapes.py` |
| `build_animation.py` | solve -> validate -> previews -> FBX -> merge `clips.json` -> `anim_map.json` + CLIPS.md / COVERAGE.md |
| `anim_table.py`, `anim_map_gen.py` | move -> clip table for all 160 moves + 5 chained actions (MARTIAL_ARTS §4, §4.17) and its resolver (falls back through `CLIP_FALLBACK` only if a clip is missing) |
| `test_anim_data.py`, `test_unreal_import.py`, `review.py` | data self-test; Unreal import against a scripted fake editor; film-strip renderer for iterating |
| `ffa_audit.py`, `ffa_compare.py` | motion-quality audit (+ sim-fitted timing suggestions); solved-pose snapshots and before / after MP4s + strips |
| `SourceArt/Animation/` | `A_<clip>.fbx` (60 fps, first key frame 0), `previews/<clip>.jpg` (gameplay camera + side view at start / anticipation / contact / follow-through / end), `previews/<clip>.mp4` (P0, 640x360), `previews/before_after/<clip>_before_after.{mp4,jpg}` (2026-10-09 rework), `validation_report.json` |
| `Content/Fourfold/Data/clips.json`, `anim_map.json` | runtime data, schemas exactly ARCHITECTURE §8.3 |
| `Content/Python/fourfold/animation/__init__.py` | Unreal import: `build_all(force=False)` |

## Regenerate / test here (macOS, since 2026-10-09)
Solve / validate / previews run on the system python (numpy 2.0 + PIL, `~/Library/Python/3.9`); the FBX export needs
`bpy`, so it runs inside Blender 5.0.1 (args after `--`, `--no-sheets` because Blender's python has no PIL). MP4s use
`ffmpeg` from PATH or the `imageio-ffmpeg` wheel.
```
cd unreal; BL=/Applications/Blender.app/Contents/MacOS/Blender
python3 Tools/blender/animation/build_animation.py --no-export --no-sheets --no-json      # solve + validate all (~18 s)
python3 Tools/blender/animation/build_animation.py --only "w_*" --no-export --review /tmp/rv   # iterate on some clips
$BL -b --factory-startup --python Tools/blender/animation/build_animation.py -- --no-sheets --check-fbx
                                     # export every FBX + re-import check, merge clips.json, anim_map.json, CLIPS.md (~45 s)
python3 Tools/blender/animation/build_animation.py --no-export --no-json --videos P0        # sheets + P0 MP4s (~15 min)
python3 Tools/blender/animation/review.py w_lash --every 2 --views side,game              # film strip (FFA_REVIEW_DIR)
python3 Tools/blender/animation/ffa_audit.py --only "f_*"      # motion-quality numbers (see ffa_audit.py docstring)
python3 Tools/blender/animation/ffa_audit.py --timing          # sim-fitted (contact, frames) suggestions
python3 Tools/blender/animation/ffa_compare.py dump --toolkit <old toolkit copy> --only "f_jab" --out /tmp/before.pkl
python3 Tools/blender/animation/ffa_compare.py video --before /tmp/before.pkl --out <dir>   # before | after MP4s
python3 Tools/blender/animation/test_anim_data.py
python3 Tools/blender/animation/test_unreal_import.py
python3 Tools/py_mock/run_with_mock_unreal.py Content/Python/fourfold/animation/__init__.py --call fourfold.animation:build_all
```
The FBX conventions are untouched (`Tools/blender/common/ff_fbx_export.py` stays frozen: centimetre copy, 60 fps, one
action, first key 0); `ffa_export.py` only creates the action through the layered-action API when `Action.fcurves`
is gone (Blender 5) and compares re-imported heads in world space.

## Motion layers (ffa_dsl, applied to every clip)
The clip keys are poses; how the body travels between them is decided by these layers (each can be scaled per clip:
`Clip(..., flow=, chain=, settle=, wave=, hang=)`):
* **flow** - `io` segments are a monotone cubic through the keys (auto-clamped tangents): zero velocity only where a
  channel turns round, the neighbours' slope where it passes through, C1 with neighbouring snap / ease-in / hold
  segments.  Before, every key was a dead stop (pose-to-pose robot timing).
* **kinetic chain** (clips with a contact) - channel offsets now fade to 0 at every contact frame, so contact poses are
  exact while the hips lead (+1.5 f on top of the authored offsets), the chest follows, the shoulders trail, the
  striking hand lags (-1.5 f) and therefore accelerates into the contact (whip) and hangs after it (`hang`: x that
  lag; the Shaolin strikes use 0.3 = snap straight back); trunk channels arriving at a contact on an ease-in use the
  `brake` ease (peak speed at 2/3, stopped at the contact) - the hips stop and the arm goes through.
* **settle** - the velocity the body loses at each contact rings out as a damped spring (3.2 Hz, zeta 0.42) on the
  trunk, neck, shoulders and the free hand: an overshoot and a settle instead of a freeze.
* **spine wave** - the upper back samples the spine channel 1 frame after the lumbar (`spine_d`, solver
  `SPINE_WAVE`), so turns travel up the back.
* **polish ladder** - if a layer breaks a validation rule for a clip (contact no longer the extreme, a pop in a fast
  spin, a planted leg out of reach) it is backed off step by step for that clip only; `validation_report.json`
  `stats.polish` records the rung (0 = full).
* **timing** - `clips/timing.py` re-times 42 strike clips with `Clip.retime(contact, frames)` so they play at ~1x in
  the sim (contact ~ startup, length ~ contact + active + recovery); see its docstring.

**Validator** (per clip, MARTIAL_ARTS §5): frame count / NaN; planted feet: the anchor (least-moving floor point:
heel, ball or toe) moves <= 3 mm (ball / heel pivots allowed, translation caught; gaits in the treadmill frame), planted
feet on the floor, nothing below the floor, legs reach planted feet; joint limits (no knee / elbow hyperextension,
wrist < 80 deg); no bone turns > 40 deg in one frame (fingers excluded); loops: last == first frame and the seam
velocity no worse than the clip's own interior; contact frame = the most extended pose (reach, height or forward
metric per clip); start / end within 1 deg per bone and 3 mm of the base stance; centre-of-mass warning.

## How to add or change a clip
```python
@clip("e_example")                                   # in clips/earth.py (or any module loaded by clips/__init__.py)
def e_example():
    c = Clip("e_example", 24, "e_stance", contact=8, priority="P1", technique="what it is",
             hands=("tiger", "fist"), strike="hand_r", offsets=_eoff(hand_r=0.0))   # strike hand never lags
    c.k(4, ease="io", pel=dict(dz=-0.02, yaw=-10.0), hand_r=_hw((-0.21, 0.0, 0.96), CHAMBER_R))  # chamber
    c.k(8, ease="in3", pel=dict(y=0.08, yaw=8.0), hand_r=HS((0.1, 1.0, 0.0), ext=0.95, f=..., m=...))  # contact
    c.hold(10)                                       # 2-frame hold
    c.k(24, ease="io", base=True)                    # back to the base stance
    return c
```
Positions are metres in the A-frame (x = character left, y = forward, z = up, origin on the floor between the
feet); `HW` = hand target in that world frame, `H` = chest space (rides the torso), `HS` = direction from the
shoulder x arm length (exact extension); `f` = finger direction, `m` = palm normal, `e` = elbow pole; `F(at=(x, y),
yaw, pv='heel'|'ball'|..., lift, pitch, kyaw, kup)` = foot. Then map moves in `anim_table.py`, run the build and
`test_anim_data.py`, look at `previews/<clip>.jpg`.

## Per-element notes (what the clips do)
* **Earth - Hung Gar.** Base: sei ping ma (deep horse, 0.74 m stance, thighs ~70 % depth) with double tiger claws.
  Strikes sink first (pelvis drops 2-5 cm), the lead foot slides into a bow and the hips square at contact; stomps
  rise 6-8 frames and slam in 2 with the knees absorbing (`e_lift`, `e_wall`). Iron-wire tension (`e_stone_skin`,
  `e_guard`) breathes with a sinking exhale and a fine tremble. Bridge hands, tiger claw, crane beak / wing, hanging
  back-fist, double hammer, pressing palms, embrace-the-mountain hold, shadowless kick.
* **Water - Tai Chi (Yang).** Base: left Ward-Off (peng), weight 60 % back. Every release sits back first (front foot
  empties, toes lift on the heel) then flows forward into a bow with the rear heel turning out; the waist leads
  (pelvis offset +2 frames), arms stay rounded, contacts have a soft hold and a long exhale. Postures used: Part the
  Wild Horse's Mane, Hands Play the Pipa, Push (an), Press (ji), Roll Back (lu), Cloud Hands (procedural circles),
  Hold the Ball (rolling sphere), Single Whip (hook hand), Snake Creeps Down, Needle at Sea Bottom, Separate Foot
  heel kick, Repulse the Monkey, White Crane, Fair Lady Works the Shuttles, a turning whip.
* **Fire - Northern Shaolin.** Base: springy bladed long-fist ready (rear heel up). Chamber -> hip snap -> full
  extension at contact (exact via `HS`, 0.98-0.99 arm length) -> snap back; Tan Tui snap kick (foot in line with the
  shin at waist height), outside crescent, tornado kick, low spinning sweep on a true arc, sitting-stance charge with
  fists at the hips, double palms, great-circle Inferno, rising palm, stomps.
* **Lightning / Combustion.** Sword fingers throughout; Tai Chi sword gathering circles for the charge (procedural,
  hands separating once per circle), a stepping two-finger release, Return Current redirect with two contacts (catch
  10 / release 26) drawing the arc across the lower belly, Skybreak with a held trembling pause at the top; point ->
  fist-snap detonation, fuse focus loop, three chained stomps (contacts 12 / 24 / 36).
* **Air - Baguazhang.** Base: dragon posture, hips turned away and chest wound back to the centre, ox-tongue palms.
  Wind -> palm change -> strike at the end of the turn; real kou bu / bai bu stepping turns for full spins
  (`a_hurricane`, `w_maelstrom`; `a_spin` and `f_corona` pivot on the ball of one foot), piercing palm sliding over the forearm,
  swallow skims the water, circling-palm guard (procedural), mud-wading circle walk (stride-matched gait at 2 m/s).
* **Shared.** Wu-ji idle breathing; walk 1.4 m/s and run 5.5 m/s are stride-matched (stance feet travel back at exactly
  the design speed, Hermite swing with matched touchdown velocity, no sliding); guard shuffles / back-pedal keep the
  stagger and never cross the feet; evades are low hops with contact = apex; hits, stagger, guard break, knockdown
  (ends lying) and getup (starts lying), block shudder (small enough to use additively), parry, jump / fall / land /
  glide, flight, cross-legged hover, skate (feet glide: empty `foot_plants`), surf, salute, turns in place.

## What is real and what is stylised
* Real (public martial traditions): stances and their geometry, the sequence of each named posture, the order
  ground -> legs -> waist -> spine -> arm, weight shifts (Hung Gar down, Tai Chi back-then-forward, Shaolin forward,
  Bagua around), hand shapes, sword fingers, the stepping patterns.
* Stylised for the game: everything is compressed in time so the contact lands at the sim's startup (0.1-0.3 s instead
  of seconds); forms are cut to the one technique a move needs; spins and hops are in place (the sim moves the root);
  arcs and holds are exaggerated a little for readability from the 6-9 m camera; the charge / hold loops are invented
  to read "gathering power" (based on Tai Chi sword gathering, iron wire and the Shaolin waist chamber).

## On the Mac (owner)
1. Run the setup (`fourfold_setup.py`: fx -> character -> **animation** -> world -> audio) or, after the character
   import created `SKEL_Fighter`, run in the editor's Python console: `import fourfold.animation as fa; fa.build_all()`.
   Re-import after regenerating: `fa.build_all(force=True)`.
2. Check `/Game/Fourfold/Characters/Fighter/Anims/A_*` (131 assets) and the report notes; open a few (A_f_cross,
   A_w_lash, A_a_hurricane) on SK_Fighter. If anything fails, send `Saved/Fourfold/setup_report.json` (the
   `animation` section lists every property that could not be set).

## Integration notes for other streams
* **game:** clip timing is in `clips.json` (contact frames verified against `move_index.json` startups: 4 moves still
  hit the 1.6 rate clamp, see `test_anim_data.py`); `foot_plants` are auto-detected [start, end) ranges per foot
  (gait clips: in the treadmill frame); `skate` deliberately has none. Guard loops: `e_guard`, `w_shield`, `guard`,
  `a_guard`; perfect guards play `deflect` (Lightning: `l_redirect`). `evade_*` is used for the directional evades.
  Hand shapes: every clip's `hands` is its contact shape. Requests: [REQUESTS.md](REQUESTS.md) (stride-matched hold
  loops so `a_circle_walk` can replace `run` for the Air run modes).
* **character:** the clips only key deform bones (rotation; pelvis also location); `ik_*`, `interaction`,
  `center_of_mass` and the `ff_*` spring bones stay at rest. Extreme poses worth checking for skinning: deep horse
  (`e_*`), drop stances (`w_snake`, `a_low_palm`), lying (`knockdown`, `getup`), cross-legged `hover`, overhead arms
  (`e_overhead_slam`, `l_skybreak`, `f_inferno`), full wrist bends on push palms.
* **fx / audio:** contact frames (`clips.json` `contacts`) are where the elemental effect / impact sound should start;
  `l_redirect` has two (catch, release), `c_chain_stomp` three.

## Known gaps
* FBX size: the frozen exporter bakes all 103 bones on every frame, so `SourceArt/Animation` is ~120 MB; consider
  Git LFS for `*.fbx` (owner / architect decision).
* Motion is keyed on a proxy body; final polish (secondary motion, cloth) comes from the runtime springs and should be
  reviewed in Unreal on `SK_Fighter`. Hand-keyed motion is solid but not mocap-quality; the rig is Manny-compatible so
  bought mocap retargets via IK Retargeter (ARCHITECTURE §8.5).
* `turn_l90` / `turn_r90` and `e_shadowless_kick` are authored but not referenced by `anim_map.json` (the runtime has
  no turn-in-place state; no move uses the shadowless kick yet).
* The Unreal import is written blind (APIs verified from docs and open-source projects, see API_NOTES.md).
