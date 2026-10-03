# Fourfold VFX / rendering layer

Owner: rendering/VFX. Code: `game/presentation/vfx/`, shaders: `game/presentation/shaders/`,
validation: `game/tests/vfx/`. Godot 4.7.2, **Mobile** renderer (Metal on iOS), typed GDScript.

Visual direction: restrained stylized realism. Coherent shapes, tactile materials, convincing light,
deliberate colour. No neon, no persistent glow clouds, no heavy screen-space effects.

## Contract with the simulation

Views are driven by **plain parameters every rendered frame**. They never own gameplay state and can change
look continuously without despawning. The key continuum is one material in four states:

```
stone --heat--> glowing cracks --melt--> molten blob --(LavaWaveView)--> ground wave --crust--> cooled rock
```

`StoneView` and `LavaWaveView` share one shading function (`vfx_rock.gdshaderinc: rock_surface`) with the
same knobs (`heat`, `melt`, `crust`), so a stone that spreads into a wave (cross-fade the two views) or a
wave that cools keeps the same plates, fissures, colours and sheen.

All setters are cheap and change-guarded; call them every frame if convenient.

## Quick start

```gdscript
VfxTextures.warm()                       # once, loading screen (about 50 ms: 32^3 noise volume + 128^2 noise)
var pool := VfxPool.new(); add_child(pool)
await pool.prewarm(Vector3(0, -100, 0))   # optional: compiles every effect's pipelines up front

var stone: StoneView = pool.get_fx("stone")
stone.setup(seed, 0.4)                   # deterministic mesh from seed
stone.set_thermal(heat01, melt01)        # every frame, from the sim
stone.set_crust(crust01)

var fb: FireBurstFX = pool.get_fx("fire_burst")
fb.play(origin, dir, 3.0, 1.0)           # releases itself to the pool when finished
```

Effects extend `VfxEffect` (a Node3D; `StoneView` is a plain Node3D as specified): `reset()`, `advance(dt)`, `is_playing()`, signal `finished`.
They step themselves in `_process`; set `manual_time = true` to drive `advance(dt)` yourself (tests, replays,
rollback). Effects that live in world space (`LightningArcFX`, `ChargeAimFX`, `GlideTrailFX`) are
`top_level` and take world coordinates; the others are placed by `play()`/their transform.

## Class reference

### StoneView (extends Node3D)
| call | meaning |
|---|---|
| `setup(seed: int, radius: float)` | builds/caches the rock mesh for `seed` (cube-cut icosphere, ~320 tris, flat shaded, slightly non-spherical) |
| `set_thermal(heat01, melt01)` | heat: glowing crack network. melt: vertices relax to a smooth blob, glossy molten surface, bright fissures between dark plates |
| `set_crust(c)` | crust plates close the fissures, emission dims and reddens, surface turns rough basalt |
| `reset()` | cold stone |
| `use_light` (export, default true) | one shadowless warm OmniLight3D scaled by glow |

State to look (see `stone.gdshader`, `vfx_rock.gdshaderinc`):
* (0,0,0): dusty grey-brown stone, dust on up-facing faces, faint dark hairline cracks (a fraction of Voronoi cell borders).
* heat 0.3-0.6: more borders become cracks, deep red then orange cores, plate rims glow slightly.
* heat 1: wide yellow-orange cracks, rock darkened by heat.
* melt 0.1-1: unequal vertex relax toward an ellipsoid blob (per-vertex stagger), belly bulge / flattened bottom, slow viscous wobble; plates turn near-black glossy crust and stand proud; fissures widen to about 16 % of a plate width and go yellow-white.
* crust 0-1: fissures shrink to hairlines (dull red), emission about 25 %, roughness 0.85, wobble stills.
* (heat 0, melt 0, crust 1) = cooled rock; (0, 1, 1) = cooled blob.

### LavaWaveView (extends VfxEffect)
`set_path(points: PackedVector3Array, widths: PackedFloat32Array)` - polyline **tail to front** on the ground (may step
down ledges), `widths` = full width per point. Builds a strip with a rounded cross-section (9 vertices across),
height 0.40 m at the front (+28 % bulge), 0.20 m at the tail, rounded front cap, slope-aware up vector so it drapes over
ledges. `set_state(melt01, crust01, flow_speed)`: melt 1 = liquid, 0 = solid; crust plates form and stop the flow;
`flow_speed` m/s of plate drift (phase is integrated in `advance`, so changing the speed never makes the pattern jump).
Banks cool and crust before the centre, the front stays hot. At melt 0 / crust 1: dark solid ridge with faint residual red
hairlines. Opaque, no alpha. Exports: `height_front`, `height_tail`, `front_bulge`, `plate_size`, `pattern_seed`.
`set_path` rebuilds the mesh (arrays reused): call it when the path changes, at most once per frame.

### WaterRibbonView / WaterBlobView (extend VfxEffect)
`WaterRibbonView.set_points(points, radius)`: 10-sided tube, smooth normals, rounded caps, rotation-minimising frames.
`set_state(frozen01)` for both. `WaterBlobView.setup(radius)`, `set_wobble(amount)`.
Water: one transparent layer, back faces culled, refracts the opaque screen with a lens-like offset and slight chromatic
split, depth-of-volume absorption, fresnel + specular, scrolling ripples and flow streaks. Ice: pale, frosted (blurred
refraction), faceted (geometric normals), jagged crystal displacement, internal fractures. A ribbon `flow_speed` export scrolls ripples.
Water-state continuity: `frozen01` blends everything (colour, frosting, facets, crystal displacement all scale with it); there is no pop at any value.

### FireBurstFX
`play(origin, dir, length, intensity)`. Two additive-style mesh flame layers (outer tongues + hot core), 2 draw calls,
728 tris, plus a brief OmniLight3D (no shadow) with attack ~15 ms and exponential decay (peak energy 3.0 x intensity).
0.55 s. No particles, no persistent glow. `FireChargeFX.set_charge(t01)`: steady teardrop flame at the hands (parent it
to the hand bone), flickering light (`0.15 + 1.65 t^2`), exports `base_size`, `direction`.

### LightningArcFX
`strike(points: PackedVector3Array, seed: int)`: main bolt passes exactly through the points (random-walk jitter pinned at
the nodes) plus 1-3 short tapering branches, **one** camera-facing ribbon mesh (billboarded in the vertex shader, so it
never needs rebuilding when the camera moves). 0.2 s with flicker/re-strike, light flash capped at 2.2 energy, no shadow.
Same seed gives the same bolt. `ChargeAimFX.set_aim(from, to, t01)`: thin crackling line (static mesh, uniforms only, no
allocation), calms and brightens with charge, glow dot at the source; `hide_aim()`.

### AirPushFX / GlideTrailFX
`AirPushFX.play(origin, dir, radius, length)`: travelling pressure band (soft tail + three thin crests) on an open cone that
refracts the screen by at most 0.75 % of the screen with low opacity (enemies stay visible), plus 14 dust streaks.
`GlideTrailFX`: `begin(follow_node)` / `push(world_pos)` / `end()` / `set_width(w)`; soft wind ribbon from the last 24 samples,
rebuilt each frame from reused arrays.
`VfxMaterials.screen_refraction = false` (set before creating water/air effects) switches to the **lite** shaders:
no screen texture read at all (this is the single cheap distortion in the whole layer; lite removes it).

### Particle one-shots (GPUParticles3D, soft billboard shader `puff.gdshader`)
`DustPuffFX.play(pos, normal, strength)`, `SteamFX.play(pos, amount01)`, `SplashFX.play(pos, normal, strength)`,
`EmberFX.play(pos, normal, strength)`. Fixed small particle counts (below), local fade where a puff would clip into the
ground, `tint` export on DustPuffFX (match the floor).

### ScorchDecal
`place(pos, radius, kind: "scorch"|"wet", lifetime, normal = UP)`. One lifted alpha quad (not a `Decal` node: projected decals
are capped and unreliable on Mobile; 1 draw call, 2 tris). Fades in 0.08 s, out over the last 35 %; scorch glows ~1 s. Flat ground only.

### EarthWallView
`setup(seed, width = 2.4, height = 1.0, thickness = 0.55)`, `set_rise(t01)` (centre block first, cubic ease-out, buried
below y = 0 at 0 so the ground hides it), `set_damage(t01)` (unlit cracks widen). 5 chamfered blocks, 200 tris. Same stone shader as
StoneView with `u_detail = 2.2` (finer plates). Dust puffs at the base while rising (`dust` export).

### VfxPool (extends Node)
`get_fx(key) -> Node`, `release(node)`, `release_all()`, `register(key, factory, cap)`, `set_cap`, `get_stats()`,
`prewarm(at, frames)`. Keys: built-in strings, or a `PackedScene` / `GDScript` (auto-registered, `default_cap` = 8).
At cap: recycles the **oldest active** instance when `recycle_oldest` (default) else returns `null`.
One-shot effects release themselves when done; persistent views (stone, wall, wave, water) are released by the caller.

| key | class | cap |
|---|---|---|
| stone | StoneView | 24 |
| earth_wall | EarthWallView | 6 |
| lava_wave | LavaWaveView | 4 |
| water_ribbon / water_blob | WaterRibbonView / WaterBlobView | 4 / 3 |
| fire_burst / fire_charge | FireBurstFX / FireChargeFX | 4 / 2 |
| lightning_arc / charge_aim | LightningArcFX / ChargeAimFX | 3 / 2 |
| air_push / glide_trail | AirPushFX / GlideTrailFX | 3 / 2 |
| dust_puff / steam / splash / ember | ... | 8 / 6 / 6 / 4 |
| scorch_decal | ScorchDecal | 8 |

## Arena materials (all procedural, no textures)
Use `VfxMaterials.arena("arena_ground")` etc. for one shared material per shader, then
`VfxMaterials.set_arena_wetness(w)` darkens/glosses flagstones, metal and ledge stone together ("global" wetness); duplicate a
material for a local variation. Or set the `wetness` uniform on your own ShaderMaterial.

| shader | purpose | key uniforms |
|---|---|---|
| `arena_ground.gdshader` | courtyard flagstones in world XZ: running bond, per-stone tone/tilt, bevelled AA seams, wear, wetness | `tile_size`, `seam_width`, `stone_a/b`, `seam_color`, `wear`, `wetness` |
| `metal_plate.gdshader` | dark brushed metal deck, 1 m plates, bolts, rust staining | `plate_size`, `metal_color`, `bolt_radius`, `bolt_inset`, `rust`, `wetness` |
| `ledge_stone.gdshader` | cut-stone masonry: courses on vertical faces, paving slabs on top faces (world space) | `course_height`, `block_width`, `stone_a/b`, `grime`, `wetness` |
| `pool_water.gdshader` | calm pool surface, **opaque** (no blend, no depth prepass, no screen read): rounded-rect SDF depth tint, wet-stone shoreline, foam line, 2 ripple layers. Fragments outside the rounded rectangle are discarded | `half_extent`, `corner_radius`, `shore_width`, `depth_range`, `shallow/deep/shore_color`, `ripple_strength`, `flow_speed` |

Each uses at most 3 value-noise evaluations per fragment and no texture fetches. `pool_water` expects a flat
PlaneMesh/QuadMesh in the pool's local XZ plane.

## Shared infrastructure
* `VfxTextures`: 32^3 RGBA8 smooth-noise volume (4 frequencies, sampled with mirrored coordinates and C1 interpolation),
  128^2 tileable 2D noise. Generated in code, cached, ~50 ms total. Crack networks are **analytic Voronoi** in the shaders (crisp at
  any distance), only tonal variation / domain warp come from the volume.
* `VfxMaterials`: shader cache, `make`, `make_stone/water/air_push`, `shared`, `arena`, `set_arena_wetness`, `screen_refraction`.
* `VfxMesh`: icosphere, rock (cached per seed, max 48), wall, flame, cone, streak meshes.
* Shader includes: `vfx_common.gdshaderinc` (mirror sampling, hashes, black-body ramp, analytic Voronoi 2D/3D with gradient, bump),
  `vfx_rock.gdshaderinc`, `water_core.gdshaderinc`, `air_push_core.gdshaderinc`.

## Budgets (measured geometry, estimated GPU)
Measured with the gallery `cost` station (visible pass, one effect alone; shadow casters add the same triangles once per shadow cascade).

| effect | draw calls | triangles | transparent layers | lights | particles |
|---|---|---|---|---|---|
| StoneView | 1 | 320 | 0 | 0-1 omni (optional) | 0 |
| EarthWallView | 1 | 200 | 0 | 0 | 14 (dust, 2 bursts) |
| LavaWaveView (10 pts) | 1 | ~230 (+~18 per extra point) | 0 | 0 | 0 |
| WaterRibbonView (18 pts) | 1 | ~440 | 1 | 0 | 0 |
| WaterBlobView | 1 | 624 | 1 | 0 | 0 |
| FireBurstFX | 2 | 728 | 2 | 1 omni, ~0.15 s | 0 |
| FireChargeFX | 2 | 728 | 2 | 1 omni | 0 |
| LightningArcFX | 1 | ~50-130 | 1 | 1 omni, ~0.2 s | 0 |
| ChargeAimFX | 1 | 58 | 1 | 0 | 0 |
| AirPushFX | 2 | 388 | 2 (1 screen read) | 0 | 0 |
| GlideTrailFX | 1 | <= 46 | 1 | 0 | 0 |
| DustPuffFX | 1 | 28 | 1 | 0 | 14 |
| SteamFX | 1 | 20 | 1 | 0 | 10 |
| SplashFX | 2 | 52 | 2 | 0 | 22 + 4 |
| EmberFX | 1 | 24 | 1 | 0 | 12 |
| ScorchDecal | 1 | 2 | 1 | 0 | 0 |

Worst case with every pool at cap: about 24 stones + 6 walls + 4 waves + 4 ribbons + 3 orbs = 41 opaque/semi-transparent draws,
fire/lightning/air/particles about 40 more, of which at most 4 + 2 + 3 + 2 = 11 omni lights are ever active together (realistic gameplay:
1-3). Recommended play caps are lower than the pool caps (see pool table); lower them with `set_cap` for low-end devices.

CPU per call (desktop, headless; multiply by ~3-4 for an iPhone 12): `WaterRibbonView.set_points` (18 pts) 120 us,
`LavaWaveView.set_path` (10 pts) 130 us, `LightningArcFX.strike` 60 us, `GlideTrailFX.advance` 18 us, `StoneView.set_thermal+set_crust` 2 us.

GPU (estimates, not measured on device): the heaviest fragment shader is `stone.gdshader` (27-cell 3D Voronoi, 2 volume fetches,
about 300 ALU); a stone covers little of the screen, so ten large stones is about 5 GFLOP/s, a few % of an A14 GPU. Lava is a 9-cell 2D
Voronoi. Transparent effects are small and non-overlapping by design (<= 2 layers per effect). The only screen-space cost is the
opaque-framebuffer copy triggered once per frame as soon as any visible water/air-push material reads the screen texture
(shared by all of them); `VfxMaterials.screen_refraction = false` removes it.

## Mobile performance notes
* No alpha blending on rock/lava/arena/pool (opaque). Transparent: water (1 layer), flames (2), lightning (1), puffs (1).
* Shadows: stones and walls cast, everything else does not. All lights are shadowless OmniLights; cap simultaneous lights.
* Shaders avoid dynamic indexing, use few varyings, no discards except `pool_water` (outside the pool outline).
* All animation is uniform- or vertex-driven; meshes are rebuilt only when their input changes (ribbons: once per frame while moving).
* `custom_aabb` is set wherever the vertex shader moves vertices; pooled effects hide + stop processing when idle.
* New shader pipelines compile on first use (and lavapipe JITs lazily): call `VfxPool.prewarm()` behind the loading screen.
* Do not enable per-instance `instance uniform`s on these shaders: they proved unreliable on the Mobile path in testing, so each view owns one
  ShaderMaterial (the draw cost is identical, nothing batches across objects anyway).

## Validation
* `tools/scripts/godot.sh --render --resolution 1600x900 res://tests/vfx/vfx_gallery.tscn` renders every station and writes
  `shot_<station>[_n].png` to the scratch dir (`--out=/path` to change). Station names can be passed after the scene to render a subset
  (`-- stones lava`); flags: `--noshadow`, `--lite`, `--skip=a,b`. `-- cost` prints the draw-call / triangle table.
  Effects are driven with manual time so screenshots are deterministic; the harness waits ~3 s for software-Vulkan pipelines to compile.
* `tools/scripts/godot.sh --headless -s res://tests/vfx/vfx_smoke_test.gd`: API, determinism, triangle budget, pool cap/recycle/release, prewarm.
* Stations: `stones` (heat 0/.5/1, melt .35/.65/1, crust .45/.8/1, cooled rock/blob, seeds), `lava` (crust 0/.5/1, solid ridge, 0.5 m ledge step),
  `water` (whip + orb at frozen 0/.5/1), `particles`, `fire`, `lightning` (+ aim line), `air` (+ glide trail), `wall` (rise, damage, scorch/wet),
  `arena` (flagstones dry/wet, metal, ledge, pool), `hero` (composite).

## Known limitations
* Screenshots were validated on lavapipe (CPU Vulkan, Mobile renderer); look and geometry are right, but there are no on-device timings yet.
* The relaxed molten shape is an ellipsoid with the rock's proportions, not a volume-conserving simulation; wobble displaces vertices
  without recomputing exact normals (approximated from the blob direction).
* Crack/plate pattern is object-space Voronoi: very small radii (< 0.15 m) look busy, large radii look coarse (use `u_detail` / `plate_scale`).
* Lava strip drapes linearly between points: a 0.5 m drop wants at least ~0.15 m of horizontal run to avoid a stretched texture; hairpin
  turns sharper than ~90 deg per segment can fold the ribbon.
* Water refracts the **opaque** scene only (never other transparent effects); ice frosting uses mip-blurred screen reads.
* Flame uses premultiplied alpha (0.8 cover for the outer layer) so it stays readable on bright ground; on pure black it looks slightly less hot than additive.
* ScorchDecal is a flat quad: no wrapping over ledges or slopes.
* Lightning is a ribbon (no volumetric glow); intensity is capped by design to avoid flashes.
* The first draw of a never-seen effect can hitch on device unless prewarmed.
