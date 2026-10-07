# Quality pass plan (started 2026-10-07)

Owner feedback after the first play: "textures, movement not smooth, lighting and effects look cheap; needs much better
look, fluid movement, physics and dynamics." Decisions: free Epic assets first (MetaHuman + Game Animation Sample),
Mac high-end look first, iPhone scaled down afterwards. The owner downloads Epic/Fab content; Claude wires it up.

Root cause: every asset was script-generated in a container without Unreal (hand-keyed clips, procedural textures,
home-made VFX meshes/sprites, mesh sky dome, no atmosphere, no Niagara, no Chaos), and the renderer was set up
mobile-first (TargetedHardwareClass=Mobile, static lighting, no volumetrics).

| # | Step | Needs owner? | State |
|---|---|---|---|
| 1 | Lighting + post: Sky Atmosphere, volumetric clouds, volumetric fog, movable sun, Lumen high, TSR, auto exposure, graded post volume; desktop hardware class. iOS keeps its own cheaper settings in `Config/IOS/IOSEngine.ini`. | no | in progress |
| 2 | Animation runtime: inertialization between clips, foot IK / ground locking, procedural lean + hip sway, hit reactions via physical animation blend. | no | todo |
| 3 | Locomotion from the Game Animation Sample (mocap, motion matching), retargeted onto the fighter skeleton. | download GASP | todo |
| 4 | Fighter = MetaHuman (Creator plugin is built into 5.8). Clothing per element. | create character | todo |
| 5 | Arena surfaces: real PBR textures (Poly Haven CC0 / free Fab Megascans) instead of generated ones. | approve downloads | todo |
| 6 | VFX in Niagara: GPU fire/smoke/steam, water ribbons + splashes, rock debris, wind ribbons, lightning beams; mobile fallbacks. | no | todo |
| 7 | Physics: Chaos Geometry Collections for stone walls/projectiles (fracture on hit), cloth on sash/sleeves, ragdoll blend on knockdown. | no | todo |
| 8 | Martial-arts strikes: buy a mocap pack later; until then refine the hand-keyed clips (contact timing, overlap, follow-through). | optional purchase | later |
