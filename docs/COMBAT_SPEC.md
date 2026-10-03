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

## 10. Extension plan (not implemented)

Metal (conductive manipulable bodies: `mat=METAL`, conductivity node, magnetic-style control), sand (granular split/merge
clouds using CLOUD form), mist (water CLOUD that conducts weakly and blocks sight), vortex (air CLOUD with angular
velocity bending projectiles). Each reuses MatBody state, splits/merges and the contest resolver.
