# Earth kit — Stone · Metal · Sand · Magma

_Earth stream · 2026-10-05 · implements docs/MOVESET.md §7.1–§7.4 and the Earth column of §8.1 on the moveset engine
(docs/COMBAT_SPEC.md "Engine")._ Numbers are design targets; tune them in the Lab (`Moves.set_override`).

**Code** (`game/combat/kits/earth/`): `kit_earth.gd` (registration, dispatcher for module `kit_earth`, shared helpers:
`pour_wave`, `guard_tier`, aura cue), `earth_stone.gd`, `earth_metal.gd`, `earth_sand.gd`, `earth_magma.gd`,
`earth_rules.gd` (counter cells + 27 `earth_*` outcomes). `game/combat/act_earth.gd` (legacy Stone strike / technique)
gains the T2/T3 ladder and T+A Split. Every Earth def has module `kit_earth`; ids the kit doesn't handle fall through to
the generic verbs, so most moves are data on a verb plus a `hook_execute` / `hook_impact`.

**Tests** (`game/tests/sim/`): `test_kit_earth_moves.gd` (every slot of every sub-element at T0–T3: starts, pays,
ends, catalogued fx; schema; anim clips), `test_kit_earth_stone.gd`, `_metal.gd`, `_sand.gd`, `_magma.gd` (behaviour,
owner examples, the §8.1 column at the reference powers incl. partial bands), `test_kit_earth_ledgers.gd` (a scripted
60 s two-fighter exchange using 34 moves: ground / metal / water ledgers exact, energy identity, same seed → same
hash), helpers in `test_kit_earth_util.gd`.

**Showcases**: `--autoplay=show_earth:40` (legacy part + new Stone moves from 19 s), `show_earth_metal:20`,
`show_earth_sand:20`, `show_earth_magma:20` (`game/actors/showcase_earth*.gd`, shared helpers in
`showcase_earth_common.gd`).

## Moves

Costs: F = Focus at the press (tier surcharges `cost_add` / `heat_add` at release, drain 8 F/s from T1 unless noted),
HU = heat paid like Fire (reserve first, then 10 HU/Focus), kg = metal satchel. CP = counter power by tier.

### Stone (sub 0, the legacy kit)

| Slot | Move | Cost | What it does (T0 → T3) |
|---|---|---|---|
| strike | **Stone Shot** `earth_attack` (legacy) | 7 F, +7 heave | 20 kg @17 → Heave 45 kg @14 (legacy, **no drain**) → **Boulder** 65 kg @12 (+6 F, 8 F/s) → **Crag Breaker** 80 kg @11, bursts into 3 rubble on impact |
| thrust | **Spear Stone** | 8 F | 12 kg flat spear @26 → twin → triple → Pike Volley (5) |
| ground | **Rising Fangs / Earthrise** | 9 F | spike line 9 m @14 (launch 6) → 12 m → tall & wide; where it ends or meets a ground line it erupts into **standing spikes** (WALL `spikes`, CP 20, 1.5 s) that stop lava / water / sand waves and fire lines; T3 **Earthrise**: 4 spike walls in a ring r 3.5 + a 40 PU eruption |
| sweep | **Rubble Fan** | 7 F | 3 × 6 kg in 40° → 5 → 7 → 9 skipping (ricochet) |
| guard | **Bulwark** (legacy wall) / Return Stone | 8 F | 120 kg; **thickens with the hold: 160 kg at 1.0 s, 200 kg at 1.8 s (CP 30 / 40 / 50)** |
| push | **Ram Wall** | 6 F | the standing wall slides 6 m @9 (K = mass × 9 / 20 = 54): shoves bodies and waves back (they become yours), hits fighters, contests enemy walls, ends in rubble |
| sink | **Swallow** | 8 F | ground opens 6 m 60°; CP 22 / 30 / 40 / 55 from the guard hold: solids SNK (ground_returned), lava waves drain (T1+), water waves diverted |
| tech | **Seize** (legacy) / **T+A Split** | 6 F, +3 F | seize ≤ 80 kg (incoming preferred); T+A: three spears (⅓ mass) fanned 30° @20 back at the target |
| evade | evade (legacy) | — | unchanged |
| evade_hold | **Stone Skin** / **Burrow Step** | 8 F/s / 6 F | held in place: 40 % armor, anchored (anchor CP 40), ×0.4; held **with a direction**: burrow 3.5 m (i-frames, concealed: ground lines pass over), erupt at the exit |

### Metal (sub 1) — satchel 12 kg (`ActorState.metal_carried`, max 30 with recalls), scrap 10 kg from the arena plate (1.2 s cooldown)

| Slot | Move | Cost | What it does |
|---|---|---|---|
| strike | **Razor Disc** | 5 F + 2 kg/disc | 2 kg @24 homing 10°/s → 2 curving (30°/s) → 3 that ricochet once → **Disc Storm**: 5 orbit you 0.6 s then fire |
| thrust | **Iron Lance / Railspike** | 8 F + 6–12 kg | 6 kg @30 → 8 kg → 10 kg pierces one body → 12 kg @42; embeds in stone/glass walls and arena solids as a **rod** (conductive) |
| ground | **Lodestone Line / Iron Garden** | 8 F + 3 kg | filings snake 8 m, then the filings *become* a caltrops ZONE r 2 / 2.5 / 3 / 4 m for 4 s (slowed, 2 HP/s, conductor node); afterwards loose scrap |
| sweep | **Chain Arc** | 7 F | 5–7 m 120° (360° at T3): yanks fighters toward you; wraps stones / metal ≤ 20 kg out of the air to your feet (CP 10 / 16 / 24 / 32, ×1.2 vs light solids: T1+ catches a stone shot) |
| guard | **Aegis Plate / Magnet Catch** | 8 F + 6 kg (returned) | held plate CP 20 (6 kg × 3.3); heats under hot rock / flames (≥ 300 °C dropped, burns the hand); bolts grounded on stone, **conducted into you ×1.2** in water / on the plate; perfect: metal → satchel, stones deflected |
| push | **Plate Rush** | 4 F | the held plate @20, spinning |
| sink | **Rod Plant** | 6 F + 3 kg | a rod (METAL ZONE `rod`, r 6, 8 s): bolts entering the field are grounded up to 60 E, above that it melts and the bolt continues (E − 30); a rod touching a puddle conducts into it |
| tech | **Lodestone Grip / Reforge / Recall** | 6 F | seize metal ≤ 80 kg at 10 m (grip ×1.3, steals discs, lances, held plates); T+A Reforge lance → 3 shards → disc; nothing to grip: **Recall** every owned piece (they fly back as your attack and hit what is in between); on the plate: rip 10 kg scrap |
| evade | **Magnet Glide** | 5 F | dash ≤ 6 m to the nearest metal within 8 m, else 3 m sidestep |
| evade_hold | **Iron Stance** | 6 F/s | anchored (CP 30), 25 % armor, ×0.5 |

### Sand (sub 2) — ground grit (`ground_taken`; spent sand settles back after 1.2 s); sand ↔ glass ↔ sandstone booked

| Slot | Move | Cost | What it does |
|---|---|---|---|
| strike | **Grit Shot / Sand Cannon / Dune Breaker** | 5 F | 5 kg slug @22, bursts blinding (1 s, lock-on off) → 3 slugs → 15 kg slug bursts into a 2 m sand cloud → 30 kg into a 4 m sandstorm (3 s, abrades 2 HP/s); the cloud *is* the slug's sand |
| thrust | **Sandblast / Scour** | 7 F | abrasive beam 8–10 m P 6 / 9 / 14 / 20; T2/T3 sustained 1 / 2 s (pulses 0.2 s); abrades glass ×2 |
| ground | **Sand Surge / Desert Tide** | 9 F | sand wave 9 m/s, 30 / 40 / 53 / 75 kg (K 13.5 / 18 / 24 / 34), 2–4 m wide: knockdown, carries loose and incoming solids back, smothers fire fields, buries puddles (mud), crusts lava (×1.5) |
| sweep | **Veil of Grit** | 6 F | sand cloud r 4 / 5 / 6 / 8 m (T3 sandstorm): enemies inside blinded, you inside concealed; projectiles dragged once (−30 % K); bolts through it halved + a glass bead; flames smothered; sound / mist absorbed |
| guard | **Dune Wall / Engulf** | 7 F | 100 kg sand wall, held 115 / 130 / 160 kg (CP 25 → 40): captures solids (×1.2), smothers fire (×2), absorbs blasts / sound (×1.5), grounds bolts up to 60 and **turns to glass**; water → **mud wall** (+5 CP, collapses after 4 s); perfect **Engulf**: a projectile ≤ 30 kg spat back |
| push | **Dune Push** | 4 F | the dune itself collapses forward into a Sand Surge (its mass = the hold) |
| sink | **Quicksand** | 8 F | 3 m pit 5 s, CP 18 / 26 / 36 / 50: walkers ×0.3, rooted after 1 s; landing solids SNK; lava crusts to glass and stalls; water → mud bog (longer, roots faster) |
| tech | **Sandform / Compress** | 4 F + 2 F/s | gather 6 kg/s up to 30 kg (or seize a rival sand cloud / surge / slug by contest); release = a blinding slug; T+A **Compress**: sandstone (STONE, same mass) thrown as a stone |
| evade | **Sand Surf** | 5 F | 4 m slide |
| evade_hold | **Sand Surf (ride)** | 10 F/s | ride at 8 m/s (×1.45) |

### Magma (sub 3) — heat paid like Fire; molten holds cost the core upkeep (3 F/s, insulated)

| Slot | Move | Cost | What it does |
|---|---|---|---|
| strike | **Ember Clot / Magma Bomb / Caldera** | 4 F + 80 HU | 4 kg molten glob @18 (burn 2 s) → 3 globs (+160 HU) → 12 kg lobbed bomb → a 2 m **lava pool** (+160) → 20 kg → 3.5 m pool (+320); pools are ZONE `lava_pool` made of the bomb's own lava (burn walkers, melt what lands, set into rock when cold) |
| thrust | **Lava Lash / Molten Lance** | 6 F + 60 HU | whip cone 6 m (H 6) → 7 m → 0.6 s stream → 1 s **Molten Lance** that pours 300 HU into a stone wall's face; when the face holds enough to be half molten it **slumps** on your side and the wall crumbles (two lances slump a Bulwark) |
| ground | **Magma Surge / Lava Tide** | 8 F | every molten body within 6 m (yours, the rival's, slumped faces, lava pools, settled waves) is re-poured as a wave toward the target — **"push it back on them"**; nothing molten: an 8 kg vein (+160 HU); T1/T2 budget ×1.3 / ×1.6; T3 **Lava Tide** merges it all into one wave (×1.8) |
| sweep | **Spatter Arc** | 5 F + 50 HU | 5 × 1 kg hot droplets 70° → 7 → 9 → a 12-droplet ring |
| guard | **Magma Curtain / Obsidian Set** | 10 F + 150 HU | 100 kg wall (CP 28) with a molten face (`heat_payload`): small solids stick and fuse; lava absorbed; flames feed it; ice boils; water → steam and the face **sets to obsidian** (0.38 / kg); perfect: the projectile fuses in (+5 CP) |
| push | **Slag Wave** | 4 F + 150 HU | 15 kg of the curtain + its face pour forward as lava |
| sink | **Melt Pit** | 8 F + 120 HU | 2.5–3.6 m pit 2–3.5 s, CP 20 / 28 / 38 / 50 (+60 / 120 / 200 HU by guard hold): landing solids heat / melt in, ice boils, walkers burn; sand surges glaze to glass |
| tech | **Magma Hold / Cool & Set / Reverse Tide** | 6 F + 3 F/s | seize lava / hot rock / lava pools / slumped faces at 8 m; release pours a wave (molten) or throws (solid); T+A **Cool & Set** dumps the heat into the ground (booked removed); on a live enemy lava wave: **Reverse Tide** contest (grip ×1.3 vs the wave's authority) — win: it turns back on the pourer |
| evade | **Cinder Step** | 5 F | 3.5 m dash |
| evade_hold | **Lava Wade** | 10 F/s | immune to burns, ×0.6 |

## Counter cells (MOVESET §8.1, Earth column)

`EarthRules` adds the cells of: `wall_stone` (non-legacy rows: metal → embeds as a rod, sand, glass, vine, steam,
flame / blue fire (wall heats), blast, gust, tornado, vacuum, sound → reflect), `wall_obsidian`, `wall_sand`, `wall_mud`,
`wall_glass`, `plate_metal`, `spikes`, `rod`, `caltrops`, `swallow`, `quicksand`, `melt_pit`, `grip_stone` (glass),
`grip_metal`, `grip_sand`, `grip_magma`, `wave_sand`, `wave_lava`, `sand_cloud`, plus the kit's own classes `ram`
(Ram Wall face; `wall_stone` × walls = the push contest) and `chain`, and the spike line as a threat (`spikes` × waves).
`EarthRules.CELLS` lists every key. Legacy and environment cells are never touched (`add_rule` would refuse them).
Reference results the tests pin: stone shot 17 vs Swallow 22 → SNK, vs Aegis 20 → BLK, vs Dune 25 × 1.2 → CAP;
heave 31.5 vs Swallow T0 → partial, T2 → SNK; boulder 110 vs Swallow T3 55 → partial (then Ram Wall 54 nearly stops it);
20 kg lava wave 27.3 vs Swallow T0 → partial, T1 → SNK, vs spikes / dune / quicksand / sand surge → stopped & crusted;
Storm Bolt 36 vs a fresh Bulwark → shatters, vs a 1 s Bulwark (40) → grounded; Skybreak 52 vs Rod / Dune (cap 60) →
GND, E 70 → the rod melts (continues with 40); tornado 35 vs Stone Skin anchor 40 → holds, 25 vs Iron Stance 30 → holds.

## Deviations from MOVESET and engine notes

* **Legacy Stone strike T2/T3**: `Moves.register` refuses legacy ids, so tier data is *added* (only keys the base def
  lacks) to the live `Moves.DEFS.earth_attack`; `tier_times` = 0.55 / **1.1** / **1.9 s** (T2 at 1.1 s, not 1.0 s, so
  the frozen 60-tick heave test still gets 45 kg); T1 has `charge_drain 0` (the legacy heave never drained).
* **Bulwark thickening** runs lazily from a channel hook on untagged bodies (`Interactions.register_channels(&"")`,
  chaining any previous hook): when a guarded Bulwark acts as a counter it grows to its hold mass (booked). The view
  only sees the new mass after the first contact; `charge` events for the guard tier are emitted then.
* **Stone evade** stays the legacy evade (tests pin it); Burrow Step is the directional `evade_hold`.
* Push / sink tiers come from the guard hold on the move's own tier clock (`KitEarth.guard_tier`).
* **Ram contest** (`wall_stone` × walls): CP ≥ K → the ram stops (the other wall takes 0.5·TP/CP damage); partial →
  both break; fail → the other wall breaks and the ram continues slower.
* **Reverse Tide authority** = cohesion(tier) + 0.25·min(mass / 40, 1.5) + grip margin 0.12: a 20 kg wave turns at
  ≤ ~3.5 m, a 45 kg wave can't be turned (wall it or evade).
* **Magma Hold** enables the core's insulated hold by setting `kit.magma` on the fighter for the hold only.
* **Rod Plant** is one body: a METAL ZONE `rod` with `props.barrier` (bolts entering its 6 m cylinder resolve against
  it). Bolts cast from *inside* the field are not drawn (core cylinder test ignores segments starting inside).
* **Sand cloud drag** applies once per (cloud, body) (`earth_drag`), not per zone tick.
* Wall specs (Dune Wall, Magma Curtain) carry no `counter.cls`: the wall body is the counter; a hit that reaches the
  fighter around it meets the legacy Earth guard (CP 10, perfect redirect).
* Mud walls collapse 4 s after turning (world ticks: guarded walls reset their age every tick).
* The satchel accepts up to 30 kg (recalls / catches); overflow stays at the fighter's feet.

## Requests to the core (not changed here)

1. `test_core_registry.test_legacy_bindings_and_fallbacks`, `test_register_bind_resolve_list_unregister`,
   `test_core_input.test_unbound_gesture_falls_back_to_the_strike` and `test_evade_hold_morphs_after_0_2_s_when_bound`
   assert that no kit binds sub-0 thrust / evade_hold or subs 1–3 strike — they fail as soon as a kit registers its
   MOVESET bindings (Earth and Water). Please run them with `Moves.BINDINGS` reset to the legacy bindings inside the
   scope (e.g. a `SimHarness.legacy_bindings()` helper), or on an element/sub without kit bindings.
2. `VerbMotion.stance_start / mode_start` and `VerbGrip.shape` emit fx with non-catalogued shapes (stance / kind
   names); the kit mutes them (`fx.aura = ""`, own Compress / Cool & Set) and emits its own cue.
3. `ActCommon.on_start` drops any held body not flagged `shield` after a guard spec started; non-water `held`
   barriers need that flag (the kit sets it for the Aegis plate). A generic VerbBarrier flag would be cleaner.
4. Any hit staggers and interrupts the action, so armor stances (Stone Skin, Iron Stance) end on the first light hit.
   A "super armor" immunity to light stagger for stances would make them read as intended.
5. An official way to extend a legacy def (`Moves.extend_legacy(id, keys)`) and a per-tick hook for untagged walls.
6. `VerbGrip._filter` excludes ZONE bodies; the kit converts lava pools / sand clouds into blobs when seized.

## Tuning notes

* Magma is strong in the open (globs + burn + re-pour): a T2 bomb then Magma Surge took a passive rival from 100 to
  ~45 in 7 s in the showcase; the counters are Swallow T1, spikes, sand, water. Watch Melt Pit + Curtain costs
  (a Curtain-then-Pit is 18 F + 270 HU).
* The satchel empties fast (Disc T2 6 kg + Lance 6–12 kg): Recall and plate scrap are the loop; Magnet Glide and
  Aegis need metal on the field / in the satchel.
* Chain Arc T0 only bends a stone shot (12 vs 17); T1 wraps it — intended (commit to catch).
* Sand Surge masses are high (30–75 kg of ground) because K = m·v/20 is the wave's power; they settle back in 1.2 s.

## Missing visuals (for the VFX stream)

From the showcase renders (2026-10-05, `--quality=2`, lavapipe): Stone and Magma read well on the existing stone,
earth-wall and lava-wave views (spears, rubble, the spike wall stopping a lava wave, the Bulwark ram, Magma Surge
waves, lava pools, slag, the Molten Lance slump). Not drawn yet (the sim emits them; views are owned by the VFX stream):

* **Metal bodies** (`mat metal`: tags `disc`, `lance`, `plate`, `rod`, caltrops ZONE, the held Aegis plate) — invisible,
  so Razor Disc / Iron Lance / Recall / Rod Plant / Lodestone Line / Magnet Glide target can't be read.
* **Sand and glass** (`mat sand / glass`): slugs, the `sand_surge` wave, Dune Wall (WALL `sand` / `mud` / `glass`),
  zones `sand_cloud`, `sandstorm`, `quicksand` — invisible.
* `spikes` walls use the full earth-wall block (should read as a low row of fangs); `spear` / `crag` use round stones.
* Magma Curtain's molten face (`heat_payload` on WALL `obsidian`) and the set obsidian look; `melt_pit` zone.
* Stance auras (`aura` cue with `on`) for Stone Skin, Iron Stance, Lava Wade, Sand Surf ride; `erupt` / `ring`
  cues of Rising Fangs / Earthrise / Burrow Step exits.
* Animation: new moves use the listed clips through the bridge; the ground-slot pose (`earth_wall`) sometimes reads as a
  dive on the new sink / ground moves.
