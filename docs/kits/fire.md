# Fire kit: Flame, Blue, Lightning, Combustion

_Owner: the Fire stream. Design: `docs/MOVESET.md` §3-§5 (incl. the §5.4 owner examples), §7.9-§7.12, §8.3, §9, §15,
§16. Engine: `docs/COMBAT_SPEC.md` "Engine". 40 moves (10 slots x 4 sub-elements; Flame keeps the legacy strike, thermal
technique and evade), ~330 counter cells, 24 custom outcomes (`fire_*`), 3 statuses, 0 core edits (conduction.gd is
owned by this kit since the core job)._

## 1. Files

| File | What |
|---|---|
| `game/combat/kits/fire/kit_fire.gd` | entry point (`Moves.ensure()` calls `register()`), module dispatch for `module: "kit_fire"` defs (`KitFire.handle(id, {start, after, phase, tick, interrupt})`), `move_ids(sub)` |
| `fire_rules.gd` | the Fire column of the counter matrix (`Interactions.add_rule`, `FireRules.CELLS` with reference expectations), custom outcomes, statuses, tag classes, channel hooks |
| `fire_util.gd` | booked heat transfer (`transfer`, `pay_into`, `melt_need`), fire fields (`spawn_field`, `field_tick`), detonations with their environment (`detonate`, `blast_inst`), `with_params`, `aim_ground` |
| `fire_flame.gd` | sub 0 Flame: Fire Column / Inferno data, SCORCH data, Fireball, Fire Line / Ring, Fire Fan / Nova, Flame Guard + extended Heat Sink, Backdraft, Ground Heat, Rocket Hop (Flare Dash registered) |
| `fire_blue.gd` | sub 1 Blue |
| `fire_lightning.gd` | sub 2 Lightning (charged bodies, Skybreak marks, Storm Grid) |
| `fire_combustion.gd` | sub 3 Combustion (fused air pockets, mines, embers) |
| `game/combat/act_fire.gd` | the legacy module (T0/T1 exact) + T2 Fire Column / T3 Inferno (`_column`), the SCORCH mode (`scorch_target`, VerbHeat), molten metal / glass "pours" |
| `game/core/conduction.gd` | the bolt graph + kit extensions: `start`, `force_target`, `meet_bodies`, `forks` def keys; `rail()`, `relay()`, `_conduct_from()` |
| `game/tests/sim/test_kit_fire_*.gd` | tests (§6); `test_lightning.gd`, `test_flagship.gd`, `test_waves.gd` unchanged and green |
| `game/actors/showcase_fire*.gd` | showcases (§7); `showcase_fire_base.gd` is the beat engine (`FireShowcase`) |

## 2. Legacy compatibility

* Flame (sub 0) keeps `fire_attack` (flare T0 / blaze T1, numbers untouched), `fire_tech` (HEAT / DRAW / VENT), `pour`,
  `vent`, and the legacy `evade`. The legacy `lightning` kit flag still turns a >= 0.65 s hold into the bolt at any hold
  length (test_lightning); without the flag the hold grows into Fire Column / Inferno.
* **Deviation:** Flame's T2 / T3 come at **1.4 s / 2.2 s** (`tier_times`), not 1.0 / 1.8 s: `test_core_charge` pins a
  1.33 s Fire hold as the legacy blaze with no drain. T1 never drains (`charge_drain` 0 on t1).
* **Deviation:** the sub-0 evade stays the legacy evade (MOVESET §3; `test_combat_rules` pins its i-frame window), so
  **Flare Dash** is registered but not bound; the legacy evade held 0.2 s morphs into **Rocket Hop**.
* The sub-0 guard is bound to the spec `flame_guard` (counter class `aura_flame`, CP 10): every legacy `aura_flame` cell
  (Heat Sink 50 %, Return Current 80 %, plain block) answers exactly as before; the extension (draw 300 HU from a magma
  blob / hot rock, melt ice) runs in the guard tick inside the perfect window, just before contact, because legacy
  cells can never be replaced. `Moves.resolve(2, 0, "guard")` is now `flame_guard` (the running action is still "guard").
* SCORCH is a new thermal mode chosen after HEAT / DRAW / water and before VENT, so legacy scenes (no walls in reach)
  pick exactly what they picked before.
* Conduction: the new def keys are opt-in; legacy bolts (the `lightning` action) meet no path bodies and never fork.

## 3. Moves

Numbers are design targets (frames at 60 Hz, F = Focus, HU = heat, paid from the reserve first, then Focus at 10 HU/F).
Values per tier are in `Moves.DEFS[id].tiers` (a ladder: t3 inherits t2/t1) and can be live-tuned (`Moves.set_override`).
Every def carries `name, desc, element, sub, slot, module, startup/active/recovery, costs, tiers, counter, threat, anim
(existing clips only), fx {mat, shape}, ai {role, range, tags}` and is bound to its slot.

### Flame (sub 0)

| Slot | Move (`id`) | Cost | S/A/R f | Tiers | Counter | Notes |
|---|---|---|---|---|---|---|
| strike | **Flare / Blaze / Fire Column / Inferno** (`fire_attack`) | 60 / 160 / 300 / 480 HU | 6/5/13 | t1 0.4 s, t2 1.4 s, t3 2.2 s | flame [3, 8, 15, 24] | T2 7 m jet + 2 s field (35 % of the heat), T3 9 m wide + 4 s field |
| thrust | **Fireball / Twin / Great / Sunfall** (`fireball`) | 120 HU (+60/120/260) | 12/4/18 | t1-t3 | fireball [6, 9, 12, 20] | a FIRE body (heat_payload); bursts on impact; T3 lobbed, leaves a 4 m field |
| ground | **Fire Line / Double / Triple / Fire Ring** (`fire_line`) | 150 HU (+90/180/330) | 16/6/20 | t1-t3 | fire_field [8, 12, 16, 24] | 12 m/s, 10 m, burning trail; puddles / the pool douse it; T3 a ring of 6 fields around the rival |
| sweep | **Fire Fan / Wide Fan / Wheel / Nova** (`fire_fan`) | 90 HU (+60/140/260) | 10/6/16 | t1-t3 | flame [4, 6, 9, 14] | 120 deg -> 360 deg |
| guard | **Flame Guard / Heat Sink** (`flame_guard`) | - | - | - | aura_flame 10 | perfect: legacy Heat Sink; magma / hot rock: -300 HU into the reserve; ice melts |
| push | **Backdraft** (`backdraft`) | the reserve | 10/6/18 | - | flame (H = reserve / 20, <= 25) | |
| sink | **Ground Heat** (`ground_heat`) | 4 F | 8/10/14 | - | - | vent + boil puddles within 2 m (lightning-safe footing) |
| tech | **Thermal + SCORCH** (`fire_tech`) | legacy; SCORCH 300 HU/s | legacy | - | heat_grip | SCORCH: a wall's 25 % face slumps in ~1.5 s; metal / sand bodies the grip can't take are heated |
| evade | legacy evade | 4 F | | | | (Flare Dash `flare_dash`: 5 F + 20 HU, 4 m, burning trail - registered, unbound) |
| evade_hold | **Rocket Hop** (`rocket_hop`) | 6 F/s | 0/0/10 | - | - | hover 2.5 m for 0.85 s |

### Blue (sub 1)

| Slot | Move (`id`) | Cost | S/A/R f | Tiers | Counter | Notes |
|---|---|---|---|---|---|---|
| strike | **Blue Needle / Lance / Searing Beam / White Core** (`blue_needle`) | 60 / 120 / 220+200 / 360+200 HU | 8/6/14 | t1-t3 | blue_fire [6, 12, 22, 33] | beam 5 / 8 / 10 / 12 m; T2-T3 sustained 0.6 / 1.0 s on one paid budget; T2 melts a flying stone, T3 melts a wall face (50 HU / pulse, slump ~0.9 s) |
| thrust | **Comet Flame** (`comet_flame`) | 100 HU (+50/140/260) | 10/4/16 | t1-t3 | fireball [8, 12, 16, 24] | @26-34 m/s, T3 pierces |
| ground | **Blue Furrow / Magma Rift** (`blue_furrow`) | 200 HU (+60/140/320) | 16/8/20 | t1-t3 | magma_rift [10, 14, 20, 30] | 8-24 kg of ground (ground_taken) melted with the paid heat: a lava wave (`lava_wave`) Earth can push; fuses a sand surge to glass from T2 |
| sweep | **Corona** (`corona`) | 120 HU (+60/130/240) | 10/16/14 | t1-t3 | corona [6, 8, 11, 16] | attached ring r 2.5-3.5 m; melts ice / metal <= 8 kg from its own heat |
| guard | **Blue Aegis** (`blue_aegis`) | 12 F/s | - | - | aura_blue 16 | ice / metal <= 8 kg melted (heat paid), flames absorbed (x1.5 perfect), stones blocked |
| push | **Flash Over** (`flash_over`) | 6 F + 80 HU | 8/4/16 | - | blue_fire 10 | burst r 3 m, knock 6 |
| sink | **Kiln** (`kiln`) | 6 F + 150 HU | 8/20/14 | - | heat_ranged | a body within 2.5 m to >= 700 C (sand -> glass); whoever seizes it is burned and drops it |
| tech | **Smelter** (`smelter`) | 6 F + 450 HU/s | 12/-/18 | - | heat_ranged | walls slump in ~1 s, metal melts, sand fuses, ice boils |
| evade | **Shimmer Step** (`shimmer_step`) | 5 F | 0/7/8 | - | - | 3 m in 0.12 s |
| evade_hold | **Afterburn** (`afterburn`) | 8 F/s (+8 HU/step) | - | - | - | run x1.3, blue footprints |

### Lightning (sub 2)

| Slot | Move (`id`) | Cost | S/A/R f | Tiers (0.65 / 1.2 / 1.8 s) | Counter | Notes |
|---|---|---|---|---|---|---|
| strike | **Spark / Bolt / Storm Bolt / Skybreak** (`spark`) | 6 / 22 / 30 / 40 F | 6/4/14 | t1-t3 | lightning [10, 24, 36, 52] | Spark 5 m, chains 2 m to wet fighters / metal; Bolt = legacy stats; Storm Bolt blasts through barriers (E - 0.5 CP) and forks to 2 conductors; Skybreak: 0.4 s mark, strike from 12 m above, deafens 0.3 s within 4 m |
| thrust | **Rail Arc** (`rail_arc`) | 10 F (+4/8/12) | 8/4/18 | t1-t3 | lightning [14, 20, 28, 36] | follows the conductors it crosses (a held jet -> its holder) |
| ground | **Ground Current / Storm Grid** (`ground_current`) | 12 F (+4/8/14) | 12/6/20 | t1-t3 | ground_current [12, 18, 24, 32] | conductive ground only, 2 m grace; frozen puddles / ice floors stop it; Grounding immune; T3 electrifies the connected network |
| sweep | **Arc Fan** (`arc_fan`) | 12 F | 10/4/18 | t1-t3 | lightning [8, 10, 12, 14] | 3-5 forks |
| guard | **Static Ward / Return Current** (`static_ward`) | - | - | - | ward_static 12 | 50 % stored (<= 60) / taken; metal deflected x1.5; perfect: 80 % return with redirect_current, else full absorb |
| push | **Static Burst** (`static_burst`) | 4 F | 8/4/16 | - | lightning (E = stored) | 4 m cone, shock 0.3 s |
| sink | **Grounding** (`grounding`) | 4 F | 4/90/10 | - | - | 1.5 s immune to conduction; drains touched conductors |
| tech | **Conductor's Hand / Arc Link** (`conductors_hand`) | 8 F | 10/-/16 | t1-t3 (hold) | lightning [12, 18, 26, 36] | charge a conductor within 10 m; release: caster -> body -> nearest rival within 8 m of it |
| evade | **Arc Step** (`arc_step`) | 6 F | 0/6/8 | - | - | 6 m, i 9 f, the trail shocks (E 6) |
| evade_hold | **Overcharge** (`overcharge`) | 12 F/s | - | - | - | +30 % speed, recovery x0.8; water on you shorts you |

### Combustion (sub 3)

| Slot | Move (`id`) | Cost | S/A/R f | Tiers | Counter | Notes |
|---|---|---|---|---|---|---|
| strike | **Pop / Burst / Blast / Detonation** (`pop`) | 40 / 100 / 200 / 340 HU | 8/4/14 | t1-t3 | blast [8, 16, 22, 34] | palm r 2 -> 6 m r 2 (fuse 0.25 s) -> 9 m r 3 -> 12 m r 4.5 (fuse 0.5 s) |
| thrust | **Spark Mine** (`spark_mine`) | 60 HU | 10/4/16 | t1-t3 | blast [12, 16, 22, 30] | sticks; proximity 1.5 m / contact / your next flick up |
| ground | **Chain Blasts** (`chain_blasts`) | 150 HU | 14/30/20 | t1-t3 | blast 10 | 3 -> 6 steps every 0.15 s |
| sweep | **Scatter Charges** (`scatter_charges`) | 120 HU | 10/4/18 | t1-t3 | blast [8-12] | 4 -> 8 embers, pop after 0.4 s |
| guard | **Reactive Blast** (`reactive_blast`) | 8 F per trigger | - | - | guard_blast 18 | deflects light solids (perfect: back), snuffs flames x1.5, disperses clouds |
| push | **Shockwave** (`shockwave`) | 80 HU | 8/4/16 | - | blast 14 | 5 m cone |
| sink | **Smother Blast** (`smother_blast`) | 80 HU | 6/4/14 | - | blast 10 | snuffs fields within 4 m, launches 1.5 m |
| tech | **Fuse** (`fuse`) | 6 F + 60-320 HU | 12/4/16 | t1-t3 (hold) | blast [14, 20, 28, 38] | steer an air pocket (10 m), release detonates |
| evade | **Blast Jump** (`blast_jump`) | 5 F + 30 HU | 0/16/8 | - | - | 4 m along the stick, straight up if neutral |
| evade_hold | **Afterglow** (`afterglow`) | 6 F/s | - | - | - | hover 0.8 s |

## 4. Counter cells (MOVESET §8.3)

`FireRules.CELLS` lists every cell with its reference (move, tier, expected outcome); `test_kit_fire_cells` checks them
all (206 references), the partial / fail bands, the coverage of every §8.3 row in every column and that no legacy cell is
replaced. Counter classes: Flame `aura_flame` (non-legacy rows), `flame`, `fire_field`, `fireball`, `heat_grip` (metal,
sand), `draw_heat` (molten metal), `heat_ranged`; Blue `aura_blue`, `blue_fire`, `corona`, `magma_rift`; Lightning
`ward_static`, `lightning`, `ground_current`, `static_field`; Combustion `guard_blast`, `blast`, `ember`. Headlines:

* **Wind vs fire** (`gust x fire_field`, ratio = fire / wind): >= 1 fanned (+30 % heat, +1 m, booked generated),
  0.5-1 blown aside, < 0.5 snuffed. A tornado that runs into a fire becomes a fire tornado (the field rides it).
* **Lightning vs barriers** (core cells): grounded at E <= CP, shattered below and the bolt continues with E - 0.5 CP.
  Path bodies (`meet_bodies`): stones shatter when out-powered, sand fuses (fulgurite, -50 %), metal / water charge.
* **Blasts** shatter ice and glass (x2), snuff flames (x1.5, oxygen) and fire fields, disperse clouds; out-powered
  tornadoes are disrupted; inside a vacuum they are suppressed (a well they out-push is filled and collapses).
* **Heat flows are booked**: a fire field / fireball pays out of its own `heat_payload`, a volume out of its paid
  budget (`FireUtil.transfer`); legacy volume cells are never used with fire *bodies* as counters (tag classes
  `fireball`, `fire_field`, `corona` route them to the kit's cells), so nothing creates heat.
* Inert bodies are pushed, not shattered (cells carry `inert: push / pass`): no cascades inside one blast.

## 5. Engine notes

* Tags: `fire_field` (fields, trails), `fire_line` (WAVE), `fireball`, `comet`, `ember` (FIRE bodies), `corona`, `kiln`,
  `static_field` (charge marks, Skybreak marks, grid patches), `fuse` (Combustion pockets; the core fuse timer is parked
  with `props.fuse = 1e9`, the kit's zone effect detonates them), `magma_rift`, `ground_current` (AIR WAVE carrying E).
* Statuses: `grounding` (immune conduct), `overcharged` (x1.3 speed, x0.8 recovery), `kiln_burn` (6 dps).
* Tech previews (HUD / AI): Flame legacy + SCORCH, Blue SMELT, Lightning ARC LINK, Combustion FUSE.
* Fire fields: ZONE mat FIRE holding paid heat; burn fighters (`burning`), warm / melt / boil / burn bodies inside via
  the `fire_field` cells, decay 35 %/s (ledger ambient).

### Needed outside this stream

* `test_core_registry.test_legacy_bindings_and_fallbacks` / `test_core_input` (core): they assume no sub-0 gesture slots,
  no sub-1-3 strikes and `resolve(2, 0, "guard") == "guard"`; every kit binds these per MOVESET §3 / §15.2.
* Air: a `flame x grip_wind` cell (reclaim) so the Wind Grip can hold fireballs; the kit feeds a fireball +20 % when a
  `grip_wind` grip releases it (tested with a test-local def).
* UI: drop the legacy `lightning` flag from Lab / Spar kits (`Progression.kit()`), so Flame's long holds reach Fire
  Column / Inferno (MOVESET §7.9 compat note).
* VFX: views for FIRE bodies / zones (`fireball`, `comet`, `ember`, `fire_field`, `fire_line`, `corona`) and AIR waves
  (`ground_current`); the fx cues (`beam` blue / lightning, `burst` blast, `cast` blast fuses, `ring`) are emitted.

## 6. Tests

`test_kit_fire_moves` (every slot of every sub-element at T0-T3 through real input: starts, pays, fx catalogued, ends,
ledgers exact; def completeness, clips exist) · `test_kit_fire_cells` · `test_kit_fire_flame` (Column / Inferno, the
lightning flag, fireball burst / water quench / wind feed +20 %, fire line + puddle, Heat Sink 300 HU + ice, Backdraft,
Ground Heat, **Scorch slumps a Bulwark in ~1.5 s**, Rocket Hop) · `test_kit_fire_blue` (**Searing Beam T2 melts a flying
20 kg stone**, **White Core T3 slumps a wall in ~1 s**, **Smelter ~1 s** with the molten face and crumble, Blue Furrow,
Aegis vs metal, Corona vs ice, Kiln trap) · `test_kit_fire_lightning` (**Storm Bolt through a Bulwark arrives with E 21,
Bolt T1 grounded**, Skybreak over a wall + deafen, Static Ward 50 % + Static Burst, perfect absorb / Return Current,
**relay around the cover wall**, Rail Arc through a held jet, Ground Current on wet / dry / frozen ground and vs
Grounding, Arc Fan) · `test_kit_fire_combustion` (tiers, **combustion snuffs a fire field**, ice shatter, **a test-local
null zone suppresses a detonation**, vapour halves, vacuum inrush x1.5, fire tornado, Spark Mine proximity / remote,
Reactive Blast deflect / reflect, Smother Blast) · `test_kit_fire_ledger` (**energy and mass ledgers exact every 6
ticks over 60 s vs each element; determinism**).

## 7. Showcases

`--autoplay=show_fire` (legacy beats + Fire Column, Fireball, Fire Line, Fire Fan, SCORCH a wall), `show_fire_blue`,
`show_fire_lightning`, `show_fire_combustion` (real InputFrames; `SHOWCASE_TRACE=1` traces). Render:
`tools/scripts/godot.sh --render --resolution 1280x592 --write-movie out.avi --fixed-fps 30 -- --autoplay=show_fire_blue:20 --quality=2`.
