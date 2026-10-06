# Animation stream - requests to other streams

## R1 (game) - stride-match hold loops that carry a design speed
* **File:** `unreal/Source/Fourfold/Private/Logic/FFGAnimDirector.cpp` (`hold_is_gait`, around the `Clip(mc.hold)` lookup).
* **Change:** treat a hold clip as stride-matched locomotion not only when it is `lib->walk` / `lib->run`, but whenever
  its `ClipDef::speed > 0` (from `clips.json` `speed`): play it as a loop at rate = actual ground speed / `speed`
  (clamped like locomotion), with its own `foot_plants`.
* **Why:** `a_circle_walk` (Baguazhang mud-wading circle walk, design speed in `clips.json`) is the intended look for
  the Air run modes (`gust_tailwind`, `vacuum_slipstream`; MARTIAL_ARTS §4.13 / §4.15). Today a non-walk/run hold
  loops at rate 1 while the sim runs at ~7 m/s, so its feet would slide; `anim_map.json` therefore maps those two moves
  to the stride-matched `run` until this lands. After the change, set their `hold` to `a_circle_walk` in
  `Tools/blender/animation/anim_table.py` and re-run `anim_map_gen.py` (no other change needed).

## R2 (game) - optional: per-move playback-rate hints
Not needed for correctness. Clips whose catalogue contact is much longer than the sim startup (rate clamped at 1.6,
listed as warnings by `Tools/blender/animation/test_anim_data.py`) skip part of their anticipation, which is the
documented runtime behaviour (ARCHITECTURE §8.4). No change requested; listed so the owner knows where to look if a
move feels clipped.
