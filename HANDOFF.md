# HANDOFF — Fourfold (elemental combat lab)

_Last updated: 2026-10-04 · branch `claude/kind-noether-t8enqd`_

## Current state
**Stack:** Godot **4.7.2** (MIT), GDScript, Mobile renderer (Metal on iOS). The Godot project in `game/` is the source of
truth; iOS ships via Godot's exported **Xcode project** (`tools/scripts/export_ios.sh`). This was built in a Linux cloud
container (no Xcode, Blender app or simulator), so the Godot project was run, tested and rendered here, and the same project
exports to Xcode on a Mac.

| Area | State |
|---|---|
| Simulation | Authoritative 60 Hz rules engine: material bodies with identity/provenance, thermal phases with hysteresis, control contests, hit dedup, energy & mass ledgers, bounded lightning conduction graph. Deterministic. |
| Flagship | Stone → (magma grip) → molten blob → poured lava wave → rival draws heat → hot rock → reusable by either fighter. Failure paths: early/late press, too heavy, out of Focus, interrupted, partial cooling, walls/ledges/pool. |
| Elements | Earth, Water, Fire (+ lightning), Air: attack (tap/hold), guard variant, technique, evade; ice, steam, conduction, redirect. |
| Opponent | Perception-delayed, kit-limited AI with counters (draw, wall, redirect, dodge), drills; counters player waves in ordinary play (tests). |
| Controls | Touch (floating stick, camera area, attack/guard/evade/technique with drag-aim and cancel, element chips, safe areas, iPad sizing, left-handed), keyboard/mouse, gamepad. |
| Presentation | Rigged fighter (22 bones, 47 clips), 10 VFX families + arena shaders, 62 original SFX, HUD with threat arrows, practice timing overlay, adaptive quality. |
| Progression | 10 scenarios + Lab mode, 5 mastery challenges unlocking techniques, saved progress/settings. |
| Verified here | 188 sim tests + 50 UI tests pass; 10-min soak clean (0 errors, flat memory); multi-agent review: 48 confirmed issues fixed and independently re-verified (`docs/REVIEW.md`). |
| **Not verified** | Anything on a real iPhone/iPad or the iOS simulator: GPU frame time, thermals, touch feel, haptics, Metal shader quirks. |

## Build / run
| What | Command (repo root) |
|---|---|
| Godot (Mac) | Install Godot 4.7.2; for iPhone also Editor ▸ Manage Export Templates ▸ download 4.7.2 |
| Play on desktop | Open `game/project.godot` in Godot ▸ Play, or `tools/scripts/godot.sh` (finds `/Applications/Godot.app` on macOS; `GODOT_BIN` overrides) |
| Touch overlay on desktop | `tools/scripts/godot.sh -- --touchui` |
| Pick a scenario | `-- --scenario=<id>` (ids in `game/scenarios/scenarios.gd`); in game: Esc ▸ Practice |
| Sim tests | `tools/scripts/godot.sh --headless -s res://tests/run_tests.gd` |
| UI tests | `tools/scripts/godot.sh --headless -s res://tests/ui/run_ui_tests.gd` |
| Script check | `tools/scripts/godot.sh --headless -s res://tests/check_scripts.gd` |
| Xcode project | `tools/scripts/export_ios.sh <APPLE_TEAM_ID>` → `build/ios/Fourfold.xcodeproj` → Signing & Capabilities ▸ your team ▸ Run |
| Record evidence | `tools/scripts/godot.sh --render --write-movie out.avi --fixed-fps 30 -- --autoplay=<flagship|show_earth|show_water|show_air|show_fire>:<s>` |
| Soak | `tools/scripts/godot.sh --headless --fixed-fps 60 -- --autoplay=soak:600 --perf=<file.json>` |

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
- No foot IK (clips are authored with planted feet and speed-matched; uneven-ground planting is future work).
- Fighter is a clean training mannequin (mitten hands, no face detail, no cloth sim). Arena is a block-out courtyard.
- Haptics use `Input.vibrate_handheld`; patterned Core Haptics needs a native iOS plugin.
- Metal, sand, mist, vortex are documented extension points only (`docs/COMBAT_SPEC.md` §10).

## Paused work (resume here)
A quality pass (character mesh/textures, animation runtime, environment textures/props/lighting, material-physics VFX,
in-game Dev/Test panel) was started and paused on 2026-10-04. Its unfinished changes are saved in `wip/` (see `wip/README.md`)
and are NOT applied to the playable code. To resume: `git apply wip/quality-pass-wip.patch`, then re-run `tools/workflows/quality_pass.js`.

## Next concrete steps (priority order)
1. On the Mac: export, run on the oldest available iPhone, play Molten Exchange 10–15 min with Settings ▸ show debug; follow `docs/PERF.md` checklist.
2. Human playtest of feel: grip window (0.25 s), draw rate (260 HU/s), wave speed/steer, guard perfect window (0.18 s). Log changes in `docs/TUNING_LOG.md`.
3. Foot IK on steps/terrace edges (Godot `TwoBoneIK3D` on thigh/shin/foot), then content: a second arena and opponent kit.

## Docs
`docs/COMBAT_SPEC.md` (rules, units, flagship), `docs/CONTROLS.md`, `docs/ANIMATION.md`, `docs/VFX.md`, `docs/AUDIO.md`,
`docs/ASSET_MANIFEST.md`, `docs/PERF.md`, `docs/TUNING_LOG.md`, `docs/REVIEW.md`.
