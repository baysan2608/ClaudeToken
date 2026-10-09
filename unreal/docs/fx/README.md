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
| `Source/FourfoldFX/Private/Logic/FxFracture.*` | Voronoi fracture pieces of rocks / wall blocks (engine-free, cached) |
| `Source/FourfoldFX/Private/FourfoldFxDebris.*` | physics debris: Chaos rigid bodies for `FractureReq` pieces on a collision copy of the sim arena |
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
| stone wave, cooled ridge | `wave` [LavaStrip] | path strip from `wave_path`; fresh hot front, crusting banks; a set ridge glows dull red by its heat (`Heat`, faded by a 6 s visual cooling clock), turns dark grey-brown basalt, and once cold for 3 s sinks into the floor (`Fade` -> WPO, 1.5 s) and stops drawing; settled ridges have no nose |
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
Parameter names = `ffx::kParamNames` (`FxTypes.h`); tuning-only scalars: `GlowScale`, `Duration`, `LightScale` (Smoke 2.2 /
Splash 1.8: the fake smoke light, balanced for a 3.14 lux sun, x the arena's 14 lux sun / sky), `EmissiveScale`
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
  Sources: `color`, `color2` (linear, skipped when alpha 0), `dir`, `-dir`, `scale`, `intensity`, `target` (the
  request's second point, a world position), each optionally
  `*<k>` (e.g. `color*4`, `-dir*600`), or a number. The glue converts to the parameter's real type; every loaded
  system's user parameters (with types) are logged once (`LogFourfoldFxNiagara`).
* Fallback: the glue reports which slots loaded (`FxFrameIn::niagaraLoaded`); `replace` only skips the procedural
  one-shot of a loaded slot, so a clone without the pack keeps exactly the procedural look. Weak requests
  (`min_intensity`, e.g. the 0.35 s periodic status puffs) and low quality (`min_quality`) stay procedural.
* Current slots (C++ defaults -> fx_config.json): blast = NS_Explosion_Medium (replace; fixed greys Smoke 0.5 / Dirt
  0.15), dust / sand / grit bursts = NS_Dirt_Explosion_Small (replace), strong `dust` (>= 1.0) = the same (adds), metal / ember / spark bursts and embers =
  NS_Spark_Burst (adds), steam = NS_Smoke_Plume for 0.5 s (adds), every bolt = NS_TeslaCoil arcs from its first to
  its last node for 0.3 s (`bolt_arc`, parameter source `target`: the request's second point). Everything else stays
  procedural: the pack's
  NS_Impact_* are bullet-sized, its bubbles do not read as water and its sparks stay orange (wrong for lightning /
  blue fire). `ff.fx.Niagara 0` switches the layer off at runtime (procedural only).
* Look checks: `ff.fx.Showcase <seconds>` plays a fixed cue list (blast, stone, metal, glass, ice, lightning, sand,
  water, steam, magma, fire cone, plant) through the normal event path 3 m beyond the first fighter, the first cue one
  interval after it is switched on (the scenario settles first); `ff.fx.ShowcaseShots 0.12|0.5|1.2` saves
  `showcase_<n>_<cue>_<ms>.png` that long after each cue (and `prewarm_<ms>.png` after the pre-warm) into `-FFShotDir`.
  Example: `-scenario=lab -ExecCmds="ff.fx.Showcase 2.5, ff.fx.ShowcaseShots 0.12|0.5|1.2" -FFShot=50 -FFShotDir=<dir> -FFShotQuit`.
* Pre-warm (`ff.fx.Prewarm`, default 3): the first time a cue system or an FX material draws, the renderer builds its
  pipelines (PSOs) - in editor builds synchronously (PSO precaching is compiled out of `WITH_EDITOR`), and the editor
  also compiles each pack system on its first spawn. The first blast of a fight used to freeze one 164 ms frame. So once
  the camera looks at the floor after a scenario load, `UFourfoldFxSubsystem` picks a point on the view ray 1.5-3.5 m
  *behind* the floor (`ffx::PointBehindFloor`: in the frustum, so things there are drawn, but depth-hidden) and
  bit 1 plays each loaded slot system there once (scale 0.1, 1.5 s, exempt from scalability culling, occlusion
  queries off so it draws whenever it has particles, ManualRelease then back to the pool), bit 2 draws a small
  triangle per FX material and each static FX mesh there for 4 frames (rocks / walls with shadows). The cost moves
  into the load frames; measured below in PROGRESS.md (Lab: first blast 164 -> 17 ms frame, first metal 49 -> 17 ms).
* Pooling: fire-and-forget cues use AutoRelease; systems the layer stops itself (slot `life`, pre-warm, loops) are
  ManualRelease, so a stop never hits a component the pool already handed to another cue.
* Persistent slots (`fx_config.json` "niagara_loops", `ffx::LCue`, same fields as the cue slots; `replace` = the
  view tones its procedural look down): views send a `LoopReq` (stable key, position, +Z direction, scale,
  intensity, colours) every frame while their body lives; `FFourfoldFxNiagara::UpdateLoops` keeps one component per
  key, moves and re-binds it, and stops it gently (`ReleaseToPool`: live particles finish) on the first frame the key
  is missing. Current: fire fields / lines -> NS_Fire at up to 4 sites (field centre + ring, line every 1.8 m;
  `Flame Color` = flame palette x 6, grey smoke, its own lights off: the procedural fire lights the scene, tongues
  at 0.55), steam / geyser / steam screen / smoke clouds -> NS_Chimney_Smoke at the base, fireballs / comets ->
  NS_RocketTrail (flare on) following the ball along its tail. Wind ribbons (tornadoes / funnels: three
  NS_SimpleRibbonTrail points circling the funnel at three heights; wind crescents: a ribbon from each tip) are coded
  and unit-tested but ship with an empty `niagara_loops.wind.path` until a look check confirms them. GPU (Mac,
  75 %, 60 fps cap): a burning fire field
  plus a steam cloud +1.7 ms (13.35 vs 11.66 ms median; +2.9 ms before the trim to 4 sites without lights). Loop
  systems are pre-warmed with the cue slots.
* GPU budget: the Mac game is GPU-bound; slots are one-shot bursts only (no persistent Niagara), the pack's own
  Effect Types handle significance / culling, `min_quality` 1 keeps them off on low.
* Profiling: `-csvCaptureFrames=2400 -ExecCmds="ff.fx.Showcase 2.5"` then `python3 unreal/Tools/vfx/csv_fx.py <csv>`
  prints per cue window GPU mean / max, worst frame / render-thread time and PSO misses, first uses apart from
  repeats. Never take showcase shots in a profiled run (each screenshot stalls 150+ ms). On this Mac add
  `-FFExec="2:t.MaxFPS 60|2:r.DynamicRes.FrameTimeBudget 16.67"` (dynamic resolution follows the frame-rate cap).

## Physics debris (Chaos fracture of stones and walls)
Visual only: the sim decides when a body breaks and spawns its own rubble bodies; the pieces never feed back.
* Pieces: `ffx::VoronoiPieces` clips a closed mesh with the bisector planes of n sites (best-candidate spread) and caps
  every cut, so the pieces fit together exactly and keep the source's local frame (the rock material's local-space
  noise runs on across the cuts). `meshlib::RockPieces(seed, n)` (Rock(seed), ~1 ms per variant, cached for the run)
  and `meshlib::WallPieces(seed, perBlock)` (each of the Wall's five blocks, closed at the base for this). Each piece
  carries <= 42 support points for its convex collision.
* Requests: `shatter` on a stone and `wall_crumble` call `BodyView::Break`; `StoneView` / `WallView` emit a
  `FractureReq` (pieces, the intact body's transform, its look parameters, inherited velocity, burst origin / speed,
  scale, life) when `FxFrameIn::physicsDebris` is set and the quality allows (`rock_pieces` / `wall_pieces`, 0 on
  low). The stone keeps drawing (the sim's body lives on; pieces are 0.65x fragments); the wall stops drawing at
  once (its pieces take its place; the sim's two rubble stones fly out beside them). Sand / mud walls and molten
  stones keep the procedural look. A frozen body's `shatter` keeps its splash + ice shards (stones used to get them
  too: the legacy handler was ice-only).
* Glue (`FFourfoldFxDebris`): pooled `UProceduralMeshComponent`s (<= 64) with `SetCollisionConvexMeshes` cooked once
  per piece shape (a component keeps its shape when idle), Destructible channel only, blocking only the arena copy
  (`UBoxComponent`s from `ff::ArenaView`: ground with the pool basin, metal plate, every solid) and each other - the
  fighters, cameras and traces never see them. Biggest pieces first when `pieces_max` runs out. After `life` a
  piece stops simulating and sinks (`sink_time`), then goes back to the pool. Tuning: fx_config.json "fracture"
  (lives, scales, burst speeds, spin, friction, restitution, damping, max depenetration, density).
* Raised stone walls send a `ColliderReq` (oriented box, stable key) while physics debris runs; the glue keeps a
  kinematic box per key, so pieces bounce off earth walls too. Hard landings (> 1.2 m/s, 0.3 s per piece) come back
  to the logic as `FxFrameIn::debrisImpacts` and kick up dust puffs.
* Cut faces carry uv2.y = -1; `FFRockFreshCut` (FFRock.ush, called from M_FX_Rock) draws them as fresh broken
  stone: lighter, greyer, matte.
* Console: `ff.fx.Debris 0/1`; look check: `ff.fx.Showcase` ends its list with `break/rock` and `break/wall`
  (`fx_test_break` events: a stone / wall breaking with no sim body behind it); `ff.fx.ShowcaseFilter break` plays
  only those.

## Look-check tools (dev)
* `ff.fx.Showcase <s>` + `ff.fx.ShowcaseFilter <text>` + `ff.fx.ShowcaseShots 0.1|0.5`: cue list `burst/*`,
  `erupt/*`, `cone/flame`, `break/rock|wall` (physics debris), `bolt/lightning` (a "lightning" event: bolt + arcs),
  `body/tornado|crescent|fire_field|steam` (a fake body added to the FX layer's copy of the snapshots for one interval -
  the sim never sees it; for persistent views without a Lab threat in frame). Screenshots land in `-FFShotDir`.
* `-FFFxConfig=<file>`: read the FX config from another file (slot experiments without touching the committed one).
* `-FFLabSpawn=<entry>@<sim s>,...` (game module): real Lab threats, e.g. `fire_field`, `steam`, `fireball`, `comet`,
  `fire_line`, `bolt`, `tornado`, `wind_crescent`, `stone_80` (list: Source/FourfoldCore/Data/lab.json).
* Process-time `-FFShot` is too coarse for short events (bolts, crescents); use the showcase shots instead.

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
$SCR/lt_gcc/ffx_preview --noise $SCR/noise.rgba8 [--fb <raw atlases>] --out $SCR/frames [--gallery] [--only fire0_slot] [--ticks 100,400,700]
python3 unreal/Tools/vfx/preview_sheet.py $SCR/frames $SCR/sheet.png
```
`ffx_preview` plays every move through the Lab and rasterises the draw list in software with the **same material
code** (generated from spec.py) over the shared HLSL through `hlsl_shim.h` - the review renders of this stream. Its sun
is the arena's (14 lux at exposure 1: 4.46 x albedo facing it). On the Mac: `export PATH=$HOME/Library/Python/3.9/bin:$PATH`
(cmake + ninja), `-DCMAKE_CXX_COMPILER=clang++`; the raw noise / atlases are the SourceArt PNGs as plain RGBA8 bytes
(PIL `Image.open(p).convert("RGBA").tobytes()`, atlas files named `<flipbook>.rgba8`, e.g. `smoke_puff.rgba8`).

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
