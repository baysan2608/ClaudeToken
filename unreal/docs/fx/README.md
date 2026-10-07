# Stream `fx` - Fourfold visual effects for Unreal 5.8 (iOS first)

Every visual effect of the game: earth, lava, metal, sand, glass, water, ice, mist / steam, plants, flame, blue fire,
lightning, blasts, wind, vortices, vacuum, sound; charge tiers; status looks on fighters. Persistent visuals come
**only** from sim body state (`ff::Snapshot`), one-shot cues **only** from sim events (`ff::Event`) - the Godot rule.
Core look: C++-driven procedural meshes + Python-built materials whose Custom nodes include our HLSL + Blender hero
meshes + simulated flipbooks. On top of it, the **Niagara cue layer** (2026-10-07): one-shot cues also request
pre-authored Niagara systems (Epic's free Niagara Examples Pack by default) - see "Niagara cue layer" below.

## Layout (all owned by `fx`)
| Path | What |
|---|---|
| `Source/FourfoldFX/Private/Logic/` | **engine-free** core (namespace `ffx`, sim space, no UE headers): director (`FxDirector`), body views (`FxViews*`), cues (`FxCues`), pooled one-shots (`FxOneShots`), geometry builders (`FxMesh`, `FxMeshLib`, `FxLightning`, `FxParticles`), mapping (`FxMapping`), config (`FxConfig`), draw list (`FxDrawList`) |
| `Source/FourfoldFX/Private/Logic/tests/` | unit tests, integration soak, shader shim, CPU preview renderer (all `FF_LOGIC_TESTS`, empty in UBT builds) |
| `Source/FourfoldFX/{Public,Private}/FourfoldFx{Subsystem,Actor}*`, `Private/FxUeConvert.h` | thin UE glue: world subsystem (binds `OnFrame`), pooled component renderer, sim -> UE conversion |
| `Source/FourfoldFX/Private/FourfoldFxNiagara.*` | Niagara cue layer: spawns the configured system per `SystemReq`, binds user parameters |
| `Source/FourfoldShaders/` | maps `unreal/Shaders` -> `/Fourfold` (PostConfigInit, guarded) |
| `Shaders/Common/*.ush` | noise (value / gradient / simplex / fbm / ridged / curl / Voronoi 2D-3D, triplanar), flipbook blending, flow, fresnel / fake lighting, dithering, black-body, `FFSurf` |
| `Shaders/FX/*.ush` | `FFRock` (stone -> lava -> crust continuum + lava strips), `FFFlame` (mesh flames + fire sprites), `FFWater` (water -> ice), `FFCrystal`, `FFMetal`, `FFVine`, `FFGround` (strips + decals), `FFLightning` (bolts, beams, sparks), `FFWind` (air + vortex), `FFShell` (shells + rings), `FFSmoke` (smoke / steam / dust / puffs / splash) |
| `Content/Python/fourfold/fx/spec.py` | **pure-data** material spec: 19 masters (blend, lighting, includes, parameters, Custom-node code with typed inputs / outputs), textures, meshes |
| `Content/Python/fourfold/fx/__init__.py` | `build_all(force=False)`: imports textures / meshes, builds the masters + rock instances |
| `Content/Fourfold/Data/fx_config.json` | runtime look tuning (palettes, bursts, clouds, shells, vortex infusions, crystals, beams, strips, quality levels, lights, asset paths) |
| `Tools/vfx/` | `noise_textures.py`, `flipbooks.py` + `ff_fluid.py` (smoke / fire solver + ray-marcher), `meshes.py` (Blender), `preview_sheet.py`, `shader_check/` (DXC checker, preview code generator), `py_mock_fx.py`, `ue_check/` |
| `SourceArt/VFX/` | committed outputs: `Textures/T_FX_Noise.png`, `Flipbooks/T_FX_FB_*.png` (+ `flipbooks.json`), `Meshes/SM_FX_*.fbx` + `T_FX_rock_<k>_N.png` |

## Frame flow
`UFourfoldSimSubsystem::OnFrame` -> `UFourfoldFxSubsystem::OnSimFrame` builds an `FxFrameIn` (prev / curr snapshot,
alpha, events, dilated dt, camera, fighter bone anchors, quality from `UFourfoldSettingsSubsystem::GetEffectiveQuality`,
Settings.Flashes) -> `FxDirector::Update` (views created on first sight, interpolated every frame, faded out when the
body disappears; events -> pooled one-shots; charge / status / aura visuals per actor) -> `DrawList` (items keyed by a
never-reused key: procedural mesh or static mesh + material slot + parameters + transform or bone attachment;
<= 4 lights) -> `AFourfoldFxActor::Apply` binds each key to a pooled component, uploads geometry only when its
version changed (recreating the section when the topology changed: `UpdateMeshSection` needs identical vertex counts),
pushes only changed parameters, hides unused components. Everything is collision-free; shadows only where an item asks
(rocks, walls, metal).

## Body -> view map (material in brackets)
| Body (mat / form / tag) | View | Look |
|---|---|---|
| stone chunk / spear / crag / rubble / bomb / glob | `stone` [Rock, static `SM_FX_rock_k` or procedural] | tumble from velocity; Heat / Melt / Crust / Frost drive one continuous stone -> glowing cracks -> molten blob (vertices relax to the blob, slump, wobble) -> crusted basalt |
| stone / sand / mud / obsidian / sandstone / plate walls | `wall` [Rock (tinted) / Metal] | blocks rise out of the ground with per-block delay; damage cracks, heat glow, slump |
| stone wave, cooled ridge | `wave` [LavaStrip] | path strip from `wave_path`; fresh hot front, crusting banks, residual red hairlines |
| lava pool zone | `lava_pool` [LavaStrip] | disc of crusting lava |
| spikes / spike lines (stone, ice, glass, metal) | `spikes` [Rock / Crystal / Metal] | staggered rise |
| metal disc / lance / rod / plate / caltrops / planted rod | `metal` [Metal, static disc / lance / plate] | spin + blur ring [Ring], red-hot tint |
| water blob / held orb / jet | `blob` [Water] | wobbling orb freezing into facets; a held jet (`tag "jet"`) is a tube from the caster's chest |
| water stream / whip | `ribbon` [Water] | tube along the body trail |
| puddle | `puddle` [Ground wet mark + Water lens] | |
| water / sand waves, rime | `strip` [Water with foam lip / GroundStrip sand / rime / mud] | |
| fog, mist, steam, sand cloud, sandstorm, steam screen, geyser, dust line, sand slug | `cloud` [Smoke, puff atlas] | camera-facing puffs, swirl / rise / column / flatten per style |
| quicksand, ice floor, mud, melt pit | `decal` [Ground] | lifted quad instead of a deferred decal (+ a light for the melt pit) |
| ice / glass walls, needles, shards | `crystal_wall` / `crystal` [Crystal, static ice shard] | grow from the ground, frost, shatter cracks |
| vines, briars, snares, plant bodies | `vine` [Vine] | growing tubes, sway, burn / frost |
| fire bodies (fireball, comet, ember), mines / bombs | `fireball` [Flame tongues + Shell core] / `shell` mine | |
| fire waves, fire lines, fire fields | `flames` [Flame tongues] | per-tongue flicker, light |
| null bubble, vacuum well, corona, static field, wind guard, sound barrier, inrush, fuse | `shell` [Shell, + Ring spiral / Lightning arcs] | |
| crescents, wind walls | `blade` [Wind] | |
| twister, funnel, spiral, tornado, eddy, vortex wall (+ sand / fire / water / steam infusion) | `vortex` [Vortex x 2 layers + Rock debris] | |
| ground current | `crackle` [Lightning] | |
| tremor, flight field | `rings` [Ring] | |
| any body with charge > 4 | + crackle overlay [Lightning] | |
| `Form::Pool` | - | the world draws the pool |

## Cues (events -> one-shots)
`fx` (cast / release / cone / beam / burst / ring / erupt / trail / splash / aura by mat and shape), `interaction`
outcomes of MOVESET §11.3 (block / deflect / transform / shatter / melt / quench / ground / redirect ... with perfect
flashes x Settings.Flashes), `charge` tiers (T1 hand ring + motes, T2 + ground ripple + rim, T3 + aura shell + light
pulse + 2-frame glint), `status` (burning flames, wet drips, frost shell, shocked crackle, grit, roots, mist veil,
anchor dust, armor aura, levitation rings, kit statuses - attached to fighter bones), `zone` open / close, `clash`,
`morph`, `slump`, `convert`, `capture`, `ricochet`, `stance`, `mode`, `inrush`, `extinguish`, `launch`, `impact`,
`lightning`, `fire_burst`, ... (61 event types seen in the soak). One-shot families: rings [Ring], particle bursts
[Smoke flipbooks + Spark], shards [Rock / Crystal / Metal / Vine], blasts [FireSprite explosion + Ring], beams
[Beam], fire bursts [Flame x 2 + FireSprite billows], air pushes [Wind cone + streaks], splashes [Splash + Smoke],
steam / dust / embers, bolts [Lightning], light pulses.

## Materials (all `/Game/Fourfold/FX/Materials/M_FX_*`)
| Slot | Blend | Notes |
|---|---|---|
| Rock, LavaStrip, Metal, GroundStrip, Vine | opaque lit | WPO melt / rise / shrink; world-space normal (Rock: baked normal map + melt + Voronoi plate relief) |
| Ground | translucent lit (Surface ForwardShading) | decals |
| Crystal, Water, Flame, FireSprite, Smoke, Splash, Beam, Ring, Shell, Vortex, Wind | unlit AlphaComposite (premultiplied: Cover 0 = additive .. 1 = covering) | <= 2 layers per effect |
| Spark, Lightning | unlit additive | |
Parameter names = `ffx::kParamNames` (`FxTypes.h`); tuning-only scalars: `GlowScale`, `Duration`, `EmissiveScale`
defaults. Texture parameters: `Noise` (T_FX_Noise), `Flipbook`, `RockNormal`. Vertex alpha travels in UV3.x.

## Budgets (iOS, ARCHITECTURE §14)
Soak (every move of every sub-element at T0 and T3 + a 90 s AI duel, quality 2, 135 600 rendered frames): max 76
items, 11 k triangles, 4 lights, 31 one-shots in flight; `FxDirector::Update` avg 0.005 ms on the container CPU (-O2),
2 frames above 2 ms (max 4.4 ms, a T3 release burst; mesh caches are pre-warmed by `meshlib::Prewarm`).
Quality levels (fx_config `quality[0..2]`): particle multiplier 0.55 / 0.8 / 1.0, cloud puffs 7+5 / 10+7 / 12+9,
flame tongues 8 / 11 / 14, shards, debris, bolt subdivision 5 / 6 / 7, lights 2 / 3 / 4. Particles <= 32 per
one-shot; geometry rebuilt at most once per frame; components pooled and pre-warmed.

## Niagara cue layer
Niagara systems cannot be authored from Python, so they come pre-made: Epic's free **Niagara Examples Pack** (Fab),
installed at `Content/NiagaraExamples/` (1.2 GB, git-ignored; to add it: start Unreal **from the Epic Games Launcher**
so the Fab plugin is signed in, open Fourfold, Window > Fab > My Library > Niagara Examples Pack > Add to Project).
* Flow: `OneShots::Blast / Burst / Shards / FireBurst / AirPush / Splash / Steam / Dust / Ember / Bolt` also emit a
  `SystemReq` (`DrawList::systems`: cue slot `NCue`, position, unit direction, scale, intensity, `color` = body /
  dust colour, `color2` = hot / spark colour). `FFourfoldFxNiagara` (`Private/FourfoldFxNiagara.*`) spawns the slot's
  system from Niagara's world pool (AutoRelease, at most 10 per frame), with +Z along the direction, and binds the
  request to the system's user parameters.
* Config: `fx_config.json` "niagara".<cue> = `path` ("" = none), `scale`, `life` (seconds until a looping system is
  told to stop), `replace`, `min_quality`, `min_intensity`, `params` {user parameter (without "User.") : source}.
  Sources: `color`, `color2` (linear, skipped when alpha 0), `dir`, `-dir`, `scale`, `intensity`, each optionally
  `*<k>` (e.g. `color*4`, `-dir*600`), or a number. The glue converts to the parameter's real type; every loaded
  system's user parameters (with types) are logged once (`LogFourfoldFxNiagara`).
* Fallback: the glue reports which slots loaded (`FxFrameIn::niagaraLoaded`); `replace` only skips the procedural
  one-shot of a loaded slot, so a clone without the pack keeps exactly the procedural look. Weak requests
  (`min_intensity`, e.g. the 0.35 s periodic status puffs) and low quality (`min_quality`) stay procedural.
* Current slots (C++ defaults -> fx_config.json): blast = NS_Explosion_Medium (replace), dust / sand / grit bursts =
  NS_Dirt_Explosion_Small (replace), strong `dust` = the same (adds), metal / ember / spark bursts and embers =
  NS_Spark_Burst (adds), steam = NS_Smoke_Plume for 0.5 s (adds). Everything else stays procedural: the pack's
  NS_Impact_* are bullet-sized, its bubbles do not read as water and its sparks stay orange (wrong for lightning /
  blue fire). `ff.fx.Niagara 0` switches the layer off at runtime (procedural only).
* Look checks: `ff.fx.Showcase <seconds>` plays a fixed cue list (blast, stone, metal, glass, ice, lightning, sand,
  water, steam, magma, fire cone, plant) through the normal event path 3 m beyond the first fighter;
  `ff.fx.ShowcaseShots 0.12|0.5|1.2` saves `showcase_<n>_<cue>_<ms>.png` that long after each cue into `-FFShotDir`.
  Example: `-scenario=lab -ExecCmds="ff.fx.Showcase 2.5, ff.fx.ShowcaseShots 0.12|0.5|1.2" -FFShot=50 -FFShotDir=<dir> -FFShotQuit`.
* GPU budget: the Mac game is GPU-bound; slots are one-shot bursts only (no persistent Niagara), the pack's own
  Effect Types handle significance / culling, `min_quality` 1 keeps them off on low.

## Tuning without a rebuild
Edit `Content/Fourfold/Data/fx_config.json` (any subset of keys; colours are display sRGB) and run console
`ff.fx.ReloadConfig`. `ff.fx.Stats 1` prints counters, `ff.fx.Enable 0` turns effects off. Material look constants
live in the masters (`GlowScale`, `EmissiveScale`, `Duration`, defaults of every parameter) and in the `.ush` files
(re-run `build_all(force=True)` or just recompile the material after editing a `.ush`). After changing C++ defaults
regenerate the JSON: `ffx_logic_tests --write-config unreal/Content/Fourfold/Data/fx_config.json` (a test keeps them
equal).

## Regenerate / test here (Linux container)
```
SCR=<scratch dir>
cmake -S unreal/Source/FourfoldFX/Private/Logic/tests -B $SCR/lt_gcc -G Ninja && cmake --build $SCR/lt_gcc
$SCR/lt_gcc/ffx_logic_tests                       # 26 tests (also with clang: CXX=clang++)
$SCR/lt_gcc/ffx_core_soak --quick --dump-params $SCR/params.json      # real sim through the director
python3 unreal/Tools/vfx/shader_check/check_shaders.py --dxc <dxc> --params $SCR/params.json   # lint + DXC
python3 unreal/Tools/vfx/py_mock_fx.py            # editor builder dry run (also the shared mock runner)
PY=/home/user/tools/bpyenv/bin/python
$PY unreal/Tools/vfx/noise_textures.py --raw $SCR/noise.rgba8
$PY unreal/Tools/vfx/flipbooks.py --preview $SCR/fb      # ~8 min, numpy smoke / fire solver (Mantaflow is broken in bpy)
$PY unreal/Tools/vfx/meshes.py --preview $SCR/meshes    # ~20 s, Cycles normal bakes
$SCR/lt_gcc/ffx_preview --noise $SCR/noise.rgba8 [--fb <raw atlases>] --out $SCR/frames [--gallery] [--only fire0_slot]
python3 unreal/Tools/vfx/preview_sheet.py $SCR/frames $SCR/sheet.png
```
`ffx_preview` plays every move through the Lab and rasterises the draw list in software with the **same material
code** (generated from spec.py) over the shared HLSL through `hlsl_shim.h` - the review renders of this stream.

## Owner steps on the Mac
1. Build the editor target (the module is in the .uproject already). Compile errors: send the log; API sources are in
   `API_NOTES.md`.
2. Run `fourfold_setup.py` (or in the Python console `import fourfold.fx as f; f.build_all()`), check
   `Saved/Fourfold/setup_report.json` for `failed` / `notes` (material compile messages are copied there).
3. Open `M_FX_Rock` and `M_FX_Flame` once: they must compile without errors for SM6 and the iOS (ES3.1 / Metal)
   preview platform. Lab: try a stone throw at T3 then heat it, a fire punch, a water whip, a lightning strike.
4. If a material fails on Metal only, report the error line; the `.ush` files are pure HLSL, no SM6 features.

## Integration notes (other streams)
* `game`: FX reads `UFourfoldSimSubsystem::OnFrame` / `OnScenarioLoaded`, the settings subsystem (quality, Flashes)
  and `AFourfoldFighter::GetBodyMesh / GetBoneLocation` (bones `pelvis spine_03 spine_05 head hand_l hand_r foot_l
  foot_r`). The character's own wet / frost / burn / glow material params stay the game's job (ARCHITECTURE §9).
* `world_audio`: the unlit translucent FX fake their lighting with `FFKeyDir()` (Shaders/Common/FFLighting.ush) =
  the direction toward the arena sun, UE (-0.5864, 0.4565, 0.6691) = the opposite of the sun Rotator (pitch -42,
  yaw -37.9) in `Content/Python/fourfold/world/level.py`; when the sun moves, update `FFKeyDir` to follow. Lab
  exposure: emissive values are tuned for a fixed exposure around EV 0 with bloom; if the level uses a different
  exposure, adjust `GlowScale` / `EmissiveScale` defaults.
* Material order in `fx_config.json` "materials" follows `ffx::MatSlot`; the setup script order is fx first.

## Known gaps
* Flipbooks are simulated with our numpy stable-fluids solver and ray-marcher, not Mantaflow + Cycles (Mantaflow's
  bake fails inside the `bpy` module: `'LevelsetGrid' object has no attribute 'setConst'`).
* Not seen on a device yet: emissive levels, translucency cost of big shells / clouds, material compile on Metal.
  The CPU preview approximates the mobile renderer (no MSAA, no bloom, no fog).
* Refraction / screen-space distortion is faked (no scene-colour reads): water and air pushes use alpha and highlights.
* `REQUESTS.md` lists one core issue (an extra untagged zone).
