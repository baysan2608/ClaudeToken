# Animation stream — PROGRESS (checkpoint file; read first on resume)

## State at resume #2 (2026-10-06)
Previous run left (no PROGRESS.md existed):
- Tools/blender/animation/: ffa_math, ffa_rig, ffa_solver, ffa_dsl, ffa_hands, ffa_proxy, ffa_render, ffa_validate, ffa_export, build_animation.py, clips/{bases,shared,earth,air,fire,water,hand_shapes}.py
- SourceArt/Animation: 7 FBX (a/e/f/w_stance, guard, hand_fist, idle), previews/*.jpg (6), validation_report.json
- No clips.json / anim_map.json / Unreal import script / docs yet.

## Done
- Pipeline end-to-end: build_animation.py (validate -> sheets -> FBX -> clips.json merge -> anim_map) works; 27 clips exported, FBX re-import check ok (frame 0, 103 bones).

## Current item
C: P1 clips. DONE+exported: earth P1 (13 clips, clips/earth.py). water P1 (7 clips; w_snake c8/26f). fire P1 (12 clips) exported. Validator: planted slide now = least-moving floor landmark (pivots allowed). gust_tailwind/vacuum_slipstream hold -> run (REQUESTS.md R1). air P1 (12 clips; retimed a_turn_palm c12, a_wall_push c10, a_downdraft c7/22f, a_spin c12, a_gather c16/40f; turns via stepping_turn()/base_turned() with continuous yaw) exported. NOW: shared P1 (clips/reactions.py): hit_light_back guard_break salute flight hover skate surf. Then P2. P0 MP4 previews rendering (build --videos P0 --no-export --no-json). Next P1 order: earth (e_thrust e_ground_rise e_ground_slap e_sweep e_push e_sink e_overhead_slam e_disc_flick e_chain_whirl e_lob e_magma_hold e_burrow e_stone_skin), water (w_press w_ground w_single_whip w_snake w_clench w_heel_kick w_repulse), fire (f_column f_inferno f_low_sweep f_crescent_kick f_stomp f_dash f_hop f_needle l_redirect l_skybreak l_ground c_point), air (a_hurricane a_pierce a_low_palm a_turn_palm a_wall_push a_downdraft a_spin a_circle_walk a_gather a_pluck a_clap a_roar), shared (hit_light_back guard_break salute flight hover skate surf).

## Next steps
1. Author remaining P0 clips (list in Plan B), review strips, export with build_animation.py --only, run tests.
2. Then P1 (MARTIAL_ARTS §3 'P1' rows), P2, MP4s for P0 (--videos P0), README completion.

## Decisions

## Audit (resume #2)
- Toolkit works: 27 clips solve+validate in 5 s (numpy solver, no bpy). Renderer = PIL software rasteriser (ffa_render).
- Failing: walk/run/strafe_l/a_stance loop-seam velocity (states() appends the wrap key with ease 'io'); run feet sink 5 mm.
- check_fbx: 'first_frame 1' is Blender's import anim_offset=1 default (file itself starts at 0); tol 0.05 deg too tight.
- Game runtime (Source/Fourfold/Private/Logic/FFGAnimLibrary.cpp) reads anim_map keys: moves.<id>.{startup,hold,release,perfect,hands{l,r},tiers{n:{..}},modes{m:{..}}};
  legacy shared ids use "guard@<el>" / "evade@<el>"; startup "evade_*" = directional evade; fallbacks.{slot,clips}.

## Plan (sub-steps; tick when done)
- [x] A1 fix loop seam ease + run sink + check_fbx offset/tol (gait swing = Hermite w/ matched velocity; seam check relative to interior)
- [x] A2 anim_table.py (ideal map, all 160) + anim_map_gen.py (resolves vs built clips, writes anim_map.json + docs/animation/COVERAGE.md) + test_anim_data.py + Content/Python/fourfold/animation/__init__.py + test_unreal_import.py (scripted fake editor) + mock dry run: ALL PASS
- [x] A3 README/API_NOTES skeleton (README to be completed at the end)
- [x] B  P0 clips (all 66 exported + validated; w_draw retimed c14/24f, a_updraft c10/26f to fit sim startups): shared(strafe_r walk_back evade_l/r/back/fwd jump fall land hit_light_front hit_heavy knockdown getup stagger block_impact deflect glide)
         water(w_lash w_freeze w_push w_shield w_draw w_hold w_release) fire(f_jab f_cross f_charge f_palm_burst f_snap_kick f_heat_draw f_thermal_hold f_pour l_charge l_release)
         air(a_palm a_double_palm a_updraft a_dash a_guard)
- [x] C  P1 clips   - [x] D P2 clips   - [x] E final docs / previews / MP4s

## How to run (resume cheat-sheet)
  B=/home/user/tools/bpyenv/bin/python; cd /home/user/ClaudeToken/unreal
  $B Tools/blender/animation/build_animation.py --only "w_*" --no-export --review <dir>   # iterate (no export)
  $B Tools/blender/animation/review.py <clip> --every 3 --views side,game                   # film strip -> scratch/review
  $B Tools/blender/animation/build_animation.py --only "w_*"        # export FBX + sheets + merge clips.json + anim_map
  python3 Tools/blender/animation/test_anim_data.py ; python3 Tools/blender/animation/test_unreal_import.py

## IN PROGRESS (toolkit robustness, started after air P1 + shared P1 authored)
- Added validator check "pop": no body bone may turn > 40 deg between frames (fingers excluded). It exposed ~70 clips
  with 1-frame flips. Causes: (a) hand orientation channel lerped as vectors (palm down->up passes through 0 -> flip),
  (b) elbow/knee pole parallel to the reach direction (degenerate bend plane), (c) forearm twist wrap at +-180,
  (d) in3/in4 eases over 2-3 frame spans (very fast last frame).
- Fixes DONE: soft twist limit; bend plane from the pole; pole continuity via ctx (two_bone prev_v); hand orientation
  slerp (ffa_dsl.hand_orient_lerp); hand frame rate limiter (_limit_hand_turn 30 deg/f); ANATOMICAL ROLL CHANNEL:
  every key's forearm roll is solved fresh and stored as st['tw_l'/'tw_r'], interpolated as a number (rate-limited
  30 deg/f), the solver uses it (swing = rot_between of the finger axis, leftover roll <= 20 deg fading at 180).
  Base mismatches: 0. Remaining: ~60 arm pops from too-fast authored elbow/shoulder moves.
- DONE: joint speed limiter (ffa_dsl._limit_joint_speed, 34 deg/f) on arm chains (anchored at clip ends + contact
  frames) and on airborne legs (lift > 1.2 cm, reverted where the foot would go through the floor); gaze fades for
  look targets behind the shoulder (spins); f_low_sweep re-authored on a true arc (contact 17); f_hop / a_updraft /
  a_spin pelvis offsets 0. ALL 117 CLIPS VALIDATE (no pops > 40 deg/f, seams, bases).
- DONE: full re-export; P2 clips (clips/p2.py: turn_l90/r90 e_spatter e_shadowless_kick w_maelstrom w_crane w_shuttle f_tornado_kick f_corona l_fan c_toss c_fuse_loop c_chain_stomp a_rising_guard) exported. 131 clips (P0 66, P1 51, P2 14), anim_map 0 stand-ins, data test + fake-editor import test pass. check_fbx tol 0.25 deg (FBX float precision).
- DONE: README.md complete, CLIPS.md generated (anim_map_gen.py), hover re-authored cross-legged. P0 MP4 re-render running in background (build --videos P0 --no-export --no-json --no-sheets).
- DONE: run re-authored (was lunge-like): gait() got `bias` (stance window behind hips); run s=0.30 z=-0.05 lift=0.20
  to=-46 bias=0.16 -> validates (step 34.7, seam 8.7/1.0). NOT yet exported; background MP4 job rendered OLD run.
- DONE (stream complete): run exported (+ --check-fbx 0.07 deg), sheet + MP4 refreshed; all 56 non-hand P0 MP4s
  (640x360, 60 fps) present; test_anim_data OK (131 clips, 160 moves, 4 timing warnings), test_unreal_import OK,
  mock dry run OK; frozen files untouched; IP term scan clean. Final report delivered.
- If resumed: nothing pending. Optional polish ideas: R1 (a_circle_walk for air run modes once game lands it),
  shrink FBX size (frozen exporter writes ~0.9 MB/clip), use turn_l90/r90 + e_shadowless_kick in the map.
- Shared P1 clips (hit_light_back guard_break salute flight hover skate surf) are authored in clips/reactions.py but
  NOT exported yet (skate needs its arm fix verified).
