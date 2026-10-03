# HANDOFF — Fourfold (elemental combat lab)

_Last updated: 2026-10-03 (cloud session, Linux container)_

## Current state
- **Stack:** Godot **4.7.2** (MIT), GDScript, Mobile renderer (Metal on iOS). Godot project = source of truth: `game/`.
  iOS ships via Godot's exported **Xcode project** (`tools/scripts/export_ios.sh`). Chosen because this session runs on
  Linux (no Xcode/Blender/simulator here), the Godot project runs/tests/renders here, and the same project exports to Xcode on the Mac.
- **Playable here:** desktop build of the lab (keyboard/mouse/gamepad + touch overlay), all scenarios, autoplay capture.
- **Built:** authoritative 60 Hz combat sim (material bodies, thermal phases with hysteresis, contests, energy/mass ledgers,
  conduction graph), 4 elements, flagship stone→lava→wave→rock exchange, sparring AI, touch controls, HUD, audio, scenarios,
  mastery unlocks, save/load, iOS export.
- **Not verified:** anything on a real iPhone/iPad or the iOS simulator (no Mac here): performance, touch feel, haptics, Metal rendering.

## Build / run
| What | Command (repo root) |
|---|---|
| Get Godot (Mac) | Install Godot 4.7.2 + export templates (Editor ▸ Manage Export Templates) |
| Run desktop | `tools/scripts/godot.sh` (Linux path) or open `game/project.godot` in Godot and press Play |
| Run with touch overlay on desktop | `tools/scripts/godot.sh -- --touchui` |
| Start a scenario | `... -- --scenario=molten_exchange` (ids in `game/scenarios/scenarios.gd`) |
| Sim tests | `tools/scripts/godot.sh --headless -s res://tests/run_tests.gd` |
| UI tests | `tools/scripts/godot.sh --headless -s res://tests/ui/run_ui_tests.gd` |
| Script check | `tools/scripts/godot.sh --headless -s res://tests/check_scripts.gd` |
| Export Xcode project | `tools/scripts/export_ios.sh <APPLE_TEAM_ID>` → `build/ios/Fourfold.xcodeproj` → open in Xcode, choose device, Run |
| Capture evidence | `tools/scripts/godot.sh --render -- --autoplay=flagship:20 --shots=<dir> --perf=<file>` |
On macOS set `GODOT_BIN=/Applications/Godot.app/Contents/MacOS/Godot` for the scripts.

## Versions
Godot 4.7.2.stable.official.ed1daf0bf · Blender 4.5.14 LTS (bpy module, Linux) · Python 3.11 · Mesa lavapipe (Vulkan, CPU) for headless renders.

## Next concrete step
On the Mac: `tools/scripts/export_ios.sh <TEAM_ID>`, open the Xcode project, run on the oldest available device, play
"Molten Exchange" for 10–15 min with the debug overlay on (Settings ▸ show debug) and record frame pacing/thermal state
(see `docs/PERF.md` checklist).

## Docs
`docs/COMBAT_SPEC.md` (rules, units), `docs/CONTROLS.md`, `docs/AUDIO.md`, `docs/VFX.md`, `docs/ANIMATION.md`,
`docs/ASSET_MANIFEST.md`, `docs/TUNING_LOG.md`, `docs/PERF.md`.
