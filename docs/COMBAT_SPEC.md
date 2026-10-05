# Fourfold — Combat & Material-State Specification

Authoritative code: `game/core/` (state, rules) and `game/combat/` (moves).
Everything here is a **game rule**, not real physics. Where a rule is fantasy, it says so.

## 1. Simulation model

* Fixed step **60 Hz** (`CombatWorld.step`). Rendering interpolates between the last two states.
* The simulation is the **only movement authority** for fighters and material. Animation is in-place
  presentation; no root motion, no physics engine in the loop. Static geometry comes from `ArenaMap`
  (analytic boxes), so tests run headless and identically.
* Tick order (stable): timers → intents/action starts → action ticks → actor movement → **grip contests**
  (sorted by body id, strength desc, actor id) → bodies (thermal, phase, motion) → body/actor contacts
  (sorted) → regen → cleanup/caps.
* Determinism: seeded RNG only; the same seed + same intents → same state (soak test hashes final state).

## 2. Units

| Quantity | Unit | Notes |
|---|---|---|
| distance | m | arena 32×32 m |
| mass | kg (gameplay scale) | stone radius = 0.18·(m/10)^⅓ m (20 kg ≈ 0.45 m stone) |
| temperature | °C (game) | ambient 20, stone melts at 1000 |
| heat | **HU** | stone c = 0.01 HU/(kg·°C), latent 10 HU/kg; water c = 0.05, fusion 3.3, vapour 22 |
| Focus | 0–100 | regen 14/s after 0.7 s idle; generating heat costs 1 Focus per 10 HU |
| Balance | 0–100 | 0 → knockdown; regen 22/s after 1 s |
| Heat reserve | 0–500 HU | dissipates 12 HU/s |

A 20 kg stone needs ≈196 HU to reach 1000 °C and 200 HU more to fully melt (≈40 Focus generated from scratch).

## 3. Material bodies (`MatBody`)

One logical object with stable `id`. Fields: provenance (`origin`, `parent_id`, `lineage`, `absorbed`),
composition (`mat`: STONE / WATER / STEAM), `phase`, `form` (representation), `mass`, `temp`,
`liquid` (melt fraction for stone, 1−ice for water), kinematics (`pos`, `vel`, `max_speed`),
control (`controller`, `authority`, `residual_owner/authority`), attack instance (`attack_id`,
`hit_set`, damage), last interaction, `charge`, lifetime.

* **Forms never change identity**: CHUNK → BLOB (molten, held) → WAVE (poured) → CHUNK (cooled ridge).
  Views are swapped/cross-faded by `BodyViews`; the body is the same object.
* **Split** (`split_body`): child takes an explicit mass, same intensive state, records parent and lineage.
  Used by ice shatter and wall crumble.
* **Merge** (`merge_bodies`): larger absorbs smaller; mass, heat (exact thermal energy) and momentum conserved;
  absorbed id recorded. Used by puddles and pool.
* Caps: 32 bodies, 8 puddles, 6 inert remnants (oldest decays back into the ground; the ledger records it).

## 4. Thermal rules (`Thermal`)

* `apply_heat` moves heat in this order: sensible → latent → superheat (heating); superheat → latent → sensible (cooling).
  It returns what was actually applied, so callers charge exact costs. Extraction never cools below ambient.
* **Phase labels with hysteresis** (stone, on liquid fraction): SOLID→SOFTENED ≥0.15, SOFTENED→MOLTEN ≥0.80,
  MOLTEN→SOFTENED ≤0.55, SOFTENED→SOLID ≤0.05. Water: LIQUID→FROZEN ≤0.10, FROZEN→LIQUID ≥0.60. No flicker near a boundary.
* Passive loss to air ∝ m^⅔ × ΔT (molten 20 kg loses ≈43 HU/s). Molten mass held by a magma-capable fighter
  is insulated (upkeep 3 Focus/s).
* Lava on water: quench 1400 HU/s; the water flash-boils (`flash_boil`): mass leaves as steam, energy carried away is recorded.
* Wave speed = 7.5 m/s × flow factor (smoothstep of liquid). Cooling slows it; at SOLID it stops and becomes rock.

### Energy ledger
`system_energy()` (heat in bodies + reserves) always equals the start value + `ledger_balance()`:
`generated (from Focus) + ambient − vapour − reserve dissipation − vented − spent (attacks into air/actors)
+ freeze dump − removed`. Heat can only be **created** from Focus (bounded regen). Drawing heat costs Focus too,
goes into a **capped** reserve that leaks, and is spent by fire attacks or vented. Repeated heat/cool cycles therefore lose energy.

## 5. Control and contests

* Grip requests are queued during the tick and resolved after all actors acted, in a stable order. One winner per body per tick.
* Strength = base × distance falloff − mass penalty − low-Focus penalty. A challenger must beat the holder by
  **0.12** (hysteresis). A thrown body keeps decaying residual authority for its thrower (0.6, −1.7/s), so instant re-catches are hard.
* Mass above the fighter's control limit (80 kg) always fails with `control_fail(mass)`.
* Catching a moving body transfers momentum: pushback on the catcher and Focus ∝ impulse.
* Releasing as an attack creates a **new attack instance**; each actor can be hit once per instance (`hit_set` + `hits_taken`).

## 6. Actions

Each move (`Moves.DEFS`) has startup (anticipation, input acknowledged on frame 1), active, recovery and cancel rules.
Tap vs hold is decided at `max(startup, 0.18 s)`. Recovery can be cancelled into guard/evade only after the move's
`cancel` fraction. Held techniques/charges can always be cancelled by guard/evade (explicit cancel) or the cancel zone.
Presses during non-cancellable states are buffered for 0.15 s. Element switching affects only the **next** action.
Hits interrupt (light 0.26 s, heavy 0.5 s, knockdown 1.1 s + getup with i-frames). Interrupted actions drop held material with its state intact.

| Element | Attack (tap / hold) | Guard | Technique | Evade |
|---|---|---|---|---|
| Earth | Stone shot (rip 20 kg or reuse a loose stone) / heave 45 kg | Raises a 120 kg wall; guard pressed ≤0.18 s before a stone meets it **redirects** it at the thrower | Seize nearest stone in the aim cone (incoming preferred), hold, drag to aim, release to throw; rips one from the ground if none | sidestep |
| Water | Lash (arc, wets) / **ice lance** (freezes 4 kg; shatters into 2 fragments that melt into puddles) | Water shield from held water or the 6 kg waterskin; blocks fire (steam) | Draw from pool/puddle/waterskin, shape, release as a stream (becomes a puddle) | sidestep |
| Fire | Flare jab / blaze; with *lightning*: hold ≥0.65 s → bolt | Perfect guard absorbs flame heat into the reserve; with *redirect current*: returns lightning | **Thermal**: HEAT (magma grip a stone → melt; release molten = **pour lava wave**, not molten = throw hot stone) / DRAW (extract heat from lava or hot rock into the reserve) / VENT (dump reserve) | sidestep |
| Air | Palm gust (pushes, turns light projectiles <30 kg, disperses steam) / cyclone | Turns light projectiles, blocks fire | Updraft (≈2 m lift, reaches the high ledge); with *glide*: keep holding to glide | air dash 4.6 m |

Thermal mode rule (fixed, shown on the HUD before commit, never switches while held):
molten/hot stone not held by me → **DRAW** · solid stone → **HEAT** (needs *magma*) · water/ice → **HEAT** · nothing + reserve ≥40 → **VENT**.

## 7. Flagship exchange: stone → lava → wave → rock

1. Rival rips a stone (visible rise = telegraph, 0.24 s) and throws it (17 m/s, ≈0.7 s flight over 12 m).
2. Responses: sidestep (i-frames 0.14 s), hold guard (block), Earth guard timed to the wall (redirect), or **Fire technique** (magma grip).
3. Magma grip: startup 0.12 s, then a **0.25 s grip window** with 5 m reach. Too early (stone not in reach in the window) → whiff + recovery;
   too late (hit during startup) → hit. Heavier mass → weaker grip; >80 kg → fails. Catch costs Focus ∝ momentum.
4. Held stone heats at ≤650 HU/s (reserve first, then Focus): glowing cracks → softened → molten blob (same body id).
   Out of Focus → conversion stalls partway (softened); hit → dropped with its partial state.
5. Release while MOLTEN → **pour**: the same body becomes a ground WAVE (budget 6 m + 0.3 m/kg), following walkable ground,
   dropping off ledges (budget −1 m), stopping at walls/earth walls/rises > 0.3 m, quenching in water.
   *Fantasy rule:* while fluid, the pourer bends the wave toward their target (≤32°/s × fluidity).
6. Counter — **heat draw** (*fantasy technique, not real thermodynamics*): 0.35 s windup, 9 m range, line of sight,
   ≤260 HU/s (half at max range), costs Focus, needs reserve room. Lava crusts, slows, cracks and **sets into hot rock**
   (≈1000 °C, still the same body and mass). Hot rock burns on contact (≤1 per 0.8 s) and can be seized by either fighter (Earth).
7. Extracted heat goes to the drawer's capped reserve → spent on fire attacks or vented; it leaks 12 HU/s.

Overlaps resolve through the same rules: heating and drawing the same body both apply in actor order; control
contests use the hysteresis margin; partial cooling leaves a slower, softened wave; obstruction stops it.

## 8. Lightning (bounded conduction graph)

Built once per discharge. Nodes: pool, metal plate, liquid puddles, actors standing on them. Edges only by geometric contact
(puddle overlaps pool/plate/another puddle). Pool and plate do **not** touch in the lab, so they are separate unless puddles bridge them.
Stone, lava and ice never conduct. Charge ≥0.65 s (visible aim line + sound), range 14 m, cost 22 Focus.
Direct hit 24 (+50% if wet); conduction: max 4 hops, a fixed 26-damage budget split between reached actors (min 5 each),
the caster included if they stand in the connected water. One hit per actor per bolt. Barriers (arena walls, earth walls) stop it.
Counters: break line of sight, leave the water/metal, **grounded stance** (*game rule*: Earth guard on stone takes 40%),
or **redirect** (*redirect current* technique + Fire element + guard within the perfect window → bolt returns at 80%).

## 9. Opponent

`AiBrain` reads only observable state (bodies in flight, visible actions/telegraphs), perceives each threat after a
reaction delay (default 0.28 s ± jitter), decides once per threat, pays the same costs and uses only its kit
(`elements`, techniques). Tunables: `aggression`, `counter` (chance to pick the best counter), `reaction`, drills.

## 10. Extension plan (superseded by §11 and `docs/MOVESET.md`)

Metal (conductive manipulable bodies: `mat=METAL`, conductivity node, magnetic-style control), sand (granular split/merge
clouds using CLOUD form), mist (water CLOUD that conducts weakly and blocks sight), vortex (air CLOUD with angular
velocity bending projectiles). Each reuses MatBody state, splits/merges and the contest resolver.

## 11. Moveset extension: sub-elements, charge tiers and the counter rule

Design: `docs/MOVESET.md` (authoritative for moves, numbers, the counter matrix and the engine contract). Summary of the rules this adds:

* **Sub-elements.** Each element has four: Earth (Stone, Metal, Sand, Magma), Water (Water, Ice, Mist, Plant), Fire (Flame, Blue,
  Lightning, Combustion), Air (Gust, Vortex, Vacuum, Sound). Sub-element 0 of each element is today's kit, unchanged. Selecting a
  sub-element, like an element, only affects the next action.
* **Slots.** Every kit fills the same slots: `strike` (attack tap/hold), `thrust` / `ground` / `sweep` (attack flick up / down / side),
  `guard` (+ perfect), `push` / `sink` (guard flick up / down), `tech` (hold, aim, release; `T+A` shapes held material), `evade`, `evade_hold`.
  A move is `Moves.resolve(element, sub, slot)`; gestures morph a running attack startup once (first 0.12 s, or at charge release).
* **Charge tiers.** T0 tap (< 0.18 s), T1 at the move's `heavy_min` (default 0.40 s), T2 1.00 s, T3 1.80 s (per-move `tier_times`); Focus drains
  while charging (`charge_drain`); an empty pool stalls the tier, never drops the charge; hits interrupt; every tier-up emits `charge`.
* **Power and counters.** Threat power TP = K (`m·v/20`) + H (`heat/20`) + C + E + P (rule-weighted); counter power CP = barrier
  `mass·hardness` or the move's tier power, ×1.5 when perfect, × efficacy for the material pair. `ratio = CP_eff / TP`: ≥ 1 full outcome,
  0.5–1 partial (subtractive weaken, bend, slow, partial transform), < 0.5 the counter breaks and the threat continues weakened.
  Special bands (wind vs fire: fan / deflect / extinguish; lightning vs barriers: ground / blast through). All heat and mass moved by an
  outcome goes through the ledgers.
* **Legacy behaviour is a set of rules**, not a special case: guard chip (12 % / 55 %), perfect deflect, Earth redirect, wall blocks
  waves and bolts (E ≤ CP), grounded stance (40 %), redirect current (80 %), water-shield steam block, air guard vs fire, flame absorb,
  gust deflection, lava quench, heat draw — all keep their tested numbers.

The engine API (registry, `Agent` / `Interactions`, verbs, hooks, events) is documented by the core implementation in an "Engine" section
below once it lands; `docs/MOVESET.md` §15 is the contract it implements.

## Engine (moveset core, implemented)

Code: `game/core` (`sim.gd`, `materials.gd`, `charge.gd`, `agent.gd`, `interactions.gd`, `core_rules.gd`, `outcomes.gd`,
`status.gd`, `fx_events.gd`, `combat_world.gd`, `conduction.gd`, `thermal.gd`) and `game/combat` (`moves.gd`, `verbs/*`,
`kits/<element>/kit_<element>.gd`). Kits, VFX, audio, AI and UI plug in **without editing these files**. Tests:
`tests/sim/test_core_*.gd`; harness helpers in `tests/sim/sim_harness.gd`; timing soak `tests/sim/perf_core.gd`.

### E1 Registry and bindings (`Moves`)

| API | Meaning |
|---|---|
| `Moves.BASE_DEFS` | const, today's moves with unchanged values |
| `Moves.DEFS` | static var: BASE_DEFS + kit registrations + Lab overrides (`Moves.DEFS.pour` keeps working) |
| `Moves.ensure()` | idempotent; `CombatWorld._init` calls it. Order: legacy bindings, `Interactions.ensure()` (core rules), `Status.ensure()`, `Verbs.ensure()`, `KitEarth/KitWater/KitFire/KitAir.register()` |
| `Moves.register(id, def)` / `unregister(id)` | kits; legacy ids are refused; missing keys get `module "verbs"`, startup/active/recovery 0, `name = id` |
| `Moves.bind(element, sub, slot, id)` / `unbind(...)` | slots `Sim.SLOTS`: strike thrust ground sweep guard push sink tech evade evade_hold |
| `Moves.resolve(element, sub, slot) -> id` | exact binding -> the sub-0 binding -> legacy id -> "" |
| `Moves.list(element, sub)`, `Moves.slot_of(element, sub, id)` | move list UI |
| `Moves.set_override(id, key, value)`, `clear_overrides()`, `overrides()` | Lab live tuning (nothing changes until called) |
| `Moves.save_state()` / `load_state(st)` | snapshots (tests use `SimHarness.begin_scope()/end_scope()`) |

Sub-0 bindings: Earth `earth_attack / guard / earth_tech / evade`, Water `water_attack / guard / water_tech / evade`, Fire
`fire_attack / guard / fire_tech / evade`, Air `air_attack / guard / air_tech / air_dash`. A slot unbound for a sub falls
back to the sub-0 binding; an unbound gesture slot (thrust/ground/sweep) plays the strike.

**Def schema** (core reads these; kits may add keys): `name, desc, element, sub, slot, module ("verbs" | "kit_earth" |
"kit_water" | "kit_fire" | "kit_air" | legacy "common/earth/water/fire/air"), verb, startup, active, recovery, cancel, chain,
counter_cancel, heavy_min, tier_times, charge_drain, cost (Focus), heavy_cost, cost_add, heat (HU), heat_add, water (kg),
metal (kg), upkeep, tiers {t1, t2, t3: {param: value}}, counter {cls, power: [t0..t3] | number}, threat {cls}, move_channel,
move_startup, anim, anim_hold, anim_active, fx {cast, release, impact, mat, shape, <key remaps>}, sfx {}, ai {role, range, tags},
hook_execute / hook_tick / hook_impact (Callables)` + the verb parameters (E6). Guard specs: a def bound to the `guard` slot;
the running action stays `"guard"` with `inst.data.spec` (id) and `inst.data.spec_def`; sub 0 keeps the legacy wall / water shield.

### E2 Input to move (`CombatWorld`)

* `ActorIntent` / `InputFrame` / `PlayerController`: `sub_select` (-1 or 0-3), `attack_gesture`, `guard_gesture`
  (`Sim.Gesture {NONE, UP, DOWN, SIDE}`), `evade_held` (level; `clear_edges` keeps it). `ActorState.subs[4]`, `sub()`, `sub_of(e)`,
  `subs_unlocked`. Sub switching affects the next action only and emits `element {actor, element, sub}`.
* Press + gesture on one tick -> slot (UP thrust, DOWN ground, SIDE sweep) -> `Moves.resolve(element, sub, slot)`.
* **Gesture morph**: a gesture during the first 0.12 s of an attack startup, or with the release of a CHARGE, morphs the running
  attack once (`morph_action(a, id, slot, it, at_release)`, public for kits): elapsed time, paid Focus (`paid_focus` credit),
  paid heat (`heat_paid`), tier and the held body (`data.morph_body`) carry over. Event `morph {actor, from, to, slot, tier, at}`.
* `guard_gesture` UP/DOWN while a guard channels -> push / sink (`data.from_guard, guard_t, tier, keep_wall, held, guard_spec`);
  the wall stays maintained and the held body kept. A plain attack press during a guard still cancels it into an attack.
* `evade_held` for 0.2 s morphs the evade into `evade_hold` when bound.
* Cancels (MOVESET §9.1) are data keys: `chain` (fraction of recovery after contact; whiffs only after `cancel`; <= 3 moves, each
  slot once; events `chain`), weave (another element/sub inside a chain window: +6 Focus, once per string, event `weave`),
  `counter_cancel` (into tech/guard when a hostile body arrives within 0.5 s and the tech context is ok: +8 Focus). Moves without
  these keys keep today's rules exactly. `ActionInst` gains `sub`, `slot`, `tier()`; `action` events gain `sub, slot, tier`.

### E3 Charge (`Charge`)

`tier_for(def, held_s)` (times `[heavy_min or 0.40, 1.00, 1.80]` or `tier_times`; heavy_min gates T1; max tier = highest `tN`
in `tiers`, 1 for legacy `heavy_min` moves) · `param(inst, key, default)` / `pget(def, tier, key, default)` (`tiers.t<n>` ->
**lower tiers** -> def -> default: a ladder, t3 inherits t2/t1) · `counter_power(def, tier)` · `held(inst, it)` (input by slot) ·
`tick(w, a, inst, it)` (runs for every CHARGE/CHANNEL: tier-ups emit `charge {actor, move, element, sub, tier, ready, slot}`;
from T1 drains `charge_drain` (8) Focus/s for defs with t2/t3; empty -> stall with one `insufficient {reason: "charge"}`) ·
`progress(inst) -> Vector2(tier, frac)`. Tier lives in `inst.data.tier`; released bodies keep it (`MatBody.tier`, cohesion
`0.6 + 0.1·tier`). Guards and techniques use their hold as the tier clock (guard specs thicken: `tiers.tN.mass`).

### E4 Interaction engine (`Agent`, `Interactions`, `Outcomes`, `CoreRules`)

* `Agent.of_body(w, b, against = null)`, `of_volume(w, actor, inst, cls, pos, dir, {K,H,C,E,P, heat_hu})`, `of_hit(w, info)`,
  `of_guard(w, actor)`, `of_move(w, actor, move_id, tier, perfect)` (pure: `def.counter` or a barrier spec), `of_env(w, kind, pos)`
  (`pool puddle plate arena_wall ground`), `of_stance(w, actor)` (anchor). Fields: `kind, cls (threat), ccls (counter), body, actor,
  inst, def, pos, dir, ch {K,H,C,E,P}, mass, speed, heat (HU budget of a volume), tier, perfect, power, hostile, data`.
* `Interactions.classify(b)`, `counter_class(b, w)`, `is_barrier(w, b)`, `family(cls)`, `counter_family(ccls)`,
  `register_tag_class(tag, threat_cls, counter_cls)`, `register_channels(tag, cb)`, `threat_power(threat, rule, cfam)`,
  `counter_power(counter)`, `rule(t, c, tier, fallback)`, `has_rule`, `add_rule(t, c, rule) -> bool`, `can_add`, `remove_rule`,
  `all_rules()`, `allows(body, ccls)` (technique legality), `predict(w, threat, counter) -> {outcome, band, ratio, tp, cp, cp_eff,
  eff, perfect, rule, rule_id, to}` (pure), `resolve(w, threat, counter, ctx, fallback)` (applies + emits `interaction`),
  `register_outcome(name, cb)`, `cohesion(tier)`, `disrupt_threshold(tier)`, `save_state/load_state`.
* TP: K = m·v/20, H = heat above ambient/20, C = cold/20 (weight 1 only vs heat-family counters), E = `MatBody.charge` or volume
  E, P = `MatBody.power` (zones, air bodies; `props.channel` moves it to another channel). CP: explicit power (move tier / zone
  power / guard) else barrier `mass × hardness` (`Materials.hardness`: stone .25, obsidian .33, glass .30, ice .44, sand .25,
  metal 3.3, vine .4, held water 1.0; `MatBody.hardness` overrides; `props.cp_bonus`), plain guard 10, Wind Guard 12; ×1.5
  perfect (`perfect_mult`), × `eff` (× `eff_insulator` on insulating bodies).
* **Rule format**: `outcome, perfect, partial, fail, bands [[min_ratio, outcome], ...], eff, eff_insulator, w {K,H,C,E,P}, full_at 1,
  partial_at 0.5, absorb_on_fail 0.5, perfect_mult 1.5, when {mass_lt, mass_max, mass_min, hostile, mats, tags, forms} + else,
  inert (+ inert_else) for non-hostile bodies, by_form {form: outcome}, tiers [..] (counter tiers it applies to), aura (ignores
  guard facing), fallback, to, target ("threat" | "counter"), stops, legacy, id` + handler params (chip bal knock perfect_balance
  perfect_range absorb_reserve requires aim speed_mult min_speed wall_push residual dmg_div side up verb kind share rate energy
  hu_per_pu factor event guard_kind angle bend_impulse push_mult amp release_speed max_captured pieces keep heat_share heat_mult
  even_at). Lookup `t|c -> t|cfam -> tfam|c -> tfam|cfam -> *|c -> *|cfam -> t|* -> tfam|* -> default`; per key, rules with
  `tiers` are tried first, then in registration order (deterministic). A later non-legacy rule with the same key and tiers replaces
  the earlier one; a **legacy cell can never be replaced** (`add_rule` returns false, `push_error` + debug assert).
* **Bands**: full >= 1 -> outcome (perfect -> `perfect`), partial 0.5-1 -> `partial` (default weaken), fail -> `fail` (default
  overwhelm: the counter breaks, the threat continues with TP - 0.5·CP_eff); custom `bands`. Defaults: `DEFAULT_RULE` (block /
  weaken / overwhelm), `CLASH_RULE` (projectile <-> projectile, wave <-> wave), `PASS_RULE` (zones, waves meeting bodies, volume
  sites).
* **Outcomes** (`Outcomes.apply`): block deflect redirect reflect reclaim capture absorb transform (steam water rock hot_rock
  obsidian lava molten_metal ice snow mist glass sandstone mud ash) shatter sink conduct ground pass amplify extinguish weaken (every
  channel × (TP-CP)/TP, heat booked: water counters boil, others ambient) bend (60°·f, or legacy impulse) slow overwhelm clash disrupt
  neutralize + legacy helpers heat push disperse. Each sets `res.result` (actor-guard result), `stopped`, `pass_scale`, `knock_scale`,
  `counter_broken`, `heat_used`, `absorbed`, `conduct(_to)`, `shielded`, `reflected`.
* **Contact sites**: projectile -> actor (`hit_actor`: the guard is `Agent.of_guard`; stance anchor vs pressure; armor; immunities),
  projectile -> WALL (`_body_hits_wall`), projectile -> arena solids (env `arena_wall`; `props.ricochet` bounces first),
  projectile <-> projectile and wave <-> wave (`_clash_pass`), body <-> ZONE (`_zone_pass`, per pair every `props.rate` s, 0.1),
  tagged wave -> bodies on its front (`_wave_sweep`), wave -> walls / pool / puddles, cones / beams / bursts -> actors and bodies
  (`VerbVolume.meet_body`: barrier bodies counter the volume, loose bodies are countered by it), lightning path
  (`Conduction.barriers_on` + resolve: ground / shatter-and-continue; arena solids always stop it), technique legality
  (`Interactions.allows`: `grip_stone`, `grip_water`, `draw_heat`, `heat_grip`, `heat_ranged`).

#### E4.1 Class catalogue
Threats: `stone stone_heavy (31-80 kg) boulder (>80) hot_rock magma lava_wave metal molten_metal sand sand_cloud sand_surge glass
water water_wave ice mist steam vine flame blue_fire fire_field gust tornado vacuum ember puddle pool wall_*`; volumes `flame
blue_fire lightning blast gust sound water sand steam vacuum frost`. Counters: legacy actor guards `guard guard_earth shield_water
aura_flame guard_wind`; barriers `wall_stone wall_obsidian wall_glass wall_sand wall_mud wall_ice wall_vine plate_metal shield_water
screen_steam fog aura_blue ward_static guard_blast wall_vortex bubble_null barrier_sound spikes rod anchor`; active `swallow quicksand
melt_pit grip_* heat_grip heat_ranged draw_heat freeze condense wave_water wave_sand wave_lava rime gust tornado vacuum_well suction
flame blue_fire lightning blast sound water_jet spray sand_cloud frost`; environment `pool puddle plate arena_wall ground`. A kit
zone with an unknown tag uses its tag as class. Families: threats `solid_light solid_heavy molten liquid granular vapor plant heat
electric pressure sonic cold`; counters `guard barrier_solid barrier_soft barrier_energy liquid cold heat electric pressure sonic sink
grip anchor`.

#### E4.2 Who owns which cells
**Core** (`CoreRules`, all `legacy: true` unless noted): legacy guards × legacy threats + `*` (plain guard: chip 12 % / 55 %, knock
35 %, guard break; perfect deflect, attacker within 3 m -18 balance; perfect Earth redirect ≤ max_control_mass ×1.05; perfect fire
guard keeps 50 % flame heat; redirect current 80 %; air guard vs flare clean block `fire_air`; Wind Guard power rule for light
solids/water/heavy stones), `wall_stone` (block with wall_damage += momentum/900, perfect redirect, waves settle `blocked`, bolts
ground at E ≤ 30 else shatter), `gust` T0-T1 (light hostile shots turned and re-owned, heavy bend 60/m, loose light bodies pushed,
clouds dispersed, waves/puddles/walls untouched), `water_jet` T0-T1 (lash deflects ≤ 25 kg), flare cells (water takes 60 %: steam
/ melt + `steam_block`; stones 50 %), lava quench (`pool`/`puddle`: transform rock at 1400 HU/s, flash boil), lightning (`arena_wall`
ground, grounded stance `ground` 40 %, `pool/puddle/plate` conduct, non-legacy `electric × barrier_*` ground/shatter with insulators
×1.5), technique legality cells, environment (`*|arena_wall` block; non-legacy liquid absorbed by pool/puddle, pass otherwise) and the
non-legacy anchor cell (`pressure|anchor`). **Earth, Water, Fire, Air kits**: the cells of their own counter classes (their column
of MOVESET §8; §15.4 list) with `Interactions.add_rule`, restricting `tiers` where they refine around a legacy fallback (e.g. Gale
T2/T3 cells for `gust`).

### E5 Materials and ledgers
`Materials.PROPS[mat]`: c, melt, latent, max_temp, hardness, conductive, brittle, porous, flammable, magnetic, insulator (+ water
hardness_frozen, plant ignite / burn_rate, fire fire_decay); helpers `prop, is_fusible, c, melt, latent, max_temp, hardness(b),
is_brittle, conducts, conduction_factor, insulates, is_flammable, is_magnetic`. Thermal treats metal / sand / glass like stone with their
constants (metal melts at 1200 °C -> molten_metal; sand fuses at 1200 °C and sets as GLASS when it cools: `convert_mat`, energy exact),
plant heats sensibly and burns above 250 °C (`burn_plant`, ledger `burned`), FIRE bodies hold `heat_payload` (counted by
`thermal_energy`, decays 35 %/s to ambient, or spent by a burst). `system_energy()` also counts heat a running move paid and still
carries (`inst.data.heat_paid`). Mass ledgers added: `metal_taken metal_returned water_to_plant plant_from_ground plant_returned
moisture_taken burned sand_to_glass glass_to_sand sand_to_sandstone`; `earth_mass()` (stone+sand+glass), `metal_mass()` (incl.
satchels), `plant_mass()`; `water_mass()` adds `water_to_plant - moisture_taken`; `stone_mass()` unchanged. Booked helpers:
`heat_body, boil_water, quench_energy, split_body, merge_bodies, decay_body, convert_mat, burn_plant, grow_plant, spawn_zone,
close_zone, release_captured`.

### E6 Verbs (`game/combat/verbs`, module `"verbs"`)
Lifecycle `Verbs.on_start / after_startup / on_phase / on_tick / on_interrupt / execute`; costs `Verbs.pay(w, a, inst, "start" |
"release" | {focus, heat, water, metal})` (morph credits first; tech Focus × deafened); helpers `target_point, launch_vel(from, to,
speed, gravity_scale), arm(w, a, inst, body, damage, balance), fx(w, a, inst, key, extra), take_heat(inst)`. Every parameter goes
through `Charge.param` (tier ladder).
* **projectile** (`VerbProjectile.fire`): source ground|waterskin|metal|moisture|heat|held|none, mat, tag, form, frozen, mass, count,
  spread, speed, mass_speed, gravity, arc, reach, homing, pierce, ricochet, on_impact shatter|stick|burst|puddle|sprout|zone (+ impact_*),
  damage, balance, heat, temp, charge, power, life, hit_status(_t).
* **cone** (`VerbVolume.cone`): cls, channel, power, range, angle, damage, balance, knock, lift, heat, status(_t, _mag), wet.
* **beam**: range, width, pierce, pulse (sustained), conduct / cls lightning (through `Conduction.discharge` with E = power,
  conduct_budget, max_hops, allow_redirect).
* **burst** (`burst`, `burst_at(w, a, inst, pos, prm)`): at self|ahead|aim, distance, range, radius, power, damage, balance, knock,
  lift, cls, status, fuse (a `fuse` zone, re-triggerable with `props.trigger`).
* **ground_line** (`VerbGroundLine.launch`): tag, mat, source, mass, speed, budget, width, steer, knock, lift, kind, power, channel,
  viscous, leave_zone (+ zone_radius / zone_life / zone_power), trail_zone (+ trail_radius / trail_life), hit_status.
* **barrier** (guard spec; `VerbBarrier.start / tick / end / raise_free`): barrier wall|held|aura|zone, mat, tag, source, mass,
  hardness, half, dist, rise, upkeep, radius, height, stops_bolts, counter {cls, power}; tiers thicken it (booked).
* **zone** (`VerbZone.spawn`) / **summon** (`summon_start / tick / release`): tag, radius, life, power, channel, mat, mass + source,
  at, distance, range, attach, height, walk_height, friction, surface, actor_status, status_t, status_mag, dps, ground_only, rate,
  barrier, ccls, cls, walk_speed, spare_owner, drag; summon adds steer_speed, linger, upkeep (tier from the technique hold).
* **grip** (`VerbGrip.tick / shape / throw / drop / preview`): ccls, reach, cone, base, grip_mult, max_mass, rip_time, rip_source
  (ground|metal_plate|waterskin|moisture|none), rip_mat, rip_mass, rip_cost, speed, damage, balance, gravity, shape
  (split|freeze|compress|cool|condense|retag; T+A), pieces, spread, shape_tag, shape_cost, upkeep, mode_label.
* **ranged_heat** (`VerbHeat`): range, cone, rate (HU/s, paid like Fire), slump_fraction (0.25), slump_at (0.5), target_temp.
  **Wall slump**: the heated 25 % face is split off on the caster's side; at liquid ≥ 0.5 it slumps into a molten body there and
  the rest crumbles (event `slump`); a channel ended early merges the face back.
* **dash** (`VerbMotion.dash_*`): distance, active, iframes, dir stick|aim|toward|back, up, hidden, burrow, trail.
* **mode**: kind glide|surf|skate|flight|hover|burrow|run, speed_mult, height, glide_fall, glide_speed, upkeep, status.
* **stance**: stance, armor, anchored + anchor_cp, status, upkeep, speed_mult, held (false = timed by `active`).

### E7 Hooks and statuses
`CombatWorld.register_body_tick(tag, cb(w, b, dt) -> bool)`, `register_zone_effect(tag, cb(w, zone, dt))`, `register_status(name,
spec)`, `register_tech_preview(element, sub, cb(w, a, dir) -> {mode, body, ok, reason})`, `tech_preview(actor, dir)` (registered ->
legacy Fire HEAT/DRAW/VENT -> grip-verb preview), `unregister_hooks(tags)`; queries `bodies_in_zone(z)`, `actors_in_zone(z)`,
`incoming_threat(a, within)`, `zone_surface_at(p)`. Statuses (`Status.apply / remove / has / tick / speed_mult / recovery_mult /
friction_mult / tech_cost_mult / rooted / armor / immune / lock_blocked / hidden`): `wet` (mirrors wetness > 0.3) `burning chilled
frozen rooted slowed muddy slick blinded concealed deafened shocked anchored armored levitating charged`; spec keys speed, recovery,
friction, armor, tech_cost, dps, rooted, immune [burn conduct lift knockback pull ground], no_lock, hidden. Applied in movement
(speed, root, friction, ZONE surfaces with `walk_height` / `friction` / `surface`), hits (armor vs kinetic, knockback/lift
immunity, anchor counter vs pressure), targeting (blinded, concealed beyond 2 m), conduction (`conduct` immunity), costs (deafened).

### E8 Events (`FxEvents`)
New: `charge`, `fx {fx, mat, shape, actor, body, element, sub, move, tier, pos, dir, radius, length, angle, height, path, dur, power,
seed (+ on)}` (`FxEvents.FX` cast release cone beam burst ring erupt trail splash aura; `MATS`; `SHAPES`), `interaction {threat,
counter, outcome, band, ratio, tp, cp, perfect, pos, dir, threat_actor, counter_actor, threat_body, counter_body, to, rule, tier}`,
`status {actor, status, on, t, mag}`, `zone {body, kind, phase open|close, radius, owner, pos, tier}`, `morph`, `clash`, `chain`,
`weave`, `counter_cancel`, `capture`, `release_captured`, `slump`, `wall_face`, `shape`, `sink`, `ricochet`, `pierce`, `stick`,
`convert`, `overwhelm`, `mode`, `stance`, `barrier_grow`. `hit`, `block`, `impact` gain `power, mat, tier, dir`; `action` gains
`sub, slot, tier`; `element` gains `sub`. Body tags: `FxEvents.BODY_TAGS`; `FxEvents.is_known(kind, key)`, `fx / fx_for / charge /
zone / status / mat_of(b)`. All legacy events keep their names and fields.

### E9 Example: a kit registering one move, one rule, one body tick
```gdscript
static func register() -> void:                                   # KitEarth (Earth/Metal = sub 1)
	Moves.register("razor_disc", {"element": 0, "sub": 1, "slot": "strike", "verb": "projectile",
		"startup": 0.17, "active": 0.07, "recovery": 0.27, "cancel": 0.6, "chain": 0.25, "cost": 5.0,
		"source": "metal", "mat": "metal", "tag": "disc", "mass": 2.0, "speed": 24.0, "homing": 10.0,
		"damage": 9.0, "balance": 12.0, "fx": {"mat": "metal", "shape": "disc"},
		"tiers": {"t1": {"count": 2}, "t2": {"count": 3, "ricochet": 1}, "t3": {"count": 5}}})
	Moves.bind(Sim.Element.EARTH, 1, "strike", "razor_disc")
	Interactions.add_rule("metal", "plate_metal", {"outcome": "block", "perfect": "reclaim",
		"partial": "weaken", "id": "aegis_vs_metal"})
	CombatWorld.register_body_tick(&"disc", func(w: CombatWorld, b: MatBody, dt: float) -> bool:
		b.spin = 40.0                                              # the view reads spin; default motion continues
		return false)
```

### E10 Deviations from MOVESET §15 and documented behaviour changes
* `Charge.param` is a ladder (`t3` falls back to `t2`, `t1`, then the def) rather than `t<n>` -> def only.
* Extra classes: actor guards `guard_earth` (Earth sub 0) and threat family `cold`; families `guard`; zone tags as classes;
  extra outcomes `heat`, `push`, `disperse` (legacy gust/flare semantics).
* The legacy water shield and the flame: `(flame, shield_water)` is the steam cell (a shield guard blocks the flame cleanly,
  `block kind fire_water`) instead of the plain-guard chip when only the fighter (not the shield body) is in the cone.
* **Air guard vs projectiles** (no test pinned the old rule): instead of "deflect anything < 30 kg", the Wind Guard counters by
  power (CP 12 × eff 1.5 light solids / 1.0 water / 2.0 metal / 0.6 heavy stones): deflect when CP_eff ≥ TP, a perfect guard
  returns the projectile to its thrower (`perfect_deflect verb reflect`), otherwise a plain guard block.
* New physical interactions in normal play: projectiles of different owners clash; thrown charged bodies keep cohesion
  `0.6 + 0.1·tier` (0.6 at T0 = legacy); conduction reaches conductive bodies (metal, held water, fog at 60 %, charged bodies)
  — legacy scenes have none of these, so all 188 legacy tests keep their meaning unchanged.
* `MAX_BODIES` stays 32 and also counts zones (zones and captured bodies are trimmed last).
* Perf (headless, this container, 2 fighters, legacy + every verb, random input, 120 s): sim step mean 0.20 ms, p95 0.32 ms,
  p99 0.44 ms (`tools/scripts/godot.sh --headless -s res://tests/sim/perf_core.gd`).
