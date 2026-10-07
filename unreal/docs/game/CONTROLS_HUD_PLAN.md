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
