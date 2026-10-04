# Paused work: quality pass (2026-10-04)

`quality-pass-wip.patch` holds the in-progress (unfinished, unreviewed) changes from the quality pass, taken against
commit 3a40c6c. The playable code on this branch is unchanged by it.

Contents at pause time:
- Animation runtime (Opus · high): new `game/presentation/anim/` (locomotion blender, leg IK, secondary motion,
  hit reactor, debug draw/hotkeys) + `game/presentation/fighter_view.gd` changes. Compiled and ran a 20 s headless
  flagship with 0 script errors; not yet visually reviewed.
- Character asset (Sonnet · high): Blender script work (new body/garments/character modules, skeleton additions);
  `fighter.glb` was not yet regenerated.
- Environment, VFX and test-controls streams had not written files yet.

Resume:
1. `git apply wip/quality-pass-wip.patch`
2. Re-run the workflow script `tools/workflows/quality_pass.js` (five streams + reviewers; models/efforts inside),
   telling each stream to continue from the existing files.
