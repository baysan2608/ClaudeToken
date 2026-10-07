# Quality pass plan (started 2026-10-07)

Owner feedback after the first play: "textures, movement not smooth, lighting and effects look cheap; needs much better
look, fluid movement, physics and dynamics." Decisions: free Epic assets first (MetaHuman + Game Animation Sample),
Mac high-end look first, iPhone scaled down afterwards. The owner downloads Epic/Fab content; Claude wires it up.

Root cause: every asset was script-generated in a container without Unreal (hand-keyed clips, procedural textures,
home-made VFX meshes/sprites, mesh sky dome, no atmosphere, no Niagara, no Chaos), and the renderer was set up
mobile-first (TargetedHardwareClass=Mobile, static lighting, no volumetrics).

| # | Step | Needs owner? | State |
|---|---|---|---|
| 1 | Lighting + post: Sky Atmosphere, volumetric clouds, volumetric fog, movable sun, Lumen high, TSR, auto exposure, graded post volume; desktop hardware class. iOS keeps its own cheaper settings in `Config/IOS/IOSEngine.ini`. Photo-scanned CC0 arena textures (Poly Haven, `Tools/world/fetch_polyhaven.py`). Courtyard stripe artefact fixed (sun per-pixel atmosphere transmittance off). | no | done (exposure / grading still to tune) |
| 2 | Animation runtime: inertialization, foot IK, lean (existed; fixed by the skeleton unit fix). Physical reactions: PhysicalAnimationComponent upper-body flinch on hits; ragdoll knockdowns (pelvis world-driven to the sim target), pose-snapshot hand-back into trimmed mocap get-ups aligned to the lying body. | no | done (tuning open) |
| 3 | Locomotion from the Game Animation Sample (mocap), retargeted onto the fighter skeleton: `Tools/gasp/migrate_from_gasp.py` copies the content (git-ignored), setup part `mocap` builds IK_Fighter + RTG_UEFN_to_Fighter and the A_mm_* clips, `clips_mocap.json` / `anim_map_mocap.json` overlay the clip data (multi-cycle gaits, measured speed, left-foot phase, foot plants). Root fix (2026-10-07): the retargeters' Root Motion op had no root bones and copied the pelvis, so the locked in-place clips sank the body ~90 cm into the floor; fixed in both builders. Run starts / stops: `ffg::LocoTransition`, distance-matched GASP clips (root travel curves in clips_mocap.json), settle capped at 0.45 s. Next: turn-in-place (Stand_Turn_090/180 L/R), pivots, walk / strafe starts / stops, then element stances on MetaHuman proportions. | GASP downloaded | gaits + get-ups + run start / stop done |
| 4 | Fighter = MetaHuman: Kellan from GASP (Tools/gasp/migrate_metahuman.py, setup part `metahuman`): all clips retargeted, face / grooms assembled at runtime, role tints. Outfit (owner, 2026-10-07): bare-chested, barefoot, training pants. Kellan's own body mesh is partial (only what his clothes leave visible), so the fighters render the sample's complete m_med_nrw body (`SKM_FF_Body`, metahuman.json `body_mesh`) with Kellan's skin; head materials copied with bUseNeckHide off. Open: proper loose training pants + sash (cargo pants for now), a thin pale seam at the neck base, own MetaHuman when the owner makes one. | optional: custom MetaHuman | bare chest done; pants / sash open |
| 5 | Arena surfaces: real PBR textures (Poly Haven CC0 / free Fab Megascans) instead of generated ones. | approve downloads | todo |
| 6 | VFX in Niagara: GPU fire/smoke/steam, water ribbons + splashes, rock debris, wind ribbons, lightning beams; mobile fallbacks. Niagara cue layer: one-shot cues spawn systems from Epic's Niagara Examples Pack (git-ignored `Content/NiagaraExamples`; config `fx_config.json` "niagara", docs/fx/README.md); blasts and dust / sand bursts now use them, sparks / steam add to the procedural look; without the pack everything stays procedural. First uses are pre-warmed at scenario load, hidden behind the floor (`ff.fx.Prewarm`: first blast 164 -> 17 ms frame). Next: Chaos fracture of stone walls / thrown stones (fx), then persistent Niagara looks (fire fields, water ribbons, wind) where the pack falls short. | Niagara Examples Pack downloaded | cue layer + pre-warm done |
| 7 | Physics: ragdoll blend on knockdown (done, row 2). Chaos fracture of stone walls / thrown stones: FX session (FourfoldFX). Cloth: todo. | no | partly |
| 8 | Martial-arts strikes: buy a mocap pack later; until then refine the hand-keyed clips (contact timing, overlap, follow-through). | optional purchase | later |

## Skeleton unit fix (2026-10-07)
Every FBX used to be written in metres; Unreal's importers put the x100 unit conversion on the top node, which is the
root bone (root scale 100, every bone offset in metres). That collapsed IK-retargeted clips and fed the native anim runtime
(which reads bone translations as centimetres) a skeleton 100x too small: foot IK, planting, landing and look-at worked
on a 1.7 cm fighter. `Tools/blender/common/ff_fbx_export.py` now exports a centimetre copy (FBX unit factor 1);
`convert_fbx_to_cm.py` re-wrote the 3 meshes + 131 clips; SK_Fighter / SKEL_Fighter / all clips were re-imported
from scratch (root scale 1, pelvis at 96 cm).

## Mac performance (2026-10-07)
Target 60 fps (16.6 ms) in a duel, Mac game window 1600x900, M4 Pro. Measure with
`bash Tools/mac/shot.sh <dir> 30,66 -scenario=spar -autoplay=duel -csvGpuStats -FFExec="31:csvprofile frames=300|..."`
(`-FFExec` runs console commands at given seconds, so one run can A/B several settings) and summarise with
`python3 Tools/mac/csv_gpu.py [a.csv [b.csv]]`. `ProfileGPU` in `-FFExec` dumps the pass tree to the game log.

- **The "75 % TSR" never applied:** `r.ScreenPercentage.Default.Desktop.Mode=1` is "based on display resolution" (not manual),
  and `Scalability::FQualityLevels::SetFromSingleQualityLevel(3)` also sets `sg.ResolutionQuality=100`, i.e. `r.ScreenPercentage
  100`. The game rendered at 100 %: ~18 ms GPU. Fixed: Mode 0 (manual 75 %), the settings subsystem keeps
  `ResolutionQuality=0` on the Mac and turns on **dynamic resolution** (50..100 %, budget = 1000 / frame-rate cap).
- Result (60 fps cap): fixed 75 % = 12.4-13.2 ms GPU median; dynamic resolution settles at ~81 % = 14.5 ms, 0 dropped frames
  in 4 x 270-frame windows. With the 120 fps cap (owner's saved setting on this Mac) the budget is 8.3 ms and the picture drops
  to the 50 % floor.
- The GPU work is almost purely per-pixel (50 % = 9 ms, 100 % = 18 ms). On Apple GPUs the per-pass split is unreliable: pass
  timestamps overlap (TBDR), so a ~3 ms block lands on whatever pass follows translucency ("VSM Log Stats And Status", TSR,
  "Unaccounted"). Only A/B toggles give real savings. Measured at 100 %: volumetric fog -0.9 ms, cheaper cloud sky capture
  (cloud resolution divider 4, 1 face / frame) -0.25, Lumen probe downsample 24 -0.3, cloud ray cap 96 / VSM off / skin
  cache off ~0. Volumetric clouds cost ~1.7 ms at any resolution (traced at 1/4 res).
- iOS note: the same scalability call maps iOS levels 0 / 1 / 2 to 50 / 71 / 87 % screen percentage; decide in the iOS budget.
