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
| 3 | Locomotion from the Game Animation Sample (mocap), retargeted onto the fighter skeleton: `Tools/gasp/migrate_from_gasp.py` copies the content (git-ignored), setup part `mocap` builds IK_Fighter + RTG_UEFN_to_Fighter and the A_mm_* clips, `clips_mocap.json` / `anim_map_mocap.json` overlay the clip data (multi-cycle gaits, measured speed, left-foot phase, foot plants). Next: motion matching (PoseSearch) for starts / stops / pivots. | GASP downloaded | walk / run / strafe / back + get-ups done |
| 4 | Fighter = MetaHuman: Kellan from GASP (Tools/gasp/migrate_metahuman.py, setup part `metahuman`): all clips retargeted, face / clothing / grooms assembled at runtime, role tints. Next: martial-arts outfits per element (current: hoodie / cargo pants), own MetaHuman when the owner makes one. | optional: custom MetaHuman | done (outfit open) |
| 5 | Arena surfaces: real PBR textures (Poly Haven CC0 / free Fab Megascans) instead of generated ones. | approve downloads | todo |
| 6 | VFX in Niagara: GPU fire/smoke/steam, water ribbons + splashes, rock debris, wind ribbons, lightning beams; mobile fallbacks. Niagara cue layer: one-shot cues spawn systems from Epic's Niagara Examples Pack (git-ignored `Content/NiagaraExamples`; config `fx_config.json` "niagara", docs/fx/README.md); blasts and dust / sand bursts now use them, sparks / steam add to the procedural look; without the pack everything stays procedural. Next: Chaos fracture of stone walls / thrown stones (fx), then persistent Niagara looks (fire fields, water ribbons, wind) where the pack falls short. | Niagara Examples Pack downloaded | cue layer done |
| 7 | Physics: ragdoll blend on knockdown (done, row 2). Chaos fracture of stone walls / thrown stones: FX session (FourfoldFX). Cloth: todo. | no | partly |
| 8 | Martial-arts strikes: buy a mocap pack later; until then refine the hand-keyed clips (contact timing, overlap, follow-through). | optional purchase | later |

## Skeleton unit fix (2026-10-07)
Every FBX used to be written in metres; Unreal's importers put the x100 unit conversion on the top node, which is the
root bone (root scale 100, every bone offset in metres). That collapsed IK-retargeted clips and fed the native anim runtime
(which reads bone translations as centimetres) a skeleton 100x too small: foot IK, planting, landing and look-at worked
on a 1.7 cm fighter. `Tools/blender/common/ff_fbx_export.py` now exports a centimetre copy (FBX unit factor 1);
`convert_fbx_to_cm.py` re-wrote the 3 meshes + 131 clips; SK_Fighter / SKEL_Fighter / all clips were re-imported
from scratch (root scale 1, pelvis at 96 cm).
