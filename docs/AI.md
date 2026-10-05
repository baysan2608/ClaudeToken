# Fourfold — Opponent AI (matrix-driven)

Code: `game/actors/ai_brain.gd` (`AiBrain`: perception, execution, offense, drills), `game/actors/ai_planner.gd`
(`AiPlanner`: counter enumeration, feasibility, utility), `game/actors/ai_presets.gd` (`AiPresets`: difficulty table).
Design: `docs/MOVESET.md` §13 (and §5, §8, §15.3). Tests: `tests/sim/test_ai_*.gd`, `tests/sim/test_regressions_ai.gd`.

The AI keeps its honesty: it reads only what a player can see (bodies in flight, waves, zones, the rival's visible
wind-ups and charge tiers, statuses that have a visual), perceives each threat after `reaction` ± jitter, decides
**once per threat**, pays the same costs through the same `ActorIntent` path as the player (element/sub switch,
press, gesture, hold, release) and uses only its configured kit. It never reads the rival's intent, buffered presses,
RNG, or another brain's plan.

## Config API (for the UI / Lab)

```gdscript
var ai := AiBrain.new(world, actor, cfg_dict, seed)        # legacy constructor, unchanged
ai.configure({"preset": "adept", "elements": [0, 2], "subs": {0: [0, 1, 2, 3], 2: [0, 2]}, "drill": ""})
AiBrain.PRESETS                                             # {"novice": {...}, "adept": {...}, "master": {...}}
AiBrain.preset_names()                                      # ["novice", "adept", "master"]
AiBrain.DRILLS                                              # ["", "passive", "stone_rain", "seize", "lightning", "matrix", "element:<e>/<sub>"]
ai.describe() -> Dictionary                                 # current config (preset, elements, subs, drill, planner on/off)
ai.debug_state                                              # one-word state for the debug overlay ("counter swallow T1", "offense spark", ...)
ai.last_plan                                                # last counter decision {id, element, sub, slot, tier, perfect, outcome, band, ratio, utility, threat}
```

`configure(opts)` keys (all optional; unknown keys are merged into `cfg` so every legacy tunable still works):

| Key | Type | Meaning |
|---|---|---|
| `preset` | `"novice" \| "adept" \| "master"` | loads the preset row below (then the other keys override it) |
| `elements` | `Array[int]` (0 Earth, 1 Water, 2 Fire, 3 Air) | kit elements. Omitted: the preset's element count, starting from the fighter's current element |
| `subs` | `{element: Array[int]}` or `Array` of 4 arrays | sub-elements per element. Omitted: the preset's sub count (0, 1, ... in order) |
| `drill` | String | `""` free sparring · `"passive"` · `"stone_rain"` · `"seize"` · `"lightning"` (legacy) · `"element:<e>/<sub>"` · `"matrix"` |
| `interval` | float s | drill cadence (default 2.6) |
| `reaction`, `counter`, `misjudge`, `timing_err`, `aggression` | float | override one difficulty number |
| `unlock` | bool (default true) | also unlock the configured elements/subs (and the preset's techniques) on the AI's own fighter |
| `kit` | Dictionary | extra technique flags merged into the fighter's kit (`magma`, `heat_draw`, `lightning`, `redirect_current`, `glide`) |
| `only` | `Array[String]` | Lab / tests: restrict the counter candidates to these move ids (evade stays available) |

`element:<e>/<sub>` accepts numbers or names: `"element:2/1"`, `"element:fire/blue"`. It makes the AI spam that
sub-element's threats (every attack slot and summon at mixed tiers) so the player can practise the answers.
`matrix` cycles through every registered threat class (one launch per `interval`, every kit move with a `threat` cls,
grouped by class), whatever kit is configured.

Calling `configure` switches the brain to the **planner** (matrix-driven) mode. A brain built with the legacy
constructor and never configured keeps the legacy decision rules exactly (`tests/sim/test_regressions_ai.gd` and
`test_ai_flagship.gd` pin them). The UI should guard with `if ai.has_method("configure")`.

## Presets (MOVESET §13)

| Preset | reaction | counter (picks the best) | power misjudge | perfect-timing error | kit | offense |
|---|---|---|---|---|---|---|
| `novice` | 0.45 s | 0.30 | ±40 % | ±0.12 s | 1 element, 2 subs | single moves, slow cadence (aggression 0.35) |
| `adept` | 0.30 s | 0.65 | ±20 % | ±0.07 s | 2 elements, 4 subs | 2-hit chains, environment reads (wet → lightning, plate/pool → conduction) |
| `master` | 0.20 s | 0.90 | ±8 % | ±0.03 s | 4 elements, 4 subs | 3-hit chains + weaves, punishes recoveries and over-charges (Disrupt / interrupt), barrier answers |

Preset rows also carry `aggression`, `chain` (max string length), `weave`, `punish`, `env`, `tiers` (highest tier
it charges on offense) and `kit` (technique flags). The table is `AiPresets.TABLE` (also exposed as `AiBrain.PRESETS`).

## How it decides (planner mode)

1. **Perceive.** Threat sources: hostile projectiles closing on the fighter, waves/ground lines heading at it, hostile
   zones around or moving at it, and the rival's visible wind-ups (startup / charge / channel of an attack move whose
   reach covers the fighter; charge tiers read from `Charge.progress`). A threat becomes actionable `reaction` ± jitter
   after it is first seen (pour wind-ups and charges start the clock early, as before). One decision per threat key.
2. **Enumerate counters** (`AiPlanner.candidates`): every move of the configured kit bound to a slot whose def has a
   `counter` (guards always; techniques through `CombatWorld.tech_preview` for the context mode, e.g. Fire DRAW),
   at every tier the move can reach, plus perfect variants of guards. Each is scored with
   `Interactions.predict(threat, Agent.of_move(...))` against the *perceived* threat (TP × (1 ± misjudge)).
3. **Feasibility**: time to impact vs element/sub switch (1 tick) + current recovery (cancel windows for guard/evade)
   + startup + charge hold to the tier (+ the guard hold before a push/sink); the counter's reach (cone/beam range,
   grip reach) and line of sight; Focus, heat (reserve or Focus), carried water and metal.
4. **Utility** = outcome value × success probability − cost. Values (MOVESET §13): REC 1.0 > DEF↩ (redirect/reflect)
   0.9 > XFM/SNK/CAP/ABS/EXT/GND 0.8 > BLK/DEF 0.6 > partial > evade 0.25 × P(escape) > fail/pass/amplify −1.
   Kit outcomes (`earth_*`, `water_*`, `fire_*`, `air_*`) are valued from the **counter's** side in `AiPlanner.KIT_VALUE`
   (e.g. `water_burn` = your vines burn = −1, `fire_conduct_owner` = the bolt shocks the stream's owner = 0.85,
   `earth_ram_push` = 0.9); unknown names fall back to keywords, then the band.
   Deviations from the §13 table, found in play: a plain-guard block (rules with `chip`) is worth 0.35 (chip damage
   and balance loss; vs waves, zones and volumes a perfect plain guard is too), `heat` 0.2 and `push` 0.4 (they do
   not stop a threat), partial outcomes `bend` 0.2 / `weaken` `slow` 0.15 (the threat still lands, so a clean
   evade, 0.25 × P(escape), is usually better).
   Perfect variants: success = timing error vs the 0.18 s window; a missed window falls back to the plain outcome.
   Cost = Focus (+ heat/10) / 100 + 0.02 per element/sub switch + hold time. Ties prefer the higher outcome value.
5. **Difficulty**: with probability `counter` the best option is taken, otherwise a random feasible one (or evade).
   Perfect-window counters (redirects, Return Wind, Magnet Catch, Flash Freeze, Echo Return, Return Current) are pressed
   at contact − 0.09 s ± `timing_err`, like the legacy redirect.
6. **Offense** (`AiPlanner.offense`): every attack-slot / summon move of the kit with an `ai.role` is scored by range band
   and the target's visible state — wet → lightning / conductors, on the plate or in the pool → conduction, near an arena
   wall → push/knockback (splat), airborne → fast juggle hits, charging → Disrupt / quick interrupt, behind a barrier →
   Storm Bolt (T2 spark), Melt & Return (heat the wall), sound bank shot, summons around cover. Chains continue after
   contact (`chain` fraction of recovery, each slot once), weaves switch element/sub inside the window (Master).
   The legacy `interval`/`aggression` cadence is kept.

New kit moves are picked up automatically: give them `counter {cls, power}`, `threat {cls, power}` and
`ai {role, range, tags}` metadata (MOVESET §15.2) and rule cells for their counter classes.

Charging threats: a visible charge is answered at the next tier it can reach (it may still grow), with the reach of
its longest tier; a guard answer is held as long as the charge is visible (as the legacy bolt guard). An imminent
threat (< 0.7 s) while the AI holds its own charge or channel cancels it (guard / evade are explicit cancels).

Resources: projectiles drawn from the waterskin or the metal satchel count their mass as water / metal cost; magma
grips count the heat to melt the stone (~19.8 HU/kg, reserve first, then Focus); grips respect `max_mass`.

## Tests

`tests/sim/test_ai_planner.gd` (full counters per kit — lava wave → Earth wall answer / Fire DRAW / far wave → T3 gale —,
a weak gust never stops lava, REC over BLK, costs and time to impact, Novice hit more than Master over 20 seeded throws,
no hidden state, determinism), `test_ai_offense.gd` (wet → lightning, Storm Bolt / Smelter / bank shot vs barriers,
Melt & Return in play, Disrupt of a charge, chains), `test_ai_drills.gd` (presets, kit breadth, element and matrix
drills, legacy drills via configure), `test_ai_duel.gd` (120 s master-vs-adept duel over all elements: ledgers,
finite state, no stuck actions or endless holds, ≥ 20 distinct moves), plus the legacy `test_regressions_ai.gd` and
`test_ai_flagship.gd` (unchanged: legacy mode).

## Known gaps

* Only a water-only kit walks to the pool to refill an empty waterskin; mixed kits fall back to moves that need no water.
* Counters are predicted with `Interactions.predict` at decision time: a kit whose real resolution differs from its
  rule (contest strength, grip reach at contact time) can still fail in play — that is honest, not a planner error.
* Bank shots use the four arena boundary walls only (not pillars or the cover wall).
* `PlayerController` maps an aim only while the technique is held, so in `--autoplay=duel` the player-side brain's
  attack aims (bank shots) fall back to the lock target.

## Autoplay

`--autoplay=duel:<seconds>` — AI vs AI on the `spar` scenario (both fighters: master preset, all four elements and
sub-elements). `--autoplay=soak:<seconds>` — random input over every element, sub-element, slot, gesture, guard
flick and evade hold (soak / perf). Both go through the real input path (`InputFrame` → `PlayerController`).
