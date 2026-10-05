# HANDOFF — Fourfold (elemental combat lab)

_Last updated: 2026-10-05 · branch `claude/kind-noether-t8enqd`_

## Current state
**Stack:** Godot **4.7.2** (MIT), GDScript, Mobile renderer (Metal on iOS). The Godot project in `game/` is the source of
truth; iOS ships via Godot's exported **Xcode project** (`tools/scripts/export_ios.sh`). This was built in a Linux cloud
container (no Xcode, Blender app or simulator), so the Godot project was run, tested and rendered here, and the same project
exports to Xcode on a Mac.

| Area | State |
|---|---|
| Simulation | Authoritative 60 Hz rules engine: material bodies with identity/provenance, thermal phases with hysteresis, control contests, hit dedup, energy & mass ledgers, bounded lightning conduction graph, interaction engine (threat power vs counter power with full / partial / fail bands). Deterministic. |
| Moveset | **16 sub-elements × 10 slots, all bound** (Earth: Stone/Metal/Sand/Magma · Water: Water/Ice/Mist/Plant · Fire: Flame/Blue/Lightning/Combustion · Air: Gust/Vortex/Vacuum/Sound), 157 bound moves, charge tiers T0–T3, 1265 interaction cells (164 core + 1101 kit). Sub-element 0 keeps the legacy moves exactly. Per-kit docs: `docs/kits/{earth,water,fire,air}.md`. |
| Counters | Every element answers every Lab threat (21 threats × 4 elements, `test_integration_matrix`); strength scales with mass, heat, speed and charge (a palm gust bends lava, a T3 gale / Cyclone Fortress sets it to rock). Armor stances hold through chip hits. |
| Opponent | Matrix-driven planner AI (`docs/AI.md`): Novice / Adept / Master presets, honest perception (charge tiers telegraphed), counters by predicted outcome, offence by situation; Free Spar difficulty + kit pickers; AI-vs-AI duel autoplay. |
| Controls | Touch: ATTACK tap/hold/flick (thrust/ground/sweep), GUARD + push/sink flicks, TECHNIQUE + second-finger shape tap, EVADE tap/hold, element chips + sub-element ring; keyboard and gamepad mirror it (`docs/CONTROLS.md`). |
| Lab | Lab scenario + dev panel (` / F2): spawner (39 threats, inert or thrown by the rival), move list with **Try** (every move through the real input path), 28 combos with live detection, counter-matrix viewer, live tuning. |
| Presentation | Rigged fighter (42 bones, 59 clips incl. 12 `mv_*` move clips, foot IK / planting), VFX for every body family and outcome (`docs/VFX.md` moveset layer), 116 SFX, HUD with tier ring, statuses, resources. |
| Verified here | **528 sim tests + 95 UI tests pass**, 182 scripts load, 16 animation tests, VFX smoke test; 5-min soak (all elements / subs / slots / gestures): 0 errors, sim p95 0.50 ms, memory 192 → 200 MB. |
| **Not verified** | Anything on a real iPhone/iPad or the iOS simulator: GPU frame time, thermals, touch feel, haptics, Metal shader quirks, audio by ear. |

## Build / run
| What | Command (repo root) |
|---|---|
| Godot (Mac) | Install Godot 4.7.2; for iPhone also Editor ▸ Manage Export Templates ▸ download 4.7.2 |
| Play on desktop | Open `game/project.godot` in Godot ▸ Play, or `tools/scripts/godot.sh` (finds `/Applications/Godot.app` on macOS; `GODOT_BIN` overrides) |
| Touch overlay on desktop | `tools/scripts/godot.sh -- --touchui` |
| Pick a scenario | `-- --scenario=<id>` (ids in `game/scenarios/scenarios.gd`); in game: Esc ▸ Practice |
| Sim tests | `tools/scripts/godot.sh --headless -s res://tests/run_tests.gd` (528; `-- test_kit_fire` runs a subset) |
| UI tests | `tools/scripts/godot.sh --headless -s res://tests/ui/run_ui_tests.gd` (95) |
| Script check | `tools/scripts/godot.sh --headless -s res://tests/check_scripts.gd` (182) |
| Animation / VFX | `... -s res://tests/anim/run_anim_tests.gd` (16) · `... -s res://tests/vfx/vfx_smoke_test.gd` |
| Xcode project | `tools/scripts/export_ios.sh <APPLE_TEAM_ID>` → `build/ios/Fourfold.xcodeproj` → Signing & Capabilities ▸ your team ▸ Run |
| Record evidence | `tools/scripts/godot.sh --render --resolution 1280x592 --write-movie out.avi --fixed-fps 30 -- --autoplay=<flagship\|show_owner\|show_<element>[_<sub>]\|duel>:<s> --quality=2 [--shots=<dir>]` |
| Soak | `tools/scripts/godot.sh --headless --fixed-fps 60 -- --autoplay=soak:300 --perf=<file.json>` (random play over every element, sub-element, slot and gesture) |

Evidence (real renders from this build): `docs/media/` — flagship + four element videos and stills.

## Working locally on a Mac
1. Get the repo: `git clone -b claude/kind-noether-t8enqd https://github.com/baysan2608/ClaudeToken.git Fourfold` (or GitHub ▸ branch ▸ Code ▸ Download ZIP).
2. Install **Godot 4.7.2** (standard, not .NET) into `/Applications`; for iOS export also Editor ▸ Manage Export Templates ▸ 4.7.2.
3. Open `game/project.godot` in Godot (first open imports assets, ~1 min), press Play. Scripts in `tools/scripts/` work as-is on macOS.
4. Optional, only to regenerate assets: Blender 4.5 LTS (`blender -b -P tools/blender/build_fighter.py`), Python 3 + `pip install numpy scipy`
   (`python3 tools/audio/synth_sfx.py`).
5. `CLAUDE.md` has the conventions and test commands for Claude Code sessions on the repo.

## Versions
Godot 4.7.2.stable.official.ed1daf0bf · Blender 4.5.14 LTS (bpy module) · Python 3.11 · Mesa lavapipe (CPU Vulkan) for renders.

## Known limitations
- Renders here are CPU-rasterised; lighting/shadow quality and all frame timings must be judged on device.
- Haptics use `Input.vibrate_handheld`; patterned Core Haptics needs a native iOS plugin.
- New sounds (54) were checked by measurement / spectrogram only, never by ear.
- Fingers are grouped tubes (blunt fist); linear-blend skinning pinches at wide leg splits; sash / top-knot springs are
  runtime-only.
- See "Known gaps (moveset)" below.

## Quality pass (resumed and landed)
The 2026-10-04 quality pass (`wip/` patch) was re-applied and finished: character asset (face, hair, garments, finger bones,
spring bones), animation runtime (foot IK and planting, pivots, hit springs), environment (sky, pool water, scenery), VFX.
`wip/` is kept only as history.

## Moveset expansion (2026-10-05): built and integrated
Design: **`docs/MOVESET.md`** (input grammar, charge tiers, counter rule §5, matrix §8, combos §9). Built by parallel streams
(core, four element kits, VFX, audio, AI, UI/Lab, character, animation, environment) and integrated on 2026-10-05.
Per-element move lists, numbers and deviations: `docs/kits/{earth,water,fire,air}.md`; AI: `docs/AI.md`; controls: `docs/CONTROLS.md`.

### Play and test the new moves (Mac)
1. `tools/scripts/godot.sh -- --scenario=lab` (or in game: Esc ▸ Practice ▸ **Lab**). All 16 sub-elements are open; three
   dummies and a passive rival stand in the yard. Add `--touchui` to try the touch layout with the mouse.
2. Keyboard: **1–4** element, the **same number again** (or Q / E) cycles the sub-element, **J** strike (hold = T1/T2/T3; the ring
   on the button shows the tier), **U / N / H** thrust / ground / sweep, **K** guard (K+J push, K+N sink), **L** technique
   (J while held = shape), **Space** evade (hold = movement mode), Tab target, Esc pause. Gamepad: X / Y / LT / B, RB guard,
   RT technique, A evade, LB + d-pad sub-element (full table: `docs/CONTROLS.md`).
3. **Dev panel: ` (backquote) or F2** (or "Dev" in the pause menu): *Spawn* throws any of 39 threats at you (stone 20–200 kg,
   lava wave, metal disc, sand slug, water wave, fireball, tornado, bolt...), *Moves* lists every move with **Try** (plays it
   through the real input path, at any tier), *Combos* (28, live success detection), *Matrix* (any threat vs any counter: TP, CP,
   band, outcome; "Stage it" sets it up), *Tuning* (live edits of every move number, saved to `user://tuning.cfg`).
4. **1v1 vs AI:** Esc ▸ Practice ▸ **Free Spar**; pick the rival's difficulty (Novice / Adept / Master) and kit below the entry.
   Turn Lab mode on (Practice list) to give yourself every technique in Free Spar.
5. Watch the owner's examples played by the game itself: `tools/scripts/godot.sh -- --autoplay=show_owner:55` (charge tiers; a thrown
   stone split & spiked back / swallowed / carried back by a wave / melted to lava / deflected by wind; a palm gust failing against
   lava and a Cyclone Fortress setting it to rock; a wall melted and pushed back; a bolt grounded and a Storm Bolt through stone).
   Per sub-element: `show_earth`, `show_earth_metal|sand|magma`, `show_water`, `show_water_ice|mist|plant`, `show_fire`,
   `show_fire_blue|lightning|combustion`, `show_air`, `show_air_vortex|vacuum|sound`; AI vs AI: `--autoplay=duel:60`.
   `SHOWCASE_TRACE=1` prints the key sim events (interactions, hits) of a showcase.

### Known gaps (moveset)
- Feel and balance are untested by humans: Magma is strong in the open, the 12 kg metal satchel runs dry fast, Flame T2/T3 sit
  at 1.4 / 2.2 s (a core test pins a 1.33 s blaze), Earth T2 at 1.1 s.
- Some sim hooks are approximations: Bulwark thickening applies when the wall is hit; Veil's +20 % balance bonus has no hook;
  cone / beam volumes don't read the zones they cross (fog dampens fire bodies, not instant flame cones); evade moves have no tiers;
  Wind Guard blocks thrown fireballs (legacy clean block) instead of feeding them.
- AI: bank shots use only the arena walls; in AI-vs-AI duel the player-side brain's attack aim is not applied; only water kits
  walk to the pool to refill.
- Sub-elements are not gated by progression (the HUD / ring / keys honour a lock mask; nothing sets it yet).
- Lab combo `result` signatures are best guesses for kit-specific combos; check them in play.

## Next concrete steps (priority order)
1. On the Mac: export, run on the oldest available iPhone; play the Lab and Free Spar (Master) 10–15 min each with Settings ▸ show
   debug; follow the `docs/PERF.md` checklist (watch zone-heavy moments: tornado + fire field + fog, Cyclone Fortress, Disc Storm).
2. Human playtest of feel per sub-element (Lab ▸ Moves ▸ Try, then Free Spar); log every change in `docs/TUNING_LOG.md`.
3. Listen to the new SFX on device; tune the per-element charge pitch.
4. Progression for sub-elements (lock mask), then content: a second arena.

## Docs
`docs/COMBAT_SPEC.md` (rules, units, flagship), `docs/MOVESET.md` (moveset, counter matrix, engine contract), `docs/CONTROLS.md`, `docs/ANIMATION.md`, `docs/VFX.md`, `docs/AUDIO.md`,
`docs/ASSET_MANIFEST.md`, `docs/PERF.md`, `docs/TUNING_LOG.md`, `docs/REVIEW.md`, `docs/AI.md`, `docs/kits/*.md`.
