# Water kit: Water, Ice, Mist, Plant

_Owner: the Water stream. Design: `docs/MOVESET.md` §3-§5, §7.5-§7.8, §8.2, §9, §15, §16. Engine: `docs/COMBAT_SPEC.md`
"Engine". 39 moves (10 per sub-element; Water keeps the legacy strike / shield / draw), 197+ counter cells, 15 custom
outcomes, 8 statuses, 0 core edits._

## 1. Files

| File | What |
|---|---|
| `game/combat/kits/water/kit_water.gd` | entry point (`Moves.ensure()` calls `register()`), the module dispatch for defs with `module: "kit_water"` (`KitWater.handle(id, {start, after, phase, tick, interrupt})`) |
| `water_water.gd` | sub 0 Water: the legacy kit extended (Torrent, Maelstrom Lash, Draw extras), Water Bullet / Pressure Jet / Cutting Jet, Tidal Rush, Spray Fan, Surge Orb, Slick, Riptide Step / Dive, Wave Ride |
| `water_jet.gd` | the Pressure / Cutting Jet: a liquid body held by its caster, so lightning on it conducts back |
| `water_ice.gd` | sub 1 Ice |
| `water_mist.gd` | sub 2 Mist |
| `water_plant.gd` | sub 3 Plant |
| `water_rules.gd` | the Water column of the counter matrix (`Interactions.add_rule`), custom outcomes (`water_*`), statuses, tag classes, zone / body hooks |
| `water_util.gd` | water sources (`take`, `give_back`, `make_puddle`), booked phase changes (`freeze_body`, `freeze_any`, `make_steam`), moisture, evade helper, zones |
| `game/combat/act_water.gd` | the legacy module (T0/T1 exact) plus Torrent / Maelstrom, condense, seize, T+A Freeze |
| `game/tests/sim/test_kit_water_*.gd` | tests (§8); `test_water_ice.gd` (legacy water / ice, unchanged) |
| `game/actors/showcase_water*.gd` | showcases (§9); `showcase_water_base.gd` is the beat engine |

Everything is data on the generic verbs where possible (`module: "verbs"`); `hook_execute` callables hold the move-specific
parts; `module: "kit_water"` moves override a lifecycle stage (guard start for the Ice Wall, mode zones for Skate / Fog
Walk, ...). No core, verb or other kit file was edited.

## 2. Legacy compatibility

* Water sub 0 keeps `water_attack` (lash T0, ice lance T1), `water_tech` (draw, shape, release) and the legacy shield
  (`guard`, counter class `shield_water`). T0 / T1 behaviour and numbers are untouched (`test_water_ice.gd`, the legacy
  regressions and the 188 legacy tests pass). T2 / T3 of the strike and the new technique contexts are additions.
* The legacy lash cell (`* x water_jet`, tiers 0-1) and every legacy shield cell (`water / ice / flame / gust / lightning /
  stones x shield_water`) are never replaced; the kit adds the cells of the *non-legacy* threats and tiers 2-3 of the jet.
* `ActWater._water_filter` (what Draw may take from the world) is now restricted to PUDDLES: the kit's new
  `water x grip_water` cell (seizing a rival's stream in flight) would otherwise have let a held blob draw from itself.

## 3. Moves

Numbers are design targets (frames at 60 Hz, Focus, HU of heat, kg of water). Tier columns list which `tiers` blocks a move
has; the values are in the defs (`Moves.DEFS[id].tiers`) and can be live-tuned with `Moves.set_override`. Every move def
carries `name, desc, element, sub, slot, module, startup/active/recovery, cost (+ heat / water), tiers t0-t3, counter, threat,
anim (existing clips only), fx {mat, shape}, ai {role, range, tags}` and is bound to its slot.

### 3.1 Water (sub 0)

| Slot | Move | Cost | S/A/R f | Tiers | Counter | Threat | Anim | FX |
|---|---|---|---|---|---|---|---|---|
| strike | **Lash / Ice Lance / Torrent / Maelstrom** (`water_attack`) | 5.0 | 10/7/17 | t1,t2,t3 | water_jet [8.0, 5.7, 16.0, 18.0] | water | water_whip | water  |
| thrust | **Water Bullet** (`water_bullet`) | 4.0 | 10/4/16 | t1,t2,t3 | water_jet [2.3, 6.9, 10.0, 16.0] | water | water_whip | water needles |
| ground | **Tidal Rush** (`tidal_rush`) | 8.0 | 16/6/22 | t1,t2,t3 | wave_water [18.0, 24.0, 32.0, 45.0] | water_wave | water_whip | water ground |
| sweep | **Spray Fan** (`spray_fan`) | 4.0 | 10/6/16 | t1,t2,t3 | spray [4.0, 6.0, 9.0, 12.0] | water | water_whip | water fan |
| guard | **Guard** (`guard`) | 0 | 0/0/6 |  | -  | - | guard |   |
| push | **Surge Orb** (`surge_orb`) | 4.0 | 8/4/16 |  | water_jet 5.0 | water | water_whip | water small |
| sink | **Slick** (`slick`) | 3.0 | 6/4/14 |  | -  | puddle | water_draw | water down |
| tech | **Draw & Shape** (`water_tech`) | 6.0 | 9/5/18 |  | grip_water  | - | water_draw | water  |
| evade | **Riptide Step** (`riptide_step`) | 4.0 | 0/16/8 |  | -  | puddle | evade | water  |
| evade_hold | **Wave Ride** (`wave_ride`) | 0.0 | 0/0/8 |  | -  | water_wave | glide | water  |

* **Torrent (strike T2, 1.0 s hold)**: a 10 kg water slug at 20 m/s (waterskin, then the pool / a puddle in reach), up to
  16 damage and 34 balance. **Maelstrom Lash (T3, 1.8 s)**: a 360 degree whip, radius 5 m, P 18; everyone around is hit and
  thrown outward, loose bodies meet it as a `water_jet` counter, 2 kg of spray lands around you as puddles.
* **Water Bullet**: 1.5 kg slug at 30 m/s; T1 three slugs; **T2 Pressure Jet** (0.8 s, 10 m, P 10) and **T3 Cutting Jet**
  (1.0 s, thin, P 16, cuts sand / mud / vine walls) fire a jet that is a liquid water body *held by the caster*
  (`inst.data.jet`). The conduction graph treats a held conductive body as part of the holder, so a bolt that strikes the jet
  hurts its caster. The jet's water is booked (waterskin / pool) and falls as a puddle when it ends.
* **Tidal Rush**: a `water_wave` ground line (9 m/s, 2 m wide, 10 m budget; T1 12 m, T2 3.2 m wide, T3 the 22 kg Deluge from
  the pool). T0 takes 6 kg (a full waterskin makes a full T0 wave, CP 18); T1-T3 take 10 / 14 / 22 kg. Counter power = the
  tier power **scaled by the water actually in it** (away from water a T3 is 6 / 22 of 45, floor 35 %). `Agent.of_move`
  applies the same scale (`counter_scale` hook, `WaterWater.tidal_scale`), so the AI planner, the Lab matrix and predictions
  match the wave the sim makes. It carries solids (`water_carry`: captured, then thrown at the first rival it reaches as the caster's
  attack: "make a wave back"), quenches lava (`water_quench`: heat moves from the lava into the wave through `heat_body`; a
  small wave cannot quench a big lava wave), douses fire fields and drowns tornado zones; it leaves puddles.
* **Draw & Shape (tech)**: besides the pool and puddles it now **condenses vapour** (steam clouds, mist, fog zones: the mass
  moves into the held water), **seizes a rival's stream or wave in flight** (a grip contest against the thrower's cohesion
  0.6 + 0.1 x tier) and, with an attack tap while holding (**T+A Freeze**), freezes the held water into an ice block.
* **Slick (sink)**: pours the shield (or 2 kg) as a 2.5 m puddle with a `slick` zone; runners lose 20 balance and get wet.
  **Surge Orb (push)** throws the shield as an orb that bursts (wet, knock 5). **Riptide Step** slides 4 m on a water
  film (i-frames 9 f, a slick trail); in the pool it is **Dive** (submerge, 18 f i-frames, resurface 4 m away). **Wave Ride**
  (hold evade) surfs at about 8 m/s spending 2 kg of water per second (falls behind as puddles).

### 3.2 Ice (sub 1)

| Slot | Move | Cost | S/A/R f | Tiers | Counter | Threat | Anim | FX |
|---|---|---|---|---|---|---|---|---|
| strike | **Frost Shard** (`frost_shard`) | 4.0 | 10/4/16 | t1,t2,t3 | frost [1.3, 5.7, 12.0, 15.0] | ice | water_freeze | ice needles |
| thrust | **Icicle Volley** (`icicle_volley`) | 5.0 | 10/6/18 | t1,t2,t3 | frost [1.3, 1.3, 1.3, 1.3] | ice | fire_jab | ice needles |
| ground | **Rime Path** (`rime_path`) | 7.0 | 14/6/20 | t1,t2,t3 | rime [10.0, 14.0, 20.0, 30.0] | frost | water_freeze | ice ground |
| sweep | **Hoarfrost Fan** (`hoarfrost_fan`) | 5.0 | 10/8/18 | t1,t2,t3 | frost [6.0, 9.0, 13.0, 18.0] | frost | water_freeze | ice fan |
| guard | **Ice Wall** (`ice_wall`) | 8.0 | 0/0/0 | t1,t2,t3 | wall_ice  | wall_ice | water_freeze | ice plate |
| push | **Glacier Shove** (`glacier_shove`) | 4.0 | 10/18/18 |  | wall_ice 22.0 | ice | earth_heavy | ice plate |
| sink | **Frost Floor** (`frost_floor`) | 5.0 | 6/4/14 |  | -  | frost | water_freeze | ice down |
| tech | **Freeze-Draw** (`freeze_draw`) | 6.0 | 12/4/18 |  | grip_ice  | ice | water_draw | ice  |
| evade | **Ice Glide** (`ice_glide`) | 4.0 | 0/14/6 |  | -  | frost | evade | ice  |
| evade_hold | **Skate** (`skate`) | 0.0 | 0/0/6 |  | -  | frost | run | ice  |

* Ice's water is **ambient moisture** (`mass_ledger.moisture_taken`, subtracted by `water_mass()`): free of a waterskin, still
  booked. Every freeze goes through `WaterUtil.freeze_body` (heat dumped in `ledger.freeze_dump`).
* **Frost Shard ladder**: 1 kg needle (chill 1.5 s) -> 4 kg Ice Lance -> 8 kg Glacier Spear (pierces one body, then stands as
  an ice spike for 3 s) -> 15 kg Frost Comet (lobbed, shatters into 6 shards). **Icicle Volley**: 3 / 5 / 7 / 12 needles.
* **Rime Path** (`rime` wave, frozen 3-8 kg): leaves `ice_floor` zones (friction 0.12; your own never slides you: status
  `icegrip`), freezes puddles (no conduction), turns an enemy **water wave into an ice ridge** (a WALL, tag `ridge`, 6 s), crusts a
  lava front (`water_quench` to obsidian) and over the pool leaves zones whose surface is at pool level: a **walkable strip**.
  T2 adds spikes (dps, slow); T3 stands as a ridge where it ends.
* **Hoarfrost Fan**: chill, **wet targets freeze solid** (`frozen` 0.8-1.5 s, rooted), puddles and streams in the fan freeze,
  hot rock cools, vines turn brittle.
* **Ice Wall**: 50 kg (+20 beside water) at hardness 0.44 (CP 22; held 1.0 s 65 kg, 1.8 s 80 kg). It is an **insulator**
  (core cell: E up to 1.5 x CP is grounded, a T2 storm bolt shatters it), freezes water that hits it, melts under fire and slumps
  into a puddle. **Flash Freeze (perfect)**: water / steam / mist threats freeze solid and drop, a water wave becomes a ridge,
  light stones are frost-locked. **Glacier Shove** slides the wall 6 m, **Frost Floor** a 3 m slick floor, **Freeze-Draw** draws a
  growing ice block (<= 12 kg) or seizes incoming ice / water (T+A **Shatter**: six shards), **Ice Glide** a 5 m slide on ice,
  **Skate** (hold evade) 7 m/s on ice that follows you, across the pool too.

### 3.3 Mist (sub 2)

| Slot | Move | Cost | S/A/R f | Tiers | Counter | Threat | Anim | FX |
|---|---|---|---|---|---|---|---|---|
| strike | **Scald Puff** (`scald_puff`) | 4.0 + 40.0 HU | 8/6/14 | t1,t2,t3 | steam [3.0, 6.0, 12.0, 20.0] | steam | fire_jab | steam  |
| thrust | **Fog Lance** (`fog_lance`) | 4.0 | 10/4/16 | t1,t2,t3 | frost [4.0, 6.0, 8.0, 10.0] | frost | water_whip | mist  |
| ground | **Creeping Fog** (`creeping_fog`) | 6.0 | 14/6/20 | t1,t2,t3 | fog [6.0, 8.0, 10.0, 12.0] | mist | water_draw | mist ground |
| sweep | **Veil** (`veil`) | 5.0 | 10/8/16 | t1,t2,t3 | fog [6.0, 7.0, 8.0, 9.0] | mist | water_hold | mist  |
| guard | **Steam Screen** (`steam_screen`) | 6.0 + 60.0 HU | 0/0/0 | t1,t2,t3 | screen_steam [10.0, 12.0, 15.0, 18.0] | steam | water_shield | steam open |
| push | **Steam Blast** (`steam_blast`) | 4.0 + 60.0 HU | 8/6/16 |  | steam 9.0 | steam | fire_release | steam  |
| sink | **Dew Fall** (`dew_fall`) | 3.0 | 6/6/12 |  | -  | water | water_draw | mist down |
| tech | **Vapor Draw** (`vapor_draw`) | 5.0 | 10/4/16 |  | grip_vapor  | steam | water_draw | mist  |
| evade | **Mist Step** (`mist_step`) | 5.0 | 0/15/8 |  | -  | mist | evade | mist  |
| evade_hold | **Fog Walk** (`fog_walk`) | 0.0 | 0/0/8 |  | -  | mist | walk | mist  |

* **Steam = water + heat**: the heat is paid like Fire (`pay_heat`, booked `generated`), the water leaves the waterskin / pool and
  `CombatWorld.heat_body` vaporises it (`ledger.vapor`, a real steam cloud body). **Scald Puff** boils 0.5 kg with 40 HU, **Steam
  Jet** 0.8 kg; **Geyser (T2)** buries a steam pocket (booked water + heat, tag `geyser`) that erupts after 0.5 s (launch 8);
  **Boiling Pillars (T3)** three in a line. **Fog Lance**: wet + chill + blind 0.8 s, 12 m.
* **Creeping Fog**: a `fog` ZONE (mat STEAM, 2-4 kg booked from the waterskin / pool) that rolls 5 m/s and settles (r 4-6 m, 6-8 s).
  Inside: concealed (`fogbound`: lock-on breaks beyond 2 m, AI perception +0.25 s), fire and blast *bodies* are dampened,
  sound is absorbed, sand turns to mud, and **lightning cast into the fog hits everyone inside at 60 %** (core conduction).
* **Veil** (attached mist), **Steam Screen** (an attached `steam_screen` zone: slows solids x0.85, dampens flames x1.5, melts ice
  shards, scalds walkers, CP 10-18; perfect = **Condense**: water / steam / mist into your waterskin, flames snuffed), **Steam
  Blast** (closes the screen, 5 m cone), **Dew Fall** (all vapour within 6 m falls as puddles), **Vapor Draw** (draw fog / steam
  into a held ball from loose clouds *and* fog zones; release = steam bomb, T+A condense to water), **Mist Step**, **Fog Walk**.

### 3.4 Plant (sub 3, P2)

| Slot | Move | Cost | S/A/R f | Tiers | Counter | Threat | Anim | FX |
|---|---|---|---|---|---|---|---|---|
| strike | **Bramble Lash** (`bramble_lash`) | 5.0 | 10/8/18 | t1,t2,t3 | vine [6.0, 10.0, 16.0, 24.0] | vine | water_whip | plant  |
| thrust | **Burr Shot** (`burr_shot`) | 4.0 | 10/4/16 | t1,t2,t3 | vine [3.0, 3.0, 3.0, 3.0] | vine | fire_jab | plant seed |
| ground | **Root Snare** (`root_snare`) | 7.0 | 18/6/20 | t1,t2,t3 | wave_vine [12.0, 16.0, 22.0, 30.0] | vine | earth_wall | plant ground |
| sweep | **Thicket Fan** (`thicket_fan`) | 6.0 | 12/8/18 | t1,t2,t3 | briar [8.0, 10.0, 13.0, 17.0] | vine | water_whip | plant fan |
| guard | **Living Lattice** (`living_lattice`) | 7.0 | 0/0/0 | t1,t2,t3 | wall_vine  | wall_vine | water_shield | plant plate |
| push | **Lattice Roll** (`lattice_roll`) | 4.0 | 10/18/16 |  | wall_vine 10.0 | vine | earth_heavy | plant plate |
| sink | **Deep Roots** (`deep_roots`) | 0.0 | 6/0/10 |  | anchor 35.0 | vine | guard | plant  |
| tech | **Vinegrip** (`vinegrip`) | 5.0 | 8/4/18 |  | grip_vine  | vine | earth_hold | plant  |
| evade | **Vine Swing** (`vine_swing`) | 5.0 | 0/18/8 |  | -  | vine | evade | plant  |
| evade_hold | **Canopy** (`canopy`) | 0.0 | 0/0/10 |  | -  | vine | glide | plant  |

* Vines grow from water: **1 kg water -> 1 kg vine**, booked `water_to_plant` (ambient moisture also books `moisture_taken`,
  so `water_mass()` and `plant_mass()` both stay exact). They **burn x3** (`water_burn`: `CombatWorld.burn_plant`, ledger
  `burned`), are cut by edges (metal / crescents are partial against the lattice), and turn **brittle when frozen**
  (`water_brittle`: hardness 0.08).
* **Bramble Lash** yanks light targets about 2 m in (T2 grab and slam, T3 **Briar Storm** 360 degrees; the whip's vine withers
  after 2.5 s). **Burr Shot** seeds sprout `snare` zones (root 1-1.5 s). **Root Snare** (`roots` wave, underground at 10 m/s with a
  cracking telegraph, flying fighters pass over) roots 1.2-1.8 s; T3 leaves a Strangler Grove (r 3 m). **Thicket Fan** a 4-8 s
  briar zone (slow 40 %, dps, catches small projectiles).
* **Living Lattice**: 40 kg vine wall (CP 16; 50 / 60 kg held) that **captures solids (x1.3)**, drinks water (the wall grows,
  booked), burns easily; perfect = **Catch & Sling** (the projectile is thrown back at its sender). **Lattice Roll** rolls it 7 m
  and entangles; **Deep Roots** anchors you (anchor CP 35: the core `pressure x anchor` cell) and drinks the puddles under you.
* **Vinegrip**: seizes bodies <= 40 kg at 9 m (half the usual range falloff), T+A **Wrap** (the body roots whoever it hits),
  on a fighter **Hook** (yank 3 m, -20 balance). **Vine Swing** swings to the nearest arena anchor within 9 m (pillar, ledge, cover
  wall) else sidesteps; **Canopy** hovers 1.5 s.

## 4. Counter cells (MOVESET §8.2, Water column)

`WaterRules.CELLS` lists every registered cell (`threat|counter` -> rule id, reference move / tier / expected outcome). The cell
tests (`test_kit_water_cells.gd`) check every reference cell at the reference threat power, the three bands of every rule, that
every outcome name exists, and that every row of §8.2 has an answer in every sub-element column.

| Sub-element | Counter classes | Provided by |
|---|---|---|
| Water | `shield_water` (non-legacy threats), `wave_water`, `water_jet` (tiers 2-3), `spray`, `grip_water` | legacy shield, Tidal Rush, Pressure / Cutting Jet + Maelstrom, Spray Fan, Draw & Shape |
| Ice | `wall_ice`, `rime`, `frost`, `freeze`, `grip_ice` | Ice Wall (+ Flash Freeze), Rime Path / Frost Floor zones, Hoarfrost Fan / Fog Lance, Freeze-Draw |
| Mist | `screen_steam`, `fog`, `condense`, `grip_vapor` | Steam Screen, Creeping Fog / Veil / mist zones, Condense (perfect), Vapor Draw |
| Plant | `wall_vine`, `grip_vine`, `briar`, `wave_vine`, (`anchor`: core cell) | Living Lattice, Vinegrip, Thicket Fan / snares, Root Snare |

Custom outcomes (`Interactions.register_outcome`; the `interaction` event reports a catalogued outcome name and the target in
`to`, the rule id says which):

| Outcome | Event outcome | What it does |
|---|---|---|
| `water_carry` | capture | a wave carries a solid, throws it at the rival it reaches |
| `water_ridge` | transform -> ridge | a water wave freezes into a standing ice ridge (same body, same mass) |
| `water_freeze` | transform -> ice | water / steam freezes solid and drops (Flash Freeze, frost walls) |
| `water_skin` | absorb | the threat's water goes into the defender's waterskin (Condense) |
| `water_hot_block` | block | a hot threat gives up to `hu` of heat to an ice / water barrier (melts it, vapor ledger), then it blocks |
| `water_dampen` | weaken | fog / steam take `k` of a fire's heat and speed (booked ambient) |
| `water_condense_in` | absorb | steam / mist condenses into a held water shield |
| `water_quench` | transform / weaken | a water body cools lava / hot rock by heat transfer (small water = partial quench) |
| `water_brittle` | transform -> brittle | frost makes a vine brittle |
| `water_feed` | absorb | a vine barrier drinks water and grows (booked water_to_plant) |
| `water_burn` | transform -> ash | fire burns vine x3 (burn_plant) and passes on, weaker |
| `water_drown` | neutralize | a big wave collapses a tornado zone |
| `water_melt` | transform -> water | steam melts ice shards |
| `water_sling` | redirect | Catch & Sling |

### Headline interactions, worked through

* **"Someone throws a stone at me: make a wave back"**: stone TP 17 vs Tidal Rush T0 CP 18 (6 kg: the waterskin): `water_carry`.
  T1-T3 away from water are partial (the skin cannot fill them): the stone is *slowed* and hits softer (x the slow factor).
* **Water wave vs Ice Wall / Rime Path**: the wall always stops a wave (the legacy wall rule); a perfect guard (Flash Freeze) or a
  Rime Path turns it into a ridge.
* **Lava wave (TP 27.3)**: Tidal Rush T0 18 x 1.5 = 27 (0.99: partial), T1 24 x 1.5 = 36 (full); Rime Path T2 20 x 1.2 = 24
  (partial crust), T3 30 x 1.2 (full obsidian); Ice Wall T0 22 x 0.7 = 15 (partial, melts); Steam Screen 10 (partial);
  a 45 kg lava wave (61.5) needs a combination.
* **Lightning**: Ice Wall insulates E <= 33 (grounded) and shatters under E 36 (core cell, eff_insulator 1.5); fog conducts at
  60 % to everyone inside; a held jet conducts back to its caster; a frozen puddle / pool strip / ice floor has no conduction node;
  wet vines ground, dry vines block.

## 5. Statuses, zones, tags (the §15.6 contract)

* Statuses (`CombatWorld.register_status`): `fogbound`, `fogwalk` (hidden, AI perception +0.25 / +0.3 s), `scalded` (dps),
  `surfing`, `skating` (friction 0.35), `icegrip` (cancels your own ice's friction), `entangled`, `hooked`, `veiled`. Core statuses
  used: `wet`, `chilled`, `frozen`, `rooted`, `slowed`, `slick`, `blinded`, `concealed`, `anchored`, `levitating`.
* Body tags: projectiles `needle lance spear comet slug orb seed`, waves `water_wave rime roots`, walls `ice ridge vine`, zones
  `fog mist steam steam_screen ice_floor geyser briar snare slick`; hooks registered: zone effects (`fog mist ice_floor steam
  steam_screen`), body ticks (`ice ridge rime geyser vine roots water_wave`), tag classes (`rime`, `roots`, `ridge`, `briar`).
* Events: `fx {cast release cone beam burst ring erupt trail splash aura}` with `mat` water / ice / mist / steam / plant, `charge`,
  `zone`, `status`, `interaction`, plus kit events `jet`, `dive`, `slip`, `dew`, `hook`, `swing`, `slam`, `lash {around}`.

## 6. Ledgers

| Conversion | Booking |
|---|---|
| waterskin / pool / puddle -> any water body | `WaterUtil.take` (pool heat to `ledger.removed`), exact |
| ambient moisture -> ice / mist / vine wall | `mass_ledger.moisture_taken` |
| water -> vine | `mass_ledger.water_to_plant` (+ `moisture_taken` for moisture); wither -> `plant_returned`, burn -> `burned` |
| freezing | `WaterUtil.freeze_body` -> `ledger.freeze_dump` (steam -> ice via `freeze_any`) |
| steam | `heat_body` -> `ledger.vapor`; pockets (`geyser`) carry the paid heat in `heat_payload` until they erupt |
| fog / mist zones | MatBody mat STEAM, mass booked; dissipates into `mass_ledger.vapor` |
| condense / dew | the mass moves between STEAM and WATER bodies (water_mass counts both); the heat is already booked |

`test_kit_water_ledger.gd` plays 60 s of random input (all four sub-elements, mirror and against Earth / Fire / Air) and checks
energy and every mass ledger every second (worst drift about 1e-11), body cap, no stuck actions and determinism.

## 7. Known gaps / notes for other streams

* **Core / registry test**: `test_core_registry.test_legacy_bindings_and_fallbacks` (frozen, core-owned) asserts that nothing is
  bound to sub 0 thrust / evade and that subs 1-3 fall back to the sub-0 strike. Every kit stream binds those slots, so it needs
  updating by its owner (it fails once any kit is registered; the engine behaves as documented).
* Same cause: `test_core_input.test_unbound_gesture_falls_back_to_the_strike` / `test_evade_hold_morphs_after_0_2_s_when_bound` and `test_core_registry.test_register_bind_resolve_list_unregister` assume no kit binds sub-0 thrust / evade.
* **Missing visuals** (seen in the renders, presentation not edited): no view yet for the ice WALL (a round water blob is drawn), `ice_floor` / `fog` / `mist` / `steam` / `geyser` / `briar` / `snare` ZONES, the `rime` / `roots` waves, the held `jet`, the plant bodies and vine WALL; the Skate camera loses the rival at the pool.
* `FxEvents.SHAPES` does not list the shapes the verbs emit as `shape` (mode kinds `surf skate walk roots ...`, grip shaping kinds
  `split freeze condense ...`); the kit tests allow them.
* A cone / beam volume does not read zones it passes through, so fog / steam dampen fire and blast **bodies** (balls, waves,
  cones' bodies) but not instant flame cones from outside the zone; the cone verb would need to apply `pass_scale`.
* Veil's "+20 % balance damage on your next attack" and fog's "sound x1.2 muffle" have no sim hook (no per-actor damage
  modifier status); the status `veiled` marks it for a later core hook.
* VFX / audio to map: the kit's `fx` cues use `mat` `water ice mist steam plant`; persistent visuals are the bodies / zones listed
  in §5 (a `ridge` WALL, `ice_floor` / `fog` / `steam` ZONES, a PLANT wave tagged `roots`, a held `jet` water body from hand to
  `jet_end`).

## 8. Tests

`test_kit_water_moves` (defs complete, every move at T0-T3 with exact ledgers and catalogued fx, costs), `_water`, `_ice`, `_mist`,
`_plant` (move behaviour: carry back, quench, conduction through the jet, ridge, frozen puddle / pool strip, fog 60 %, ...),
`_cells` (the matrix), `_ledger` (60 s soaks + determinism).

## 9. Showcases

`--autoplay=show_water[:s]`, `show_water_ice`, `show_water_mist`, `show_water_plant` (real InputFrames: press, hold, flick, drag).
Frames of the renders: see the stream report; missing visuals are listed there rather than edited into the presentation layer.
