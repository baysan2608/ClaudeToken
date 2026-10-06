# character stream - PROGRESS (checkpoint; read first on resume, append after every sub-step)

Scratch: /tmp/claude-0/-home-user-ClaudeToken/37fdfe41-9b78-53ea-8620-b33d5491ff57/scratchpad/ue/character
(final.blend = last full build scene, final_build.log, validate.json).

## State found at 15:28 (resume after limit reset)
- Run 1 finished a full build at 14:34 (516 s): SK_Fighter.fbx + _LOD1/_LOD2.fbx, 27 textures, 6 previews,
  character.json, build_report.json, README.md, API_NOTES.md. validate.json: failures [] (tris 29298/14576/5789).
- Commit 4f84acf ("character done") contains it. Nothing uncommitted in owned paths.

## Resume checklist (tick when re-verified this run)
- [x] look at previews (quality pass) -> issues: tunic shows a sternum cleft over the pecs (reads female), hair-cap
      edge spikes beside the ears, fist preview pose has thumb sticking out, lips parted, thumb looks darker
- [x] validator green (15:35)
- [x] rig self-test green (15:36)
- [x] mock dry run of fourfold.character:build_all (exit 0, 'created 39 failed 1' by design)
- [ ] decide on improvements (log below), then final report

## Log (newest last)
- 15:45 polish pass planned (review close-ups via scratch/polish/review.py <blend> <tex_dir> <out> shots [pose];
  quick build: build_character.py --no-previews --quick --out scratch/outq --blend scratch/outq.blend ~20 s):
  [x] P1 tunic (ch_garments.bridge_hollows, verified in quick build): bridge the sternum cleft / spine groove (outward-only smoothing on the torso shell) - reads female now
  [x] P2 hair tail (ch_hair._tail_clumps = one flat 4-lobe lock with split end; verified): needle-point single tube reads as a horn -> 3 clumps, fuller, blunter ends (ch_hair.build_knot_and_tail)
  [x] P3 ears (ch_body RECIPE ears wing/flap-decr, shape-round; files vendored via fetch_makehuman.py): pointed helix tops stick out in front view -> MakeHuman ear targets (round / flap-decr), vendored + PROVENANCE
  [x] P4 mouth (texture only: LIP colour nearer skin, softer upper border; geometry is closed): dark crease reads as parted lips -> expression unit mouth-compression small weight and/or lighter crease AO
  [x] P5 preview fist (ch_pose.FIST_THUMB, solved by scratch/polish/thumbopt.py; thumb folds over the fist): thumb sticks out (ch_pose FIST thumb) - preview only
  [ ] full rebuild (~9 min) -> validate -> rig test -> mock -> README measured numbers -> final report
- 15:45 all P1-P5 code changes in place; quick build green. NEXT: full build (background) -> look at previews.
- 15:50 README updated (polish notes + review_closeups.py moved into Tools/blender/character). Full build running (final_build.log).
- 16:00 full build done (745 s, EXIT 0, tris 29300/14580/5787). NEXT: look at previews, validate, rig test, mock, docs numbers.
- 16:05 findings: the "bust" in hands_closeup.jpg was an illusion of the two mirrored fist panels meeting at the
  sheet's centre (chest close-ups are clean); bridge_hollows kept (harmless, validator green). Ear tops still pointed
  in front view -> RECIPE ears shape-round 0.70 + shape-pointed -0.35 (vendored l/r-ear-shape-pointed). Quick build ok.
- 16:08 full build #2 started (final_build.log); after it: validate, rig test, mock, README numbers, final report.
- 16:22 full build #2 done (510 s, EXIT 0, tris 29300/14582/5787). Fixed AO-cache staleness (mesh+UV signature in ao_cache.npz). NEXT: verify + previews look + docs.
- 16:30 VERIFIED after build #2: validator PASSED (validate_run3.log), rig self-test OK, mock dry run exit 0
  ("created 39 failed 1" by design), character.json rewritten (same keys), README measured numbers updated.
  AO cache fix tested (2nd quick run reused the bake: ao_bake_s 0.0).
- STATE: stream COMPLETE again. If resumed: nothing open. Optional next polish ideas (not started): eyes a touch more
  open (expression units eye-*-opened-up, fetched to scratch/polish/mhtest), hair-cap rim light reads whitish in
  front views (lighter cap edge in ch_textures.gen_hair), ear-top notch at the cap edge (smaller now).
- 16:35 final report returned. Stream complete; nothing open.
