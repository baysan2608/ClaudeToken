# Air kit: Gust, Vortex, Vacuum, Sound

_Owner: the Air stream. Design: `docs/MOVESET.md` §3-§5, §7.13-§7.16, §8.4 (the Gust / Wind Guard / Wind Grip columns),
§8.6, §9, §15, §16. Engine: `docs/COMBAT_SPEC.md` "Engine". 40 bound slots (10 slots x 4 sub-elements; Gust keeps the legacy
strike, technique, evade and guard; 37+ Air move defs), 273 counter cells, 15 custom outcomes (`air_*`: `air_cool`, `air_cut`, `air_split`,
`air_shrink`, `air_shatter` in the Gust column, plus `air_infuse`, `air_catch`, `air_slow_bend`, `air_spatter`, `air_contest`, `air_compress`, `air_snuff`, `air_spit`, `air_pop`,
`air_still`), 7 statuses, 0 core edits._

## 1. Files

| File | What |
|---|---|
| `game/combat/kits/air/kit_air.gd` | entry point (`Moves.ensure()` calls `register()`), module dispatch (`KitAir.reg(id, sub, slot, def)`, `KitAir.handle(id, {start, after, phase, tick, interrupt})`; unknown handlers fall back to the Verbs lifecycle) |
| `air_util.gd` | aim / ground / cone helpers, zone helpers (`zone`, `zones_of`, pair marks), `guard_zone_effect` (the shared per-tick resolution of a guard zone with the perfect window), `shove`, `carriable` (<= 30 kg), arena slab reflection (`arena_hit`), booked `condense`, the **inrush** (`spawn_inrush`, `inrush_tick_at`) |
| `air_outcomes.gd` | the Gust column's custom outcomes: `air_cool`, `air_cut`, `air_split`, `air_shrink`, `air_shatter` |
| `air_rules.gd` | the Air counter matrix: `CELLS` (key `threat|counter|tiers` -> rule + reference expectation), `REF` (reference threats), `COLUMNS`, statuses, tag classes; the Gust, Wind Guard and Wind Grip columns |
| `air_gust.gd` | sub 0: legacy extension (`air_attack`, `air_tech`, `air_dash`) + Crescent, Dust Line, Crosswind, Wall of Wind, Downdraft, Wind Grip, Tailwind |
| `air_vortex.gd` | sub 1: Twister / Tornado / Fortress, Spiral, Dust Funnel, Eddy Ring, Vortex Wall, Unleash, Funnel Down, Eye of the Storm, Spin Step, Whirl Lift + the vortex cells |
| `air_vacuum.gd` | sub 2: Pressure Palm / Cannon / Implode / Collapse, Suction, Mine, Arc, Null Bubble, Pressure Wave, Anchor, Vacuum Well, Hop, Slipstream + the vacuum cells |
| `air_sound.gd` | sub 3: Clap ... Resonance, Lance, Tremor, Echo Ring, Sound Barrier, Thunder Step, Ping, Flight, Boom Step, Hover + the sound cells |
| `game/combat/act_air.gd` | the legacy module (T0 / T1 exact) + T2 Gale / T3 Hurricane tier handling in `_push`, the Wind Grip context in `air_tech` |
| `game/tests/sim/test_kit_air_*.gd` | tests (§6); `perf_air.gd` is the soak |
| `game/actors/showcase_air*.gd` | showcases (§7); `showcase_air_common.gd` is the beat engine (`AirShowcase`) |

## 2. Legacy compatibility

* Gust (sub 0) keeps `air_attack` (T0 Palm Gust / T1 Cyclone push, numbers untouched), `air_tech` (Updraft / glide) and
  `air_dash`. They are extended in place, only with keys the base defs do not have (`tiers t2` Gale, `t3` Hurricane, `counter`,
  `threat`, `fx`, `ai`, descriptions). Legacy cells are never replaced: the Gust column's cells for stone, hot_rock, ice,
  metal, glass, sand, water, stone_heavy, boulder, magma, lava_wave, flame, gust, lightning, puddle and `*` stay for tiers 0 / 1
  (`test_cells_never_replace_legacy_cells`); the kit's cells cover **tiers 2 / 3** and the new classes only.
* **Wind Guard (the sub-0 guard):** the legacy `guard_wind` cells answer exactly as before (stone `wind_guard_light`, flame
  `legacy_air_vs_flare` = a clean block). The fire bands of MOVESET §8.4 (amplify / deflect / extinguish with the guard's
  perfect reflect) therefore apply to **ember, blue_fire and fire_field**; a thrown fireball classifies as `flame` and is only
  blocked. See §9.
* Wind Grip is the **context** of `air_tech`: a light body (<= 30 kg, fireballs too) in the aim cone within 9 m morphs the
  technique into `gust_grip` (grounded only); otherwise Updraft as before.
* `Moves.resolve(3, 0, "guard")` stays the legacy `guard`; the other three sub-elements bind their own guard def (the running
  action is still `"guard"` with a `spec`, like the Fire / Water kits).
* Evade and evade_hold moves carry no `t0..t3` tiers (they are not chargeable, by design).

## 3. Moves

All numbers are in the defs (`desc`, `ai` role / range / tags); the tables below name the move and the one rule that
matters. Costs are Focus. `tNs` are the charge tiers (hold times `Charge.TIER_TIMES`: 0.4 / 1.0 / 1.8 s).

### Gust (sub 0)
| Slot | Move | Notes |
|---|---|---|
| strike | Palm Gust -> Cyclone (legacy) -> **Gale** (T2, 8 m / 50 deg, knock 14, P 18, +6 Focus) -> **Hurricane** (T3, 10 m / 60 deg, knock 18, P 28, +12) | T2 / T3 turn a lava wave to rock (transform, `hu_per_pu 15`, `heat_mult 0.35`), bend heavy stone, cool hot rock |
| thrust | Wind Crescent | 22 m/s; light shots are deflected, vines cut (x2); T1 two crossing crescents, T2 wide, T3 Scythe (pierces two) |
| ground | Dust Devil Line | trips (-30 balance), knock-up 3, dust (light blind) |
| sweep | Crosswind | 120 deg / 5 m, curves projectiles by up to 40 deg by mass and speed |
| push | Wall of Wind | from the guard: a moving wall 8 m/s |
| sink | Downdraft | from the guard: slams airborne enemies (ends flight), flattens fire fields |
| tech | Updraft / **Wind Grip** | grip a light body, drag-aim, release to fling; a gripped fireball grows 20 % (booked as `generated`) |
| evade_hold | Tailwind | x1.3 run speed (6 Focus/s) |

### Vortex (sub 1)
| Slot | Move | Notes |
|---|---|---|
| strike | Twister (T0 / T1) -> **Tornado** (T2, zone, 4 s, walks to the target) -> **Fortress** (T3) | captures and orbits bodies <= 30 kg, lifts / swirls fighters (`windborne`), takes up **infusions**: sand (blinded, sandblasted), fire (burning), water (wet, conducts), steam (scalded), magma (spatter) |
| thrust | Spiral Lance | pierces mist / sand clouds / sand walls |
| ground | Dust Funnel | a ground vortex that gathers loose bodies and carries them to the target |
| sweep | Eddy Ring | r 3 m for 1.5 s, orbit-deflects projectiles, pushes light fighters out |
| guard | Vortex Wall | captures light projectiles (<= 30 kg; **Vortex Catch** with a perfect guard: up to 45 kg) instead of stopping them |
| push | Unleash | flings the captured bodies as YOUR attack (new attack id, perfect catch +25 % speed) |
| sink | Funnel Down | crater, drops the captured at your feet |
| tech | Eye of the Storm | a steerable tornado at the aim point; over an enemy tornado: contest, the stronger takes it over (neutral owner -1 if an infusing hostile body gets captured) |
| evade / hold | Spin Step / Whirl Lift | 3 m sidestep with i-frames / hover 1.5 m (8 Focus/s) |

### Vacuum (sub 2)
| Slot | Move | Notes |
|---|---|---|
| strike | Pressure Palm -> Air Cannon -> **Implode** (T2) -> **Collapse** (T3) | Implode / Collapse are timed wells: a vacuum point that pulls, then collapses into an **inrush** |
| thrust | Suction Line | pulls a fighter 2 m; yanks a light body <= 12 kg into the hand; grapples you to a wall / anchor |
| ground | Pressure Mine | zone mine, blows the fighter who steps on it 7 m up |
| sweep | Vacuum Arc | snuffs flames x2, silences sound |
| guard | Null Bubble | fire dies in it (x2), sound nullified (x3), steam / mist collapse to water, lightning E <= 1.5 x CP blocked; perfect: **Null Catch** (spits a snuffed ball back) |
| push / sink | Pressure Wave / Anchor | ring shove (r 3 m, P 16) / stance anchor CP 35 (immune to knockback, pulls, lift) |
| tech | Vacuum Well | pulls projectiles, clouds (steam -> water, sand -> sandstone, booked), fighters; released or interrupted it **collapses** and spawns the inrush |
| evade / hold | Pressure Hop / Slipstream | float landing / x1.3 speed, slows projectiles behind you |

### Sound (sub 3)
| Slot | Move | Notes |
|---|---|---|
| strike | Clap -> Shout -> **Roar** (T2, shatters ice / glass / thin walls, -8 Focus) -> **Resonance** (T3, any brittle body, deafens 2 s) | the cone first **disrupts** channels and charges, then hits |
| thrust | Sound Lance | passes through water / mist, ricochets (max 2) off stone, metal and arena walls |
| ground | Tremor Hum | knocks over, cracks walls once each, pops loose stones |
| sweep | Echo Ring | reveals (fog, Mist Step, burrowers), interrupts |
| guard | Sound Barrier | shatters ice / glass, cancels sound; perfect: Echo Return |
| push / sink | Thunder Step / Ground Ping | boom-dash / pulse that reveals burrowers and disrupts ground lines |
| tech | Flight | tap: rise to 2.2 m, fly 6 m/s, all attacks usable (6 + 10 Focus/s); tap again: land and glide; `status flight` = immune to ground effects, x1.5 balance damage from wind |
| evade / hold | Boom Step / Hover | 6 m dash with a 2 m boom / hold height (6 Focus/s) |

## 4. Counter matrix (the Air columns)

`AirRules.CELLS` (273 cells) answers the three Air counter columns of MOVESET §8.4 plus the kit's own classes. A cell is
`{id, threat, counter, ref, tiers}`; `ref` is the reference scene (`move`, `tier`, `perfect`, `tp`, `mass`, `expect`) that
`test_kit_air_cells` evaluates through `Interactions.predict` (146 reference cells). Classes added by the kit:

| Counter class | Source | Notes |
|---|---|---|
| `gust` T2 / T3, `crescent`, `crosswind` | Gust strike / thrust / sweep | light solids deflect (eff 1.5, ice / metal 2.0), heavy bend, lava wave -> rock, hot rock cooled, fire bands amplify / deflect / extinguish |
| `guard_wind`, `grip_wind` | Wind Guard / Wind Grip | reclaim for light classes, else pass |
| `tornado`, `wall_vortex`, `eddy` | Vortex | capture / infuse / slow-bend; lava on a tornado bands pass -> spatter (liquid_floor 0.5) -> transform at ratio 1.2 |
| `bubble_null`, `vacuum_well`, `suction`, `vacuum` | Vacuum | snuff / compress / spit; lightning bands weaken -> block |
| `sound`, `barrier_sound`, `ground_ping`, `tremor` | Sound | `air_pop` / `air_still`; shatter via `air_shatter` |

Rows covered in every Air column (checked by `test_every_row_has_an_answer_in_every_air_column`): stone, stone_heavy,
boulder, hot_rock, magma, lava_wave, metal, sand, sand_surge, water, water_wave, ice, mist, steam, vine, flame, blue_fire,
lightning, blast, gust, tornado, vacuum, sound. Outcome names are all catalogued; every move's fx keys, mats and shapes are
catalogued in `FxEvents` (`fx_catalogued` in the tests).

## 5. Conservation and the zones

* Every heat / mass change goes through the ledgers: convective cooling by wind is booked `ledger.ambient`
  (`air_cool`); a gripped fireball's +20 % is `ledger.generated`; steam -> water and sand -> sandstone book through
  `condense` / `convert_mat`; a cut vine's half returns through `decay_body(..., "cut")`; the lava wave turned to rock
  uses the core `transform` (heat stays on the books). The soak (`perf_air.gd`) checks energy and all four mass ledgers every
  second.
* **Shatter:** `air_shatter` runs the core shatter once and marks the pieces (`props.shattered`) so a volume that sweeps the
  live body list does not shatter the fragments again; the pieces get `gravity_scale 1`, so a stone that was flying falls.
* **Inrush:** a Vacuum Well / Implode collapse spawns a zone tagged `inrush` (0.6 s, at the collapse point). Fire's
  combustion may read it with `AirUtil.inrush_tick_at(w, pos)` (or `FireUtil.zones_at(w, p, ["inrush"])`): air rushing back
  in feeds a flame and drives a combustion detonation. Nothing in Air depends on Fire reading it.
* `flight_field` (a persistent zone attached to the flying actor) and `inrush` zones are tag classes of the kit.

## 6. Tests (all green in isolation; 76 Air tests)

`tools/scripts/godot.sh --headless -s res://tests/run_tests.gd -- test_kit_air` runs them.

| File | Tests | What |
|---|---|---|
| `test_kit_air_util.gd` | base | `AirKitTest`: `duel`, `run_move`, `run_when`, ledger snapshots, `fx_catalogued` |
| `test_kit_air_gust.gd` | 23 | legacy numbers, Gale / Hurricane vs a lava wave, crescent deflect / cut, dust line, crosswind curve, Wall of Wind, Downdraft vs flight, Wind Grip + fireball +20 %, Tailwind, Wind Guard bands |
| `test_kit_air_vortex.gd` | 19 | Tornado walk / lift, infusions (sand, fire, water, steam), Vortex Wall capture + Vortex Catch + Unleash (attack id changes), Funnel Down, Eye contest, Spin Step, lava vs tornado |
| `test_kit_air_vacuum.gd` | 13 | Null Bubble snuff / Null Catch, Suction (pull, yank, grapple), Mine, Well pull / compress / collapse / inrush, Anchor, Hop |
| `test_kit_air_sound.gd` | 16 | Clap tiers + disrupt pre-pass, Roar / Resonance shatter (no recursion), Lance bounces, Tremor, Echo Ring, Barrier, Ping, Flight (rise, hold, land, tornado ends it), Boom Step |
| `test_kit_air_cells.gd` | 5 | the 146 reference cells, band consistency of the simple cells, the coverage matrix, legacy cells untouched, every Air slot bound |
| `perf_air.gd` | soak | 120 s random input, both fighters cycling every element: mean 0.22 ms, p95 0.38 ms, p99 0.59 ms, max 2.9 ms per sim step; worst 17 bodies; ledgers exact |

Known failing **frozen core** tests (not Air's, not fixable by this kit): `test_core_input.test_unbound_gesture_falls_back_to_the_strike`,
`test_core_input.test_evade_hold_morphs_after_0_2_s_when_bound`, `test_core_registry.test_legacy_bindings_and_fallbacks`,
`test_core_registry.test_register_bind_resolve_list_unregister`. They assumed sub-0 slots and extra subs of Earth / Water /
Air were unbound; the three kits now bind them (the assertions need to follow the registry).

## 7. Showcases

`--autoplay=show_air` (Gust: the legacy beats + Gale vs a lava wave, Crescent, Dust Line, Crosswind),
`show_air_vortex`, `show_air_vacuum`, `show_air_sound` (~20 s each). Every beat writes an `InputFrame` (press / hold / flick
/ aim drag) so the moves run through `PlayerController` and the real sim; the rival is passive and its threats (stone, lava
wave, fireball) are spawned with the same ledger bookkeeping its own moves use. The showcases top Focus up (to 60 when it falls
under 35) so the big moves can be chained. `SHOWCASE_TRACE=1` prints a line per 6 ticks.

```
tools/scripts/godot.sh --render --resolution 1280x592 --write-movie out.avi --fixed-fps 30 -- --autoplay=show_air_vortex:22 --quality=2 --shots=dir
```

## 8. Tuning notes (feel)

* Gale / Hurricane cost +6 / +12 Focus on release and drain 8 Focus/s while charging T2; a fallback to the lower tier happens
  when Focus is short.
* The lava wave answer is deliberately a **stall, not a clean block**: T2 weakens (heat_mult 0.35) and leaves a hot, slowed
  wave; T3 crusts it into rock. `hu_per_pu 15` keeps a 20 kg wave (396 HU) from being fully quenched by a Gale.
* Tornado: 4 s, walk speed 4 m/s, lift speed 5 m/s up to 3.4 m, balance wear 7 / s (10.5 / s on flyers): a rival caught
  in it loses about 7 balance per second at T2 and is `windborne` (off the ground) while inside.
* Null Bubble Catch and Vortex Catch rely on `Moves.PERFECT_WINDOW` (0.18 s); the showcases press the guard ~0.2 s before
  the shot arrives.
* `perf_air` numbers above are the budget (60 fps on an iPhone 12-class GPU leaves 16 ms; the sim worst case is 2.9 ms).

## 9. Gaps and requests for the core / other streams

1. The four frozen core tests above need updating (registry / input assumptions).
2. `flame|guard_wind` is a legacy cell (clean block): fireballs are blocked, not amplified; only ember / blue_fire /
   fire_field use the fire bands.
3. A water-infused tornado conducts through `z.charge = 0.02` (a hack). Suggested core change: `Materials.conducts` should
   honour `props.conducts`.
4. `FxEvents.BODY_TAGS` lacks the zone tags `flight_field` and `inrush` (and the VFX director has no view for the Air zones
   yet: the showcase renders show gameplay and fighter poses, the tornado / wall / well volumes are not drawn until
   `VortexView` & co. are wired in `body_views.gd`).
5. Fire's combustion should read the inrush (`detonate(inrush_tick)` is currently unwired).
6. The first resolution of a (zone, body) pair ignores `props.rate` (core `_zone_pass`).
7. A hit staggers an anchored stance (Anchor): the stance is lost.
