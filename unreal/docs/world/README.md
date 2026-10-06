# World - the arena, its look, the one-click setup and the project configuration

Stream `world_audio`, mission A.  Everything the player sees *around* the fighters: a quiet mountain training courtyard (lime
plaster, timber, grey clay tile, flagstones; original architecture, no franchise reference), built exactly on the sim's arena,
lit for the iPhone first (mobile forward renderer, 4x MSAA, baked lighting) and rich on the Mac (Lumen), plus the editor automation
that creates every asset in one run and the reviewed `Config/*.ini`.

Previews (Cycles look-dev renders of the generated meshes + textures from the gameplay camera; the real Unreal materials / lighting
differ in detail): `docs/world/previews/view_*.jpg` (`default` = behind the player, `south` = rival's side, `wall`, `pool`, `ledge`,
`east`, `high`, `gate`, `overview`).

## What exists

| Piece | Where | Notes |
|---|---|---|
| Texture generators | `Tools/world/{texgen_common,gen_env_textures,gen_env_textures2}.py` | numpy / scipy / PIL, deterministic, 1024^2.  Ported from the Godot build: flagstone, wall ashlar, ledge cap, pool tile, metal plate.  New: plaster, timber, roof tile, ground, rock, water normal, noise, clouds, foliage atlas, banners (original four-part emblem), arena mask (contact AO / wall dirt / pool splash / rust halo, baked from `sim.json`).  Output `SourceArt/Environment/Textures/T_Env_*.png` (colour sRGB, normals DirectX, ORM = AO / roughness / metal). |
| Mesh pipeline | `Tools/world/{meshkit,build_arena,build_scenery,bpy_io,export_env,env_spec}.py` | pure-numpy mesh kit (sim coordinates, world-projected UV0 in metres, lightmap UV1 shelf packer) + Blender FBX export.  Output `SourceArt/Environment/Meshes/SM_Env_*.fbx` (27 meshes), `meshes.json` (slots, tri counts, expected size in Unreal cm, bounds), `level_layout.json` (placements + lights). |
| Look-dev mock-up | `Tools/world/mockup_render.py` | the same meshes + textures + layout in Blender / Cycles from the gameplay camera (vfov 62, pitch 18 deg, 5.6 m behind) |
| HLSL | `Shaders/Env/FFEnv.ush` | surface grade (grime, wetness, saturation, macro tint), arena mask UV, water, banner / foliage wind, flicker, sky dome.  Portable HLSL; every Custom-node body is compiled with DXC (DXIL + SPIR-V) by `Tools/world/check_hlsl.py` |
| Editor builder | `Content/Python/fourfold/world/` | `build_all(force=False, lighting="auto")`: textures -> 9 master materials + 19 instances + `MPC_Arena` -> static meshes (Interchange, classic importer as fallback) -> level `/Game/Fourfold/Maps/L_Lab` -> lighting build |
| Orchestrator | `Content/Python/fourfold_setup.py`, `init_unreal.py` | `main(force, only, lighting)`; "Fourfold" main-menu entry (Build all, Rebuild all, Build one part, Open report) |
| Config | `Config/*.ini` | reviewed against the 5.8.2 headers, see "Config review" |

## The level (what `build_all` creates)

* **Arena = sim.json, exactly.**  Each of the 10 solids is its own `StaticMeshActor`, centred on the sim box centre
  (`UE = 100 * (x, z, y)`), tagged `FFSolid_<name>`, no collision, shadow on.  The meshes are authored at the real size with the
  pivot at the box centre; dressing leaves the box only above head height (wall caps, pilaster capitals, pillar braziers) or by
  <= 14 cm relief.  The floor top is at Z = 0; pool basin floor -0.30 m, water plane -0.05 m, metal plate top +0.02 m (all from sim.json).
* Root marker actor `FourfoldArena` (a `TargetPoint`, tag `FourfoldArena`; all arena actors carry the tag too) - the game then skips
  its cube placeholder arena.
* Dressing: flagstone floor with the pool opening, boundary walls (stone plinth, plaster body, timber pilasters + tie beam, gabled
  clay-tile cap, roundel blind windows, hanging lanterns, 12 cloth banners with the four-part emblem), pool basin + animated water,
  metal plate, bronze start rings, two pillar braziers with flickering coals.
* Scenery (Movable, lit dynamically): ground, three mountain layers with snow caps and haze, 15 plaster / tile halls with lit windows,
  two pavilions, a three-tier tower, a timber ceremonial gate, a training yard (wooden posts, a weapon-less stone-lock rack, a bench), ~90 pines / cypresses / broadleaf trees as foliage cards, boulders, six pennant poles, the sky dome
  (gradient, sun glow, two cloud layers).  What the gameplay camera sees of it is sky, ridges, roofs, tree crowns and pennants above the
  3.5 m walls - the effort went into silhouettes, colour and depth.
* Lighting: stationary sun (warm, 3.2 lux; rotation pitch -55.2, yaw -37.9 so the light comes from the camera side and above: fighters are front-lit and
  read well from the gameplay camera; the direction toward the light is exactly the fx stream's `FFKeyDir` (-0.45, 0.35, 0.82) in Unreal axes, so crystal glints,
  water reflections and smoke shading agree with the real sun), stationary sky light captured from the dome, exponential
  height fog, 2 reflection captures, 10 stationary point lights (lanterns, braziers), unbound post-process volume, Lightmass importance +
  character-indirect volumes.  `lighting="auto"`: Lightmass bake (medium quality, `LevelEditorSubsystem.build_light_maps`); if it fails
  the script switches sun and sky light to Movable with real-time sky capture and static meshes cast dynamic shadows ("dynamic" mode;
  the report says which mode was used).
* **Exposure convention (important for every stream):** the post-process volume fixes EV100 at -0.263, i.e. exposure scale exactly 1.0.
  Emissive 1.0 displays as 1.0; a diffuse white under a 3.14 lux light displays as 1.0 (sun 3.2 lux, sky light 1.0, lanterns 2.4 cd).  VFX
  emissive values and light intensities should be authored in these units.  One dial to brighten / darken the whole look: the
  `FF_Post` volume's *Exposure Compensation* (`auto_exposure_bias`).

Budgets (measured from `meshes.json` / `level_layout.json`): 28 placed actors, 90 material sections (~draw calls, no shadow passes),
46 k triangles (arena 12 k, scenery 34 k), 37 textures at 1024^2 (~20 MB with ASTC), samplers per material <= 6.  Targets were <= 150 draws,
<= 250 k triangles.

## Regenerate / test here (no Unreal needed)

```bash
PY=/home/user/tools/bpyenv/bin/python                 # numpy + scipy + PIL + bpy
$PY unreal/Tools/world/gen_env_textures.py            # ~1 min, ported sets      -> SourceArt/Environment/Textures
$PY unreal/Tools/world/gen_env_textures2.py           # ~1 min, new sets (+ --only plaster,foliage,...)
$PY unreal/Tools/world/export_env.py                  # FBX + meshes.json + level_layout.json  (--no-fbx --stats to just measure)
$PY unreal/Tools/world/mockup_render.py --view default,south --out /tmp/mock     # look at it (Cycles CPU, ~20 s a view)
python3 unreal/Tools/world/check_hlsl.py              # DXC compile of FFEnv.ush + every Custom-node body (path to dxc as argument)
python3 unreal/Tools/world/verify_props.py            # editor-Python property names vs the UE 5.8.2 public headers (all OK)
python3 unreal/Tools/world/dry_run_world.py [--mirror]  # fake-unreal dry run: solids at the sim centres, tags, lights, mirror detection
python3 unreal/Tools/py_mock/run_with_mock_unreal.py unreal/Content/Python/fourfold_setup.py   # orchestrator dry run
```

## Owner steps (Mac)

1. Nothing special beyond `MAC_SETUP.md` step 4: **Tools > Execute Python Script > `Content/Python/fourfold_setup.py`** (afterwards
   **Fourfold > Build all**).  The world part takes a few minutes (texture + mesh import, material compile, Lightmass bake).
2. Open `Saved/Fourfold/setup_report.json`: `parts.world.notes` lists what could not be applied (a renamed property shows up there
   instead of aborting), `parts.world.info.lighting` says `baked` or `dynamic`.
3. Open `L_Lab`, check: courtyard matches the previews; **Platform Preview > iOS** shows the phone look; walls fade out correctly when the
   camera is behind them (game side).  If a mesh looks mirrored the report says "FBX import looks MIRRORED": the builder already
   compensated (actor scale Y = -1) - tell us anyway.
4. iPhone: edit `BundleIdentifier` and `IOSTeamID` in `Config/DefaultEngine.ini` (marked EDIT-ME) or in Project Settings > Platforms > iOS.
5. Tuning dials: `FF_Post` exposure compensation (overall brightness), `FF_Sun` / `FF_SkyLight` intensity, `MI_Env_*` parameters
   (`Saturation`, `Tint`, `GrimeAmount`, `MacroStrength`), `MPC_Arena.Wetness` (0..1 wets every arena surface), `M_Env_Water`
   (`DeepColor`, `ShallowColor`, `RippleStrength`), `M_Env_Sky` (`CloudCover`, colours), `r.MobileContentScaleFactor` in `Config/IOS/IOSEngine.ini`.

## Integration notes for other streams

* **game**: tags as requested (`FourfoldArena`, `FFSolid_<name>` on actors; the whole actor is hidden with `SetRenderInMainPass(false)`,
  its baked shadow stays).  `MPC_Arena` (`/Game/Fourfold/Env/Materials/MPC_Arena`, scalar `Wetness`) drives arena wetness if you want rain /
  water-hit wetness.  Materials of the walls do not use collision; the sim is the only collision.  `UFourfoldSettingsSubsystem` quality
  levels map to `Config/DefaultScalability.ini` (levels 0..3 as documented there).
* **fx**: exposure convention above (EV100 -0.263 = scale 1.0, not EV 0: emissive 1.0 still shows as 1.0, so FX glow defaults need no rescale; if you want
  to keep EV 0 the single dial is `FF_Post` exposure compensation); the sun's direction toward the light is `FFKeyDir` = (-0.45, 0.35, 0.82) (`FF_Sun` rotation
  pitch -55.2, yaw -37.9; the sky material's `SunToward` parameter uses the same vector); the sun is a stationary directional light, so hit flashes / lights should be Movable or Stationary
  point lights (<= 4 visible); water surface at Z = -5 cm over the pool rectangle (x 7..13, sim z -5..3).
* **character**: the character materials receive the stationary sun's CSM + the sky light / volumetric lightmap; use the same
  exposure units (diffuse white = 1.0 at 3.14 lux).
* **animation**: nothing.

## Config review (every key checked against the UE 5.8.2 public headers: `RendererSettings.h`, `IOSRuntimeSettings.h`, `InputSettings.h`, `Engine.h`, `AudioSettings.h`)

| Key / area | Decision |
|---|---|
| `r.Mobile.ShadingPath=0`, `r.Mobile.AntiAliasing=3` (MSAA), `r.MSAACount=4` | kept; **fixed**: the architect's `r.MobileMSAA` is not a 5.8 setting - the sample count is `r.MSAACount` |
| `r.MobileHDR=True` | kept (project setting "Mobile post-processing" in 5.8) |
| `r.MobileNumDynamicPointLights`, `r.MobileDynamicPointLightsUseStaticBranch`, `r.Mobile.EnableMovableSpotlights`, `r.Mobile.AllowMovableDirectionalLights`, `r.Mobile.DisableVertexFog` | **removed**: not 5.8 project settings (5.8 deprecated the buffer variant of local lights); replaced by `r.Mobile.Forward.EnableLocalLights=1` (clustered local lights) + `r.Mobile.EnableMovableLightCSMShaderCulling=True` |
| `r.Shadow.CSM.MaxMobileCascades=2` | moved to `[SystemSettings]` (plain cvar, not a project setting); iOS adds `r.Shadow.MaxCSMResolution=1024` |
| `r.VertexFoggingForOpaque` | set `False`: the courtyard meshes are coarse, per-vertex fog would band |
| `r.SupportLowQualityLightmaps=True` | added (mobile baked lighting) |
| `r.DefaultFeature.AutoExposure(.Method/.Bias/.ExtendDefaultLuminanceRange)` | explicit; the level's post-process volume fixes EV100 |
| `r.SupportSkyAtmosphere=False` | the sky is a mesh dome (cheaper, identical on both platforms) |
| `r.DynamicGlobalIlluminationMethod=1`, `r.ReflectionMethod=1`, `r.Shadow.Virtual.Enable=1`, `r.GenerateMeshDistanceFields=True` | kept (Mac: Lumen software tracing needs distance fields) |
| `+TargetedRHIs=SF_METAL_SM6` (MacTargetSettings) | added (Metal SM6 for Lumen / Nanite on Apple silicon, macOS 15+); unverified key name - harmless if ignored |
| `FrameRateLock` | `PUFRL_None` (enum has only None / 20 / 30 / 60 in 5.8) + `bSupportHighRefreshRates=True` (= ProMotion, writes `CADisableMinimumFrameDurationOnPhone`) + `bEnableDynamicMaxFPS=True`: the game's `t.MaxFPS` (30 / 60 / 120) is the cap.  Satisfies the game's REQUESTS |
| `MinimumiOSVersion=IOS_17`, `bSupportsIPad/IPhone`, orientations, `PreferredLandscapeOrientation` | set / kept; signing fields `BundleIdentifier`, `IOSTeamID`, `bAutomaticSigning` marked EDIT-ME at the top |
| iOS audio | `AudioSampleRate=48000`, `AudioCallbackBufferFrameSize=512`, `AudioNumBuffersToEnqueue=2`, `AudioMaxChannels=32`; global quality level MaxChannels is 32 (raise it in Project Settings > Audio > Quality Levels if `ff.audio.dump` reports dropped voices) |
| `bShowConsoleOnFourFingerTap=False` | per the game's request; console stays on `Tilde` (backquote); added `bEnableGestureRecognizer=False`, `bEnableMotionControls=False` |
| Android section | removed (not a target) |
| `DefaultScalability.ini` | new: shadow / post / texture / effect groups for levels 0..3 matching the game's mapping (iOS 0..2, Mac 1..3) |
| `DefaultGame.ini`, `DefaultEditor.ini` | unchanged (cooking of `/Game/Fourfold`, UFS staging of `Fourfold/Data`, Python developer mode) |
| `[XcodeProjectSettings]` signing keys | architect's placeholders kept (key names could not be verified without the engine); `bUseModernXcode=True` is documented by Epic |

## Known gaps / risks

* Everything Unreal-side is written blind: the first run on the Mac may report property / node problems.  They end up in
  `setup_report.json` notes; material graph link failures are logged as `link failed` lines.  `verify_props.py` + DXC + the dry run
  remove the typo classes; node output names (`RGB`, `R`, `A`, `""`) are tried in several spellings.
* If the Lightmass bake is unavailable / slow on Apple silicon the level falls back to dynamic lighting (slower on phones, same look).
* The Unreal materials are not pixel-identical to the Blender mock-up (tone mapping, sky capture); expect to nudge `FF_Post` exposure,
  `MI_Env_*` saturation / tint and the sky colours on the first look.
* Textures are 1024^2 everywhere (floor 4 mm / texel at 4 m): fine on a phone, may look soft on a 5K Mac - bump `T_Env_Flagstone_*` to 2048 if wanted.
* The rock texture reads like large paving (it is mostly out of sight); trees are cards (no wind LOD); no lake geometry (a lake behind
  3.5 m walls is not visible from the gameplay camera - the haze band at the mountain foot stands in for it).
* Lantern / brazier lights are stationary (baked); their flicker is only in the emissive meshes.
