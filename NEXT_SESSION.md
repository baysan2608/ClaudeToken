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

CURRENT STATE (2026-10-07, second Mac session) - details: unreal/docs/QUALITY_PLAN.md, LOCAL_ENV.md
- Builds green on UE 5.8.3 + Xcode 27 beta; CoreTests 531/531 (needs cmake: export
  PATH="$HOME/Library/Python/3.9/bin:$PATH"; FF_COMPILERS=clang++); logic tests 60/60; setup_report clean.
- Look: SkyAtmosphere + volumetric clouds/fog, fully dynamic Lumen, TSR, Poly Haven CC0 arena textures.
- Perf: the "75 % TSR" never applied (Desktop.Mode=1 + scalability ResolutionQuality=100 -> 100 %, ~18 ms). Now manual
  75 % default + dynamic resolution 50..100 % (budget = 1000 / frame-rate cap): duel holds 60 fps at ~72-81 %, 0 drops.
  NOTE: this Mac's saved settings use a 120 fps cap (8.3 ms budget -> 50 % res); for 60 fps measurements pass
  -FFExec="2:t.MaxFPS 60|2:r.DynamicRes.FrameTimeBudget 16.67". Apple per-pass GPU timings overlap: A/B toggles only.
- Mocap: GASP gaits / get-ups + distance-matched run start / stop (ffg::LocoTransition). Root fix: retargeted mocap had
  the root at hip height and sank ~90 cm into the floor whenever a mocap gait played - fixed.
- Fighters: MetaHumans (Kellan), bare-chested + barefoot (owner's choice), complete body SKM_FF_Body, cargo pants
  tinted per role. -FFCharacter=fighter = old fighter.
- Physics: hit flinch, ragdoll knockdowns, get-ups aligned to the lying body.
- VFX: parallel FX session owns unreal/Source/FourfoldFX/**, Shaders/**, Content/Python/fourfold/fx/**,
  Data/fx_config.json, docs/fx/**, Tools/vfx/**, QUALITY_PLAN row 6 (Niagara cues + pre-warm, next Chaos fracture).
  Coordinate with SendMessage ("running UE until hh:mm" / "done"); commit only your paths (`git commit -o -- paths`).
- Dev tools: Tools/mac/shot.sh <dir> <secs,...> [game args] = in-engine screenshots (locked screen OK); game args:
  -scenario=lab|spar -autoplay=duel, -FFBurst=<tag>:<n>:<dt> -FFBurstQuit=<n> (tags: ragdoll flinch getup getup_ko
  loco_start loco_stop), -FFLabSpawn=<entry>@<sim s>, -FFMove="<s>:<x>,<y>|..." (scripted stick, Lab),
  -FFExec="<s>:<console cmd>|..." (timed commands, e.g. csvprofile frames=300 for in-run A/B), -csvGpuStats;
  Tools/mac/csv_gpu.py [a.csv [b.csv]] summarises CSVs (unreal/Saved/Profiling/CSV).
- Owner request (relayed 2026-10-07): everything - move interactivity, physics, HUD, controls, graphics, sound, feel,
  speed, animation - at AAA level, with the feel of a well-known animated elemental martial-arts series as a quality
  reference only (100 % original IP). Controls and HUD must be redesigned around elemental moves: very flexible, many
  different things you can do.

WHAT TO DO (priority order)
1. Locomotion: turn-in-place (GASP Stand_Turn_090/180 L/R with a root-yaw offset so the feet stay planted while the
   sim facing tracks the target), pivots, walk / strafe starts / stops (8 directions); check foot planting up close.
2. Outfit: proper loose training pants + waist sash (Blender generator from the MetaHuman body: export SKM_FF_Body
   FBX, offset field + folds + cuffs, weights copied, import on metahuman_base_skel), per-element sash colour; fix the
   thin pale seam at the neck base.
3. Controls + HUD redesign for elemental moves (owner request above): design doc first, then implement.
4. iOS budget: mobile renderer, MetaHuman LODs / groom cards, no volumetrics, scalability maps iOS levels to
   50 / 71 / 87 % screen percentage (decide), static vs dynamic lighting for the phone.
5. Arena dressing: replace the paper-cutout trees / flat backdrop (Fab/Megascans or CC0 assets).
6. Sound mix, counter feel, iPhone deploy (`stat unit`).
Work in small verified steps; commit + push after each step; keep docs/QUALITY_PLAN.md current.

CONVENTIONS
- Every heat/mass change goes through the ledgers; the C++ core must stay deterministic and match Godot.
- Kits never replace legacy sub-0 move ids or legacy rule cells.
- Log feel/tuning changes in docs/TUNING_LOG.md. Generated assets are produced by scripts only.
- Don't push to other branches; don't force-push.
```
