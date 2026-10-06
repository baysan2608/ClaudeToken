# NEXT SESSION — paste this into Claude Code on the MacBook

> How to start: Terminal → `cd ~/Fourfold && git fetch origin && git checkout -f -B claude/loving-dirac-0gtvnt origin/claude/loving-dirac-0gtvnt && claude`
> then paste everything inside the box below as your first message.

---

```text
You are continuing "Fourfold" locally on my MacBook (Apple silicon M3/M4, 24 GB). Repo: ~/Fourfold
(GitHub baysan2608/ClaudeToken, branch claude/loving-dirac-0gtvnt — always work and push on this branch).
Keep replies short and clear; keep token use low; if unsure, search the web or ask me.

GOAL
An original elemental martial-arts fighting game for iPhone/iPad (and Mac), now being rebuilt in UNREAL ENGINE 5.8.3.
Super high quality: smooth physical animation, beautiful fire/water/earth/air VFX, good sound. 4 elements x 4
sub-elements x ~10 moves (~160), hold-to-charge tiers, and a coherent physical counter matrix (a weak gust can't
stop lava, a tornado can; lightning grounded by thin stone at T1, blasts through at T2; melt a wall and push it
back; a thrown stone can be split & spiked back / sunk / waved back / melted to lava / wind-deflected).
Movement is based on REAL martial arts: Water = Tai Chi, Earth = Hung Gar, Fire = Northern Shaolin,
Air = Baguazhang, Lightning = sword-finger energy work. 100% ORIGINAL IP: never use names, characters, terms,
symbols or designs from any TV franchise (no "bending", "Avatar", nation names) in code, assets, docs or commits.
Test modes first: Lab sandbox + 1v1 vs AI. Mocap may be bought later; skeleton is UE5-Mannequin compatible.

STEP 0 — ORIENT (do this first, quietly, then give me a 5-line status)
1. Read in this order: NEXT_SESSION.md (this file), unreal/HANDOFF.md (if present), unreal/README.md,
   unreal/docs/ARCHITECTURE.md, unreal/docs/MAC_SETUP.md, then skim unreal/docs/*/README.md and every
   unreal/docs/*/PROGRESS.md and REQUESTS.md (open cross-stream requests), unreal/docs/core/PORT_STATUS.md,
   docs/MOVESET.md (design + counter matrix), unreal/docs/MARTIAL_ARTS.md.
2. Find the apps on this Mac (don't assume; verify):
   - Unreal 5.8: `ls -d "/Users/Shared/Epic Games"/UE_5.*` (last known: /Users/Shared/Epic Games/UE_5.8).
     Editor: <UE>/Engine/Binaries/Mac/UnrealEditor.app ; build: <UE>/Engine/Build/BatchFiles/Mac/Build.sh
   - Xcode: `xcode-select -p` and `mdfind "kMDItemCFBundleIdentifier == 'com.apple.dt.Xcode'"`.
     Last build used a BETA Xcode at ~/Downloads/Xcode-beta.app (Mac SDK 27, Apple clang 21). If odd compiler
     errors appear, check Epic's supported Xcode for 5.8 and suggest installing the release Xcode into /Applications.
   - Optional tools: Blender 4.5 LTS (`mdfind "kMDItemCFBundleIdentifier == 'org.blenderfoundation.blender'"`),
     Godot 4.7.2 (/Applications/Godot.app), cmake + ninja (`brew install cmake ninja` if missing), python3.
   - Write what you found into unreal/docs/LOCAL_ENV.md (paths + versions) and use those paths from then on.
3. `git log --oneline -15` to see the latest work.

HOW TO BUILD / RUN / TEST
- One command (pull, build editor, open Unreal + run asset setup): `bash unreal/Tools/mac/run.sh`
  (UE_ROOT=/path overrides the engine path). Build log: logs/build_editor.log. You can read it directly.
- Manual editor build: "<UE>/Engine/Build/BatchFiles/Mac/Build.sh" FourfoldEditor Mac Development
  -project="$PWD/unreal/Fourfold.uproject" -waitmutex
- Asset setup (in editor): Tools > Execute Python Script > unreal/Content/Python/fourfold_setup.py
  (or headless: UnrealEditor-Cmd unreal/Fourfold.uproject -run=pythonscript -script=<that file>).
  Report: unreal/Saved/Fourfold/setup_report.json ; log: unreal/Saved/Logs/Fourfold.log
- Play: Content Browser > Fourfold/Maps/L_Lab > Play. Keys: WASD move, J strike (hold = charge),
  U/N/H thrust/ground/sweep, Esc pause. Mobile preview: Settings > Preview Platform > iOS.
- iPhone/iPad: unreal/docs/MAC_SETUP.md section 6 (bundle id, team id, Platforms > iOS > Launch).
- Engine-free gameplay core tests (must stay green): `bash unreal/CoreTests/run_all.sh`
  (531 tests incl. the 13,300-row golden counter matrix that must match the Godot reference).
- Game-module logic tests: see unreal/docs/game/README.md (Private/Logic tests).
- Godot reference build (source of truth for rules): game/ ; tests:
  tools/scripts/godot.sh --headless -s res://tests/run_tests.gd (546), res://tests/ui/run_ui_tests.gd (96),
  res://tests/check_scripts.gd. Re-export data for Unreal: see MAC_SETUP.md section 10.
- After the editor runs once, copy unreal/Intermediate/PythonStub/unreal.py to unreal/Tools/py_stub/unreal.py
  and commit it, so editor scripts can be checked against the exact engine API.

LAYOUT (unreal/)
- Source/FourfoldCore: engine-free C++ sim (ported from Godot; data in Source/FourfoldCore/Data).
- Source/Fourfold: game module (sim subsystem, player controller, fighter, native anim runtime, camera,
  feel director, Slate touch UI/HUD/menus/Lab).  Source/FourfoldFX: VFX (procedural + HLSL in Shaders/).
  Source/FourfoldAudio: audio.  Source/FourfoldShaders: maps Shaders/ to /Fourfold.
- Content/Python/fourfold/*: editor-Python builders (character, animation, fx, world, audio) + fourfold_setup.py.
- SourceArt/: FBX character + LODs, animation clips (+ previews/*.mp4), textures, audio WAVs.
- Tools/blender, Tools/vfx, Tools/world, Tools/audio: generators (regenerate assets, never hand-edit outputs).
- docs/<stream>/: README, API_NOTES, REQUESTS, PROGRESS per stream.

CURRENT STATE (as of the last cloud session — verify with git log and unreal/HANDOFF.md)
- All six build streams done (core, game, character, animation, fx, world_audio); a review + fix pass ran
  after them in the cloud; its results are summarised in unreal/HANDOFF.md / docs once it finished.
- First Mac compile (Xcode beta, clang 21) failed with 4 -Wunreachable-code-loop-increment errors in
  AirVacuum.cpp, FireFlame.cpp, FireUtil.cpp, WaterWater.cpp — FIXED. More compile/link errors are expected
  on the next attempt; nothing in UE was ever compiled before this Mac.

WHAT TO DO (priority order)
1. Run `bash unreal/Tools/mac/run.sh`. Fix every compile/link error yourself, rebuild, repeat until the
   editor opens. Keep fixes minimal and correct; never disable warnings globally to hide real bugs.
   Re-run unreal/CoreTests/run_all.sh after touching FourfoldCore.
2. Run the asset setup; fix Python errors using the real engine API (py_stub step above) until
   setup_report.json has no failures.
3. Play L_Lab and a 1v1 vs AI. Fix crashes/obvious bugs. Use screenshots (screencapture) to check visuals.
4. Then quality: animation smoothness, VFX beauty, sound, counter feel; mobile preview; then iPhone deploy
   and `stat unit` perf (60 fps target).
Work in small verified steps; commit + push after each working step with clear messages.
Before big multi-file work, write a short plan and keep docs/<stream>/PROGRESS.md updated so any
interruption loses nothing.

CONVENTIONS
- Every heat/mass change goes through the ledgers; the C++ core must stay deterministic and match Godot.
- Kits never replace legacy sub-0 move ids or legacy rule cells.
- Log feel/tuning changes in docs/TUNING_LOG.md. Generated assets are produced by scripts only.
- Don't push to other branches; don't force-push.
```
