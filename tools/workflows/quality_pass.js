export const meta = {
  name: 'fourfold-quality-pass',
  description: 'Big quality pass: character asset + textures, animation runtime physics, environment textures/props/lighting, material physics VFX, in-game test controls; each independently reviewed',
  phases: [
    { title: 'Build', detail: 'five file-disjoint workstreams with chosen models/efforts' },
    { title: 'Review', detail: 'independent reviewer per stream: tests + rendered frames' },
  ],
}

const ROOT = '/home/user/ClaudeToken'
const SCR = '/tmp/claude-0/-home-user-ClaudeToken/1c5b9df4-f793-5f68-9f97-4ed835b254e3/scratchpad/quality'

const COMMON = `Project "Fourfold": original elemental martial-arts combat game for iPhone/iPad, Godot 4.7.2 (GDScript, Mobile renderer → Metal on iOS).
Repo ${ROOT}; Godot project ${ROOT}/game. Read ${ROOT}/HANDOFF.md, ${ROOT}/docs/COMBAT_SPEC.md and the doc for your area (docs/ANIMATION.md, docs/VFX.md,
docs/CONTROLS.md, docs/ASSET_MANIFEST.md). Evidence of the current look: ${ROOT}/docs/media/*.jpg|mp4 (Read the jpgs to see the baseline).
Tools in this Linux container: Godot 4.7.2 via ${ROOT}/tools/scripts/godot.sh (--headless for logic; --render for Vulkan lavapipe CPU rendering under Xvfb);
exact-version class docs at /home/user/tools/doc/doc/classes/*.xml (VERIFY every API there; Godot 4.7, not 3.x); Blender 4.5.14 LTS as a Python module:
/home/user/tools/bpyenv/bin/python (import bpy; Cycles CPU baking works; no GUI); Python 3.11 + numpy (pip install into a venv under ${SCR} if you need more); ffmpeg.
Renders/videos of the real game: 'tools/scripts/godot.sh --render --resolution 1280x592 --write-movie <out.avi> --fixed-fps 30 -- --autoplay=<flagship|show_earth|show_water|show_air|show_fire>:<seconds> --quality=2 [--shots=<dir>]'
(choreographies in game/actors/showcase_*.gd; --scenario=<id> picks a scenario, ids in game/scenarios/scenarios.gd), then ffmpeg to extract frames, and LOOK at them with the Read tool.
Tests (must pass at the end): 'tools/scripts/godot.sh --headless -s res://tests/run_tests.gd' (188), 'tools/scripts/godot.sh --headless -s res://tests/ui/run_ui_tests.gd' (50),
'tools/scripts/godot.sh --headless -s res://tests/check_scripts.gd'. If you add class_name scripts or assets, run 'tools/scripts/godot.sh --headless --import' once.
Always wrap commands with 'timeout' (renders up to 1200 s). Four other engineers work concurrently on OTHER files; if a run fails with a parse error in a file you
don't own, wait a minute and retry. NEVER edit files you don't own. No state-changing git commands. Scratch space: ${SCR}/<your stream>/.
Quality bar: restrained, premium stylized realism that runs at 60 fps on an iPhone 12-class GPU (Mobile renderer: no SSAO/SSR/SDFGI/volumetric fog; keep draw calls,
transparent layers and texture memory modest; prefer baked detail). Original designs only — no franchise references. Iterate: render, look, fix, until it clearly
looks better than the baseline. Finish with a concise report (<300 words): what changed, files, before/after screenshot paths, test results, known limitations.`

const STREAMS = [
  { key: 'character', model: 'sonnet', effort: 'high',
    own: 'tools/blender/*, assets_src/*, game/assets/characters/*, docs/ANIMATION.md',
    work: `CHARACTER ASSET. Upgrade the training fighter built by tools/blender/build_fighter.py (reproducible from an empty scene) so it looks far more real and premium:
- Mesh: better anatomy/proportions and silhouette, simple but real face (brow, eye sockets with painted eyes, nose, lips, ears), hair with volume, garments with
  folds/seams/hems, wrapped forearms/shins with visible wraps, shoes with soles; 8k–14k triangles; clean normals.
- Hands: replace mittens with articulated hands (at least thumb + grouped fingers, 2 bones each) and update clips so fists, open palms and two-finger
  lightning poses read clearly. ADD bones only: keep all 22 existing bone names, rest pose, scale (~1.75 m), +Z forward, in-place root policy.
- Secondary-motion bones for the runtime: two sash tails (3-bone chains each, e.g. sash_tail.L.001..003 / sash_tail.R.001..003) and a short hair/topknot chain
  (2 bones), weighted to the mesh; leave them unanimated in clips (the runtime simulates them with SpringBoneSimulator3D). List their names in docs/ANIMATION.md.
- Textures: UV-unwrap and BAKE real PBR maps in Blender (Cycles CPU, procedural node materials → albedo, normal, roughness [+AO]) at 1024² per material
  (or an atlas): woven cloth with seams and wear, leather/linen wraps, skin with subtle tone variation, hair strands. Keep the five material slot names
  (skin, cloth_main, cloth_accent, wraps, hair): the game recolours slots by multiplying albedo_color, so cloth/wrap albedo maps should be near-neutral detail
  (light greys/off-white) while skin/hair keep natural colour. Embed textures in fighter.glb. Keep total texture memory reasonable for mobile.
- Keep every clip name, duration, loop flag and contact time in fighter_clips.json (you may improve poses/motion quality: weight shift, overlap, follow-through,
  no foot skate; re-run the foot-skate validation).
- Validate in Godot: import, render close-ups and in-game frames (e.g. show_fire / show_earth showcases) and compare with docs/media stills.` },
  { key: 'animation', model: 'opus', effort: 'high',
    own: 'game/presentation/fighter_view.gd and NEW files under game/presentation/anim/ and game/tests/anim/',
    work: `ANIMATION RUNTIME & PHYSICAL FEEL (FighterView). Keep its public API (setup, set_accent, push_state, snap, render, play_one_shot, hand_position) and the
contact-frame alignment rules. Add, using Godot 4.7 SkeletonModifier3D family (verify in docs: TwoBoneIK3D, LookAtModifier3D, SpringBoneSimulator3D, etc.):
- Smooth locomotion blending (e.g. AnimationTree with synced blend spaces for idle/walk/run/strafe/back driven by actual sim velocity relative to facing),
  no pops between gaits, speed-matched playback (no skating), acceleration/deceleration lean and turn banking, landing compression.
- Foot IK: plant feet on the real ground (terrace edges 0.6 m, step block 0.35 m, pool floor -0.3 m, metal plate) using the analytic ArenaMap (reach it via
  the Game node: FighterView is a child of Game, '(get_parent() as Game).world.arena'), with pelvis lowering; only for planted feet; fade out while airborne/actions that lift feet.
- Head and upper-body look-at toward the lock target or an incoming threat (subtle, clamped), aim chest toward the attack direction during startup.
- Physical hit reactions: additive spring-damper offsets on spine/head/arms in the hit direction scaled by damage/balance, plus brief recoil for blocks;
  knockdown keeps the authored clip. Must never fight the sim (sim stays the movement authority).
- Secondary motion: SpringBoneSimulator3D for sash tails and hair chains IF those bones exist (the character engineer is adding sash_tail.L/R.001..003 and a
  hair chain concurrently — detect bones by name at runtime and enable when present; until then it must no-op cleanly).
- Mobile budget: up to 6 fighters on screen at 60 fps; disable expensive layers for distant/dummy fighters.
Add a headless test (game/tests/anim/) that instantiates fighters and steps them through locomotion/actions/hits without errors, and verify visually with renders
(walk/run transitions, terrace edge foot planting, hit reactions) — LOOK at frames and iterate.` },
  { key: 'environment', model: 'sonnet', effort: 'high',
    own: 'game/presentation/arena_view.gd, game/presentation/shaders/{arena_ground,ledge_stone,metal_plate,pool_water}.gdshader (+ new arena/env shaders you add), NEW tools/textures/*, NEW game/assets/textures/*, NEW game/presentation/props/*',
    work: `ENVIRONMENT REALISM. Make the courtyard arena look real and premium:
- Generate tileable PBR texture sets reproducibly (Blender Cycles bake of procedural materials, or numpy) at 1024²: stone flagstones with grout/wear/chips and
  subtle moss in cracks, cut-stone/brick walls, ledge cap stones, pool basin tiles, brushed metal plate with bolts/scratches, plus detail normal maps. Use them in
  the arena shaders with world-space/triplanar UVs so texel density is consistent on every box (no stretching), with roughness/normal/AO detail and distance
  blending to avoid tiling repetition. Water: better pool surface (depth tint, caustic shimmer on the basin, edge foam) within the Mobile renderer.
- Dressing: original props built reproducibly (Blender script → glb, or Godot meshes): stone lanterns, training posts/dummies stands, banners with original
  element glyphs, planters/shrubs, wall trims/caps, weathering decals. Place them ONLY along the boundary walls/corners and behind the terrace so they never
  block play (the gameplay arena is defined by game/core/arena_map.gd which you must not edit); purely visual.
- Lighting: warm late-day key + sky fill; bake LightmapGI if it works on this machine (try; fall back to baked AO in textures + ReflectionProbe if bake fails);
  keep it mobile-friendly and document the regeneration steps.
- Keep set_quality() tiers working (lower tiers drop expensive bits). Compare before/after renders at the same camera (e.g. flagship autoplay shots).` },
  { key: 'vfx', model: 'sonnet', effort: 'high',
    own: 'game/presentation/vfx/*, game/presentation/shaders/* EXCEPT arena_ground/ledge_stone/metal_plate/pool_water, game/presentation/body_views.gd, game/presentation/fx_director.gd, game/tests/vfx/*',
    work: `MATERIAL PHYSICS & VFX. Make material behaviour look physical and premium:
- Stones visibly tumble while flying/rolling (angular velocity from linear velocity and radius; settle when resting; keep the sim body as truth), a rolling dust
  trail, impact debris chips (pooled, short-lived) and ground cracks/scorch on heavy impacts, a stone-rip crater with dust when ripped from the ground.
- Lava: viscous bulging front, drips/glow pulses, crust plates that visibly slow and crack as it cools, steam when touching water.
- Water: thicker readable stream with droplets/spray, splash crowns on impact, ice with refraction-like facets and frost; steam/mist puffs that dissipate.
- Fire: layered flame tongues with flicker and heat shimmer, embers; lightning: brighter core + glow + afterimage; air: dust swept by gust cones.
- Keep budgets (draw calls, particles, transparent layers) documented in docs/VFX.md and respect the Mobile renderer; keep pool caps; prewarm new effects.
Verify with the element showcases (show_earth/water/fire/air) and the flagship autoplay: LOOK at frames before/after.` },
  { key: 'controls', model: 'sonnet', effort: 'medium',
    own: 'game/game.gd, game/combat/moves.gd, game/ui/settings_panel.gd, NEW game/ui/dev_panel.gd, NEW game/ui/controls_help.gd, game/tests/ui/*, docs/CONTROLS.md',
    work: `TEST CONTROLS. Give the owner great in-game tools to test and tune the game on desktop and iPhone:
- A Dev/Test panel (toggle: backquote key and F2 on desktop, a 'Dev' button in the pause menu on touch; pauses-safe): time scale (0.25–1.0), infinite Focus,
  god mode, AI on/off + aggression/counter/reaction sliders, AI drill selector, spawn at the player's aim point: stone 20/45/200 kg, lava pool, puddle, ice shard;
  reset scenario, clear bodies, toggle the debug overlay / practice timing bars, element+technique unlock toggles (lab mode), free-camera toggle,
  and LIVE TUNING sliders for key rules (magma grip window/reach, heat rate, draw rate, wave speed/budget, perfect-guard window, run/walk speeds...) with
  'save', 'reset to defaults' and 'export' (writes user://tuning.cfg and prints the values). For that, make Moves tunable at runtime with minimal change
  (e.g. static var DEFS initialised from a const BASE_DEFS + an overrides layer); timing constants you can't reach without editing files you don't own: skip and list.
  All sim tests must still pass (defaults unchanged).
- A Controls screen in the pause menu (keyboard, gamepad, touch layout diagrams), and an optional on-screen key-hint strip on desktop (toggle with H).
- Touch-friendly sizes; works at iPhone 2532×1170 and iPad 2732×2048 (render screenshots of the panel and Controls page and LOOK at them).
- Update docs/CONTROLS.md with the dev panel and tuning workflow. Add UI tests for the panel's core actions.` },
]

const REVIEW = { type: 'object', properties: {
  verdict: { type: 'string', enum: ['better', 'mixed', 'worse'] },
  issues: { type: 'array', items: { type: 'object', properties: {
    severity: { type: 'string', enum: ['high', 'medium', 'low'] }, title: { type: 'string' }, evidence: { type: 'string' }, fix: { type: 'string' } },
    required: ['severity', 'title', 'evidence'] } },
  tests: { type: 'string' }, screenshots: { type: 'string' } }, required: ['verdict', 'issues', 'tests'] }

const results = await pipeline(STREAMS,
  s => agent(`${COMMON}\n\nYOUR STREAM: ${s.key}. You own ONLY: ${s.own}.\n\n${s.work}`,
    { label: `build:${s.key}`, phase: 'Build', model: s.model, effort: s.effort }),
  (report, s) => agent(`${COMMON}\n\nYou are an independent reviewer for the "${s.key}" stream (read-only: do NOT edit repo files; scratch under ${SCR}/review_${s.key}/).
The engineer was asked:\n${s.work}\n\nTheir report:\n${report}\n\nCheck the claims: run the three test suites, render the relevant scenes, extract frames, LOOK at them and compare with the
baseline stills in docs/media. Judge whether it clearly looks/feels better, and list concrete issues (visual artefacts, regressions, broken behaviour,
performance risks for iPhone 12-class GPUs, files edited outside ownership) with evidence and minimal fixes.`,
    { label: `review:${s.key}`, phase: 'Review', model: 'sonnet', effort: 'medium', schema: REVIEW })
    .then(r => ({ stream: s.key, report, review: r })))

return results.filter(Boolean)