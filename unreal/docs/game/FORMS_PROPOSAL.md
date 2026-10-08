# Part B - form gestures: draft for the owner (2026-10-08)

Status: **proposal, nothing implemented.** CONTROLS_HUD_PLAN.md part B says the forms need move design with the owner.
This draft gives one concrete answer to react to: which four new forms every sub-element gets, what each does, and how it
would be built. Names are working titles (all original). Mark up anything; "no" to a row is fine.

## 1. The gesture set (same for every element)

Drawn with one finger starting on ATTACK (touch), owner decision 2026-10-07. Today's flicks stay exactly as they are.

| Form | Drawing | Meaning (every element) | Today's equivalent |
|---|---|---|---|
| push / slam / arc | straight up / down / side flick | thrust / ground / sweep | unchanged |
| **circle** | a closed loop (>= 270 deg turn, ends near the start) | the sub-element *around you*: a ring, orbit, whip spin | - |
| **lift-then-push** | up, stop >= 0.15 s, then out toward the target | raise material from the arena, then launch it | - |
| **pull** | down toward your thumb (start above, end below + slow) | draw / seize from the environment or reel something in | technique (draw) only |
| **zig-zag** | >= 2 sharp direction reversals | quick 3-hit flurry of the sub-element | - |

Recognition (logic island, unit-tested like `FFGFlick`): a stroke recogniser over the touch path (resampled to 32 points,
total turn angle for circle, a dwell for lift, reversal count for zig-zag). The plain flick still fires inside 0.25 s if
the path is straight, so nothing gets slower. While drawing, a ghost trail follows the finger and the recognised form's
move name pops up before release (same pill style as the petals).

Desktop / gamepad (question 3 below): either **RMB drag draws the form** (as the plan says) or **one key per form**
(e.g. `I` circle, `O` lift, `P` pull, `Y` zig-zag; pad: TECH held + right-stick circle / up-out / down / shake).

## 2. Draft moves (16 sub-elements x 4 forms)

Each row reuses an existing verb of that sub-element's kit and, where possible, an existing threat class, so the counter
matrix needs few new cells (see §3).

| Sub-element | circle | lift-then-push | pull | zig-zag |
|---|---|---|---|---|
| Earth / Stone | **Orbit Stones** - 3 stones circle you 2 s (block light shots), release fires them | **Slab Throw** - tear a 60 kg slab from the floor, launch it | **Drag Stone** - pull a loose stone / wall chunk to your hands | **Stone Flurry** - 3 fist stones, alternating hands |
| Earth / Metal | **Blade Ring** - razor discs orbit, cut vines | **Plate Hoist** - lift the arena plate, hurl it | **Magnet Recall** - pull every metal body on the field toward you (theirs too) | **Needle Burst** - 3 quick shards |
| Earth / Sand | **Dust Cyclone** - grit ring, blinds close foes | **Sand Pillar** - raise a column, push it over as a wave | **Undertow** - drag the floor under the foe toward you (balance) | **Grit Lashes** - 3 sand whips |
| Earth / Magma | **Lava Wheel** - molten ring, leaves a burning circle | **Magma Geyser** - raise a molten column, hurl it as a bomb | **Draw Heat** - pull heat from fire fields / hot rock into your molten hold | **Spatter Volley** - 3 molten spits |
| Water / Water | **Water Whip** - 360 deg whip spin | **Pool Column** - raise a column from pool / puddle, throw it as a wave | **Draw from Pool** - pull a water mass from the nearest source to hold | **Ripple Strikes** - 3 quick lashes |
| Water / Ice | **Frost Halo** - shard ring, frosts the ground around you | **Ice Pillar** - raise a pillar, kick it forward | **Rime Pull** - freeze a puddle / wet foe and drag it in | **Shard Flurry** |
| Water / Mist | **Fog Ring** - a vision-blocking ring | **Steam Plume** - raise a scald cloud, blow it forward | **Condense** - pull mist / steam into a water orb | **Scald Jabs** |
| Water / Plant | **Thorn Coil** - vine ring roots whoever stands in it | **Root Heave** - roots lift the foe, push flings them | **Vine Reel** - reel the foe (or a body) toward you | **Whip Flurry** |
| Fire / Flame | **Fire Wheel** - flaming spin around you | **Flame Pillar** - lift a pillar out of a fire field, throw it | **Draw Flames** - pull nearby fire fields into your hands (heat) | **Ember Flurry** |
| Fire / Blue | **Blue Halo** - hot ring, melts light shots | **Comet Lift** - raise a blue sphere overhead, drive it forward | **Siphon Heat** - pull heat out of molten / hot bodies (cools them) | **Needle Rush** |
| Fire / Lightning | **Arc Ring** - static ring that grounds bolts near you | **Sky Call** - raise a hand, a bolt falls on the target after 0.5 s | **Draw Current** - pull charge from a charged / wet foe or a ward | **Chain Spark** - a bolt that bounces between targets |
| Fire / Combustion | **Ring of Charges** - 4 charges around you | **Lifted Charge** - raise one charge overhead, lob it | **Recall Charges** - pull your mines toward you, they blow on the way | **Firecracker String** |
| Air / Gust | **Wind Ring** - a ring that turns light shots | **Lift and Throw** - updraft lifts a light body, push throws it | **Inhale** - pull light bodies / clouds toward you | **Gust Flurry** |
| Air / Vortex | **Whirl Cage** - spinning ring traps light bodies | **Funnel Lift** - lift the foe in a funnel, push flings | **Into the Eye** - pull bodies into an eye zone | **Twin Spirals** |
| Air / Vacuum | **Null Ring** - pressure ring that pops projectiles | **Pressure Drop** - compress air above, drop it as a cannon | **Suction Step** - pull the foe a step toward you | **Pop Chain** - 3 small implosions |
| Air / Sound | **Echo Halo** - ring of sound, interrupts charges | **Rising Note** - a held note rises, push releases a lance | **Hum Draw** - vibration pulls loose sand / stone toward you | **Staccato** - 3 sound jabs |

Overlaps to settle: some sweeps are already rings (Vortex Eddy Ring, Sound Echo Ring, Flame Fire Ring at T3). Either the
circle replaces nothing and is a *different* ring (orbit / cage), or the circle becomes "the sweep, all the way round".

## 3. How it would be built (and what it costs)

The Godot build is the source of truth for rules and moves (the Unreal core must match it; moves.json and the golden
matrix are exported from it). So B is mostly kit work in Godot, then export, then the Unreal input side:

1. Sim: four new slots (`circle`, `lift`, `pull`, `zigzag`) in the input grammar (`attack_gesture` gains 4 values) with
   the same morph rule as flicks; `Moves.bind` per sub. Godot first, then port the slot plumbing to `FourfoldCore`.
2. Kits: 64 move defs (Godot `game/combat/kits/<element>/`), reusing verbs; counter cells only where a form makes a new
   threat class (estimate: ~15 classes, the rest map to existing ones such as `stone`, `wave_water`, `fire_field`).
3. Export data, re-run CoreTests (golden matrix stays byte-identical for old rows; new rows added).
4. Unreal: stroke recogniser + ghost trail + preview pill (touch), the desktop / gamepad mapping, AI use (planner roles).
5. Animation: 64 clips (or a shared form set per element: 16 clips, 4 per element) - the animation stream.

Rough size: recogniser + input 1 session; Godot slots + 16 moves per element ~1 session per element; animation separate.
A smaller first slice: **circle + pull for all 16** (the two the plan called out first: rings and drawing from the arena),
lift and zig-zag later.

## 4. Questions for the owner

1. Keep the four forms (circle, lift-then-push, pull, zig-zag), or change the set?
2. The table: which rows to keep / rename / redo? Start with the "circle + pull" slice?
3. Desktop: RMB drag draws forms, or one key per form?
4. Animation: one shared form animation per element (16 clips), or per move (64)?
