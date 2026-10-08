# Controls + HUD redesign plan (proposal, 2026-10-07)

Owner request: controls and HUD "much better and tailored to elemental moves: very flexible, lots of different things
you can do", at AAA level. This is the proposal to agree on before implementation. The current grammar (docs/MOVESET.md
§3, `FFGTouchControls`, `FFGDesktopInput`) stays the base: every slot keeps working, the sim stays deterministic, and
everything new maps onto sim inputs (`ff::InputFrame`) so the AI, replays and tests see the same moves.

## What is missing today
1. The player cannot *see* the physics. The counter matrix (split / sink / melt / wave back / deflect / ground a bolt) is
   the heart of the game, but nothing on screen says which answers exist for the threat that is coming.
2. Moves feel like buttons, not forms. Thrust / ground / sweep are flicks on one button; there is no way to shape a move
   (curve it, spin it, lift-then-throw) beyond the technique drag.
3. The HUD is a classic overlay (bars, chips, text). It does not read in the heat of a fight and hides the fighters.

## Proposal (four parts, each shippable on its own)

### A. Context counters on the guard (highest value)
- When a threat is inbound, the GUARD button grows up to 3 petals labelled with the answers the current element / sub can
  give to *that* threat, taken from the counter rules (`Interactions`, the golden matrix): e.g. stone incoming with Water
  selected -> "Wave back" (↑), "Sink" (↓), "Freeze" (side); with Fire -> "Melt", "Blast", "Deflect".
- A petal shows the predicted outcome band (clean / partial / overwhelmed) as a colour, from the same power comparison
  the sim uses (TP vs CP incl. the current charge tier). No new rules: it only exposes what the sim would do.
- Desktop / gamepad: the same answers on K+U/N/H and RB+Y/LT/B; a small radial near the fighter shows them.
- Perfect timing keeps its window; the petal flashes at the perfect moment (honest telegraph).

### B. Form gestures (shape the move)
- On touch, the ATTACK and TECHNIQUE drags become short *forms* drawn with one finger. Recognised shapes, the same for every
  element, each mapped per sub-element in the kit data (`moves.gd` / kits) - no free-text gesture zoo:
  - **push** (straight out) = thrust; **slam** (down) = ground; **arc** (side) = sweep - as today;
  - **circle** (closed loop) = the sub's *ring / whip / spin* variant (water whip, fire wheel, wind ring, orbiting stones);
  - **lift-then-push** (up, pause, out) = raise-and-launch (lift a slab, raise a water column, then throw it);
  - **pull** (toward the fighter) = draw / seize from the environment (water from the pool, stone from the floor);
  - **zig-zag** = the sub's quick multi-hit / chain variant.
- Curve: the end direction of a push bends the projectile path (sim gets an aim + curvature value, capped per move).
- Desktop: forms on the mouse (RMB drag draws the form); gamepad: right-stick flick patterns while TECHNIQUE is held.
- Each form has a "ghost" trail on screen while drawing and the recognised move name pops up before release.

### C. Environment and chain prompts
- World-space glyphs on usable sources near the fighter (pool, loose stone, fire field, wall): tap-hold on the glyph or
  pull toward it to draw from it.
- Chain ribbon: after a hit, the 2-3 moves that may chain (same sub, MOVESET §9.1) light up for their window.

### D. HUD in the world
- Health / balance / focus as thin arcs at the fighter's feet (both fighters), full bars only in the pause view.
- Charge tiers as glyphs at the hands (already in VFX) + a ring on the button; no centre-screen charge bar.
- Outcome callouts in a stylised script near the impact ("Grounded", "Melted", "Split", "Overwhelmed") - short, big,
  readable, also teaching the matrix.
- Opponent intent: the rival's charge tier and element shown as a coloured rim on them, not a panel.
- Elements: a compact quarter-wheel in the corner; element / sub switch by flicking from the wheel (one finger, no menu).

## Order and size
1. A (context counters) - needs a read-only "answers for threat X" query in the core (pure function over the rule
   tables, unit-tested against the golden matrix), touch petals, desktop radial. ~2 sessions.
2. D (world HUD) - Slate + world widgets; ~1-2 sessions.
3. B (forms) - recogniser in the logic island (unit-tested like `FFGFlick`), kit data for the circle / lift / pull / zig-zag
   variants (new moves per sub = design work with the owner), sim inputs. ~3+ sessions.
4. C (prompts) - after B (pull / draw).

## Owner decisions (2026-10-07)
- First: A (context counters) + D (world HUD). Then B.
- Predicted outcome colours on the petals: always on.
- Touch forms start on the ATTACK / TECHNIQUE buttons; one-finger camera drag on the right half stays.

## Questions for the owner (answered above)
- Start with A + D (readability of the physics), or B (more moves / shapes) first?
- Petals: show predicted outcome colours always, or only in the Lab / on easy difficulty?
- Forms on touch: one-finger drawing on the right half (replacing the camera drag there; camera moves to two fingers),
  or only from the ATTACK / TECHNIQUE buttons?

## Progress
- 2026-10-07 - A, first pass: `App/CounterHints` (core, read-only, CoreTests `test_counter_hints`) picks the most urgent
  threat and evaluates guard (+ perfect) / push / sink / technique of the current element / sub with
  `Interactions::predict`; `HudModel::counters`; HUD strip above the charge bar (threat class + seconds, answers in band
  colours, pulse near impact); touch GUARD petals name the push / sink answers in band colours. Next: the desktop radial
  near the fighter, perfect-window flash, petals for the attack slots that also counter (thrust / ground / sweep), D.
- 2026-10-07 - D, first pass: vitals as ground arcs around the player and its opponent (health / balance, focus for the
  player; gauges centred on the side facing the camera, shrinking toward it); the corner / rival-panel bars step back
  while the arcs are on screen. Rival intent: charge tiers as an outer rim in its element colour, pulsing faster per
  tier. Outcome callouts from every `interaction` event at the impact (`ff::CounterOutcomeLabel`; green / amber / red
  from the player's side, PERFECT prefix). Guard pill reads "GUARD NOW" in the last 0.2 s before impact.
  Next in D: the element quarter-wheel with flick switching; charge glyphs at the hands replacing the centre bar.

- 2026-10-08 - D checked in game (Lab threats + autoplay duel captures, `logs/hudD*`): arcs readable at the feet,
  counter strip names the threat in words ("HEAVY STONE 0.7 s", `ff::ThreatLabel`), rival rim visible. Fixes: callouts
  of the same outcome nearby merge ("TO SNOW x12", "PERFECT BLOCK x3"), the rest stack upward instead of overprinting
  (max 4); arc underlays darker for light floors. Charge moved into the world: the player's own wind-up is the same
  rim as the rival's (fills from the camera side, dark notches at the tier steps) with the move name + tier under it;
  the centre charge bar is only the fallback when the player's ring is off screen (touch keeps its button ring).
  Seen: other HUD texts ("You're down", body labels) can still sit under a callout; FX issues sent to `fx` via
  docs/game/REQUESTS.md. Next in D: the element quarter-wheel with flick switching.
- 2026-10-08 - Touch: fixed a crash on the first touch-overlay paint (a glyph stroke closed by adding an element of the
  array to itself; UE 5.8 asserts). `-FFTouchUi` shows the touch overlay in Mac captures. D, element switching: a chip
  plus a flick toward the petal column picks element and sub-element in one stroke without the long-press (logic test
  `touch_chip_flick_picks_sub_without_long_press`). The chip arc already sits as a quarter-wheel around the button
  cluster, so no separate wheel was added; to check on a phone: flick distance (1.6 chip radii) and direction slack.
- 2026-10-08 - A: the counter pills name the input on the device in use (keyboard `K` / `K + J` / `K + N` / `L`,
  gamepad `RB` / `RB + X` / `RB + LT` / `RT`, touch GUARD / GUARD ↑ / GUARD ↓ / TECH). Perfect-window telegraph from the
  sim's own `Moves::PERFECT_WINDOW` (`HudModel::perfect_window`): the guard pill fills and reads "NOW", and on touch the
  GUARD button rings bright (`TouchContext::guard_now`). Seen: the lowest tier that reaches a band can swap between
  T1 / T2 while a stone flies (the prediction follows distance); leave as is unless it reads as flicker on device.
  Next in A: answers on the attack slots that also counter (thrust / ground / sweep); a radial near the fighter was
  not needed on desktop (the strip sits right under the player's ground arcs).
- 2026-10-08 - A: attack slots that also counter. `CounterHints` evaluates thrust / ground / sweep / strike too (instant
  volumes only with contact moves, as the AI) and adds the best one that works (partial or better) as a fifth answer:
  e.g. a 20 kg stone vs Earth = ground "Block", vs Water / Air = thrust "Deflect", vs Fire = thrust "Body burst". The
  strip shows it with its key (`U` / `N` / `H` / `J`, pad `Y` / `LT` / `B` / `X`, touch ATTACK ↑ / ↓ / ↔); on touch the
  matching ATTACK flick petal lights in the band colour (a strike answer: "TAP: ..." above the button). Touch petals
  now carry the tier like the strip ("Sink T2").
- 2026-10-08 - C, chain ribbon (no dependency on B): `App/ChainHints` (core, read-only twin of
  `CombatWorld::_chain_ok`, CoreTests `test_chain_hints`) lists the attack slots that would chain out of the player's
  recovery right now (same sub-element after contact, each slot once, at most 3; a weave also needs Focus).
  `HudModel::chain`; the HUD shows "CHAIN 2" / "WEAVE 3" + key pills with the move names under the player's arcs and a
  bar that drains as the window closes; on touch the matching ATTACK flick petals light up. Seen in autoplay duels
  ("CHAIN 2: Comet Flame / Blue Furrow / Corona", "WEAVE 3: Magma Surge / Spatter Arc"). Fix from the same captures: a
  rival holding a charge (e.g. lightning) is a threat with no known release time - the strip now reads "CHARGING"
  instead of a fake 0.1 s countdown and "NOW" only lights when the impact time is known (`HudModel::threat_charging`).
  Still open in C: world glyphs on usable sources (pool, loose stone, fire field, wall) - needs B's pull / draw.
- 2026-10-08 - B: draft for the owner in `FORMS_PROPOSAL.md` (four forms x 16 sub-elements, how they would be built
  through the Godot kits, a smaller "circle + pull" first slice, four questions). Not started until the owner answers.
- 2026-10-08 - D: desktop / gamepad element quarter-wheel in the bottom-left corner (touch keeps its chip arc): the four
  elements on the outer band with their keys (`1`-`4`, pad d-pad arrows), the current element's sub-elements on the
  inner band, "EARTH · STONE" and the Q / E (LB + d-pad) hint beside it; bright for 1.5 s after a switch, then calm. It
  replaces the small top-left element line on desktop.
