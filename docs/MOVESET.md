# Fourfold — Moveset, Counter Matrix & Implementation Contract

_Lead design doc · 2026-10-05 · status: design complete, implementation pending (plan: §15–§16 and HANDOFF "Design")._

This is the complete design for the elemental moveset: four elements × four sub-elements, a unified charge
system, a **physical counter rule** (every element can answer every element, but only with enough power),
combos, game feel, VFX/audio direction, the opponent AI, the Lab tooling and the engine contract the
implementation plugs into. Rules for the existing sim (units, ledgers, flagship) stay in `docs/COMBAT_SPEC.md`;
this document extends them. Everything is original: no franchise names, terms or assets.

**Contents** — §1 Pillars & research · §2 Notation · §3 Controls grammar · §4 Charge · §5 Power & counter rule ·
§6 Elements overview · §7 Move lists (16 sub-elements) · §8 Counter matrix · §9 Combos & tricks ·
§10 Physics, motion & game feel · §11 VFX direction · §12 Audio · §13 Opponent AI · §14 Lab tooling ·
§15 Engine contract (for implementers) · §16 Priorities & open items · Sources

---

## 1. Pillars and what the research says

| Pillar | Meaning in Fourfold | Research basis |
|---|---|---|
| **Material truth** | Every attack is a *material in a state* (stone, lava, ice, steam, flame, charge, pressure, sound). What it does to another material follows one rule table, the same for player, AI and environment. Designers define interactions, not results; outcomes are discovered. | Systemic "chemistry" design: define rules between element states and let outcomes emerge [S1][S2]. |
| **Every threat has an answer — if you bring enough** | Each of the 4 elements has a coherent counter to every threat class (§8), but a counter only fully works when its *counter power ≥ threat power* (mass·speed, heat, charge, pressure). Weak answers give partial results (bent, slowed, crusted), never nothing silently. | Counterplay as a core value: "you can't just stomp the other player; they must be able to do something" [S3]; intransitive (rock–paper–scissors) relations with *unequal payoffs* so choices are reads, not coin flips [S4][S5]. |
| **Hold = commit = power** | Holding builds tiers (T0 tap → T3) that scale size, mass, speed, heat and counter power, cost Focus over time, are visibly telegraphed and can be interrupted. | Charge attacks trade vulnerability for power; tiered charge creates decisions [S6][S7]. |
| **One grammar for 16 kits** | Same buttons, same gestures in every sub-element: tap = quick, hold = strong, flick ↑ = reach, flick ↓ = ground, flick ↔ = area; guard = defend, guard flick = push / sink; technique = take control; evade = move. Learn one kit, read all kits. | Cancel/chain hierarchies in fighting games (normal → special → super) give structure to combos [S8]. Mobile: gestures on existing buttons instead of more buttons. |
| **Feel before numbers** | Hit-stop scaled by power, directional shake, anticipation and follow-through, distinct audio/haptics per outcome. | Hit-stop 3–12 frames scaled to strength, attacker+victim freeze, shake that decays [S9][S10][S11]; "juice": many small responses to one input [S12]. |
| **Readable on a phone at 60 fps** | Stylised, low-noise shapes; ≤ 2 transparent layers per effect; mesh + shader effects over particle soups; pooled everything; no SSR/SSAO/SDFGI/volumetric fog (Mobile renderer). | Stylised VFX read better with less micro-detail [S13]; flipbooks capped ~4×4, mesh-based effects with panning noise (tornado = layered cones, inner layers spin faster) [S14][S15]; mobile TBDR GPUs: minimise overdraw/transparent area [S16][S17]; Godot Mobile renderer lacks SSR/SSAO/SDFGI/volumetric fog [S18]; GPUParticles: small `amount`, `amount_ratio`, `fixed_fps` + interpolate [S19]. |
| **Responsive input** | 0.15 s press buffer (exists), gesture morph window 0.12 s, cancel windows defined per move. | Input buffers of ~100–200 ms; too long causes ghost inputs [S20]. |

---

## 2. Notation used in this document

* **Elements / sub-elements**: Earth (Stone · Metal · Sand · Magma), Water (Water · Ice · Mist · Plant\*), Fire (Flame · Blue ·
  Lightning · Combustion), Air (Gust · Vortex · Vacuum · Sound). \*Plant is the optional/stretch kit (P2).
  Short codes: `E/St E/Me E/Sa E/Mg  W/Wa W/Ic W/Mi W/Pl  F/Fl F/Bl F/Li F/Co  A/Gu A/Vo A/Va A/So`.
* **Inputs**: `A` attack tap · `A[T2]` attack held to tier 2 · `A↑ A↓ A↔` attack flick up / down / side (thrust / ground / sweep) ·
  `G` guard hold · `G*` guard pressed inside the perfect window · `G↑` guard push · `G↓` guard sink · `T` technique (hold, drag-aim, release) ·
  `T+A` attack tapped while the technique holds material (shape) · `Ev` evade tap · `Ev[h]` evade hold · `»` cancel / switch into.
* **Frames** are 60 Hz ticks: `S/A/R` = startup / active / recovery. 12 f = 0.2 s.
* **Power** in PU (power units, §5). `K` kinetic, `H` heat, `C` cold, `E` electric, `P` pressure/sonic.
* **Outcomes**: `BLK` block · `ABS` absorb · `REC` reclaim (you now control it) · `XFM→x` transform into x · `SHT` shatter ·
  `DEF` deflect (`DEF↩` back to the sender) · `RFL` reflect (waves/beams) · `CND` conduct · `GND` ground · `CAP` capture (held in a zone) ·
  `SNK` sink into the ground · `EXT` extinguish · `AMP` amplify (bad for the defender) · `WKN` weaken (partial) · `SLW` slow ·
  `BND` bend · `PASS` no effect · `FAIL` no counter here (evade).

---

## 3. Controls grammar (touch first; keyboard and gamepad mirror it)

Every sub-element fills the same **slots**. The slot comes from the button and the gesture; the move comes from the slot and the
selected element/sub-element (registry binding, §15.2).

| Slot | Touch | Keyboard / mouse | Gamepad | What it is |
|---|---|---|---|---|
| `strike` | ATTACK tap; hold = charge (ring shows T1/T2/T3 notches) | J / LMB (hold to charge) | X (hold) | quick attack; charged = the sub-element's power ladder |
| `thrust` | flick ↑ from ATTACK (toward the target), tap-flick or hold-then-flick | U | Y | reach: projectile / line |
| `ground` | flick ↓ from ATTACK | N | LT | ground-travelling line, eruptions, traps |
| `sweep` | flick ← or → from ATTACK | H | B (A = evade, see below) | arc / area / multi-hit |
| `guard` | GUARD hold; press just before contact = **perfect** | K | RB | the sub-element's barrier; perfect = its signature counter |
| `push` | flick ↑ from GUARD while guarding | K + J (chord) | RB + X | send the barrier/held material forward ("push it back on them") |
| `sink` | flick ↓ from GUARD while guarding | K + N (chord) | RB + LT | into/through the ground: swallow, pit, anchor, ground the charge |
| `tech` | TECHNIQUE hold, drag to aim, lift to commit; tap ATTACK with another finger while holding = **shape** (`T+A`) | L / RMB hold; J while holding = shape | RT hold; X while holding = shape | take control: seize / draw / grip / channel; context mode shown on the button (like today's HEAT/DRAW/VENT) |
| `evade` | EVADE tap (direction = stick) | Space | A | the sub-element's evade |
| `evade_hold` | EVADE hold ≥ 0.2 s | Space hold | A hold | sustained movement mode (glide, surf, skate, flight, anchor…) |
| element | element chips (tap) | 1 2 3 4 | D-pad | Earth / Water / Fire / Air |
| sub-element | tap the **active** chip again → ring of 4 petals (tap one), or long-press a chip → slide to a petal | same number key again cycles; Q / E previous / next | LB + D-pad | Stone/Metal/Sand/Magma etc. Switching only affects the **next** action (like elements today). |
| cancel / target / pause | unchanged | Esc · Tab · Esc | LB (while technique held) · R3 (target; moved from Y) · Start | |

**Gesture rules (sim side, device independent).** The device reports `attack_gesture` / `guard_gesture` ∈ {NONE, UP, DOWN, SIDE}
on the tick a flick is recognised (touch: ≥ 6 mm travel from the button within 0.25 s, or at release after a hold).
* A press starts the `strike` startup immediately (responsiveness). A gesture that arrives during the first **0.12 s** of
  startup, or at release from a charge, **morphs** the running attack into the gesture's move once (elapsed time, paid Focus and charge tier carry over).
* Desktop/gamepad send press + gesture on the same tick (dedicated keys), so the gesture move starts directly.
* `guard_gesture` while a guard is up (any time, after the perfect window or inside it) starts `push` / `sink`.
  A plain attack press while guarding still cancels the guard into an attack (legacy rule, tests depend on it).
* Touch hint: while ATTACK is held, three small labelled petals appear around it (the current sub-element's thrust/ground/sweep names);
  while GUARD is held, ↑ push / ↓ sink labels. Strong-labels setting shows them always.

Legacy mapping: sub-element 0 of each element (Stone, Water, Flame, Gust) keeps **exactly** today's strike, technique, guard
and evade moves (`earth_attack`, `earth_tech`, …); the new slots and sub-elements are additions. All 188 + 50 existing tests keep their meaning.

---

## 4. Charge system (unified)

| Tier | Hold time (default) | Gate | Typical scaling vs T0 | Focus |
|---|---|---|---|---|
| **T0** | tap (released < 0.18 s, today's `HOLD_THRESHOLD`) | — | ×1 | base cost at press |
| **T1** | held past 0.18 s; fires at the move's `heavy_min` (default **0.40 s**; legacy Earth 0.55, Water 0.45) | committed once the hold passes 0.18 s | mass/size ×1.6–2.3, power ×1.6–2 | + T1 surcharge (legacy `heavy_cost − cost`) |
| **T2** | **1.00 s** | still held | ×2.3–3, new property (pierce, wider, sustained, leaves a zone) | drain `charge_drain` (default 8 Focus/s) from T1 on |
| **T3** | **1.80 s** | still held | ×3–4, signature finisher behaviour | drain continues; T3 adds a 2-frame white "ready" glint |

* Per-move overrides: `tier_times` (e.g. Lightning's Bolt keeps the legacy **0.65 s** T1). Moves without `t2`/`t3` data stop at T1 (legacy moves until a kit upgrades them).
* **Stall, not drop**: if Focus can't pay the drain, the charge stops growing at the tier reached (one `insufficient` event) and can still be released.
* **Interruptible**: a hit, guard-break or **Disrupt** (Sound) during CHARGE interrupts; held material drops with its state; spent Focus is lost.
  Disrupt succeeds when the sound's P ≥ the charge's cohesion `6 + 4·tier`.
* **Telegraph honesty**: every tier-up emits `charge {actor, move, tier}`; the VFX shows a tier ring at the hands (T1), ring + ground ripple + rim light (T2),
  full aura + rising motes + light pulse (T3). Audio: one rising tone per tier in the element's motif. The AI perceives tiers like the player.
* **Charge as counter power**: defensive moves that can be charged (guard held longer, technique held longer, a gale used to stop lava) use the
  tier's power in the counter rule (§5). A wall held 1.0 s is thicker (Bulwark 120 → 160 kg), a Swallow from a guard held 1.8 s is stronger.
* Techniques that "summon" (Eye of the Storm, Vacuum Well, Fuse, Magma Surge without lava) use the technique hold time as their tier clock.

---

## 5. Power and the counter rule (the physics)

### 5.1 Threat power (TP)

A threat is a body (stone, blob, wave, cloud, zone) or an instant volume (flame cone, bolt, blast, sound). Its power has channels:

| Channel | Formula | Examples (PU) |
|---|---|---|
| **K** kinetic | `mass(kg) · speed(m/s) / 20` | stone shot 20 kg @17 → **17** · heave 45 @14 → **31.5** · T3 crag 80 @11 → 44 · launcher boulder 200 @11 → **110** · ice shard 4 @24 → 4.8 · water stream 12 @16 → 9.6 · lava wave 20 kg @7.5 → 7.5 |
| **H** heat | `thermal energy above ambient (HU) / 20` | 20 kg molten (396 HU) → **19.8** · hot rock 20 kg @1000 °C (196 HU) → 9.8 · flare 60 HU → 3 · blaze 160 HU → 8 · inferno 480 HU → 24 |
| **C** cold | `abs(thermal energy below ambient) / 20` (ice) | 4 kg ice at −5 °C (≈18 HU) → 0.9 · frost comet 15 kg → 3.4 (cold counts vs heat-based counters only) |
| **E** electric | move-defined | spark 10 · bolt 24 (legacy) · storm bolt 36 · skybreak 52 |
| **P** pressure / sonic / blast | move-defined, by tier | palm gust 7 · cyclone 11 · gale 18 · hurricane 28 · tornado 25–35 · burst 16 · detonation 34 · roar 22 |

`TP = Σ w_ch · channel` with per-rule channel weights (default 1). Example: a 20 kg lava wave = K 7.5 + H 19.8 = **27.3**; a 45 kg wave = 16.9 + 44.6 = **61.5**.
A charged projectile also carries **cohesion** = residual authority `0.6 + 0.1·tier` against grip/reclaim contests (harder to steal a charged stone).

### 5.2 Counter power (CP)

* **Barrier bodies**: `CP = mass · hardness` — stone 0.25 (Bulwark 120 kg → **30**, held to T2 160 kg → 40, T3 200 kg → 50), obsidian 0.33,
  glass 0.30, ice 0.44 (50 kg from ambient moisture → **22**; 70 kg next to the pool or a puddle → 30), sand 0.25 (100 kg → 25),
  metal 3.3 per kg of plate (6 kg Aegis → **20**), vine 0.4 (40 kg → 16), held water 1.0 per kg (6 kg shield → **6**).
* **Active counters** (gust, wave, blast, sound, swallow, beams…): the move's `power` per tier (tables in §7).
* **Plain guard** (any element, no barrier): CP 10, outcome always "block with chip" (legacy: 12 % damage, 55 % balance).
* **Perfect timing** (guard pressed ≤ 0.18 s before contact, not mashed): CP × **1.5** and the rule's `perfect` outcome.
* **Efficacy** `eff` per (threat class, counter class) encodes material physics: water vs fire 2.5, sand vs fire 2.0, wind vs light solids 1.5–2.0,
  wind vs heavy solids 0.6, magma vs ice 3.0, sound vs ice/glass 2.5, vacuum vs fire 2.0, etc. `CP_eff = CP · eff`.

### 5.3 Bands and partial outcomes

`ratio = CP_eff / TP`

| Band | Default | What happens |
|---|---|---|
| **full** `ratio ≥ 1.0` | rule `outcome` (or `perfect`) | the threat is stopped / turned / transformed / reclaimed as the rule says |
| **partial** `0.5 ≤ ratio < 1` | rule `partial` (default **WEAKEN**) | the counter removes `CP_eff` from the threat (subtractive: K → speed scaled, H → heat removed through the ledger, E/P reduced); special partials: **BEND** (deflect angle `60°·f`, f = (ratio−0.5)/0.5), **SLOW**, **partial transform** (crust/soften/partly melted) |
| **fail** `ratio < 0.5` | rule `fail` (default **OVERWHELM**) | the counter breaks (wall crumbles, shield boils away, guard-break balance damage); the threat continues with `TP − absorb_on_fail · CP_eff` (default absorb 0.5) |
| **special bands** | per rule | e.g. wind vs fire: `< 1` **AMP** (fanned: +30 % heat, +1 m), `1–2` **DEF** (blown aside; perfect: back), `≥ 2` **EXT**. Lightning vs barriers: `≥ 1` **GND**, `< 1` **SHT** the barrier and pass with `E − 0.5·CP` |

**Stacking is physical.** Several counters on one threat subtract in order: a 200 kg boulder (110) survives a Swallow T3 (−55) but a Ram Wall clash (54)
then stops it. Big threats are answered by combos, small ones by single moves.

**Ledgers stay exact.** Heat removed by a counter goes somewhere booked: the drawer's reserve (Heat Sink, DRAW), steam (`vapor`), ambient
(convective cooling by wind: `ambient`), the ground (`removed`, Cool & Set). Mass moves only through `split_body`, `merge_bodies`, `decay_body`
(sink = `ground_returned`), and new booked conversions (sand↔glass↔sandstone, water↔plant, metal satchel↔field). Tests check conservation (§15.9).

### 5.4 The owner's examples, worked through

| Situation | Numbers | Result |
|---|---|---|
| **Someone puts up a stone wall → melt it and push it back on them** | Bulwark 120 kg (CP 30). Fire/Blue **Smelter** (T, 450 HU/s, range 6 m) heats the wall's facing shell (25 % = 30 kg): 30·(9.8 + 0.5·10) ≈ **444 HU ≈ 1 s** (reserve first, then Focus). The shell **slumps** into a 30 kg molten pool on the heated side; the rest crumbles (2 rubble stones, the remainder sinks). Then switch **Earth/Magma » A↓ Magma Surge**: the slumped lava becomes a wave aimed at the builder (budget 6 + 0.3·30 = 15 m). | ✔ "Melt & Return" (§9 combo 1). Flame **Scorch** does the same at 300 HU/s. |
| **Lightning blasts through stone** | Bolt T1 E 24 vs Bulwark grounding 30 → ratio 1.25 → **GND** (today's rule, tests unchanged). **Storm Bolt** T2 E 36 → ratio 0.83 → the wall **SHATTERS** and the bolt continues with 36 − 15 = **21**. **Skybreak** T3 strikes from above (walls between don't matter). | ✔ |
| **Holding longer makes it more powerful** | Every strike/thrust/ground/sweep has T0–T3; guards thicken with hold time; summons scale with technique hold. | ✔ §4 |
| **Stone thrown at me (TP 17): split it and spike it back** | Earth/Stone **Seize** (T) catches it (incoming preferred, ≤ 80 kg), **T+A Split** → 3 spikes of 6.7 kg, release → fan @20 m/s back at the thrower. | ✔ REC + split |
| … **or put it down into the ground** | Earth/Stone **G↓ Swallow**: CP 22 ≥ 17 → **SNK** (mass returns to the ground ledger). Earth/Sand **Quicksand** swallows it where it lands; Earth/Magma **Melt Pit** melts it in. | ✔ |
| … **or make a wave back** | Water **A↓ Tidal Rush** (CP 18 ≥ 17): the wave **CAP**tures the stone and carries it back at 9 m/s. Earth/Sand Sand Surge does the same with sand. | ✔ |
| … **or turn it into lava** | Fire/Flame **Thermal** (magma grip, the flagship): catch, ≈ 396 HU (≈ 40 Focus) → molten → release = pour. Fire/Blue **Searing Beam T2** melts it mid-air (falls short as a magma blob). | ✔ |
| … **or deflect it with wind** | Air **Wind Guard** 12 × eff 1.5 = 18 ≥ 17 → **DEF**; perfect (×1.5 = 27) → **DEF↩** back to the thrower. Palm Gust T0 (7 × 2 = 14, ratio 0.82) only **bends** it; Cyclone T1 (22) deflects. | ✔ |
| **You can NOT block lava with a simple air attack** | 20 kg lava wave TP 27.3: Palm Gust 7 (0.26 **FAIL**), Cyclone 11 (0.40 **FAIL**), Gale T2 18 (0.66: crusts + slows), **Hurricane Palm T3 28 (1.03): stalls the front and sets it into rock** (convective cooling, booked as `ambient`). Tornado T2 25 (0.92 partial; risk: picks up spatter = magma vortex), **Cyclone Fortress T3 35 (1.28) ✔**. A 45 kg wave (61.5) needs a combo (Hurricane + Tidal Rush, or a wall). | ✔ |

---

## 6. Elements and sub-elements at a glance

| Element · sub | Identity | Material source / resource | Strong at | Weak to |
|---|---|---|---|---|
| **E/Stone** (legacy) | mass, walls, the ground itself | ground (rip, ledger `ground_taken`) | blocking, reclaiming stones, sinking | blue fire/scorch (melt), storm bolts, boulders |
| **E/Metal** | edges, magnetism, conduction | **satchel 12 kg** (refill by Recall or ripping scrap from the arena plate) | blades, reclaiming metal, lightning rods | heat (plate turns red-hot), water+lightning, magnet steal |
| **E/Sand** | grit, smothering, flow | ground grit (`ground_taken`, returns when it settles) | fire, lightning (→ glass), sound, concealment | water (→ mud), wind, vortices (feeds them) |
| **E/Magma** | molten earth under control | heat from reserve, then Focus (10 HU/Focus); any lava on the field | reclaiming lava, burning, melting ice/vines | water (→ obsidian), sand, heat draw |
| **W/Water** (legacy) | flow, push, quench | waterskin 6 kg, pool, puddles, condensed mist/steam | fire, lava, sand, carrying things back | lightning (conducts to you), vacuum, sound passes |
| **W/Ice** | solid water, brittle, insulating | same water sources + ambient moisture (fantasy, booked) | lightning (insulates), water/steam (freezes), slick terrain | fire, lava, sound, blasts (shatter) |
| **W/Mist** | vapour: fog, steam, concealment | water + heat (steam) or water (fog) | hiding, dampening fire/blasts/sound, scalding | wind, vacuum, ice, lightning (fog conducts) |
| **W/Plant\*** | growth, binding, catching | water converted to vines (booked) | catching solids, rooting, long-reach grip | fire (×3), blades, frost |
| **F/Flame** (legacy) | heat in cones and fields; the thermal technique | heat reserve, then Focus | ice, vines, mist, reclaiming heat | water, sand, vacuum, strong wind |
| **F/Blue** | concentrated heat: melt, fuse, smelt | ×1.5 heat cost | walls (melt), metal, ice, sand → glass | water (still boils away), vacuum |
| **F/Lightning** | instant, conductive, ignores mass | Focus; **static charge 0–60** stored by the Static Ward | shattering, punishing wet/metal, blasting through walls (T2+) | sand/earth grounding, ice & vacuum insulate, rods |
| **F/Combustion** | detonations at range | heat (reserve, Focus) | shattering brittle, snuffing fires, dispersing clouds | vacuum (suppressed), mist/steam (dampened) |
| **A/Gust** (legacy) | push, deflect, disperse | Focus | light projectiles, clouds, fire (if strong enough) | dense/heavy threats, lightning, vacuum |
| **A/Vortex** | capture and return | Focus | catching volleys, sandstorms, lifting | water mass ("drowned"), vacuum wells, walls |
| **A/Vacuum** | absence: no fire, no sound, no charge | Focus | fire, combustion, sound, lightning (insulates) | solids and liquids pass through |
| **A/Sound** | resonance and flight | Focus | ice/glass (shatter), disrupting charges, bank shots | sand/fog/vines absorb, vacuum nullifies, walls reflect it back |

---

## 7. Move lists

Columns: **Slot** (input) · **Move** (tier names) · **Cost** (Focus · heat HU · material) · **S/A/R** frames at T0 · **Tiers** T0 → T1 → T2 → T3 ·
**Power** (PU by tier, channel) · **Interactions / notes** · **Anim · FX** (existing clip names from `fighter_clips.json`; FX keys from §15.6).
`cast/release/cone/beam/burst/ring/erupt/trail` FX are parameterised by `mat` (§15.6). Rows marked *legacy* are today's moves, unchanged
for T0/T1 (tests pin them); their T2/T3 are additions.

### 7.1 Earth / Stone (sub 0, legacy kit)

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Stone Shot** → Heave → Boulder → Crag Breaker (*legacy T0/T1*) | 7 · +7 at T1 · drain 8/s | 14/4/18 | rip/reuse 20 kg @17 → 45 kg @14 → 65 kg @12 → 80 kg @11 that splits into 3 rubble on impact (crater dust) | K 17 / 32 / 39 / 44 | reuses a loose stone ≤ 25 kg at the feet first (legacy) | earth_lift → earth_throw / earth_heavy · cast(stone), release, impact |
| thrust `A↑` | **Spear Stone** → twin → triple → Pike Volley | 8 · drain 8/s | 12/4/20 | 12 kg spear @26, flat (gravity ×0.4) → 2 → 3 in a staggered line → 5 | K 15.6 each | pierces mist/sand clouds; shatters ice shards it meets (clash); sticks in sand walls | earth_lift → earth_throw · release(stone,"spear") |
| ground `A↓` | **Rising Fangs** → long → tall → **Earthrise** | 9 · drain | 16/6/22 | spike line 9 m @14 m/s, 0.9 m spikes → 12 m → 1.4 m & wide → ring r 3.5 m around you | K/P 18 / 24 / 30 / 40 (launch 6) | spikes stand 1.5 s as low walls (CP 20): stop ground lines (lava/water/sand waves, ground current), ground bolts | earth_wall · erupt(stone) |
| sweep `A↔` | **Rubble Fan** → 5 → 7 → 9 skipping | 7 · drain | 12/6/20 | 3 × 6 kg @18 in 40°, 8 m | K 5.4 each | many small hits: clears vines/thickets, breaks ice needles | earth_throw · release(stone,"fan") |
| guard `G` / `G*` | **Bulwark** (*legacy wall*) / **Return Stone** (*legacy redirect*) | 8 | rise 8 f | 120 kg wall; held 1.0 s → 160 kg; 1.8 s → 200 kg | CP 30 / 40 / 50 | blocks solids, waves, flames (wall heats), grounds bolts with E ≤ CP; reflects sound. Perfect vs stone ≤ 80 kg → ballistic return to the thrower | earth_wall, guard · barrier(stone) |
| push `G↑` | **Ram Wall** | 6 | 10/20/18 | the standing wall slides 6 m @9 m/s, then crumbles to rubble | K 54 (120 kg) | pushes bodies and waves back; vs an enemy wall you touch: push contest (your K vs their CP) | earth_heavy · trail(stone), burst |
| sink `G↓` | **Swallow** | 8 | 6/18/16 | ground opens: cone 6 m, 60°; strength from guard hold time | CP 22 / 30 / 40 / 55 | **SNK** solids (stone, metal, ice, hot rock, sand slugs); lava waves drain into the trench (WKN/SNK); booked as `ground_returned` | earth_wall · erupt(stone, open) |
| tech `T` / `T+A` | **Seize** (*legacy*) / **Split** | 6 · +3 per split | legacy | seize ≤ 80 kg at 7.5 m (incoming preferred), drag-aim, release = throw. Split: held stone → 3 spikes (⅓ mass) fanned 30° @20 | reclaim | "split it and spike it back"; rips a ground stone if nothing to seize (legacy) | earth_hold → earth_throw · cast(stone) |
| evade `Ev` | **Burrow Step** | 6 | 0/14/8, i-frames 13 f | sink and resurface 3.5 m away | — | passes under ground lines; not on the metal plate, in the pool or on ledges | evade_* · erupt(dust) at exit |
| evade_hold | **Stone Skin** | 8/s | — | while held: 40 % armor vs K damage, anchored (no knockback/pull/lift), speed ×0.4 | anchor CP 40 | answers tornado lift and vacuum pull | guard · aura(stone) |

### 7.2 Earth / Metal (sub 1)

Resource: **metal satchel 12 kg** (`ActorState.metal_carried`). Thrown metal stays on the field and can be **Recalled**; the arena metal plate
is an unlimited scrap source (10 kg per rip, 1.2 s cooldown). Metal conducts (§8 lightning row).

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Razor Disc** → pincer → ricochet → **Disc Storm** | 5 + 2 kg/disc | 10/4/16 | 2 kg disc @24, homing 10°/s → 2 curving → 3 that ricochet once off walls → 5 orbit you 0.6 s then fire | K 2.4 (edge: 9 dmg, ×1.5 vs unarmored) | low power = easy to deflect, but many; cuts vines ×2; recallable | fire_jab · release(metal,"disc") |
| thrust `A↑` | **Iron Lance** → 8 kg → piercing 10 kg → **Railspike** | 8 + 6–12 kg | 14/4/20 | 6 kg @30 straight → 8 kg → 10 kg pierces one body → 12 kg @42 | K 9 / 12 / 15 / 25 | embeds in stone/glass walls and stays as a **rod** (conductor, lightning relay) | earth_throw · release(metal,"lance") |
| ground `A↓` | **Lodestone Line** → … → **Iron Garden** | 8 + 3 kg | 14/6/22 | filings snake 8 m @12 m/s and spring into caltrops r 2 m for 4 s → r 3 m → r 4 m | P 10 / 14 / 18 / 24 | slow 40 %, chip 2/s; caltrops are a **conductor node**; Recall collects them | earth_wall · erupt(metal) |
| sweep `A↔` | **Chain Arc** → long → heavy yank → **Whirling Chain** | 7 | 12/8/20 | chain 5 m 120°: yanks light targets/bodies 2 m toward you → 7 m → yanks heavy → 360° | K/P 10 / 16 / 24 / 32 | wraps stones/metal ≤ 20 kg (CAP), then you hold them | water_whip · cone(metal) |
| guard `G` / `G*` | **Aegis Plate** / **Magnet Catch** | 8 + 6 kg | rise 4 f | held 6 kg plate | CP 20 | blocks blades/stones; vs flames it **heats** (≥ 300 °C → dropped, burns); lightning: **GND** if you stand on stone/sand, **CND into you** if in water/on the plate. Perfect vs metal → caught (REC) into hand; vs stone → DEF | guard · barrier(metal) |
| push `G↑` | **Plate Rush** | 4 | 10/4/18 | hurl the plate spinning @20 | K 6 (edge 12 dmg) | | earth_throw · release(metal,"plate") |
| sink `G↓` | **Rod Plant** | 6 + 3 kg | 8/4/16 | plant a rod: lightning attractor r 6 m for 8 s | capacity 60 E | bolts within r are drawn to the rod and **GND**; above capacity the rod melts; a rod touching a puddle **conducts into it** | earth_wall · erupt(metal,"rod") |
| tech `T` / `T+A` | **Lodestone Grip** / **Reforge**; no target → **Recall** | 6 | 8/4/18 | seize metal ≤ 80 kg at 10 m (incoming preferred; grip ×1.3 vs metal); rip 10 kg scrap from the plate; T+A reshape held metal disc ↔ lance ↔ 3 shards; Recall pulls every piece you own back (they are projectiles on the way) | reclaim | steals the opponent's discs/lances/plates; Recall hits from behind | earth_hold → earth_throw · cast(metal) |
| evade `Ev` | **Magnet Glide** | 5 | 0/14/6, i 10 f | 6 m dash toward the nearest metal within 8 m (rod/plate/disc), else 3 m sidestep | — | | air_dash · trail(metal) |
| evade_hold | **Iron Stance** | 6/s | — | anchored + 25 % armor vs edges | anchor CP 30 | | guard · aura(metal) |

### 7.3 Earth / Sand (sub 2)

Sand is ground grit (ledger `ground_taken`, returns as it settles). Sand **fuses to glass** with lightning or blue fire, becomes **mud** with water,
**sandstone** (stone) when compressed.

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Grit Shot** → triple → **Sand Cannon** → **Dune Breaker** | 5 · drain | 10/4/16 | 5 kg slug @22 bursts on hit (blind 1 s) → 3 slugs → 15 kg slug bursts into a 2 m cloud → 30 kg slug bursts into a 4 m sandstorm (3 s) | K 5.5 / 8 / 13.5 / 27 | blind = lock-on broken, AI perception +0.2 s | earth_lift → earth_throw · release(sand), burst(sand) |
| thrust `A↑` | **Sandblast** → long → sustained → **Scour** | 7 | 12/10/18 | abrasive jet 8 m 10° → 10 m → 1 s jet → 2 s jet that cuts through ice walls/vines | P 6 / 9 / 14 / 20 | abrades barriers ×2 (ice, glass, vine), dries wet targets and puddles | fire_release · beam(sand) |
| ground `A↓` | **Sand Surge** → wide → tall → **Desert Tide** | 9 · drain | 16/6/22 | sand wave 9 m/s, 2 m wide, 10 m → 2.5 m → tall (stops projectiles) → 4 m wide, 14 m | K 13.5 / 18 / 24 / 34 | knockdown, carries loose solids, buries puddles (→ mud), smothers fire fields, crusts lava (×1.5) | earth_wall · wave body (sand_surge) |
| sweep `A↔` | **Veil of Grit** | 6 · drain | 12/8/18 | sand cloud 120° 4 m for 2.5 s → 5 m → 6 m 4 s → 8 m sandstorm | P 6 / 9 / 12 / 16 | blocks LOS/lock-on; flames inside ×2 smothered; projectiles −30 % K; bolts through it −50 % and fuse glass | water_whip · zone(sand_cloud) |
| guard `G` / `G*` | **Dune Wall** / **Engulf** | 7 | rise 8 f | porous 100 kg wall (held: 130 / 160 kg) | CP 25 / 32 / 40 | captures solids (×1.2), smothers fire (×2), absorbs blasts (×1.5) and sound (×1.5), **grounds bolts (cap 60) and turns to glass**; water → **mud wall** (+5 CP, collapses after 4 s). Perfect: swallow a projectile ≤ 30 kg and spit it back | earth_wall · barrier(sand) |
| push `G↑` | **Dune Push** | 4 | 8/6/18 | the dune collapses forward into a Sand Surge (tier = wall hold) | as Sand Surge | | earth_heavy · wave(sand) |
| sink `G↓` | **Quicksand** | 8 | 8/4/16 | 3 m pit in front for 5 s (strength by guard hold) | CP 18 / 26 / 36 / 50 | walkers ×0.3 speed, rooted after 1 s; landing solids **SNK**; lava entering → glass crust (stall); water poured in → mud bog (longer, stronger) | earth_wall · zone(quicksand) |
| tech `T` / `T+A` | **Sandform** / **Compress** | 4 + 2/s | 10/4/18 | gather 6 kg/s up to 30 kg held; release = sand spear (slug). T+A: compress into **sandstone** (mat STONE, same mass) → throw as stone. Seize enemy sand clouds/surges/heaps (REC). On lava: **Smother** (dump the held sand: crust) | reclaim | | earth_hold → earth_throw · cast(sand) |
| evade `Ev` | **Sand Surf** (tap: slide) | 5 | 0/16/8, i 9 f | 4 m slide leaving sand | — | | evade_* · trail(sand) |
| evade_hold | **Sand Surf** (ride) | 10/s | — | ride a self-made sand wave 8 m/s, steer, ramps off ledges | — | | glide · wave(sand) |

### 7.4 Earth / Magma (sub 3)

Molten earth under control. Heat is paid like Fire (`pay_heat`: reserve first, then Focus at 10 HU/Focus) — so drawn heat fuels magma.
Holding molten mass costs the existing upkeep (3 Focus/s, insulated).

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Ember Clot** → triple → **Magma Bomb** → **Caldera** | 4 + 80 HU | 12/4/18 | 4 kg molten glob @18 (burn 2 s) → 3 globs → 12 kg lobbed, splashes a 2 m lava puddle → 20 kg lobbed, 3.5 m lava pool | K+H 7.6 / 12 / 22 / 35 | puddles are quenched by water (→ rock), ignite vines; all of it is material for Magma Surge | fire_jab · release(magma) |
| thrust `A↑` | **Lava Lash** → long → sustained → **Molten Lance** | 6 + 60 HU | 12/8/20 | lava whip 6 m 25° → 7 m → 0.6 s stream → 10 m jet that pours 300 HU into a wall face | H 6 / 9 / 14 / 22 | melts ice ×3, burns vines ×3, softens stone walls (toward slump) | water_whip · beam(magma) |
| ground `A↓` | **Magma Surge** → … → **Lava Tide** | 8 (+160 HU if no lava near) | 18/6/22 | **re-pour**: every molten body within 6 m (yours, the rival's, a slumped wall, lava pools, blue-fire rifts) becomes a WAVE toward the aim; no lava → raises an 8 kg vein. T1+ more budget/speed; T3 merges all nearby lava into one wave | K+H of the lava moved | **"push it back on them"**; same wave rules as the flagship (bends toward target while fluid, stopped by walls/rises, quenched by water) | pour · wave(lava) |
| sweep `A↔` | **Spatter Arc** → … → ring | 5 + 50 HU | 12/6/18 | 5 × 1 kg droplets fan 70° 6 m → 7 → 9 → ring | H 2 each | scorch decals; ignite vines | fire_release · release(magma,"fan") |
| guard `G` / `G*` | **Magma Curtain** / **Obsidian Set** | 10 + 150 HU | rise 8 f | 100 kg stone wall with a 15 kg **molten face** | CP 28 (obsidian +10) | small solids ≤ 20 kg that hit **stick and heat**; ice → steam; vines ignite; flames feed the face; **water → steam burst + the face sets to obsidian** (harder, but brittle to sound). Perfect: the projectile is fused into the wall, the wall hardens; lava hitting it is absorbed | earth_wall · barrier(obsidian) |
| push `G↑` | **Slag Wave** | 4 | 10/6/18 | the molten face slumps forward as a 15 kg lava wave | K+H 5.6+15 | | pour · wave(lava) |
| sink `G↓` | **Melt Pit** | 8 + 120 HU | 8/4/16 | 2.5 m molten pit in front for 2 s; strength by guard hold | CP 20 / 28 / 38 / 50 | landing solids melt into it while its heat budget lasts; ice → steam; walkers burn | earth_wall · zone(melt_pit) |
| tech `T` / `T+A` | **Magma Hold** / **Cool & Set**; on a rival's wave → **Reverse Tide** | 6 + upkeep 3/s | 10/4/18 | seize molten/hot stone at 8 m without burns (blobs, waves, hot rock, slumped walls, lava pools); release = pour (wave) or throw (aim up = bomb). T+A: dump the heat into the ground (booked `removed`) → solid rock in hand. On an enemy lava wave: grip contest vs the pourer's authority; win = the wave turns around | reclaim | the Earth answer to the flagship | magma_hold → pour · cast(magma) |
| evade `Ev` | **Cinder Step** | 5 | 0/14/8, i 9 f | 3.5 m dash leaving a 1 s ember trail | — | | evade_* · trail(magma) |
| evade_hold | **Lava Wade** | 10/s | — | immune to lava/hot-rock burns; walk through lava at ×0.6 | — | | walk · aura(magma) |

### 7.5 Water / Water (sub 0, legacy kit)

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Lash** → Ice Lance (*legacy T0/T1*) → **Torrent** → **Maelstrom Lash** | 5 · +5 T1 · drain | legacy 10/7/17 | arc 4.6 m, wets → 4 kg ice lance (legacy) → 10 kg water slug @20 (knock 6) → 360° whip 5 m | P 8 / K 5.7 / 10+6 / 18 | the T1 freeze is the legacy lance; T2/T3 return to liquid force. Lash knocks light stones aside (legacy) | water_whip / water_freeze · cone(water) |
| thrust `A↑` | **Water Bullet** → triple → **Pressure Jet** → **Cutting Jet** | 4 + 1.5 kg | 10/4/16 | 1.5 kg slug @30 → 3 → 0.8 s jet 10 m (knock 3/tick) → 1 s thin jet that cuts vines/sand walls | K 2.3 / 6.9 / P 10 / P 16 | quenches flames on its path; a **jet still connected to you conducts lightning back to you** | water_whip · beam(water) |
| ground `A↓` | **Tidal Rush** → bigger → **Breaker** → **Deluge** | 8 + 8 kg | 16/6/22 | ground wave 9 m/s, 2 m wide, 10 m → 12 m → tall (stops projectiles) → 4 m wide from the pool | K 18 / 24 / 32 / 45 | knockdown; **carries loose/incoming solids ≤ CP back** ("make a wave back"); quenches lava ×1.5 (→ rock); extinguishes fire fields; leaves puddles (conductive!) | water_whip → pour · wave(water) |
| sweep `A↔` | **Spray Fan** | 4 + 1 kg | 10/6/16 | droplet fan 90° 5 m; T3 leaves a mist cloud | P 4 / 6 / 9 / 12 | wets (×1.5 lightning damage), douses embers, cools hot rock (−H 6), sand → mud | water_whip · cone(water) |
| guard `G` / `G*` | **Water Shield** (*legacy*) / **Cushion** · **Swallow Current** | — | legacy | held water orb (waterskin or drawn water) | CP 6–12 (by kg) | ×2.5 vs fire (steam block, legacy); slows solids −40 % on partial; **conducts lightning to you** (and wet ×1.5). Perfect: water/steam/ice-melt → absorbed into the waterskin; stones ≤ 30 kg → Cushion (stopped, dropped at your feet) | water_shield · barrier(water) |
| push `G↑` | **Surge Orb** | 4 | 8/4/16 | hurl the shield as an orb @14 that bursts (wet 2 m, knock 5) | K 4.2 + P 5 | | water_whip · release(water) |
| sink `G↓` | **Slick** | 3 | 6/4/14 | pour the shield as a 2.5 m puddle in front: running through it → slip (−20 balance), wet | — | sets up conduction (careful) or Ice freezing | water_draw · splash |
| tech `T` / `T+A` | **Draw & Shape** (*legacy*) / **Freeze** | legacy | legacy | draw from pool/puddle/waterskin, plus **condense mist/steam** and seize enemy streams/waves in flight (contest); release = stream (legacy). T+A: the held water freezes into an ice block (throw) | reclaim | | water_draw/water_hold/water_whip · cast(water) |
| evade `Ev` | **Riptide Step** (pool: **Dive**) | 4 | 0/16/8, i 9 f (dive 18 f) | 4 m slide on a water film (puddle trail); in the pool submerge and resurface 4 m away | — | | evade_* · trail(water) |
| evade_hold | **Wave Ride** | 2 kg water/s | — | ride a self-made wave 8 m/s while water lasts | — | | glide · wave(water) |

### 7.6 Water / Ice (sub 1)

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Frost Shard** → Ice Lance → **Glacier Spear** → **Frost Comet** | 4 + 1 kg · drain | 10/4/16 | 1 kg shard @26 (chill 1.5 s) → 4 kg lance → 8 kg spear pierces one body and stands as an ice spike 3 s → 15 kg comet lobbed, shatters into 6 shards | K 1.3 / 5.7 / 12 / 15 | ice **insulates** but is brittle: sound/blasts/lightning shatter it | water_freeze · release(ice) |
| thrust `A↑` | **Icicle Volley** → 5 → 7 → 12 | 5 + 0.8 kg each | 10/6/18 | needles @32 in a tight line | K 1.3 each | | fire_jab · release(ice,"needles") |
| ground `A↓` | **Rime Path** → **Frozen Fangs** → **Glacier Ridge** | 7 + 3 kg | 14/6/20 | ice sheet 12 m/s, 1.5 m wide → ice floor zone 6 s → ice spikes along it → a 3 m ridge travels and stands as a wall | C/P 10 / 14 / 20 / 30 | **slick** floor (decel 4 m/s²); freezes puddles (stops conduction); **freezes water waves into an ice ridge**; crusts lava fronts; over the pool: a walkable 2 m strip | water_freeze · wave(rime) |
| sweep `A↔` | **Hoarfrost Fan** | 5 · drain | 10/8/18 | freezing mist 90° 5 m | C 6 / 9 / 13 / 18 | chill −30 % 2 s; **wet targets freeze (rooted 0.8 s)**; freezes streams mid-air; weakens flames; cools hot rock; makes vines brittle | water_freeze · cone(ice) |
| guard `G` / `G*` | **Ice Wall** / **Flash Freeze** | 8 | rise 8 f | 50 kg wall (70 kg next to water); held 1.0 / 1.8 s → +30 % / +60 % | CP 22 / 30 / 38 | stops solids; **blocks bolts with E ≤ 1.5·CP** (insulator), T2+ storms shatter it; melts under fire/lava (both weaken, steam). Perfect: water/steam/mist threats **freeze solid** and drop; stones ≤ 30 kg are frost-locked to the wall | water_freeze, guard · barrier(ice) |
| push `G↑` | **Glacier Shove** | 4 | 10/18/18 | the wall slides 6 m on its own sheet | K 22 | | earth_heavy · trail(ice) |
| sink `G↓` | **Frost Floor** | 5 | 6/4/14 | 3 m ice floor around you 5 s | — | enemies slide; freezes your puddles (lightning-safe) | water_freeze · zone(ice_floor) |
| tech `T` / `T+A` | **Freeze-Draw** / **Shatter** | 6 | 12/4/18 | draw any water as a growing ice block (≤ 12 kg) or seize incoming ice (REC); release = thrown block; T+A: shatter into 6 shards fanned | reclaim | | water_draw → water_freeze · cast(ice) |
| evade `Ev` | **Ice Glide** | 4 | 0/14/6, i 9 f | 5 m slide on a momentary ice strip | — | | evade_* · trail(ice) |
| evade_hold | **Skate** | 8/s | — | ice under your feet: 7 m/s, low friction; works across the pool | — | | run · trail(ice) |

### 7.7 Water / Mist (sub 2) — mist and steam

Steam = water + heat (paid like Fire, booked as `vapor`); fog = water dispersed as droplets (a WATER CLOUD that conducts weakly).

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Scald Puff** → **Steam Jet** → **Geyser** → **Boiling Pillars** | 4 + 40 HU + 0.5 kg | 8/6/14 | steam puff 3 m 40° (scald, 0.5 s obscure) → 5 m jet, knock 4 → geyser erupts under the target (launch 8) → 3 geysers in a line | H/P 3 / 6 / 12 / 20 | | fire_jab / water_whip · cone(steam), erupt(steam) |
| thrust `A↑` | **Fog Lance** | 4 + 0.5 kg | 10/4/16 | cold mist bolt 12 m: wet + chill + blind 0.8 s | P 4 / 6 / 8 / 10 | cheap setup for lightning or frost | water_whip · release(mist) |
| ground `A↓` | **Creeping Fog** | 6 + 2 kg | 14/6/20 | mist rolls 5 m/s and settles as a fog zone r 4 m for 6 s (T3 r 6 m) | — | conceals (lock-on beyond 2 m breaks, AI perception +0.25 s), dampens fire −50 % and blasts, muffles sound −30 %; **lightning cast into fog hits everyone inside at 60 %** | water_draw · zone(fog) |
| sweep `A↔` | **Veil** | 5 + 1 kg | 10/8/16 | mist curtain around you 3 s | — | hard to target; your next attack from inside +20 % balance damage | water_hold · zone(mist) |
| guard `G` / `G*` | **Steam Screen** / **Condense** | 6 + 60 HU | rise 4 f | hot steam wall 2 s | CP 10 | flames passing lose heat (×1.5), solids −15 % speed, scalds walkers, blocks LOS, melts ice shards. Perfect: water/steam/mist absorbed into your waterskin; flames → steam burst | water_shield · barrier(steam) |
| push `G↑` | **Steam Blast** | 4 | 8/6/16 | the screen bursts forward: cone 5 m | P 6 + H 6 | | fire_release · cone(steam) |
| sink `G↓` | **Dew Fall** | 3 | 6/6/12 | condense all mist/steam within 6 m into rain → puddles (yours) | — | clears your own fog; sets up conduction or freezing | water_draw · burst(mist) |
| tech `T` / `T+A` | **Vapor Draw** / **Condense** | 5 | 10/4/16 | draw mist/steam (any owner, contest) into a held vapour ball; release = steam bomb (scald cloud 2.5 m); T+A: condense to a water blob (throw, or switch to Ice and freeze it) | reclaim | | water_draw/water_hold · cast(mist) |
| evade `Ev` | **Mist Step** | 5 | 0/15/8, i 15 f | dissolve and reform 4 m away (fog puff) | — | | evade_* · trail(mist) |
| evade_hold | **Fog Walk** | 6/s | — | inside your fog: untargetable by lock-on, AI perception +0.3 s, speed ×0.7 | — | | walk · zone(mist) |

### 7.8 Water / Plant (sub 3, optional — P2)

Vines grow from water (1 kg water → 1 kg vine, booked) and the ground; they burn (×3), are cut by edges, and get brittle when frozen.

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Bramble Lash** → … → **Briar Storm** | 5 + 1 kg | 10/8/18 | vine whip 5 m 30°, yanks light targets 2 m toward you → 6 m → grab & slam → 360° | K/P 6 / 10 / 16 / 24 | | water_whip · cone(vine) |
| thrust `A↑` | **Burr Shot** | 4 + 0.5 kg | 10/4/16 | 3 burrs @25; on landing they sprout snares (root 1 s, r 1 m) → 5 → bigger → small grove | K 1 each | | fire_jab · release(vine,"seed") |
| ground `A↓` | **Root Snare** → … → **Strangler Grove** | 7 + 2 kg | 18/6/20 | roots travel underground 10 m/s (cracking telegraph), erupt under the target: rooted 1.2 s → 1.5 → 1.8 s → grove r 3 m | P 12 / 16 / 22 / 30 | Ground Ping disrupts; burrowing targets can't be rooted | earth_wall · wave(roots), erupt(vine) |
| sweep `A↔` | **Thicket Fan** | 6 + 2 kg | 12/8/18 | thorn zone 120° 4 m, 4 s | P 8 | slows 40 %, catches small projectiles | water_whip · zone(briar) |
| guard `G` / `G*` | **Living Lattice** / **Catch & Sling** | 7 + 3 kg | rise 8 f | 40 kg vine wall | CP 16 | captures solids (×1.3), drinks water (+CP), burns (×0.3 vs fire), dry vines block sparks, wet vines conduct weakly. Perfect: catch and sling the projectile back | water_shield · barrier(vine) |
| push `G↑` | **Lattice Roll** | 4 | 10/18/16 | the lattice rolls forward as a tumble-ball (entangle 0.8 s) | K 10 | | earth_heavy · trail(vine) |
| sink `G↓` | **Deep Roots** | 5/s | 6/–/10 | anchored (no knockback/pull/lift); drinks puddles under you into the waterskin | anchor CP 35 | | guard · aura(vine) |
| tech `T` / `T+A` | **Vinegrip** / **Wrap**; actor → **Hook** | 5 | 8/4/18 | seize bodies ≤ 40 kg at 9 m with half the usual range falloff (wins long-range contests); hook an actor: yank 3 m toward you (−20 balance); T+A Wrap: the held body roots whoever it hits | reclaim | | earth_hold · cast(vine) |
| evade `Ev` | **Vine Swing** | 5 | 0/18/8 | swing to an anchor (wall, pillar, ledge) within 9 m, else sidestep | — | | evade_* · trail(vine) |
| evade_hold | **Canopy** | 6/s | — | hang/hover 1.5 s | — | | glide |

### 7.9 Fire / Flame (sub 0, legacy kit)

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Flare** → Blaze (*legacy*) → **Fire Column** → **Inferno** | 60 / 160 / 300 / 480 HU (reserve first) | legacy 6/5/13 | cone 4.6 m 18° → 6.5 m 26° → 7 m column + fire field 2 s → 9 m wide blaze + field 4 s | H 3 / 8 / 15 / 24 | wind: feeds below 1×, deflects 1–2×, extinguishes ≥ 2×; water quenches (steam); sand smothers. **Compat:** an actor with the legacy `lightning` kit flag still turns a ≥ 0.65 s hold into the bolt (tests); Lab/Spar kits use the Lightning sub instead | fire_jab / fire_charge / fire_release · cone(flame) |
| thrust `A↑` | **Fireball** → twin → big → **Sunfall** | 120 HU | 12/4/18 | flame body (120 HU payload) @18, bursts 2 m on impact → 2 → 240 HU, 3 m → lobbed, leaves a 4 m fire field | H 6 / 9 / 12 / 20 | a fireball is a *body*: Air can grip it (and feeds it), water quenches it, vacuum snuffs it | fire_release · release(flame,"fireball"), burst(flame) |
| ground `A↓` | **Fire Line** → double → … → **Fire Ring** | 150 HU | 16/6/20 | flames race 12 m/s along the ground for 10 m, leaving a 2.5 s burning trail → 2 lines → … → ring r 3 m around the target | H 8 / 12 / 16 / 24 | stopped by water (puddles/pool) and sand; ignites vines; warms stones it passes | fire_release · wave(fire_line) |
| sweep `A↔` | **Fire Fan** → … → **Nova** | 90 HU | 10/6/16 | 120° 3.5 m → … → 360° | H 4 / 6 / 9 / 14 | clears vines, mist | fire_release · cone(flame) |
| guard `G` / `G*` | **Flame Guard** (*legacy*) / **Heat Sink** (*legacy absorb*) | — | legacy | bare guard (CP 10) | CP 10 | perfect: absorb flame heat into the reserve (legacy) + vs a magma blob/hot rock: draw 300 HU instantly (it crusts mid-air) + vs ice: melt it | guard / deflect · aura(flame) |
| push `G↑` | **Backdraft** | the reserve | 10/6/18 | release the heat reserve as a cone 5 m (H = reserve/20) | H ≤ 25 | turns absorbed heat into a counter-attack | fire_release · cone(flame) |
| sink `G↓` | **Ground Heat** | 4 | 8/10/14 | vent the reserve into the ground (legacy VENT) + boil puddles within 2 m + melt ice floor | — | makes your footing lightning-safe | fire_release · burst(flame,"ground") |
| tech `T` | **Thermal** (*legacy* HEAT / DRAW / VENT) + **SCORCH** | legacy | legacy | + SCORCH mode (shown on the button): no grippable target but a wall/large body within 6 m → heat its face at 300 HU/s: walls slump (§5.4), metal softens, sand fuses to glass, puddles boil, mud dries | heat | **"melt the wall"** | magma_hold / heat_draw / fire_release · beam(flame) |
| evade `Ev` | **Flare Dash** | 5 + 20 HU | 0/14/8, i 7 f | 4 m dash with a 1 s burning trail | — | | air_dash · trail(flame) |
| evade_hold | **Rocket Hop** | 6/s | — | hop 2.5 m and hover 0.6 s | — | | jump / glide · trail(flame) |

### 7.10 Fire / Blue (sub 1) — concentrated heat

Blue fire costs 1.5× the heat of Flame for the same tier and carries ×1.5 heat intensity: it melts and fuses rather than spreads.

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Blue Needle** → **Blue Lance** → **Searing Beam** → **White Core** | 60 / 120 / 220 / 360 HU | 8/6/14 | beam 5 m 6° → 8 m → 10 m sustained 0.6 s → 12 m 1.0 s | H 6 / 12 / 22 / 33 | T2 melts a 20 kg stone in flight (falls short as magma); T3 melts through a stone wall (slump ≈ 1 s), fuses sand walls into glass | fire_jab / fire_release · beam(blue) |
| thrust `A↑` | **Comet Flame** → … → piercing | 100 HU | 10/4/16 | compact blue fireball @26 | H 8 / 12 / 16 / 24 | boils water shields 2× faster; melts ice walls on contact | fire_jab · release(blue,"comet") |
| ground `A↓` | **Blue Furrow** → … → **Magma Rift** | 200 HU | 16/8/20 | melts the ground along a line: a thin **lava channel** (1 kg/m, 8 m; stone from `ground_taken`) → … → 6 m wide rift, 12 m | H 10 / 14 / 20 / 30 | Fire's way to make lava without a held stone; the channel can be pushed by Earth/Magma Surge | pour · wave(magma_rift) |
| sweep `A↔` | **Corona** | 120 HU | 10/16/14 | blue ring around you r 2.5 m for 1 s → … → r 3.5 m | H 6 / 8 / 11 / 16 | melts incoming ice/small metal, burns vines | fire_charge · zone(corona) |
| guard `G` / `G*` | **Blue Aegis** | 12 Focus/s | rise 4 f | hot aura | CP 16 | incoming ice/metal ≤ 8 kg melted/vaporised (XFM) if CP ≥ TP; stones arrive as hot rock (partial); flames absorbed into the reserve (perfect ×1.5) | guard · aura(blue) |
| push `G↑` | **Flash Over** | 6 + 80 HU | 8/4/16 | the aura explodes outward r 3 m | H 10, knock 6 | | fire_release · burst(blue) |
| sink `G↓` | **Kiln** | 6 + 150 HU | 8/20/14 | superheat a body within 2.5 m (stone/metal/sand/glass) to ≥ 600 °C | — | trap: whoever seizes it next is burned (answer to Earth reclaim); sand → glass | fire_charge · beam(blue,"short") |
| tech `T` | **Smelter** | 6 + 450 HU/s | 12/–/18 | ranged heat 6 m without a grip on stone/metal/sand/ice/walls at 1.5× Scorch: walls slump, metal melts (molten metal blob), sand → glass, ice → steam | heat | **"melt it"** | heat_draw · beam(blue) |
| evade `Ev` | **Shimmer Step** | 5 | 0/8/8, i 7 f | 3 m in 0.12 s, heat haze | — | | air_dash · trail(blue) |
| evade_hold | **Afterburn** | 8/s | — | run 7 m/s leaving burning footprints | — | | run · trail(blue) |

### 7.11 Fire / Lightning (sub 2)

The conduction graph (COMBAT_SPEC §8) gains nodes: metal bodies, rods, caltrops, water bodies (streams, waves, held shields), fog zones (weak, 60 %),
charged bodies. Ice, frozen puddles, sand/glass, dry vines and **vacuum** insulate.

| Slot | Move | Cost | S/A/R | Tiers (`tier_times` 0.65 / 1.2 / 1.8 s) | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Spark** → **Bolt** (*legacy stats*) → **Storm Bolt** → **Skybreak** | 6 / 22 / 30 / 40 | 6/4/14 · bolt 0/7/24 | arc 5 m to the nearest target/conductor (stun 0.1 s, chains 2 m to wet/metal) → 14 m bolt + conduction graph → 16 m, blasts through barriers with grounding < E, forks to 2 conductors → strike from above at the target after a 0.4 s telegraph (walls between ignored), thunder deafens 0.3 s within 4 m | E 10 / 24 / 36 / 52 | **"lightning blasts through stone"** (§5.4); wet ×1.5 (legacy) | fire_jab / lightning_charge / lightning_release · beam(lightning) |
| thrust `A↑` | **Rail Arc** | 10 | 8/4/18 | straight line 16 m that **follows conductors in its path** (through a water stream to its holder, through metal) | E 14 / 20 / 28 / 36 | punishes Pressure Jets and held water | lightning_release · beam(lightning) |
| ground `A↓` | **Ground Current** → … → **Storm Grid** | 12 | 12/6/20 | current races 20 m/s along **conductive ground only** (puddles, plate, pool, caltrops, mud); dies after 2 m on dry stone → … → electrifies every connected conductive surface 1.5 s | E 12 / 18 / 24 / 32 | frozen puddles/ice floor stop it; Grounding stance is immune | lightning_release · wave(ground_current) |
| sweep `A↔` | **Arc Fan** | 12 | 10/4/18 | 3 forks 6 m 60°, each chains | E 8 × 3 | | lightning_release · beam × 3 |
| guard `G` / `G*` | **Static Ward** / **Return Current** (*legacy redirect*) | — | 0/–/6 | crackling guard | CP 12 | non-perfect vs a bolt: **absorb 50 % into static charge** (store ≤ 60), take 50 %; deflects metal projectiles (×1.5). Perfect + `redirect_current`: bolt returns at 80 % (legacy); perfect without it: full absorb | guard / deflect · aura(lightning) |
| push `G↑` | **Static Burst** | 4 | 8/4/16 | release the stored static as a cone 4 m (stun 0.3 s); empty → weak shove | E = stored | | lightning_release · cone(lightning) |
| sink `G↓` | **Grounding** | 4 | 4/–/10 | 1.5 s immune to conducted damage; drains charge from conductors you touch | — | | guard · burst(lightning,"ground") |
| tech `T` | **Conductor's Hand** → **Arc Link** | 8 | 10/–/16 | charge a conductive body within 10 m (metal, water body, puddle, fog, caltrops, rod): it stores E (decays 4/s; touching it shocks); release with aim = a bolt jumps you → the charged body → the nearest target within 8 m of it | E by hold tier | bank around cover through a puddle, a rod, an embedded lance | lightning_charge → lightning_release · beam(lightning) |
| evade `Ev` | **Arc Step** | 6 | 0/6/8, i 9 f | 6 m in 0.1 s; the trail shocks whoever it crossed (E 6) | — | | air_dash · trail(lightning) |
| evade_hold | **Overcharge** | 12/s | — | +30 % move speed, recovery ×0.8; but any water hitting you shocks **you** | — | | run · aura(lightning) |

### 7.12 Fire / Combustion (sub 3)

Combustion superheats a pocket of air until it detonates (energy from heat paid like Fire; booked as `spent`). Blasts shatter brittle things,
blow fires out (they consume the oxygen), disperse clouds; vacuum suppresses them, mist/steam dampen them (×0.5), wind fans them (+30 % radius).

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Pop** → **Burst** → **Blast** → **Detonation** | 40 / 100 / 200 / 340 HU | 8/4/14 (fuse 15 f from T1) | palm detonation r 2 m (knock 6) → at 6 m ahead r 2 m → 9 m r 3 m → 12 m r 4.5 m, 0.5 s fuse | P 8 / 16 / 22 / 34 | shatters ice/glass ×2, snuffs flames, disperses clouds | fire_jab / fire_release · burst(blast) |
| thrust `A↑` | **Spark Mine** (A↑ again = detonate) | 60 HU | 10/4/16 | ember thrown @14, sticks; detonates on proximity 1.5 m, contact or your next A↑ | P 12 / 16 / 22 / 30 | | fire_jab · release(blast,"ember") |
| ground `A↓` | **Chain Blasts** | 150 HU | 14/30/20 | 3 detonations stepping along the ground every 0.15 s → 4 → 5 → 6 | P 10 each | | fire_release · burst × n |
| sweep `A↔` | **Scatter Charges** | 120 HU | 10/4/18 | 4 embers fanned 6 m, each pops after 0.4 s | P 8 each | area denial | fire_release · release(blast,"ember") |
| guard `G` / `G*` | **Reactive Blast** | 8 Focus per trigger | 0/–/8 | when hit, detonate outward | CP 18 | deflects solids, snuffs flames (×1.5), disperses clouds; perfect: blasts light solids back (DEF↩) | guard · burst(blast) |
| push `G↑` | **Shockwave** | 80 HU | 8/4/16 | forward cone 5 m | P 14 | | fire_release · cone(blast) |
| sink `G↓` | **Smother Blast** | 80 HU | 6/4/14 | downward blast: extinguishes fire fields within 4 m and launches you 1.5 m up | P 10 | | jump · burst(blast,"ground") |
| tech `T` | **Fuse** | 6 + HU by tier | 12/–/16 | superheat an air pocket at the aim point (10 m); release detonates there | P 14 / 20 / 28 / 38 | inside vapour ×0.5; inside a vacuum it fails; inside a tornado → fire tornado; right after a Vacuum Well collapses (air inrush) ×1.5 | fire_charge → fire_release · burst(blast) |
| evade `Ev` | **Blast Jump** | 5 + 30 HU | 0/16/8, i 7 f | a detonation launches you 4 m (stick direction; up if neutral) | — | | jump / air_dash · burst(blast,"small") |
| evade_hold | **Afterglow** | 6/s | — | hover by small pops 0.8 s | — | | glide |

### 7.13 Air / Gust (sub 0, legacy kit)

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Palm Gust** → Cyclone Push (*legacy*) → **Gale** → **Hurricane Palm** | 5 / 12 / +drain | legacy 7/6/14 | cone 5.5 m 35° → 7 m 45° → 8 m 50° → 10 m 60° | P 7 / 11 / 18 / 28 | light solids ×2 (deflect), heavy ×0.6 (bend), clouds ×2 (disperse), lava ×1.0 (**T3 stalls + crusts a 20 kg wave**), fire bands (feed / deflect / extinguish), lightning PASS | air_push / air_gust · cone(wind) |
| thrust `A↑` | **Wind Crescent** → crossed → wide → **Wind Scythe** | 6 | 10/4/16 | air crescent body @22 (damage 8) → 2 crossing → 2 m wide → pierces 2 targets | P 8 / 11 / 15 / 22 | cuts vines ×2; deflects light projectiles it meets | air_push · release(wind,"crescent") |
| ground `A↓` | **Dust Devil Line** | 7 | 12/6/18 | low wind line 14 m/s, 10 m: trips (−30 balance, knock up 3), raises dust (light blind) | P 10 / 14 / 18 / 24 | | air_gust · wave(dust_line) |
| sweep `A↔` | **Crosswind** | 6 | 10/8/16 | lateral sweep 120° 5 m: shoves sideways and **curves projectiles in flight** ±40° | P 8 / 12 / 16 / 22 | | air_gust · cone(wind) |
| guard `G` / `G*` | **Wind Guard** (*legacy*) / **Return Wind** | — | legacy | wind around you | CP 12 | deflects light solids (×1.5), fire bands; perfect: light projectiles, flames, steam and sand go **back to the sender** | guard · aura(wind) |
| push `G↑` | **Wall of Wind** | 6 | 8/30/16 | moving wind wall 8 m/s, 3 m wide, 6 m | P 14 | pushes actors, deflects light projectiles, carries clouds | air_gust · wave(wind_wall) |
| sink `G↓` | **Downdraft** | 5 | 6/4/14 | down-blast r 3 m | P 12 | slams airborne enemies down (anti-flight), flattens fire fields, clears clouds | air_push · burst(wind,"down") |
| tech `T` | **Updraft** (*legacy*, + glide) / **Wind Grip** | 15 / 6 | legacy | context: a light body (≤ 30 kg: stone, water, ice, sand cloud, steam, mist, **fireball**, ember) in the aim cone within 9 m → Wind Grip (seize, fling); otherwise updraft (legacy) | reclaim | a gripped fireball grows +20 % (fed) — catch and return it | jump / glide · cast(wind) |
| evade `Ev` | **Air Dash** (*legacy*) | legacy | legacy | 4.6 m | — | | air_dash · trail(wind) |
| evade_hold | **Glide** (*legacy, via technique*) / **Tailwind** run | 6/s | — | Tailwind: ×1.3 run speed while held | — | | glide / run |

### 7.14 Air / Vortex (sub 1)

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Twister** → twin → **Tornado** → **Cyclone Fortress** | 6 / 10 / +drain | 12/4/18 | small vortex @12 m/s for 1.2 s (lifts light targets, captures small projectiles it passes) → 2 → tornado zone r 2.5 m, walks 4 m/s toward the target for 4 s → r 4 m for 6 s | P 8 / 12 / 25 / 35 | captures/orbits solids ≤ 30 kg; lifts actors (juggle); **infusions**: sand → sandstorm (blind, abrade), fire → fire tornado, water → water tornado (**conducts**), steam → scalding; infused by the enemy → the zone turns **neutral** (hurts both); stalls lava when ratio ≥ 1, else picks up spatter (magma vortex) | air_gust · release(vortex) / zone(tornado) |
| thrust `A↑` | **Spiral Lance** | 7 | 12/4/18 | drilling spiral @24 | P 10 / 14 / 18 / 24 | pierces mist/sand clouds and sand walls; deflected by stone/metal walls | air_push · release(wind,"spiral") |
| ground `A↓` | **Dust Funnel** | 8 | 14/6/20 | ground vortex 10 m/s: gathers loose bodies on its path and carries them to the target | P 10 / 14 / 20 / 28 | turns the arena's rubble into ammunition | air_gust · wave(funnel) |
| sweep `A↔` | **Eddy Ring** | 6 | 8/24/14 | ring vortex r 3 m for 1.5 s | P 10 / 14 / 18 / 24 | incoming projectiles curve around you (orbit-deflect, sometimes back at the thrower); light actors pushed out | air_gust · zone(eddy) |
| guard `G` / `G*` | **Vortex Wall** / **Vortex Catch** | 4/s | rise 4 f | spinning barrier | CP 16 | **captures** light projectiles (they orbit inside) instead of stopping them; heavier pass bent; flames are fed and spun (a fire vortex beside you) | air_gust (loop) · barrier(vortex) |
| push `G↑` | **Unleash** | 4 | 8/4/16 | fling everything captured at the target (each keeps mass/heat; now your attack) | as captured | "catch them all and return" | air_push · release(vortex) |
| sink `G↓` | **Funnel Down** | 4 | 6/8/14 | drive the vortex into the ground: dust crater, drop captured bodies at your feet | — | then seize them with Earth/Water | air_push · burst(wind,"ground") |
| tech `T` | **Eye of the Storm** | 10 + 10/s | 14/–/18 | create a tornado at the aim point (10 m) and steer it while holding (drag); release: it lives 2 s more. On an enemy tornado: contest (larger absorbs smaller) | P 25 / 28 / 32 / 35 | | air_gust / glide · zone(tornado) |
| evade `Ev` | **Spin Step** | 5 | 0/16/8, i 10 f | spinning sidestep that deflects light projectiles during its i-frames | — | | evade_* · trail(vortex) |
| evade_hold | **Whirl Lift** | 8/s | — | hover 1.5 m in a small vortex (immune to ground lines) | — | | glide · zone(vortex) |

### 7.15 Air / Vacuum (sub 2) — pressure and absence

A vacuum holds no fire, carries no sound and **insulates against lightning**; solids and liquids pass straight through it.

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Pressure Palm** → **Air Cannon** → **Implode** → **Collapse** | 6 / 10 / 14 / 20 | 8/4/16 | point-blank burst (knock 8, −30 balance) → pressure bullet 12 m → vacuum point 8 m ahead for 0.4 s (pulls 3 m) then collapses → r 4 m pull then crush (−60 balance) | P 10 / 14 / 22 / 30 | flames inside a vacuum die; sound inside dies | air_push · burst(vacuum) |
| thrust `A↑` | **Suction Line** | 6 | 10/12/16 | suction beam 10 m: pulls the target 2 m toward you (or you toward a heavy anchor/wall); yanks light projectiles into your hand | P 8 / 12 / 16 / 22 | REC of projectiles ≤ 12 kg | air_push · beam(vacuum) |
| ground `A↓` | **Pressure Mine** | 6 | 10/4/14 | trap r 1.2 m for 10 s; bursts when stepped on (knock up 7) | P 12 / 16 / 22 / 28 | | air_push · zone(mine) |
| sweep `A↔` | **Vacuum Arc** | 6 | 8/6/14 | near-vacuum crescent 100° 4 m | P 8 | snuffs flames in the arc (×2), silences sound, small pull | air_gust · cone(vacuum) |
| guard `G` / `G*` | **Null Bubble** / **Vacuum Catch** | 6/s | rise 4 f | vacuum shell r 1.8 m | CP 14 | **EXT** fire and combustion (×2), nullifies sound (×3), collapses mist/steam, **blocks lightning with E ≤ 1.5·CP** (partial above: bolt weakened); solids/liquids PASS. Perfect: catch a light projectile and spit it back | guard · barrier(vacuum) |
| push `G↑` | **Pressure Wave** | 4 | 8/4/16 | bubble collapse ring r 3 m | P 16 | | air_push · ring(vacuum) |
| sink `G↓` | **Anchor** | 4/s | 4/–/10 | low-pressure anchor | anchor CP 35 | immune to knockback, pull, tornado lift | guard · aura(vacuum) |
| tech `T` | **Vacuum Well** | 8 + 8/s | 12/–/16 | suction zone at the aim point (9 m, r 3 m) | P 10 / 14 / 20 / 28 | pulls projectiles out of the air (they drop in the well), pulls clouds in and **compresses** them (steam → water, sand → sandstone chunk), drags actors (−40 % moving away), suppresses fire; release = collapse (air inrush: a combustion there ×1.5) | air_gust · zone(vacuum_well) |
| evade `Ev` | **Pressure Hop** | 5 | 0/18/6, i 9 f | leap 3 m up/forward, float landing (no landing lag) | — | | jump · trail(vacuum) |
| evade_hold | **Slipstream** | 6/s | — | ×1.3 speed; projectiles behind you slowed | — | | run · trail(vacuum) |

### 7.16 Air / Sound (sub 3) — resonance and flight

| Slot | Move | Cost | S/A/R | Tiers | Power | Interactions / notes | Anim · FX |
|---|---|---|---|---|---|---|---|
| strike `A` | **Clap** → **Shout** → **Roar** → **Resonance** | 4 / 8 / 12 / 18 | 6/6/14 | cone 3 m 60° (daze 0.15 s) → 7 m 30° → 10 m → 12 m | P 8 / 14 / 22 / 32 | **Disrupts** charges/channels if P ≥ cohesion; T1 shatters ice/glass in the cone; T2 shatters ice/glass walls and stone walls ≤ 80 kg, rains out mist, −8 Focus on hit; T3 shatters any brittle body, **deafens** 2 s (techniques cost ×1.5). Absorbed by sand/fog/vines; **reflected by stone/metal walls** (back at you); nullified by vacuum; passes through water | air_push / air_gust · cone(sound) |
| thrust `A↑` | **Sound Lance** | 6 | 10/4/16 | focused pulse 14 m @34 m/s (visible ring) | P 10 / 14 / 20 / 28 | passes through water shields and mist; **reflects off stone/metal walls** (bank shots) | air_push · beam(sound) |
| ground `A↓` | **Tremor Hum** | 7 | 14/6/18 | ground hum 18 m/s, 10 m | P 12 / 16 / 22 / 30 | knocks over (−35 balance), cracks walls it hits, pops loose stones into the air (seizable from range) | earth_wall · wave(tremor) |
| sweep `A↔` | **Echo Ring** | 5 | 8/4/14 | 360° r 4 m | P 10 | reveals concealed (fog, Mist Step, Fog Walk), interrupts channels | air_gust · ring(sound) |
| guard `G` / `G*` | **Sound Barrier** / **Echo Return** | — | 0/–/6 | vibrating front | CP 12 | shatters brittle incoming (ice/glass ×2.5), deflects light solids (×1.0); perfect: reflects incoming sound back | guard · barrier(sound) |
| push `G↑` | **Thunder Step** | 6 | 6/10/14 | 4 m forward boom-dash | P 14 | knockback in front | air_dash · trail(sound), burst |
| sink `G↓` | **Ground Ping** | 4 | 6/4/12 | ground pulse r 6 m (strength by guard hold) | P 12 / 16 / 22 / 30 | reveals burrowers; **disrupts ground lines** (spike lines, currents, roots, surges) whose power ≤ P | earth_wall · ring(sound,"ground") |
| tech `T` | **Flight** | 6 + 10/s | 10/–/12 | rise to 2.2 m, fly 6 m/s with all attacks usable; release = glide down | — | airborne: immune to ground lines, quicksand, ice floor, ground current; vulnerable: gusts/tornado (×1.5 balance), lightning (no grounding) | jump / glide · trail(sound) |
| evade `Ev` | **Boom Step** | 6 | 0/12/6, i 9 f | 6 m supersonic dash, boom at the end (knock 3 in r 2 m) | — | | air_dash · trail(sound), burst |
| evade_hold | **Hover** | 6/s | — | hold your height | — | | glide |

---

## 8. Counter matrix

How to read: each cell is the **best answer of that sub-element** to the threat, as `OUTCOME move [condition]`. The power rule (§5) decides
full / partial / fail; the condition gives the tier or power needed against the reference threat in the row header. `*` = perfect timing.
A cell that says FAIL means "evade or switch": it is a deliberate, physical weakness. Every **element** has at least one working answer to
every threat (element summary §8.5). The engine implements these cells as rules keyed by *(threat class, counter class)* (§15.3).

### 8.1 Earth responds (Stone · Metal · Sand · Magma)

| Threat (ref. TP) | Stone | Metal | Sand | Magma |
|---|---|---|---|---|
| **Stone shot** ≤30 kg (17) | `DEF↩` Bulwark* · `BLK` Bulwark (30) · `SNK` Swallow (22) · `REC`+split Seize → T+A | `BLK` Aegis (20) · `DEF` Aegis* · `CAP` Chain Arc T1+ | `CAP` Dune Wall (30 eff) · `REC` Engulf* (spit back) · `SNK` Quicksand | `CAP` Magma Curtain (sticks, heats) · `SNK` Melt Pit T2+ |
| **Heavy stone** 31–80 kg (32–44) | `DEF↩` Bulwark* (45) · `BLK` Bulwark held ≥1 s (40) · `SNK` Swallow T2+ · `REC` Seize (≤80 kg, slow contest) | `WKN` Aegis (plate knocked back, −30 balance) | `CAP` Dune Wall held T2 · `SNK` Quicksand T2+ | `WKN` Curtain (wall damaged) |
| **Boulder** >80 kg (110) | `FAIL` alone → stack: Swallow T3 (−55) then Ram Wall clash (54) | `FAIL` | `WKN` Quicksand T3 (bogged on landing) | `FAIL` |
| **Hot rock** (27) | `BLK` Bulwark (wall heats) · `SNK` Swallow T1+ · `REC` Seize (burns your hands unless Lava Wade) | `WKN` Aegis (plate heats) | `CAP` Dune Wall (30 eff ≥ 27) | `REC` Magma Hold (no burn; re-melts cheaply) |
| **Magma blob** (35) | `WKN` Bulwark (splashes, pools at its foot) · `SNK` Swallow T2+ (drains into the earth) | `FAIL` (plate red-hot → dropped) | `XFM→glass` Dune Wall (×1.5: encased, crusted) | `REC` Magma Hold · `ABS` Curtain |
| **Lava wave** (27 / 62) | `BLK` Bulwark (legacy) / big: `WKN` · `SNK` Swallow trench T1+ · `BLK` Rising Fangs spikes | `FAIL` (jump/evade) | `XFM→glass crust` Dune Wall / Quicksand / Sand Surge (×1.5) | `REC` **Reverse Tide** (contest vs the pourer) · clash Magma Surge (bigger wave wins/merges) |
| **Metal** disc/lance (2–25) | `BLK` Bulwark (lances embed → rods) · `SNK` Swallow | `REC` **Magnet Catch* / Lodestone Grip** (×1.3) | `CAP` Dune Wall (Railspike: partial pierce) | `XFM` Curtain (small metal sticks, heats, melts at T2 heat) |
| **Sand** slug/cloud (5–16) | `BLK` Bulwark (clouds roll over) | `BLK` Aegis | `REC` Sandform · `ABS` Dune Wall (adds mass) | `XFM→glass beads` Curtain face |
| **Sand surge** (14–34) | `BLK` Bulwark / Fangs · `SNK` Swallow | `FAIL` (jump) | `REC` Sandform contest / clash Sand Surge | `XFM→glass` Melt Pit (stalls) |
| **Water** whip/stream (8–10) | `BLK` Bulwark | `BLK` Aegis (now wet: conductive) | `ABS` Dune Wall → mud wall (+5 CP) | `XFM→steam` Curtain (face → obsidian +10) |
| **Water wave** (18–45) | `BLK` Bulwark (30) · `WKN` Swallow (diverts) | `FAIL` | `ABS→mud` Dune Wall / Quicksand (×1.5) | `XFM→steam` Melt Pit / Curtain (mutual: lava → obsidian) |
| **Ice** shard/lance (6–15) | `BLK` Bulwark (shatters) · `SNK` Swallow | `BLK` Aegis · `DEF` Chain Arc | `CAP` Dune Wall | `XFM→steam` (×3) |
| **Mist / fog** | `FAIL` (walk out) | `FAIL` | `ABS` Veil of Grit (mud rain clears it, ×1.5) | `XFM` evaporate (Lava Lash / Spatter clears a corridor) |
| **Steam** (3–9) | `BLK` Bulwark (jets) | `BLK` Aegis (heats) | `ABS` Dune Wall (partial) | `FAIL` (hot vs hot) |
| **Vines** (6–30) | `BLK` Bulwark · `WKN` Rubble Fan | `XFM→cut` Razor Disc / Chain Arc (edge ×2) | `WKN` Sandblast (abrade ×2) | `XFM→burn` Lava Lash / Spatter (×3) |
| **Flame** (4–25) | `BLK` Bulwark (wall heats) | `BLK` Aegis (≥300 °C → drop) | `EXT` Dune Wall / Veil (smother ×2) | `ABS` Curtain (flames keep it molten) |
| **Blue fire** (6–33) | `WKN` Bulwark → wall **slumps** at T2+ | `FAIL` (plate melts) | `XFM→glass wall` (blocks that hit) | `ABS` (heat into lava) |
| **Lightning** (10–52) | `GND` Bulwark (E ≤ 30; Storm Bolt shatters it, passes with E−15) · legacy grounded stance (Earth guard on stone takes 40 %) | `GND` **Rod Plant** (cap 60) · `GND` Aegis on stone/sand (✗ `CND` into you in water/on the plate) | `GND`+`XFM→glass` Dune Wall (cap 60) | `GND` Curtain (stone-wall rule) |
| **Combustion** (8–38) | `BLK` Bulwark (damaged) | `BLK` Aegis (knocked back) | `ABS` Dune Wall (shock ×1.5) | `WKN` Curtain (splashes) |
| **Gust / crescent** (7–28) | `BLK` Bulwark | `BLK` Aegis | `BLK` Dune Wall (sand spray blinds the gust user) | `BLK` Curtain |
| **Tornado** (25–35) | `BLK` Bulwark (stalls against it) · Stone Skin anchor (no lift) | Iron Stance anchor | `AMP` ✗ (feeds a sandstorm) | `AMP` ✗ (magma vortex) |
| **Vacuum / suction** (8–30) | Stone Skin anchor · `BLK` Bulwark (breaks the suction line) | Iron Stance anchor | `FAIL` (sucked into sandstone) | `FAIL` |
| **Sound** (8–32) | `RFL` Bulwark (echo back at the sender) | `RFL` Aegis (×1.2) | `ABS` Dune Wall / Veil (×1.5) | `BLK` Curtain (obsidian face shatters at T3) |

### 8.2 Water responds (Water · Ice · Mist · Plant\*)

| Threat (ref. TP) | Water | Ice | Mist | Plant\* |
|---|---|---|---|---|
| **Stone shot** (17) | `CAP`→`DEF↩` **Tidal Rush** (carried back) · `BLK` Cushion* · `SLW` Shield | `BLK` Ice Wall (22) · `CAP` frost-lock* | `PASS` (fog breaks the thrower's lock: aimed throws lose lead) | `CAP` Lattice (21 eff) · `REC` Catch & Sling* · `REC` Vinegrip (9 m) |
| **Heavy stone** (32–44) | `CAP` Tidal Rush T2+ (32) · `SLW` Shield | `BLK` Ice Wall held (30) / `WKN` | `PASS` | `WKN` Lattice |
| **Boulder** (110) | `WKN` Deluge T3 (slowed) | `FAIL` (wall crushed) | `FAIL` | `FAIL` |
| **Hot rock** (27) | `XFM→cold stone` Shield (×2.5 vs H) + `SLW` · Spray Fan cools | `BLK`+steam Ice Wall | `WKN` Steam Screen | `FAIL` (vines ignite) |
| **Magma blob** (35) | `XFM→rock` Shield (quench; arrives cold and slowed) | `WKN` Ice Wall (mutual melt/crust) | `WKN` Steam Screen | `FAIL` (burns) |
| **Lava wave** (27 / 62) | `XFM→rock` **Tidal Rush** (×1.5 H; T1+ full) | `XFM→obsidian crust` Rime Path T1+ · `WKN` Ice Wall | `WKN` fog | `FAIL` |
| **Metal** (2–25) | `DEF` Pressure Jet · `SLW` Shield | `BLK` Ice Wall | `PASS` | `CAP` Lattice (edges cut it: partial) |
| **Sand** slug/cloud (5–16) | `XFM→mud` Spray Fan / Shield (×2) | `BLK` Ice Wall | `XFM→mud rain` Creeping Fog (×1.5) | `CAP` Lattice (filters) |
| **Sand surge** (14–34) | `XFM→mud` Tidal Rush (×1.5) | `BLK` Ice Wall | `WKN` fog (wet sand slows) | `CAP` Root Snare / Lattice (roots bind soil ×1.5) |
| **Water** whip/stream (8–10) | `REC` Draw (seize in flight) · `ABS` Swallow Current* | `XFM→ice` **Flash Freeze*** · `BLK` Ice Wall | `ABS` Condense* | `ABS` Lattice (grows) |
| **Water wave** (18–45) | `REC` / clash Tidal Rush (bigger wins, merge) | `XFM→ice ridge` **Rime Path / Flash Freeze** (the wave becomes a wall) | `PASS` | `ABS` Deep Roots / Lattice (partial) |
| **Ice** (6–15) | `ABS` Shield (melts into it) | `REC` Freeze-Draw · `BLK` Ice Wall | `XFM→water` Steam Screen (×1.5) | `CAP` Lattice |
| **Mist / fog** | `REC` Draw (condense) | `XFM→snow` Hoarfrost (clears) | `REC` **Vapor Draw** | `ABS` leaves (partial) |
| **Steam** (3–9) | `ABS` Shield (condenses into it) | `XFM→frost` Ice Wall (×2) | `REC` Vapor Draw / Condense* | `FAIL` (scalds) |
| **Vines** (6–30) | `AMP` ✗ (water feeds vines) | `XFM→brittle` Hoarfrost (then any hit shatters them) | `AMP` ✗ | `REC` Vinegrip contest / clash |
| **Flame** (4–25) | `XFM→steam` **Shield** (legacy steam block) · `EXT` Tidal Rush / Spray on fields | `WKN` Ice Wall (melts while blocking) | `WKN`/`EXT` Steam Screen / fog (×1.5) | `AMP` ✗ (burns) |
| **Blue fire** (6–33) | `WKN` Shield (boils fast, ×1.2) | `FAIL` (melts) | `WKN` | `AMP` ✗ |
| **Lightning** (10–52) | `CND` ✗ Shield conducts to you (wet ×1.5) → evade | `BLK` **Ice Wall** (insulator, eff 1.5: stops the T1 bolt; Storm Bolt shatters it) | `CND` ✗ fog (all inside at 60 %) | `GND` wet vines / `BLK` dry (sparks only) |
| **Combustion** (8–38) | `WKN` Shield | `SHT` ✗ Ice Wall (brittle ×0.5) | `WKN` **fog / steam dampen** (×1.5) | `FAIL` (shredded) |
| **Gust / crescent** (7–28) | `WKN` Shield | `BLK` Ice Wall | `PASS` ✗ (your fog is blown away) | `BLK` Lattice partial (crescents cut it) |
| **Tornado** (25–35) | `WKN→collapse` **Tidal Rush "drown it"** (water mass ≥ P) | `BLK` Ice Wall | `FAIL` | Deep Roots anchor |
| **Vacuum / suction** (8–30) | `FAIL` (the shield boils in a vacuum) | `FAIL` | `FAIL` | **Deep Roots** anchor |
| **Sound** (8–32) | `PASS` ✗ (water carries sound to you ×1.2) | `SHT` ✗ (ice shatters) | `ABS` **fog** (×1.2) | `ABS` Lattice |

### 8.3 Fire responds (Flame · Blue · Lightning · Combustion)

| Threat (ref. TP) | Flame | Blue | Lightning | Combustion |
|---|---|---|---|---|
| **Stone shot** (17) | `REC`+`XFM→lava` **Thermal magma grip** (flagship) | `XFM→magma` Searing Beam T2 (falls short) | `SHT` Bolt T1 (24 ≥ 17: splits into 3 that scatter) | `DEF` Reactive Blast (18) · `SHT` Detonation T3 |
| **Heavy stone** (32–44) | `REC` Thermal (≤80 kg, slow melt) | `XFM` White Core T3 | `SHT` Storm Bolt T2 | `WKN` Blast T2 · `SHT` Detonation T3 |
| **Boulder** (110) | `FAIL` | `WKN` T3 (softens) | `WKN` Skybreak (splits in 2) | `WKN` Detonation (stack) |
| **Hot rock** (27) | `ABS` DRAW (legacy) · Heat Sink* | `XFM→magma` (latent only: cheap) | `SHT` T2 | `DEF` T3 |
| **Magma blob** (35) | `ABS` DRAW / Heat Sink* (crusts mid-air) | `FAIL` | `FAIL` | `WKN→SHT` spatter (✗ burning droplets) |
| **Lava wave** (27 / 62) | `ABS` **DRAW** (legacy: sets to rock) | `FAIL` | `FAIL` | `WKN` Blast (splits the front) |
| **Metal** (2–25) | `FAIL` | `XFM→molten droplets` Blue Aegis | `DEF` Static Ward (×1.5) · `CND` Spark charges it | `DEF` Reactive Blast |
| **Sand** slug/cloud (5–16) | `FAIL` (smothered) | `XFM→glass` Searing Beam | `XFM→glass` (bolt −50 %, fulgurite) | `DEF` disperse (×2) |
| **Sand surge** (14–34) | `FAIL` | `XFM→glass` Blue Furrow T2+ (front) | `FAIL` (sand insulates) | `WKN` Chain Blasts |
| **Water** whip/stream (8–10) | `XFM→steam` Fire Column (×0.6, partial) | `XFM→steam` Blue Lance T1 | `CND→caster` **Rail Arc** (while it's still connected) | `DEF` disperse into spray |
| **Water wave** (18–45) | `FAIL` (Inferno: partial steam) | `WKN` | `CND` (shocks its owner if still connected) | `WKN` Blast |
| **Ice** (6–15) | `XFM→water` Flare (×2, legacy) | `XFM→steam` | `SHT` Spark / Bolt (thermal shock) | `SHT` Pop / Burst (×2) |
| **Mist / fog** | `XFM` evaporate (×1.5) | `XFM` clear | `CND` **into the fog** (60 % to everyone inside: flushes hiders) | `DEF` disperse |
| **Steam** (3–9) | `FAIL` | `FAIL` | `PASS` (weak) | `DEF` disperse (dampened ×0.5) |
| **Vines** (6–30) | `XFM→burn` (×3) | `XFM→burn` | `XFM→burn` T1 (partial) | `SHT` shred (×1.5) |
| **Flame** (4–25) | `ABS` **Heat Sink*** (legacy) | `ABS` Blue Aegis* (×1.5) | `FAIL` | `EXT` **Reactive Blast / Shockwave** (oxygen, ×1.5) |
| **Blue fire** (6–33) | `ABS` Heat Sink* (×0.7) | `ABS` Blue Aegis* | `FAIL` | `EXT` partial (×1.0) |
| **Lightning** (10–52) | `DEF↩` **Return Current*** (legacy, needs `redirect_current`) | `FAIL` | `ABS` **Static Ward** (50 % stored; perfect: return/absorb) · Grounding vs conducted | `FAIL` |
| **Combustion** (8–38) | `FAIL` | `FAIL` | `FAIL` | `DEF` clash counter-blast (bigger wins) |
| **Gust / crescent** (7–28) | `FAIL` (wind feeds flames) | `FAIL` | `FAIL` | `DEF` clash Reactive Blast |
| **Tornado** (25–35) | `AMP` ✗ fire tornado (neutral hazard) | `AMP` ✗ | `CND` only if water-laden | `DEF` disrupt: Detonation inside (P ≥ tornado P) |
| **Vacuum / suction** (8–30) | `FAIL` (flames die) | `FAIL` | `FAIL` (vacuum insulates) | `DEF` **"fill the void"**: Burst into the well (P ≥ pull) |
| **Sound** (8–32) | `FAIL` | `FAIL` | `FAIL` | `WKN` clash (×0.6) |

### 8.4 Air responds (Gust · Vortex · Vacuum · Sound)

| Threat (ref. TP) | Gust | Vortex | Vacuum | Sound |
|---|---|---|---|---|
| **Stone shot** (17) | `DEF` **Wind Guard** (×1.5 = 18) · `DEF↩` Return Wind* · `BND` Palm Gust T0 · `DEF` Cyclone T1 | `CAP` Vortex Wall / Tornado → `REC` Unleash | `CAP` Vacuum Well · `REC` Vacuum Catch* | `BND` Sound Barrier · `SHT` Roar T2 (resonance) |
| **Heavy stone** (32–44) | `BND` Gale T2 (×0.6; legacy bend) | `BND` Tornado (×0.8, slowed) | `WKN` Vacuum Well | `SHT` Resonance T3 |
| **Boulder** (110) | `FAIL` | `FAIL` | `FAIL` | `FAIL` |
| **Hot rock** (27) | `XFM` crust + `BND` Gust · `DEF` Gale T2 | `CAP` + cools Tornado | `CAP` Well | `SHT` Roar T2 (hot rock is brittle ×1.3) |
| **Magma blob** (35) | `WKN` Hurricane T3 (crust, bend) | `AMP` ✗ magma vortex unless T3 (35) → `XFM→rock` | `PASS` | `FAIL` |
| **Lava wave** (27 / 62) | `FAIL` T0–T1 · `WKN` Gale T2 (crust, slow) · **`XFM` stall + crust Hurricane T3** | `WKN` Tornado T2 · `XFM` Cyclone Fortress T3 (`AMP` if partial) | `FAIL` | `FAIL` |
| **Metal** (2–25) | `DEF` Wind Guard (×2) | `CAP` Vortex Wall | `REC` **Suction Line** | `DEF` Sound Barrier |
| **Sand** slug/cloud (5–16) | `DEF` disperse (×2, back at the sender) | `REC` → your **sandstorm** | `XFM→sandstone` Well | `FAIL` (sand absorbs) · `WKN` Roar T3 |
| **Sand surge** (14–34) | `WKN` Gale | `CAP` Dust Funnel / Tornado | `CAP` Well (mound) | `WKN→stop` **Ground Ping** (×1.5) |
| **Water** whip/stream (8–10) | `DEF` scatter (×1.0) | `CAP` water vortex (✗ conducts) | `CAP` Well (floating ball) | `FAIL` · `WKN` Roar atomises |
| **Water wave** (18–45) | `WKN` Gale · split T3 | `CAP` Tornado (water tornado) | `WKN` | `FAIL` |
| **Ice** (6–15) | `DEF` (×2) | `CAP` | `CAP` / `REC` | `SHT` **Clap+** (×2.5) |
| **Mist / fog** | `DEF` disperse (legacy) | `CAP` | `XFM` collapse (condense) | `XFM` rain (Roar) · Echo Ring reveals |
| **Steam** (3–9) | `DEF` disperse (legacy) | `CAP` (scalding vortex) | `XFM` collapse | `PASS` |
| **Vines** (6–30) | `XFM→cut` **Wind Crescent** (×2) | `WKN` Tornado tears | `FAIL` | `WKN` Roar |
| **Flame** (4–25) | bands: `<1` `AMP` fanned · `1–2` `DEF` (perfect: back) · `≥2` `EXT` (legacy air guard blocks fire) | `AMP` ✗ fire tornado (`EXT` at ≥ 2×) | `EXT` **Null Bubble / Vacuum Arc** (×2) | `WKN` Roar T2+ (sonic extinguish of small flames) |
| **Blue fire** (6–33) | bands (needs 2× to `EXT`: Hurricane 28 vs 12) | `AMP` ✗ | `EXT` (×2) | `FAIL` |
| **Lightning** (10–52) | `PASS` ✗ | `PASS` (`CND` ✗ if water-laden) | `BLK` **Null Bubble** (vacuum insulates, eff 1.5: stops sparks; weakens a bolt) | `PASS` ✗ |
| **Combustion** (8–38) | `WKN` (×0.8) | `AMP` ✗ / `WKN` | `EXT` **Null Bubble** (×2) | `WKN` clash (×0.6) |
| **Gust / crescent** (7–28) | `DEF` counter-wind · Return Wind* | `ABS` (spins your vortex faster) | `ABS` Null Bubble (×1.5) | `WKN` (×0.8) |
| **Tornado** (25–35) | `DEF` dissipate Hurricane T3 (≥ P) | `REC` **Eye of the Storm** contest / merge | `ABS` collapse Vacuum Well (×1.2) | `WKN` (×0.6) |
| **Vacuum / suction** (8–30) | `DEF` **fill it** (×1.2) | clash | `REC` / merge | `FAIL` (no medium) |
| **Sound** (8–32) | `WKN` (×0.5) | `WKN` (×0.6) | `EXT` **nullify** (×3) | `RFL` **Echo Return*** / anti-phase cancel |

### 8.5 Element-level summary (best answers; every pair has at least one)

| Threat element ↓ / answering element → | Earth | Water | Fire | Air |
|---|---|---|---|---|
| **Earth** (stone, metal, sand, lava) | walls, Swallow, reclaim (Seize, Magnet, Sandform, Magma Hold) | Tidal Rush carries stones back, quenches lava, sand → mud; Ice Wall | magma grip (stone → lava), DRAW sets lava, bolts shatter stone, Blue melts metal / fuses sand | deflect light solids, capture in vortices; **lava needs T3 gale/tornado**; heavy stones only bend |
| **Water** (water, ice, mist/steam, vines) | walls; sand drinks water (mud); magma boils it (obsidian); blades cut vines | reclaim and freeze (wave → ice ridge), condense | boil streams, melt ice, burn vines, bolt into fog/streams | disperse mist/steam, shatter ice (Sound), cut vines, **drown-proof**: water drowns tornadoes, not the reverse |
| **Fire** (flame, blue, lightning, combustion) | walls block flames, sand smothers and grounds (→ glass), rods ground bolts; **charged bolts blast through stone** | water quenches flame; **ice insulates bolts**; fog dampens blasts — but water conducts lightning | absorb heat (Heat Sink, Aegis), return/absorb bolts, counter-blast | strong wind blows fire out (weak wind feeds it); **vacuum insulates bolts and suffocates fire/blasts** |
| **Air** (gust, vortex, vacuum, sound) | walls block wind/tornado paths and reflect sound, anchors resist lift/pull, sand absorbs sound | water drowns tornadoes, fog muffles sound, roots anchor | blasts disrupt tornadoes and fill vacuums | counter-wind, reclaim tornadoes, vacuum nullifies sound, echo return |

### 8.6 Barriers: what breaks what

| Barrier | Strong against | Breakers (weak to) | Ignored by | Strengthened by |
|---|---|---|---|---|
| **Bulwark** stone wall (CP 30–50) | solids, waves, flames, bolts ≤ CP, sound (reflects) | **Scorch / Smelter → slump** (then Magma Surge it back), **Storm Bolt** (blast through), Ram Wall / heavy stones (crumble), Detonation T3, Roar T3 (≤ 80 kg walls), Sandblast (abrade), Tremor Hum (cracks) | Skybreak (from above), Burrow Step (under) | holding guard longer (thicker) |
| **Aegis** metal plate (20) | blades, stones, flames (but heats) | heat over time (red-hot drop), lightning while you stand in water/on the plate, **Lodestone Grip steals it** (magnet contest), heavy impacts | — | Iron Stance |
| **Dune** sand wall (25–40) | solids (captures), fire, bolts (→ glass), sound, blasts | water (→ mud, collapses after 4 s), Gale scour (×1.5), Spiral Lance (drills porous walls), Sand Surge clash | Skybreak | — (turns to glass when struck by lightning/blue fire) |
| **Glass** wall (fused sand, 30) | solids, fire (until T3 blue), bolts (insulator) | **sound** (×2.5), blasts (×2), heavy stones | — | — |
| **Magma Curtain / obsidian** (28 / 38) | small solids (absorbed), ice, vines, flames | sound (obsidian is brittle), sand (smothers the face), heat DRAW (face → plain stone) | — | water (sets obsidian: harder, but steam scalds the holder) |
| **Water Shield** (6–12) | fire (quench), small solids slowed | **lightning** (conducts to you), blue fire (boils), heavy solids, vacuum (boils), sound (passes) | — | drawing more water |
| **Ice Wall** (22–38) | solids, **bolts ≤ 1.5·CP** (insulator), water (freezes in), steam | fire / blue / lava (melt), **sound** (×2.5), blasts, Storm Bolt (thermal shock) | — | standing next to water (more mass) |
| **Steam Screen / fog** | fire, sight lines | wind (disperse), vacuum (collapse), ice (condense/freeze), sand (absorb); **lightning conducts through fog** | solids | more water/heat |
| **Living Lattice** (16) | solids (captures), water (grows) | fire / blue / magma (×3), blades (metal, wind crescent ×2), frost + any hit, Sandblast | — | water |
| **Flame Guard / fire field** | ice, vines | water (×2.5), sand (×2), vacuum (×2), blast (×1.5), strong wind (≥ 2×) | — | **weak wind** (fanned) |
| **Blue Aegis** (16) | ice, small metal, vines | water (×1.2), vacuum (×2) | heavy solids pass (as hot rock) | — |
| **Static Ward** (12) | bolts (stores them), metal projectiles | everything non-electric passes | stones | — |
| **Reactive Blast** (18) | light solids, flames, clouds | vacuum (suppressed), mist/steam (dampened), heavy solids | — | gust (+30 % radius) |
| **Wind Guard** (12) | light solids, clouds, weak fire | dense threats (lava, boulders), lightning (passes), vacuum (absorbs the wind) | lightning | — |
| **Vortex Wall** (16) | light solids (captures for Unleash) | heavy solids, **water mass** (drowned), Vacuum Well (collapses it), counter-vortex | — | gusts (spin faster) |
| **Null Bubble** (14) | fire, combustion, sound, **bolts ≤ 1.5·CP**, mist/steam | gust "fill" (×1.2), any solid/liquid simply passes | solids, liquids | — |
| **Sound Barrier** (12) | ice/glass (shatters), sound, light solids | vacuum (nullifies it), sand (absorbs), heavy solids | — | — |

---

## 9. Combos, cancels and tricks

### 9.1 Cancel rules (core)

| Rule | Window | Cost | Notes |
|---|---|---|---|
| **Chain** (same sub-element): strike → thrust / ground / sweep | from the move's `chain` fraction of recovery (default 0.25) **after contact** (hit, block, deflect, clash); whiffs only after the legacy `cancel` fraction | — | a chain string is at most 3 moves; each slot once per string |
| **Weave** (element or sub-element switch) | during a chain window, a press of the newly selected element/sub-element cancels at once | +6 Focus | once per string; the switch itself still only affects the next action |
| **Counter cancel** into technique / guard | any recovery, when the technique's context would REC/XFM a threat that will hit within 0.5 s | +8 Focus | lets you answer a projectile during your own recovery (e.g. magma-grip a stone after a whiffed flare) |
| **Guard / evade cancel** | legacy `cancel` fraction of recovery; charges and channels any time | — | unchanged |
| **Gesture morph** | first 0.12 s of an attack startup, or at release from a charge | — | §3 |
| **Charge carry** | none | — | releasing a charge always resolves it (no stored charges, except Static Ward's static) |

Juggles: a target launched (knock y > 0) stays airborne until it lands; each extra hit while airborne scales knock and damage ×0.8ⁿ and balance ×0.5.
Pressing EVADE within 0.2 s of landing = tech-roll (i-frames 10 f). **Wall splat**: an actor knocked into an arena wall or a barrier at > 6 m/s
bounces (−0.3 v), takes +12 balance damage and a 0.3 s stun.

### 9.2 Environment interactions

| Feature (`ArenaMap`) | What it adds |
|---|---|
| **Pool** | unlimited water (draw, Deluge, Ice walls at 70 kg), conducts lightning, quenches lava; Ice **Skate**/Rime Path make walkable strips; Water **Dive** evade |
| **Metal plate** | conductor (lightning); unlimited scrap for Earth/Metal (Lodestone rip); Magnet Glide anchor; standing on it while holding an Aegis = conducted bolts |
| **Puddles** (made by moves) | conductors; Ice freezes them (insulated); Fire boils them; Sand drinks them (mud); Slick trips runners |
| **Terrace / ledges / step block** | waves (lava, water, sand) drop off edges (−1 m budget) and can't climb > 0.3 m; Rocket Hop / Pressure Hop / Flight / Updraft reach the high ledge; ledge knock-offs |
| **Cover wall, pillars** | block LOS and bolts; **reflect Sound** (bank shots); wall splats; embed Iron Lances (relay rods); Vine Swing anchors |
| **Ground everywhere** | stone and sand source; Swallow/Quicksand/Melt Pit/Burrow; Blue Furrow melts it into lava channels |

### 9.3 Showcase combos (input · what happens · why it works)

| # | Name | Sequence | Result |
|---|---|---|---|
| 1 | **Melt & Return** | rival raises a Bulwark → F/Bl `T` Smelter 1 s on the wall face » E/Mg `A↓` Magma Surge | the wall's face slumps into lava, the rest crumbles; the lava runs back at the builder (§5.4) |
| 2 | **Thunder Through Stone** | rival behind a Bulwark → F/Li `A[T2]` Storm Bolt | E 36 > grounding 30: the wall shatters, the bolt hits with E 21 |
| 3 | **Split Return** | incoming stone → E/St `T` Seize → `T+A` Split → release aimed | three spikes fan back at the thrower |
| 4 | **Swallow** | incoming stone → E `G↓` just before impact | CP 22 ≥ 17: the stone sinks into the ground |
| 5 | **Wave Back** | incoming stone → W/Wa `A↓` Tidal Rush | the wave captures the stone and carries it back |
| 6 | **Molten Exchange** (flagship) | incoming stone → F/Fl `T` (magma grip, hold to molten) → release | the stone becomes a lava wave aimed at the thrower |
| 7 | **Turn the Wind** | incoming stone → A/Gu `G*` Return Wind | perfect wind guard (27 ≥ 17) sends it back |
| 8 | **Live Wire** | W/Wa `A↔` Spray Fan (wet) » F/Li `A` Spark | wet ×1.5 and the spray's puddles chain the arc |
| 9 | **Flash Freeze Lock** | W/Wa `A↔` (wet) » W/Ic `A↔` Hoarfrost (wet → frozen, rooted) » E/St `A[T1]` Heave | rooted targets can't evade the heave |
| 10 | **Glass Shrapnel** | E/Sa `G` Dune Wall takes the rival's bolt (→ glass wall) » A/So `A↑` Sound Lance into the glass | the glass wall shatters into shards flying away from you |
| 11 | **Fire Tornado** | A/Vo `T` Eye of the Storm on the rival » F/Fl `A↑` Fireball into it | the tornado becomes a fire tornado (you steer it) |
| 12 | **Sandstorm** | E/Sa `A↔` Veil of Grit » A/Vo `A[T2]` Tornado through the cloud | a blinding, abrasive sandstorm walks at the rival |
| 13 | **Lightning Rod Bait** | E/Me `A↑` Iron Lance into the rival's wall » F/Li `T` Conductor's Hand on the lance → release | the bolt relays through the embedded lance to the rival behind it |
| 14 | **Recall Pincer** | E/Me `A[T2]` 3 discs past the rival » E/Me `T` (no target) Recall | the discs fly back through the rival from behind |
| 15 | **Obsidian Fort** | E/Mg `G` Magma Curtain » W/Wa `A↑` Water Bullet onto your own curtain | the face sets to obsidian: CP 28 → 38 |
| 16 | **Mud Bog** | E/Sa `G↓` Quicksand » W/Wa `A↔` Spray into it | mud: longer, stronger slow; then Heave |
| 17 | **No Air, No Fire** | rival's Fire Column → A/Va `G` Null Bubble | flames die at the shell (×2) |
| 18 | **Echo Bank Shot** | rival behind the cover wall → A/So `A↑` Sound Lance off a pillar | the pulse reflects into them |
| 19 | **Geyser Launcher** | W/Mi `A[T2]` Geyser (launch) » A/Gu `A↑` Wind Crescent (juggle) » E/St `A↓` Rising Fangs on landing | air juggle into a ground finisher |
| 20 | **Ember Wade** | E/Mg `A↓` Magma Surge » E/Mg `Ev[h]` Lava Wade behind your own wave | you advance inside the lava while the rival must evade it |
| 21 | **Implosion Ignition** | A/Va `T` Vacuum Well pulls the rival » release (collapse, air inrush) » F/Co `A[T1]` Burst at the centre | combustion ×1.5 in the inrush |
| 22 | **Ice Bridge** | W/Ic `Ev[h]` Skate across the pool | walk on the pool; frozen water won't conduct the rival's bolt |
| 23 | **Quench & Seize** | rival's lava wave → W/Wa `A↓` Tidal Rush (crust → hot rock) » E/Mg `T` Magma Hold → release | catch the hot rock without burns and throw it back |
| 24 | **Vine Slingshot** | W/Pl `T` Vinegrip on a stone 9 m away → `T+A` Wrap → release | the wrapped stone roots the rival on hit |
| 25 | **Static Return** | F/Li `G` Static Ward absorbs a bolt (static 12+) » `G↑` Static Burst | the rival's lightning comes back as a stun cone |
| 26 | **Frost Shatter** | W/Ic `A` Frost Shard (chill) » W/Ic `A↔` Hoarfrost (frozen crust) » A/So `A` Clap | the crust shatters: bonus damage + stagger |
| 27 | **Reverse Tide** | rival pours lava → E/Mg `T` Magma Hold on the wave (contest won) → release | the rival's own wave turns back on them |
| 28 | **Catch & Unleash** | rival's volley → A/Vo `G` Vortex Wall (captures) » `G↑` Unleash | every captured projectile flies back |

---

## 10. Physics, motion and game feel

### 10.1 Motion rules (sim)

| Topic | Rule |
|---|---|
| Knockback | projectile: Δv = m·v / 70 × 0.9 (cap 12 m/s, legacy formula extended); pressure: Δv = P × 0.45, lift P × 0.25 (T2+ air, geysers, tremors); a heavier fighter stance (Stone Skin, Iron Stance, Anchor, Deep Roots) ignores it |
| Momentum | catches transfer impulse (legacy); clashes exchange momentum (the heavier keeps going slower, the lighter deflects); Ram Wall / Glacier Shove carry their mass |
| Mass-based throws | throw speed = base × (20 / m)^0.4 (heavy throws are slower and lower); legacy shots keep their speeds |
| Tumble and drag | stones/discs tumble in the view from linear velocity; sand, droplets and embers have drag; tornadoes and crosswinds bend trajectories |
| Terrain | ice floor: deceleration 4 m/s² (slide); mud/quicksand ×0.3–0.6 speed; lava burns unless Lava Wade; water ×0.7 (legacy) |
| Airborne | Flight/Hover/Glide/Rocket Hop/Pressure Hop; downdrafts and gusts knock flyers down (×1.5 balance) |

### 10.2 Hit-stop, camera and haptics (presentation; the sim stays tick-exact)

Hit-stop is a short **global time-scale dip** (`Engine.time_scale` → 0.05 for N real frames) driven by `FxDirector` from event `power`; the sim
remains deterministic because it only counts ticks. Camera shake uses real time so it still moves during the freeze (shake the victim, not the world).

| Event | Hit-stop (frames) | Camera | Haptic |
|---|---|---|---|
| T0 hit | 3 | shake 0.10, decays 0.20 s | light |
| T1 hit | 5 | 0.18 + 0.1 m kick along the hit direction | light |
| T2 hit | 7 | 0.28 + kick | heavy |
| T3 hit / knockdown | 9 | 0.40 + kick + FOV punch −3° (0.25 s) | heavy |
| Block (light / TP ≥ 25) | 2 / 4 | 0.08 / 0.15 | block |
| Counter success (full outcome in your favour), perfect | 6 + white flash (flashes setting) | 0.2 + slow-mo assist option (legacy) | perfect |
| Clash (projectile vs projectile) | 4 | 0.15 | light |
| Shatter | 3 | 0.12 | light |
| Transformation moments (stone → lava, wall slump, glass fuse, wave → ice ridge) | 0 | 3 % zoom toward it for 0.3 s | transform |

Caps: ≤ 12 hit-stop frames per second; reduced motion → no FOV punch, shake ×0.3, no zoom; shake amplitude also falls off with distance (1 / (1 + d / 8)).

### 10.3 Anticipation and follow-through

Startups are anticipation (input acknowledged on frame 1, clip contact aligned to the end of startup — legacy rule). T3 releases add a 2-frame
"ready" glint before the strike. Recovery frames play the follow-through. New moves reuse the 47 existing clips (column "Anim" in §7);
a clip wish-list for the character pass is in §16.

---

## 11. VFX direction

Restrained stylised realism (docs/VFX.md): coherent shapes, tactile materials, deliberate colour, low noise. One material keeps one look
across its states (the stone → lava → rock continuum is the model for every family). Budgets per effect: ≤ 2 transparent layers, ≤ 32 particles
per one-shot, shadowless omni lights only (≤ 3 lit at once), no new screen reads (the single shared refraction stays optional and is off at low
quality), meshes rebuilt at most once per frame, everything pooled and prewarmed.

### 11.1 Per material / state

| Family (`mat`) | Look | Motion | Light | Key effects |
|---|---|---|---|---|
| **stone** | grey-brown plates, dust on top faces (existing StoneView) | tumbles in flight, settles; impacts throw chips + dust | — | spear (elongated rock), rubble, fangs (eruption spikes), crater dust, wall slump |
| **metal** | blue-steel, brushed, sharp specular, edge glint | discs spin fast (blur ring), lances straight; ricochet sparks | spark flashes (no light) | disc, lance/rod, plate, caltrops (instanced), magnet pull lines, red-hot tint with heat |
| **sand** | pale ochre grains, soft volumes, no hard edges | billowing clouds (2 layers of camera-facing puffs with noise), surge = granular wave strip | — | slug burst, cloud/sandstorm, surge, quicksand (swirling decal), sandblast streak |
| **glass** | clear, slightly green, sharp facets, bright edges | shards spin, catch light | — | glass wall (transparent, faceted), shatter shards |
| **magma** | black glossy crust, orange→yellow fissures (existing rock shader knobs heat/melt/crust) | viscous, bulging front, drips | 1 warm omni per large blob | globs, lava puddle/pool, channel, curtain face, obsidian (black glassy) |
| **water** | teal, clear, refracts the opaque scene (existing) | ribbon/blob/wave with spray | — | wave strip with foam lip, jet, bullet, spray fan, puddle trails |
| **ice** | pale cyan, frosted, faceted (existing frozen01 path) | needles/shards rigid; spikes erupt | — | ice wall, rime sheet (decal + low strip), ridge, frost cone, snow fall |
| **mist / steam** | mist: soft grey-white, alpha ≤ 0.3, slow; steam: whiter, rising, heat shimmer at the base | drift + dissipate; fog zones are low flattened billboards | — | fog zone, veil, steam jet, geyser column, scald puff |
| **plant** | deep green vines, bark-brown trunks, thorns | grow along splines (ribbon tubes), sway | — | lash, lattice wall, roots eruption, burrs, burn (embers + char) |
| **flame** | orange-yellow black-body (existing flame shader) | tongues, flicker; fields = low flame strips | brief omni | fireball (core + tongues), fire line, field, column, nova |
| **blue** | cyan-white core, violet rim, tight and narrow | steadier, faster than flame | brief cool omni | needle/beam, comet, corona ring, furrow glow |
| **lightning** | white core, violet-blue glow (existing ribbon) | flicker/re-strike 0.2 s, branches | omni ≤ 2.2 energy | spark arcs, rail arc, ground current (crawling arcs on conductors), skybreak (vertical), charged-body crackle |
| **blast** | white flash → orange fireball sphere → grey smoke ring | 0.35 s expand, shock ring on the ground | flash omni 0.1 s | pop, burst, detonation, mines (pulsing ember) |
| **wind** | barely visible: dust streaks + low-opacity refraction band (existing AirPush) | crests travel | — | crescent (arc mesh with streaks), wind wall, dust line, downdraft ring |
| **vortex** | layered cone meshes with panning noise; inner layer spins faster; debris orbits | rotation, wandering path | — | twister, tornado (sand/fire/water/steam tints when infused), eddy ring |
| **vacuum** | inward distortion (lite: dark rim + inward streaks, no screen read), implosion ring | contracts then pops | — | null bubble shell (fresnel rim), well (spiral inflow), implode/collapse |
| **sound** | concentric refraction rings (lite: thin bright rings + dust ripple on the ground) | expands at 34 m/s | — | clap/shout/roar cones, sound lance ring, echo ring, tremor ground ripple |

### 11.2 Per charge tier

| Tier | Hands | Body / ground | Audio |
|---|---|---|---|
| T1 | small element ring + motes in the element colour | — | tone 1 |
| T2 | ring grows, pulses | ground ripple ring under the feet, rim light on the fighter | tone 2 |
| T3 | full ring + rising motes | aura shell (fresnel), one light pulse, 2-frame "ready" glint | tone 3 + sting |

### 11.3 Outcome cues (from `interaction` events)

`BLK` dull impact puff in the threat's material · `DEF/RFL` bright ring + spark streak along the new direction · `REC` grip glow in the
reclaimer's element colour · `ABS` inward swirl into the absorber · `XFM` the target view cross-fades to the new state (+ steam/glass/frost puff) ·
`SHT` material shards · `SNK` ground closes with dust · `GND/CND` arcs to the ground/conductor · `EXT` smoke puff · `AMP` flare-up ·
`WKN/BND/SLW` small version of the full cue · `FAIL` (counter broken) the counter's own break effect (wall crumble, shield splash).
Perfect outcomes add the existing perfect flash and sound.

---

## 12. Audio

Reuse the 62 synthesised sounds (docs/AUDIO.md) and extend `tools/audio/synth_sfx.py` with the same method (transient + body + tail,
deterministic seeds, D-pentatonic motifs, −3 dBFS). New names (the `FxDirector` plays them; missing names are silently skipped by `AudioDirector.play`):

| Family | New SFX |
|---|---|
| metal | `metal_clang`, `metal_scrape`, `disc_whirr_loop`, `magnet_hum_loop`, `metal_recall`, `rod_hum_loop` |
| sand / glass | `sand_hiss`, `sand_burst`, `sand_surge_loop`, `sandstorm_loop`, `quicksand_loop`, `glass_fuse`, `glass_shatter` |
| magma | `magma_glob`, `magma_surge`, `obsidian_set`, `lava_hiss_quench` |
| water / ice / mist / plant | `wave_rush_loop`, `water_jet_loop`, `ice_wall_raise`, `ice_crack`, `ice_slide_loop`, `frost_hiss`, `mist_loop`, `steam_jet`, `geyser`, `vine_creak`, `vine_snap`, `vine_burn` |
| fire | `fireball_whoosh`, `fire_field_loop`, `blue_roar_loop`, `blue_ignite`, `spark_snap`, `thunderclap`, `static_crackle_loop`, `explosion_small`, `explosion_large`, `fuse_tick` |
| air | `crescent_whoosh`, `tornado_loop`, `vacuum_implode`, `vacuum_loop`, `sonic_boom`, `roar_wave`, `echo_ping`, `flight_loop` |
| system | `charge_t1`, `charge_t2`, `charge_t3` (pitched per element motif), `counter_success`, `counter_fail`, `clash_solid`, `clash_energy` |

Mixing: one loop per body/zone (existing `audio.loop` keys), voice caps from the manifest, interaction stingers ≤ 1 per 0.1 s.

---

## 13. Opponent AI

The AI keeps today's honesty (`AiBrain`): it reads only observable state, perceives each threat after a reaction delay, decides once per threat,
pays the same costs and uses only its kit. What changes is **how it chooses**.

1. **Perceive** (unchanged): projectiles/waves/zones/charges/bolt telegraphs become threats after `reaction` ± jitter; charge tiers are visible.
2. **Enumerate answers from the matrix**: for each move in its kit with a `counter` role (guards, perfect guards, push/sink, techniques that
   reclaim/transform, counter-capable attacks, evades), build a hypothetical counter agent and call `Interactions.predict(threat, counter)`
   (pure, no side effects) → band (full/partial/fail), ratio, outcome.
3. **Feasibility**: time to impact vs the move's startup (+ element switch tick, + current recovery), range/LOS, Focus/heat/water/metal available.
4. **Utility** = outcome value (REC 1.0 > DEF↩ 0.9 > XFM/SNK/CAP 0.8 > BLK/DEF 0.6 > partial 0.3 > evade 0.25 > fail −1) × success
   probability (timing noise vs perfect windows) − cost.
5. **Difficulty** shapes the choice:

| Preset | reaction | counter (picks best) | power misjudge | perfect-timing error | kit | offense |
|---|---|---|---|---|---|---|
| Novice | 0.45 s | 0.3 | ±40 % | ±0.12 s | 1 element, 2 subs | single moves |
| Adept | 0.30 s | 0.65 | ±20 % | ±0.07 s | 2 elements | 2-hit chains, environment (wet → lightning) |
| Master | 0.20 s | 0.9 | ±8 % | ±0.03 s | 4 elements | combos from §9.3, weaves, punishes recoveries and over-charges (Disrupt, interrupts) |

6. **Offense**: picks moves by range band and the target's state (wet → lightning, on the plate → lightning, near a wall → splat, airborne →
   juggle, charging → Disrupt/interrupt, behind a wall → Storm Bolt / Melt & Return / Sound bank shot), with the existing interval/aggression.
7. **Drills** (Lab): `stone_rain`, `lightning`, `seize` (existing) + `element:<e>/<sub>` (spams one kit's threats so the player can practise its
   answers) + `matrix` (cycles through every threat class).

---

## 14. Lab / sandbox tooling

| Tool | What it does |
|---|---|
| **Dev panel** (desktop: backquote / F2; touch: "Dev" in the pause menu) | time scale 0.1–1.0, frame step (pause + advance one tick), infinite Focus/heat/water/metal, god mode, AI on/off + difficulty + kit, reset, clear bodies, hitbox/zone/power debug overlay |
| **Spawner** | spawn at the aim point, inert or **launched at the player by the rival**: stone 20/45/80/200 kg, hot rock, magma blob, lava wave, metal disc/lance/plate, sand slug/cloud/surge, water blob/stream/wave/puddle, ice shard/wall, mist, steam, vines, fireball, fire field, tornado, vacuum well, sound pulse, bolt (aimed), blast; sliders for mass / speed / temperature / tier |
| **Move list** | per element and sub-element: name, slot and input (for the active device), cost, S/A/R frames, tier table, short description; "Try" plays it through the real input path |
| **Combo trainer** | pick a §9.3 combo: input sequence with timing bars, live success detection from events, slow-motion option |
| **Matrix viewer** | pick a threat class (+ mass/speed/heat/tier) and a counter (+ tier, perfect): shows TP, CP, ratio, band and outcome from `Interactions.predict`; "Stage it" spawns the threat at the player with the AI set to throw it |
| **Live tuning** | every numeric field of every move def and the counter-rule thresholds; save / reset / export (`user://tuning.cfg`) |
| **Scenarios** | `lab` (all elements and subs, dummies, spawner), `spar` 1v1 vs AI with difficulty and kit pickers; existing scenarios stay |

---

## 15. Engine contract (for implementers)

The **core job** builds this contract in `game/core` + `game/combat` and documents the final API in `docs/COMBAT_SPEC.md` ("Engine").
Kits (one per element), VFX, AI and UI plug into it **without editing core files**. Names below are the intended API; the code is authoritative.

### 15.1 Enums, sub-elements, state

* `Sim.SUB_NAMES[element][sub]` (table in §2). `ActorState.subs: Array[int]` (selected sub per element, default 0), `ActorState.sub() -> int`,
  `subs_unlocked`. Sub switching mirrors element switching (affects the next action only; emits `element` with `sub`).
* `Sim.Mat` **appended** (existing values unchanged): `METAL, SAND, GLASS, PLANT, FIRE, AIR`. `Sim.Form` appended: `ZONE` (non-solid field or
  barrier attached to the ground or an actor). Shape semantics live in `MatBody.tag` (§15.6), not in new forms.
* `MatBody` additions: `sub`, `tag: StringName`, `tier`, `owner` (zones; −1 = neutral), `power` (zone/volume power), `heat_payload` (FIRE bodies;
  counted by `thermal_energy()`), `spin`, `zone_radius`, `captured: Array[int]`, `charge` (exists: electrical, now used).
* `ActorState` additions: `status: Dictionary` (§15.8), `metal_carried` (12 kg), `static_charge` (0–60), `stance` (`""`, `stone_skin`, `iron`,
  `anchor`, `roots`, `lava_wade`, `overcharge`, `flight`, `hover`…), `armor`, `anchored`, `flying`.
* `ActorIntent` / `InputFrame` / `PlayerController` additions: `sub_select` (−1 or 0–3), `attack_gesture`, `guard_gesture`
  (`Sim.Gesture { NONE, UP, DOWN, SIDE }`), `evade_held`.

### 15.2 Move registry and bindings (`game/combat/moves.gd`)

```
Moves.BASE_DEFS            const: today's DEFS, unchanged values
Moves.DEFS                 static var: BASE_DEFS + kit registrations + live-tuning overrides (Moves.DEFS.pour etc. keep working)
Moves.ensure()             idempotent; CombatWorld._init calls it; loads KitEarth/KitWater/KitFire/KitAir.register()
Moves.register(id, def)    kits
Moves.bind(element, sub, slot, id)        slots: strike thrust ground sweep guard push sink tech evade evade_hold
Moves.resolve(element, sub, slot) -> id   unbound → the sub-0 binding → legacy id ("" = nothing)
Moves.set_override(id, key, value) · clear_overrides() · overrides()      Lab live tuning
Moves.list(element, sub) -> Array[String]                               move list UI
```
Sub 0 bindings are the legacy ids (`earth_attack`, `earth_tech`, `guard`, `evade`/`air_dash`, …). `CombatWorld._try_start` maps
press + gesture → slot → `Moves.resolve(a.element, a.sub(), slot)`. Guards keep the action id `"guard"` (many rules test it); the sub-element's
barrier comes from the resolved guard **spec** def (`inst.data.spec`). Push/sink start from `guard_gesture` while a guard channels.
Evade_hold morphs a running evade when `evade_held` lasts 0.2 s and a binding exists.

**Def schema** (core reads these keys; kits may add more):
`name, desc, element, sub, slot, module ("verbs" | "earth" | "water" | "fire" | "air" | "common"), verb, startup, active, recovery, cancel,
chain, heavy_min, tier_times, charge_drain, cost, heavy_cost, heat (HU), water (kg), metal (kg), tiers {t0..t3: {param: value}},
counter {cls, power: [t0..t3], perfect, role}, threat {cls}, anim, anim_hold, anim_active (clip names), fx {cast, release, impact, mat, shape},
sfx {…}, ai {role: poke|zone|counter|finisher|mobility|setup, range: [min, max], tags: […]}`.

### 15.3 Interaction engine (`Agent`, `Interactions`)

```
Agent.of_body(w, b) · of_volume(w, actor, inst, cls, pos, dir, channels) · of_guard(w, actor)
     · of_move(w, actor, move_id, tier, perfect)   # hypothetical counter (AI, Lab matrix viewer)
     · of_env(w, kind, pos)                       # pool, puddle, plate, arena_wall, ground
Interactions.classify(b) -> StringName
Interactions.threat_power(threat, rule) -> float  ·  counter_power(counter) -> float
Interactions.rule(threat_cls, counter_cls) -> Dictionary   # exact → threat×counter-family → family×counter → family×family → default
Interactions.add_rule(threat_cls, counter_cls, rule)       # kits add the cells of THEIR counter classes; tests may add temporary rules
Interactions.predict(w, threat, counter) -> {outcome, band, ratio, tp, cp, rule}   # pure, no side effects
Interactions.resolve(w, threat, counter) -> same + applied effects; emits "interaction"
```
Rule format: `{outcome, perfect, partial, fail, eff, w: {K, H, C, E, P}, full_at: 1.0, partial_at: 0.5, absorb_on_fail: 0.5, bands: [[ratio, outcome], …], to, fx}`.

**Outcome handlers** (core, all through the ledgers): `block, deflect, redirect, reflect, reclaim, capture, absorb, transform (to: lava, rock, hot_rock,
ice, water, steam, mist, glass, mud, obsidian, sandstone, ash, molten_metal, snow), shatter, sink, conduct, ground, pass, amplify, extinguish,
weaken, bend, slow, overwhelm, clash, disrupt, neutralize`.

**Contact sites routed through `resolve`** (core): projectile → actor's guard/shield/aura/zone; projectile → barrier body; projectile ↔ projectile
(**clash**, new); body ↔ zone (per tick, rate-limited); cone/beam/blast volumes → actors and bodies; waves → barriers, pool, puddles, other
waves; lightning path → barriers/insulators/conductors (blast-through); technique targets (grip/draw/heat legality); environment.
Legacy behaviours are encoded as rules with `legacy: true` and keep the existing events (`block`, `deflect`, `perfect_deflect`, `steam_block`,
`grounded`, `lightning_redirect` …) in addition to `interaction`.

### 15.4 Class catalogue

* **Threat classes** (`classify`): `stone, stone_heavy, boulder, hot_rock, magma, lava_wave, metal, molten_metal, sand, sand_cloud, sand_surge,
  glass, water, water_wave, ice, mist, steam, vine, flame, blue_fire, fire_field, gust, tornado, vacuum, ember`; **volumes**: `flame, blue_fire,
  lightning, blast, gust, sound, water, sand, steam, vacuum, frost`.
* **Counter classes**: `guard` (plain), `wall_stone, wall_obsidian, wall_glass, wall_sand, wall_mud, wall_ice, wall_vine, plate_metal,
  shield_water, screen_steam, fog, aura_flame, aura_blue, ward_static, guard_blast, guard_wind, wall_vortex, bubble_null, barrier_sound,
  spikes, rod, anchor`; active: `swallow, quicksand, melt_pit, grip_stone, grip_metal, grip_sand, grip_magma, grip_water, grip_ice, grip_vapor,
  grip_vine, grip_wind, heat_grip, heat_ranged, draw_heat, freeze, condense, wave_water, wave_sand, wave_lava, rime, gust, tornado,
  vacuum_well, suction, flame, blue_fire, lightning, blast, sound, water_jet, spray, sand_cloud, frost`; environment: `pool, puddle, plate,
  arena_wall, ground`.
* **Families** (rule fallbacks) — threats: `solid_light, solid_heavy, molten, liquid, granular, vapor, plant, heat, electric, pressure, sonic`;
  counters: `barrier_solid, barrier_soft, barrier_energy, liquid, cold, heat, electric, pressure, sonic, sink, grip, anchor`.
* **Ownership of cells**: the core encodes legacy cells and environment cells; each element kit encodes the cells of **its own counter classes**
  (its column in §8) with `add_rule`. Every row of §8 for that column must exist as a rule (or a family fallback that gives the same answer).

### 15.5 Charge API (`Charge`)

```
Charge.tier_for(def, held_s) -> int           # tier_times default [0.40, 1.00, 1.80]; heavy_min gates T1
Charge.param(inst, key, default) -> Variant   # def.tiers["t<tier>"][key] → def[key] → default
Charge.tick(w, actor, inst, intent)           # CHARGE/CHANNEL: tier-ups (event), drain, stall-on-empty
Charge.progress(inst) -> Vector2              # (tier, fraction to the next tier) for the HUD ring
```

### 15.6 Event contract (sim → VFX / audio / AI / HUD / Lab)

* Existing events and fields are unchanged. `action` gains `sub, slot, tier`; `hit`, `block`, `impact` gain `power, mat, tier, dir`.
* `charge {actor, move, element, sub, tier, ready}` on every tier reached.
* `fx {fx, mat, shape, actor, body, element, sub, move, tier, pos, dir, radius, length, angle, height, path, dur, power, seed}` — one-shot
  cues. `fx` keys: **`cast, release, cone, beam, burst, ring, erupt, trail, splash, aura`** (aura = on/off for stances and guards).
  `mat` keys: `stone metal sand glass magma water ice mist steam plant flame blue lightning blast wind vortex vacuum sound`.
  `shape` (optional): `spear, fan, disc, lance, rod, plate, fireball, comet, ember, crescent, spiral, needles, seed, ground, down, small, open, short`.
* `interaction {threat, counter, outcome, band, ratio, tp, cp, perfect, pos, dir, threat_actor, counter_actor, threat_body, counter_body, to}`.
* `status {actor, status, on, t, mag}` · `zone {body, kind, phase: open|close, radius, owner}`.
* **Persistent visuals come from body state, never from events**: `BodyViews` maps (`mat`, `form`, `tag`) → a pooled view and drives it every
  frame from heat / melt / crust / frozen / charge / radius / zone_radius / tier / spin.
* **Body tags** — projectiles: `spear rubble crag disc lance rod plate caltrops slug glob bomb fireball comet ember crescent twister spiral needle
  seed orb block`; waves (`WAVE`): `""` (lava, legacy) `water_wave sand_surge rime fire_line ground_current roots tremor dust_line wind_wall
  funnel magma_rift spike_line`; walls (`WALL`): `""` (Bulwark, legacy) `obsidian glass sand mud ice vine plate ridge spikes`; zones
  (`ZONE`/`CLOUD`): `fog mist steam sand_cloud sandstorm fire_field quicksand ice_floor mud caltrops tornado vacuum_well null_bubble mine
  melt_pit corona eddy briar geyser static_field wind_guard vortex_wall sound_barrier steam_screen lava_pool`.

### 15.7 Verbs (generic move templates, `game/combat/verbs/`)

`projectile` (spawn/take, count, spread, gravity scale, homing, pierce, ricochet, on_impact: shatter/stick/burst/puddle/sprout) ·
`cone` · `beam` (pierce rules, follows conductors) · `ground_line` (travelling front body: speed, budget, width, follows ground, leaves a zone/trail) ·
`barrier` (WALL body or held body or ZONE aura: mass/hardness/rise/upkeep/lifetime) · `zone` (persistent area: radius, life, per-tick effect via
rules and hooks, owner) · `grip` (seize/draw/shape/release with contests and a context preview) · `ranged_heat` (scorch/smelter/kiln) ·
`burst` (detonation at a point with fuse) · `summon` (zone at the aim point; tier from technique hold) · `dash` (distance, i-frames, curve,
trail) · `mode` (sustained movement while held: glide, surf, skate, flight, hover, burrow) · `stance` (timed/held modifiers: armor, anchor…).
Most new moves are pure data on a verb; kits add custom code only where a move needs it.

### 15.8 Hooks and status

* `CombatWorld.register_body_tick(tag, callable)` (custom motion/behaviour per body tag), `register_zone_effect(tag, callable)`,
  `register_status(name, spec)`, `register_tech_preview(element, sub, callable)` (technique context mode + HUD label, like `ActFire.preview`),
  `CombatWorld.tech_preview(actor, dir)` (HUD).
* Statuses (core): `wet` (existing wetness), `burning`, `chilled`, `frozen` (rooted), `rooted`, `slowed`, `muddy`, `slick`, `blinded`
  (lock-on/aim-assist off, AI perception +0.2 s), `concealed`, `deafened`, `shocked`, `anchored`, `armored`, `levitating`, `charged` (overcharge).
  Effects are applied in movement, hit, targeting and cost code paths; events `status`.

### 15.9 Ledgers and tests

* Mass: stone/sand/glass via `ground_taken/returned` and booked conversions (sand ↔ glass ↔ sandstone); metal satchel ↔ field
  (`metal_taken` from the plate, `metal_returned`); water incl. mist/steam/plant growth (`water_to_plant`) and ambient moisture for ice
  (`moisture_taken`); burned plant (`burned`).
* Energy: identity unchanged; FIRE bodies carry `heat_payload`; wind cooling → `ambient`; Cool & Set and ground vents → `removed`; magma/steam/
  combustion heat comes only through `pay_heat` (`generated`).
* Every kit ships tests: each move at T0–T3 (start, costs, clean end), events with catalogued fx keys, its §8 column cells at the reference
  powers incl. partial bands, ledger conservation over a scripted exchange, determinism (same seed → same hash).

---

## 16. Priorities and open items

**Status 2026-10-05:** P0, P1 and P2 are implemented and integrated (all 16 sub-elements, every slot bound, T0–T3, the §8 cells,
combos in the Lab trainer, showcases per sub-element plus `show_owner` for §5.4). Deviations and numbers per kit: `docs/kits/*.md`;
open items: `HANDOFF.md` "Known gaps (moveset)". The 12 `mv_*` clips of the character pass are wired into the kit defs.

| Priority | Scope |
|---|---|
| **P0** | core engine (registry, bindings, gestures, charge, interaction engine + legacy cells + environment, verbs, hooks, events); per sub-element: strike T0–T3, thrust, ground, guard + perfect, technique; their §8 cells; VFX for P0 moves and outcome cues; AI counters via `predict`; UI: sub-element ring, gestures, charge ring with tiers, Lab dev panel (spawner, tuning, matrix viewer) |
| **P1** | sweep, push, sink, evade, evade_hold; combos 1–20; combo trainer; move list; audio set; hit-stop/camera; showcases per sub-element |
| **P2** | Water/Plant kit; tornado infusions; Disrupt depth; combos 21–28; extra Lab drills |

* **Animation**: new moves reuse the 47 clips through a presentation bridge (`FighterView.play_one_shot`) until the animation owner adds a generic
  def-driven branch in `FighterView`. Clip wish-list for the character pass: stomp (ground slot), two-hand push, spin (vortex/sweep), palm-down
  (sink), overhead lob (bombs), flight loop, skate loop, surf loop, burrow dive/emerge, clap, roar, channel-beam pose.
* **Numbers are design targets**: tune in the Lab (live tuning), record per-kit changes in `docs/kits/<element>.md`.

---

## Sources

* [S1] Systemic chemistry: designers define interactions, not results — https://thedesignlab.blog/2026/02/09/immersive-design-systemic-chemistry/
* [S2] Designing a systemic game — https://playtank.io/2024/06/12/designing-a-systemic-game/
* [S3] Counterplay as a design value — https://lawofgamedesign.com/2014/01/31/theory-what-is-counterplay/ · https://www.surrenderat20.net/2014/05/red-post-collection-design-values-of.html
* [S4] Sirlin, "Designing Yomi" (every option has a counter; RPS with unequal payoffs) — https://www.sirlin.net/articles/designing-yomi
* [S5] Doing rock-paper-scissors right — https://lawofgamedesign.com/2015/04/22/theory-doing-rps-right/ · intransitive games — https://en.wikipedia.org/wiki/Intransitive_game
* [S6] Charged attacks: vulnerability vs power — https://tvtropes.org/pmwiki/pmwiki.php/Main/ChargedAttack
* [S7] Tiered charge (small attacks build into bigger ones) — https://www.gdcreatorschool.com/docs/guides/gameplay-2/game-design-3-combat-design/
* [S8] Cancels, chains and links — https://www.eventhubs.com/guides/2007/dec/08/explanation-use-cancels-chains-and-links-street-fighter-3-third-strike/
* [S9] Hit-stop and screen shake: 3–12 frames scaled to strength — https://swordarcade.xyz/guides/game-feel-hitstop-and-screen-shake/
* [S10] Hit-stop column (both freeze; shake the victim; scale by damage) — https://nintendowire.com/news/2022/12/12/this-week-in-sakurai-12-5-12-11-fine-tuning-hit-stop-and-cheating-the-system/
* [S11] Third-person melee hit feel (camera, hit reactions, impulse) — https://www.jasondeheras.com/gamedesign/2021/4/23/how-do-3rd-person-melee-combat-games-communicate-game-and-hit-feel
* [S12] "Juice it or lose it" (Jonasson & Purho, GDC Europe 2012) — https://github.com/dr-jam/GameplayProgramming/blob/master/Juice.md
* [S13] Stylised VFX read with less micro-detail — https://80.lv/articles/from-realism-to-stylization-game-vfx-production
* [S14] Flipbooks (≤ 4×4 for stylised work) — https://www.vfxapprentice.com/blog/what-are-flipbooks-in-games
* [S15] Tornado as layered meshes with panning noise; inner layers faster — https://www.cyanilux.com/tutorials/tornado-shader-breakdown/ · https://www.1mafx.com/blog/stylized-tornado-vfx-breakdown
* [S16] Apple GPUs are TBDR (Metal) — https://developer.apple.com/videos/play/wwdc2020/10602/
* [S17] Overdraw on mobile — https://thegamedev.guru/unity-gpu-performance/overdraw-optimization/
* [S18] Godot Mobile renderer: no SSR/SSAO/SDFGI/volumetric fog — https://docs.godotengine.org/en/stable/tutorials/3d/environment_and_post_processing.html
* [S19] Godot GPUParticles on mobile (amount, amount_ratio, fixed_fps + interpolate, small transparent areas) — https://godotlab.org/en/tutorials/3d-particle-systems · https://docs.godotengine.org/en/stable/tutorials/performance/gpu_optimization.html
* [S20] Input buffering windows (100–200 ms; long buffers cause ghost inputs) — https://www.wayline.io/blog/input-buffering-fighting-games
