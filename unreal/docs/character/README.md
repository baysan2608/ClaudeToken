# Stream `character` - the fighter

One original, stylized-realistic martial artist (1.79 m, lean, athletic, calm face, hair tied into a top knot) worn by
every fighter; player / rival / dummies are palette recolours. Skinned to the frozen UE5-Mannequin-compatible rig
(`Tools/blender/common/ff_rig_spec.py`, 89 Manny bones + 15 `ff_*` spring bones), so the `animation` stream's clips play
on it as-is and bought mocap / Epic samples retarget with the IK Retargeter.

Outfit (original design, no symbols): wrap-front training tunic (left panel over right, contrasting collar band,
3/4 sleeves with cuffs) whose long hem is split at the sides into front / back panels; a two-turn sash knotted at
the front-left hip with a long tail (left) and a tucked end (right); loose trousers bloused into shin wraps; forearm /
hand wraps (fingers and thumb tip free); soft cloth shoes with layered white soles.

## What is shipped
| File | Content |
|---|---|
| `SourceArt/Character/SK_Fighter.fbx` | LOD0 skeletal mesh + skeleton (armature object `root`, frozen export settings) |
| `SourceArt/Character/SK_Fighter_LOD1.fbx`, `SK_Fighter_LOD2.fbx` | LOD1 / LOD2 (same skeleton), imported into SK_Fighter by the editor script |
| `SourceArt/Character/T_Fighter_<slot>_{BC,N,ORM}.png` | 8 slots x 3 maps: skin & cloth_main 2048, others 1024. BC sRGB (alpha = tint mask, skin: subsurface mask, eyes: iris mask), N tangent-space **DirectX** (green flipped), ORM = AO / roughness / metallic |
| `SourceArt/Character/T_Fighter_Detail_{Weave_N,Skin_N,Noise}.png` | 512 tiling detail maps (weave threads, pores, status noise) |
| `SourceArt/Character/previews/*` | turnaround, face / hands close-ups, deformation sheet, palettes, size check |
| `SourceArt/Character/third_party/makehuman/` | the CC0 base-mesh data the build starts from (see LICENSES.md) |
| `Content/Fourfold/Data/character.json` | runtime data (schema `fourfold.character/1`, + import settings) |
| `Content/Python/fourfold/character/` | `build_all(force=False)`: textures, materials, mesh + LODs + physics asset, slots |
| `Shaders/Character/FFFighter.ush` | HLSL helpers (tint, detail normal, sheen, fake subsurface, wet / frost / burn / glow) |

## Measured (LOD0 / LOD1 / LOD2)
Triangles 29,298 / 14,576 / 5,789 (budgets 30k / 15k / 6k). <= 4 influences, normalised, no unweighted vertex, mirrored
vertex pairs have mirrored weights. Texel density: skin 2,258 px/m (face x1.35 = ~3,050 px/m), cloth_main 935,
wraps ~1,480, hair ~2,025, shoes ~1,690, sash ~930, accent ~820 px/m. 8 material slots in the fixed order
`skin hair eyes cloth_main cloth_accent wraps sash shoes`. Exact numbers: `SourceArt/Character/build_report.json`.

## Pipeline (how it is made)
Modelling approach (documented choice): procedural lofting (the Godot build's method) could not reach a convincing
face and hands, so the body starts from the **CC0 MakeHuman base mesh** (hm08) - data only, pinned commit, licence
in `SourceArt/Character/LICENSES.md`. Everything else is our own geometry and code.
1. `ch_mh.py` reads the base mesh, targets, default-skeleton joints and CC0 skin weights. `ch_body.py` RECIPE: adult
   male, muscle 0.72, weight 0.38, mixed ancestry, measurement targets solved by least squares against the rig's
   landmarks (`solve_proportions.py`), plus build / face targets (square jaw, cheekbones, straight nose).
2. `ch_fit.py` fits the morphed mesh onto the frozen rig: per-limb stretch + dual-quaternion blend of rigid fits
   (arms straightened into the A-pose, palms to the thighs, legs parallel, fingers on the metacarpal / phalanx bones).
3. `ch_weights.py`: CC0 weights mapped to our bones (spine / neck re-split by height, upper / lower arm and leg
   weights distributed over the UE twist bones along the bone axis, clavicle region added), cloth smoothing,
   hem panels = pelvis -> thigh + `ff_hem_*` chains (front panels ride on the thighs in kicks), sash tails on
   `ff_sash_l/r_*`, hair tail on `ff_hair_01..03`, <= 4 influences, symmetry.
4. `ch_bodypart.py` / `ch_garments.py` / `ch_hair.py`: hidden skin removed under clothes (margins kept), forearm /
   hand wraps raised from the body surface with a lip; tunic + trousers from the base mesh's CC0 tights helper
   (loosened, draped, exact cuts on smooth fields), hem from the skirt helper (hangs from its widest point, side
   slits), collar / cuff bands swept along the cut edges (their flat part conforms to the tunic), sash band + knot +
   tails, hair cap (scalp subdivided once, cut on a smooth hairline field that keeps clear of the ears - ear vertices
   from the CC0 ear-translate targets - edge relaxed, thickness tapering to the edge) combed to a top knot (bun + tie
   + tail), eyeballs with a cornea bulge, upper-lash strips on the lid margin of the base mesh's lash helper.
5. `ch_uv.py`: analytic UVs where the pattern direction matters (wraps spiral, hair strands, bands), Blender unwrap
   with front / back seams for the garments, islands grain-aligned (world up = +V), texel density levelled, skyline
   packing per material. A second UV set (`PatternUV`, metres) drives the procedural textures.
6. `build_character.py`: budget decimation (face / lips / eyelids / ears protected, hidden scalp first), LOD1 / LOD2,
   ngons split, Cycles AO bake, `ch_textures.py` (numpy, band-limited so nothing aliases), FBX export via
   `ff_fbx_export`, previews (`ch_previews.py`, Cycles CPU), `character.json`.

## Regenerate / test here
```bash
B=/home/user/tools/bpyenv/bin/python
$B unreal/Tools/blender/character/build_character.py              # everything (~9 min with previews)
$B unreal/Tools/blender/character/build_character.py --no-previews --quick --out /tmp/x   # fast iteration
$B unreal/Tools/blender/character/validate_character.py           # reads the FBX like Unreal will
$B unreal/Tools/blender/common/test_rig_spec.py /tmp/rigtest       # frozen rig self-test (unchanged, green)
python3 unreal/Tools/py_mock/run_with_mock_unreal.py unreal/Content/Python/fourfold/character/__init__.py --call fourfold.character:build_all
python3 unreal/Tools/blender/character/check_hlsl.py <dxc>          # optional: DXC compile of FFFighter.ush
```
`retexture.py <scene.blend> <out>` regenerates textures from a saved build scene (AO bake cached). Under the mock the
dry run ends "created 39, failed 1": the mock's import creates no asset, so the SK_Fighter lookup fails by design; the
point of the run is that every code path executes without a Python error.

## On the Mac (owner)
Nothing extra: `fourfold_setup.py` runs `fourfold.character.build_all()` (order fx -> character -> animation).
Check the Output Log for `[Fourfold][character]` lines and `setup_report.json`. If the Interchange import names the
skeleton differently, the script renames it to `SKEL_Fighter` / `PA_Fighter`; if `import_lod` fails, the mesh still
works with LOD0 only (report says so).

## Notes for other streams
* **animation**: bind pose = rig rest (A-pose, palms to thighs). Mesh deforms well up to ~130 deg shoulder
  elevation *relative to the clavicle*: for arms-overhead also elevate the clavicle 25-35 deg (scapular rotation).
  Twist bones are skinned UE-style (upperarm_twist_01 near the shoulder, lowerarm_twist_01 near the wrist); keying
  ~30 % / 60 % of the forearm roll on lowerarm_twist_02 / _01 avoids wrist candy-wrapping. Fingers are individually
  weighted; the wraps cover hand and thumb base, so all hand shapes of MARTIAL_ARTS §1.4 read.
* **game**: read `character.json` (`palettes`, `mesh_yaw_offset_deg` -90, `slots`, `materials`). Every slot's
  material understands FF_Main / FF_Accent / FF_Trim / FF_ElementColor and FF_Wet / FF_Frost / FF_Burn /
  FF_ElementGlow (eyes glow in the iris). Spring chains: `ff_hair_01..03` (stiff, short), `ff_sash_l/r_01..02`
  (loose, gravity), `ff_hem_{fl,fr,bl,br}_01..02` (front panels already follow the thighs ~40 % through skinning;
  springs add swing; thigh capsules of ~9 cm radius keep the back panels out of the legs). The physics asset is
  auto-generated (capsules) and only needed for ragdoll / hit traces.
* **fx**: status looks on the body are material parameters; attach drips / flames / frost to bones (`hand_*`,
  `foot_*`, `spine_05`, `head`).

## Known gaps
* Painted-realism textures are procedural (no hand painting); the face reads well at gameplay distance and in
  close-up, but has less micro-detail than a sculpted hero head. The hair is a solid cap with strand normals (no
  cards) - a deliberate mobile choice.
* LOD import uses `SkeletalMeshEditorSubsystem.import_lod`; with Interchange this path is newer than the legacy one
  - verify on the Mac (fallback: Skeletal Mesh Editor > LOD Import, or regenerate LODs with the reduction plugin).
* Cloth stays skinned (no simulation); intersections in extreme poses are limited by weights and the spring chains.
* Every LOD keeps all 8 material sections (fixed slot names), i.e. 8 draw calls per fighter; ARCHITECTURE §8.2 asks
  for <= 4 "where possible" - merging slots for LOD2 would need an atlas pass (not done).
* Lashes are short opaque strips (no masked cards) - they read as a lash line, not as single lashes.
* Material graphs and Python are written blind (verified API patterns, mock dry run, HLSL compiled with DXC); first
  real compile happens on the Mac.
