# Jungle Ruins map (L_Ruins)

The default start map since 2026-10-16: a 288 m playable core around the stone pyramid and a river curve of Intel's
"Jungle Ruins" scene (CC BY 4.0, Cristiano Siqueira with Charlene Teets / Intel Sample Library; see `unreal/docs/CREDITS.md`).
Spec: `docs/superpowers/specs/2026-10-14-first-person-elements-design.md` §5. Plan: `docs/superpowers/plans/2026-10-14-fp-05-jungle-ruins-map.md`.

## Rebuild (every output is generated; never hand-edit)

1. `python3 -I unreal/Tools/ruins/fetch_ruins.py` — download (4.25 GB, resumable), verified extraction of 108 files (2.05 GB)
   into `unreal/SourceArt/OpenWorld/JungleRuins/extract/` (git-ignored), licence + `ruins.json`, zip deleted.
2. `/Applications/Blender.app/Contents/MacOS/Blender -b --disable-autoexec --factory-startup --python unreal/Tools/blender/ruins/prep_ruins.py`
   — 117 FBX, 84 PNG, `layout.json` in `SourceArt/OpenWorld/JungleRuins/Game/`; preview `unreal/docs/ruins/previews/layout.png`.
3. Full editor: `UnrealEditor unreal/Fourfold.uproject -ExecutePythonScript=$PWD/unreal/Tools/ruins/editor_setup_ruins.py -unattended`
   — `/Game/Fourfold/Ruins/*` and `/Game/Fourfold/Maps/L_Ruins` (setup part `ruins`; needs the `world` part's masters).
4. Full editor: `UnrealEditor unreal/Fourfold.uproject -ExecutePythonScript=$PWD/unreal/Tools/ruins/bake_ruins.py -unattended`
   — `unreal/Content/Fourfold/Data/ruins/{world.json, heights.f32, water.f32}` (tracked) + `Saved/Fourfold/ruins_bake_report.json`.
5. Tests: `python3 -I unreal/Tools/ruins/test_ruins_tools.py`; CoreTests `test_world_water`, `test_ruins_data`.

Before any Unreal step check `pgrep -fl "UnrealEditor|UnrealBuildTool"` prints nothing (shared checkout).

## Frames and the core

USD / Blender metres (Z up) -> world (sim): `x = bx - 600`, `y = bz`, `z = -(by - 728)`; Unreal `UE = 100 * (x, z, y)`.
The core is Blender x 456..744, y 584..872 (world x / z -144..144, the sim grid: 145 x 145 samples at 2 m, 8 m edge band).
Measured in the USD: pyramid 42 x 42 m at (543.7, 809.8), z -7.2..13.3; river flat at z -9.99 in the core (a gorge);
creek (sloped water) 470 m south of the pyramid — outside the core, kept as backdrop scenery. The scene camera at
(572.5, 766.5) is the spawn.

## What the sim knows (world.json)

- `terrain.edge_margin` 8, `terrain.editable` false (the ridge technique raises stone walls; the visual terrain is a mesh).
- `water.layer` = `water.f32`: per-sample surface, NaN = dry (the river). Wading, water draw sources and splashes follow it.
- `solids`: interior boxes of the pyramid (block shots / line of sight, never change footing), boxes around big trunks and
  invisible rim walls (`rim_<n>`, 5 m above the rim) along the river gorge: its banks are 8 m cliffs over a 55 m deep gorge,
  so without them a fighter could step or fall into water they could not climb out of. The bake fails on any trap.
- `sites`: Stair Warden (earth), River Keeper (water), Summit Adept (air, on the pyramid's upper tier: the 2 m grid reads
  the tiers as 0.5..0.75 ramps, so its site plan accepts a 0.5 slope); `shrines`: Ruins Shrine behind the spawn.
- `props` + `prop_assets`: the thinned jungle (trees 1 in 6 + a 120 m ring at 1 in 20, saplings 1 in 8, seedlings 1 in 60,
  shrubs 1 in 6, grass 1 in 40, pyramid grass 1 in 10), instanced at runtime by `AFourfoldOpenWorld` (`DataDirName` "ruins").

## Source quirks the pipeline handles

- The USD meshes import with split vertices (every terrain quad has its own 4): `prep_ruins.weld` merges them before
  chunking and decimation (otherwise the seam lock freezes the mesh and collapse decimation cannot work).
- Leaf and blade cards are 2-triangle islands that collapse decimation cannot remove: vegetation LODs drop a stable-hashed
  share of the card islands (survivors widened about their centre, the trunk island kept) before decimating (`lod_mesh`).
- River.usd is wound left-handed (faces point down): `face_up` turns water surfaces up, or traces and one-sided
  materials miss it.
- Static meshes compile asynchronously when a level loads: the bake turns that off (`Editor.AsyncStaticMeshCompilation 0`)
  so the level's collision exists in the frame it traces.

## Performance (spawn view, 2026-10-16)

- iPad Pro 13 M5 (iOS 27, mobile renderer): 8.33 ms frames (the 120 fps cap), GPU 6.2 ms, game thread 0.7 ms
  (`IOS_ARGS="-csvCaptureFrames=2400" bash unreal/Tools/mac/ios.sh --cook`, CSV pulled with `xcrun devicectl device copy from`).
- Mac 1600x900 (desktop renderer, Lumen): GPU 18.3 ms after the cull trim (19.5 before), over the 16.7 ms budget; shadow
  depths, base pass and foliage velocities lead. Next knobs: tree shadows at distance, WPO velocity, cloud quality.

## Known gaps

- The terrain colour is the scene's baked 12 cm map (dark, soft at eye height); a tiling detail layer belongs to the
  materials pass (plan 06). Normal maps are flipped from the USD preview convention (OpenGL) to DirectX: if lighting on
  stone bumps looks inverted in a close capture, flip `texture_set` in `prep_ruins.py` for that set and re-run steps 2-3.
- Hero stone stays at the source's 2048 maps (no 4k source exists). Its LOD0 is the authored 298 k triangles (the
  source's 159 k faces are quads); LOD1 56 k, LOD2 25 k (collapse decimation stops there).
- QueenForest trees, the Banyan, moss, nettle and anthurium are not used (size / budget).
