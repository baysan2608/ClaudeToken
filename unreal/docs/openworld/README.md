# Open world (stream `openworld`, started 2026-10-09)

Owner decisions (2026-10-09): **seamless combat bubbles**, **4 element regions around a temple hub**, **~2 x 2 km**.

## How it works

- **One heightfield, two readers.** `Tools/openworld/gen_world.py` writes `Content/Fourfold/Data/openworld/`
  (`world.json`, `heights.f32` 1025 x 1025 at 2 m, `splat.rgba`). The sim (`ff::WorldDef`, `Public/ff/OpenWorld.h`)
  walks on it; `AFourfoldOpenWorld` renders it. Never hand-edit the data: change the script and re-run it.
- **The sim stays a 40 m bubble.** `ArenaMap::make_window` turns the window of the world around the bubble origin into
  the usual analytic arena (terrain floor, the lake as the pool, ore plates as metal, world solids). Outside fights the
  session re-centres the bubble on a calm player (`app_recenter {dx,dy,dz}`, local state shifted by -d, the origin by +d).
  Grounded fighters cannot climb terrain steeper than 45 degrees (`ArenaMap::limit_terrain`, slides along cliffs).
- **Encounters.** `world.json` sites (13: hub master + 3 per region) engage when the player enters their aggro ring:
  the bubble re-centres on the midpoint, the rival spawns with an `AiBrain` (preset, element, sub). Reaching the bubble
  edge mid-fight flees; a KO'd rival is removed (site defeated); a KO'd player respawns at the nearest shrine.
  Events: `app_encounter {id, name, state: engaged|won|fled|lost}`, `app_respawn`.
- **Unreal side.** `FF::GSimOriginUE` (FourfoldCoords.h) is the bubble origin: `ToUE` / `ToSim` include it, so every
  consumer stays correct; state kept across frames in sim space shifts on `UFourfoldSimSubsystem::OnSimRecenter`
  (previous snapshot, camera springs, FX debris colliders). Foot IK / camera / FX ground read the terrain via ArenaView.
- **Terrain rendering.** 16 x 16 chunks of 128 m (procedural mesh): 8 m far section everywhere, 2 m near section with
  collision within `NearRadius`; skirts hide LOD seams. Vertex colour = splat (R grass, G rock, B sand, A ash);
  `M_OW_Terrain` blends Poly Haven CC0 sets (Tools/openworld/fetch_textures.py) + snow by height. Trees / rocks are
  HISM instances from `world.json` props, distance culled.

## Run

```
python3 unreal/Tools/openworld/gen_world.py          # world data + docs/openworld/previews/map.png
python3 unreal/Tools/openworld/fetch_textures.py     # terrain textures (CC0, git-ignored)
# editor setup part (textures, M_OW_Terrain, L_World):
UnrealEditor-Cmd unreal/Fourfold.uproject -run=pythonscript -script=<file: import fourfold_setup; fourfold_setup.main(force=True, only="openworld")>
# play: open /Game/Fourfold/Maps/L_World (roaming starts automatically) or any map with -scenario=roam
FF_MAP=/Game/Fourfold/Maps/L_World bash unreal/Tools/mac/shot.sh logs/shots_ow 18,30,44 -FFMove="10:0,1|40:0,0"
```

Tests: `CoreTests/tests/test_open_world.cpp` (terrain, re-centre, wall + lake, engage / flee, win / respawn, cliff
slide, determinism).

## Known gaps / next

- Grass reads too yellow; mountains lack far detail (macro rock / strata); snow only on gentle slopes.
- Waiting fighters are waystone markers only (no idle NPC at the site yet); defeated rivals vanish after the knockdown.
- No map / compass / region banner in the HUD yet; toasts announce encounters.
- One-shot FX events emitted in the ticks just before a forced re-centre can land offset (rare: re-centres wait for calm).
- Lakes only (one water level); no rivers / swimming; deep water clamps to wading depth.
- iPad budget not measured on the open world yet (chunks, 6.5k instances, shadows over 200 m).
