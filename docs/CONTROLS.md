# Fourfold controls

Fourfold is played in landscape on iPhone and iPad. Controllers and a keyboard/mouse work too (mainly for testing).
This page has two parts: a player guide, then a technical section for anyone touching the input layer (`game/ui/`).
The moves themselves (names, costs, frames, charge tiers) are in the in-game **Lab > Moves** page and in
`docs/MOVESET.md`; this page is about how to *play* them.

---

## Player guide

### The grammar in one paragraph

Pick an **element** (Earth, Water, Fire, Air) and one of its four **sub-elements** (Earth: Stone, Metal, Sand, Magma ·
Water: Water, Ice, Mist, Plant · Fire: Flame, Blue, Lightning, Combustion · Air: Gust, Vortex, Vacuum, Sound). Every
sub-element fills the same slots: **tap / hold ATTACK** is the strike (hold = charge, three tiers), **flick ATTACK**
up / down / sideways is the thrust / ground / sweep, **GUARD** is the barrier (press just before contact = perfect),
**flick GUARD** up / down is push / sink, **TECHNIQUE** takes control of material, **tap ATTACK with a second finger
while the technique is held** shapes it, **EVADE** tap dodges and **EVADE hold** is the sub-element's movement mode.

### Touch (iPhone / iPad)

```
 +--------------------------------------------------------------+
 |                                                       [ || ] |   pause (top corner)
 |                      (e)(e)                                   |
 |   floating            (e)(e)   [ TECH ]   [target]            |   e = element chips
 |   stick                                  [ EVADE ]            |
 |   (left half)                                                 |
 |        o                      [ GUARD ]       [ ATTACK ]      |
 +--------------------------------------------------------------+
        left thumb                        right thumb
```

| Control | What it does |
|---|---|
| **Left half** | Floating movement stick. Put your thumb down anywhere in the left half; the stick appears under it and moves relative to where you landed. |
| **Right side, empty space** | Camera. Drag to turn and tilt. |
| **ATTACK** | Tap for the quick strike. Hold for a charged strike: the inner ring fills while the move decides tap or hold, then three arc segments (T1, T2, T3) fill around the button as the charge grows. Release to fire at the tier reached. |
| **ATTACK flick** | Slide the same finger **6 mm or more** within a quarter second of touching down: up = **thrust**, down = **ground**, left or right = **sweep**. While ATTACK is held three labelled petals show the current sub-element's moves; the one you are pointing at lights up. A flick after a longer hold counts when you lift. |
| **GUARD** | Hold to block; the moment you press is the timed deflection window. While it is held, labels show **push** (flick up) and **sink** (flick down). You can flick at any time and flick again without lifting. |
| **EVADE** | Tap to dodge in the direction you steer. **Hold** (a fifth of a second) for the sub-element's sustained mode: glide, surf, skate, flight, anchor, wade... |
| **TECHNIQUE** | Press to acquire, drag the same finger to aim, lift to commit. The button shows what it would do (HEAT, DRAW, GRIP, WELL...). While you hold it, **tap ATTACK with a second finger** to shape (split, freeze, wrap...); a pill beside ATTACK names the shape. |
| **CANCEL** | A red zone appears while you hold TECHNIQUE. Drag into it, or tap it with a second finger, to abort. |
| **Element chips** | Four chips in an arc by the technique button. Tap to switch element. The selected sub-element's name sits beside the active chip. |
| **Sub-element ring** | **Tap the active chip again**: four named petals open beside the arc; tap one. Or **long-press any chip** and slide to a petal, lift to choose. A tap-opened ring closes after four seconds or on any other touch. Locked sub-elements are dimmed. Switching only affects your *next* action. |
| **Target** (crosshair) | Cycle the lock-on target. |
| **Pause** (top corner) | Opens the pause / settings screen (fires on lift, so a stray brush while panning will not pause). |

Strong button labels (Settings) keep the petals faintly visible even when nothing is held.

Things worth knowing:

- A finger belongs to whatever it touched first. A camera swipe that drifts over ATTACK will not attack; an aiming
  finger that wanders across the screen will not turn the camera; a flick finger never rotates the camera.
- You can do everything at once: move with the left thumb, guard with one right finger, swipe the camera with another.
- If the app loses focus, the phone gets a call, or you pull down Control Centre, everything is released safely:
  the stick returns to zero, held buttons let go, an open sub-element ring closes without choosing and a half-aimed
  technique is **cancelled, never fired**. A cancelled touch never counts as a flick.
- Left-handed layout mirrors everything, including the petals and the ring.

### HUD

Top-left: health, balance and Focus bars, the current **element / sub-element**, then only the resources that matter
now: heat reserve, waterskin (kg), metal satchel (kg), static charge. Under them, **status icons** (two letters, fill =
time left): WT wet, BN burning, CH chilled, FZ frozen, RT rooted, SL slowed, MD muddy, SK slick, BL blinded, CN
concealed, DF deafened, SH shocked, AN anchored, AR armored, LV levitating, CG charged. The locked rival's statuses
show under its name. Bottom centre: while you charge, the move's name and its tier bar (T1 T2 T3). On touch the same
tiers also ring the button you are holding.

### Settings (pause screen)

Control size (0.8x to 1.4x), control opacity, layout (Default / Compact / Wide), left-handed layout, strong button
labels, camera sensitivity, invert camera Y, screen shake, flashes, haptics, reduced motion, slow-motion assist,
debug overlay. "Reset progress" asks for confirmation first. **Dev** opens the Lab dev panel.

### Controller

| Input | Action |
|---|---|
| Left stick / right stick | Move / camera (aims the technique while RT is held) |
| X / Square | Strike (tap = fast, hold = charged) |
| Y / Triangle | Thrust |
| LT / L2 | Ground |
| B / Circle | Sweep |
| A / Cross | Evade (hold = movement mode) |
| RB / R1 | Guard; **RB + X** push, **RB + LT** sink |
| RT / R2 (hold) | Technique, release to commit; **X while held** = shape |
| LB / L1 | Cancel a held technique; **LB + d-pad** (left / down / right / up) = sub-element 1 / 2 / 3 / 4 |
| D-pad: left / down / right / up | Earth / Water / Fire / Air |
| R3 (right stick click) | Cycle target |
| Start | Pause (cancels a held technique first) |

### Keyboard and mouse

| Input | Action |
|---|---|
| W A S D | Move |
| Arrow keys | Camera (aim while L is held) |
| J or left mouse button | Strike (hold = charge) |
| U / N / H | Thrust / ground / sweep |
| K | Guard; **K + J** push, **K + N** sink |
| Space | Evade (hold = movement mode) |
| L or right mouse button (hold) | Technique; mouse movement or arrows aim; **J while held** = shape |
| 1 2 3 4 | Earth / Water / Fire / Air; the **active element's key again** cycles its sub-elements |
| Q / E | Previous / next sub-element |
| Esc / Backspace | Cancel a held technique, otherwise pause |
| Tab | Cycle target |
| Middle mouse drag | Camera |
| F1 | Toggle captured mouse-look |
| ` (backquote) or F2 | Lab dev panel |

Right mouse is the technique button, so mouse-drag camera lives on the middle button (or F1 mouse-look). The K + J /
K + N chords are guard flicks, not attacks: to attack out of a guard on keyboard use U or H, or release K first.

---

## Lab (dev panel)

Open with backquote / F2, or **Dev** in the pause menu on touch. The simulation keeps running behind it; player input
is ignored while it is open. Scenario **Lab** (pause > Practice > Lab, or "Quit to lab") has all 16 sub-elements, three
dummies and a passive rival who throws whatever you spawn.

| Page | What it does |
|---|---|
| **Dev** | Time scale 0.1-1.0, freeze + step 1 / 6 / 30 ticks, infinite Focus / heat / water / metal, god mode, debug overlay (bodies, zones, power), rival AI on / off, difficulty (Easy / Normal / Hard = AiPresets novice / adept / master), kit (scenario kit, one element, one sub-element, all four), behaviour (free / passive / matrix drill), reset, clear bodies, heal and refill, load a scenario. |
| **Spawn** | Every material and state: stones 20 / 45 / 80 / 200 kg, hot rock, magma blob, lava wave, metal disc / lance / plate, sand slug / cloud / surge, water blob / bullet / wave / puddle, ice shard / wall, mist, steam, vines, fireball, comet, fire line / field, tornado, wind crescent, vacuum well, tremor, and the rival performing a bolt, blast, flame cone, gust, sound pulse, scald puff, frost fan or sandblast. Sliders for mass, speed, temperature and tier; delivery = thrown at you by the rival or inert at the aim point. Kit-owned entries run the kit's own move, so they behave exactly as in a fight. |
| **Moves** | Pick element and sub-element: every bound move with its input on the active device, Focus / heat / water / metal costs, S/A/R frames, tier table with hold times, description, and **Try** (with a tier chooser) that plays the move through the real input path. |
| **Combos** | The 28 showcase combos of `docs/MOVESET.md` 9.3: steps with the inputs for your device, a timing bar per step, optional slow motion, "Start trainer" (sets the situation up, then detects success live from sim events), "Demo the steps". Data table: `game/ui/lab/combos.gd`. |
| **Matrix** | A threat (any spawn entry with mass / speed / temperature / tier) against a counter (any registry move at a tier, perfect timing, a legacy guard or an environment piece): threat power, counter power, ratio, band and outcome from `Interactions.predict`, the counter power a full block needs, and **Stage it** (the rival throws the threat, you hold the counter's element and sub-element). |
| **Tuning** | A slider for every numeric field of every move def (and tier parameter) and for the numeric thresholds of every counter-rule cell. Save / load `user://tuning.cfg`, reset all, export a text diff (clipboard + `user://tuning_export.txt`) for `docs/TUNING_LOG.md`. |

Free Spar (pause > Practice) has a **Rival** (Easy / Normal / Hard) and a **Kit** (Earth + Fire, each element, All four)
picker under it; the choice is saved.

---

## Technical

### Architecture

```
PlayerInputHub (Node)                    game calls poll_frame() once per 60 Hz tick
 |- CanvasLayer 80
 |   |- TouchControls (Control, _input)  touch state machine + drawing
 |   |   uses TouchLayout (pure geometry), UiScale (mm / safe area), UiStyle (glyphs)
 |   '- TargetMarker (Control)            lock-on reticle at target_screen_pos
 |- DesktopInput (Node)                  keyboard / mouse / gamepad, "ff_*" InputMap actions
 '- CanvasLayer 120 (process ALWAYS)
     '- SettingsPanel (Control)           pause + settings, pauses the tree

Game also owns (CanvasLayer 110): LabPanel (game/ui/lab/*)   dev panel: Dev, Spawn, Moves, Combos, Matrix, Tuning

GameSettings (RefCounted, user://settings.cfg)   shared via GameSettings.current()
Haptics (static)                                 Haptics.play(kind)
```

`poll_frame()` fills the touch frame and the desktop frame, copies the first
into one reused `InputFrame`, and merges the second (`InputFrame.merge_from`:
move/camera add, buttons OR, the stronger technique aim wins). Edge flags are
latched from the moment the event arrives until a poll consumes them, then
cleared. Hold the returned object only until the next poll (copy with
`copy_from` to keep it).

Game-side calls:

```gdscript
var hub := PlayerInputHub.new()
add_child(hub)
hub.paused_requested.connect(on_pause)       # panel also opens itself unless auto_open_settings_panel = false
hub.settings_panel.reset_requested.connect(...)
hub.settings_panel.practice_requested.connect(func(): hub.settings_panel.set_practice_items([...]))
hub.settings_panel.practice_selected.connect(func(id): ...)
hub.settings_panel.reset_progress_requested.connect(...)
hub.settings_panel.quit_to_lab_requested.connect(...)

func _physics_process(_dt):
    hub.set_context({ "element": 2, "sub": 1, "unlocked_subs": [0, 1, 2, 3], "unlocked_elements": [0, 1, 2], "tech_label": "HEAT",
                      "tech_available": true, "holding": false,
                      "attack_charge": 0.05, "attack_element": 2,
                      "target_screen_pos": cam.unproject_position(p), "target_label": "Dummy" })
    var f := hub.poll_frame()
```

Further keys: `"sub"` / `"unlocked_subs"` (selected sub-element and the usable ones of `"element"`), `"petals"`
`{up, down, side}` (names of the thrust / ground / sweep moves), `"guard_petals"` `{up, down}` (push / sink),
`"shape_label"` (what T+A does now, "" = no technique running), `"charge_ring"` `{slot: attack|guard|tech|evade, tier,
frac, max}` (`Charge.progress` of the running action; the ring is drawn around that slot's button) and
`"attack_decide"` (the running registry attack's tap / hold decision time; absent / 0 = the legacy per-element time).
`Game.gesture_petals / guard_petals / shape_label / charge_ring_context` build them from the registry.

`set_context` keys are all optional; an absent key leaves that hint unchanged,
`"target_screen_pos": null` hides the marker. `"attack_charge"` is the seconds
since the player's attack action started while its tap/hold decision or charge
runs (0 while the press waits in the sim's buffer, -1 when no attack is running
or pending) and `"attack_element"` that attack's element (-1 = the selected
one); `Game.attack_ring_context(actor)` builds both. `hub.controls_visible = false`
hides the touch HUD (cutscenes). The touch HUD is shown automatically when a
touchscreen / mobile platform is detected or a real touch arrives;
`force_touch_ui` shows it on desktop and `emulate_touch_with_mouse` lets the
mouse act as a finger for layout testing.

### InputFrame semantics

- A tap shorter than one tick still reports `*_pressed` **and** `*_released`
  in the same frame, with `*_held == false`. Consumers must handle both edges
  in one tick.
- `tech_released` is the commit. `tech_cancel` is the abort. They are mutually
  exclusive for one hold; after a cancel the later finger lift sends nothing.
  A press and a cancel in the same tick (focus loss right after touch-down)
  arrive as `tech_pressed` + `tech_cancel`, `tech_held == false`.
- On the tick that carries `tech_released`, `tech_aim` / `tech_aim_active`
  still hold the final aim (they are cleared on the next tick).
- `cam_delta` is the sum of drag since the last poll, in radians (x yaw right+,
  y pitch up+); finger right = look right, finger up = look up (inverted by
  `invert_y`).
- `move` is y-up/forward, length <= 1, dead zone already removed and rescaled.
- `pause_pressed` is set as well as the `paused_requested` signal; use either.
- `element_select` is the Sim.Element index (Earth 0, Water 1, Fire 2, Air 3)
  and is only ever an unlocked element.
- `sub_select` (-1 or 0..3, edge) is a sub-element of the current element and only ever an unlocked one.
- `attack_gesture` / `guard_gesture` (`Sim.Gesture` NONE / UP / DOWN / SIDE, edge): the flick recognised this tick. On
  touch the ATTACK flick arrives with `attack_pressed` (flick inside 0.25 s) or with `attack_released` (flick after a
  hold); keyboard / pad slot keys send press + gesture on the same tick. Guard flicks are UP (push) / DOWN (sink) only.
- A **shape tap** (ATTACK pressed while the technique is held; keyboard J / pad X / mouse LMB while L / RT / RMB is
  held) is one `attack_pressed` edge with `attack_held == false` and no `attack_released`.
- A keyboard / pad **guard chord** (K + J = push, K + N / RB + LT = sink) is a `guard_gesture` on the tick it is
  pressed, never an attack (`attack_pressed` / `attack_held` stay false).
- `evade_held` is the EVADE level (touch finger down, Space / A held); the sim morphs into `evade_hold` after 0.2 s.

### Touch ownership rules

Decided at touch-down by `TouchControls._touch_down`, kept until up / cancel:

1. A second tap inside the visible CANCEL zone while a technique is held
   cancels it; that finger then owns nothing.
2. A touch inside a control's hit radius (visible radius x 1.12, never below
   5 mm; the nearest by distance / hit radius wins) owns that control.
   A control already held by another finger, or a disabled / locked element
   chip, makes the new finger inert (it owns nothing and is not turned into a
   camera or stick finger).
3. Otherwise the touch owns the stick if it is on the stick half
   (x < 50 % of the viewport, mirrored when left-handed) and the stick is
   free, else the camera if it is free, else it is inert.

4. A touch while the **tap-opened sub-element ring** is showing: a petal selects (that finger then owns nothing), any
   other touch closes the ring first and is then decided by rules 1-3 (the active chip again just closes it).

ATTACK and GUARD fingers keep owning their button while they flick (`FlickRecognizer`: >= 6 mm from where the finger
landed; ATTACK within 0.25 s of the press or at release after a hold; GUARD any time, the origin re-bases after each
flick). A finger that landed on an element chip and stays 0.32 s opens the slide ring; sliding onto a petal and lifting
chooses it, lifting elsewhere chooses nothing. `release_all(true)` (focus loss, pause, hide) closes the ring, never
chooses, and a cancelled touch carries no gesture.

Fingers are tracked by `InputEventScreenTouch.index` / `ScreenDrag.index`
(up to 16, plain arrays, no per-event allocation). A finger index that is
reused without an up event is treated as a cancel of the stale finger.
Events are consumed in `_input` and marked handled; the overlay uses
`MOUSE_FILTER_IGNORE`. Mouse events synthesised from touch
(`DEVICE_ID_EMULATION`) are ignored by `DesktopInput`.

### Thresholds and sizes

| Item | Value |
|---|---|
| Stick radius | `clamp(0.11 x viewport height, 9 mm, 15 mm) x control_scale` |
| Stick dead zone | 8 % of radius, output rescaled so it leaves 0 smoothly and reaches 1 at the ring; base follows the thumb past the ring |
| Camera | `0.055 rad / mm x camera_sensitivity` of finger travel (physical, so iPhone and iPad feel alike) |
| Attack charge cue | ring fills over the attack's tap/hold decision time, `max(startup, Moves.HOLD_THRESHOLD 0.18 s)` rounded up to whole 60 Hz ticks (`TouchControls.attack_charge_sec`: Earth 0.25 s, Water / Fire / Air 0.183 s), of the running attack's element (else the selected one). With the game's `attack_charge` context it follows the sim's attack action, so a press buffered behind a recovery shows an empty ring until the attack starts and a full ring means the sim has already committed the charge; without it the ring times itself from touch-down. Reads full from 1 ms before the threshold (float sums of 60 Hz steps). `charge_hold_sec` > 0 overrides the time |
| Technique aim radius | `min(0.15 x viewport height, 16 mm)` maps to aim length 1 (the cap keeps iPad drags short) |
| `tech_aim_active` | drag > `max(12 px, 2 mm)` from the press point, sticky until release |
| Cancel zone | 14 mm disc, centred >= aim radius + zone radius + 0.8 x button radius + 3 mm from the technique button (so aiming at full range cannot touch it); entering it cancels immediately |
| Pause | fires on lift if the finger is still within 1.8 x its hit radius and the touch was not cancelled |
| Flick | >= 6 mm (`FlickRecognizer.FLICK_MM`) inside 0.25 s of an ATTACK press, or at release after a hold; quadrants: up / down by the dominant axis, otherwise side; the petal lights at 55 % of the travel |
| Sub-element ring | long-press 0.32 s (slide), tap-opened ring auto-closes after 4 s; petals 19 x 8 mm (x control scale) stacked in one column on the open side of the chip arc, clamped to the safe rect, never over a button |
| Gesture petals | pills of the font height 2.3 mm: UP above ATTACK, DOWN below, SIDE on the inner side (mirrored when left-handed); guard push above / sink below GUARD |
| Charge ring | three arcs (T1 T2 T3, 16 degree notches) at 1.36 x the button radius, lit by `Charge.progress`; the inner ring at 1.16 x is the tap / hold decision (unchanged) |
| Button sizes | attack 17 mm, technique 14, guard 13.5, evade 12.5, pause 9.5, target 9, element chips 8.5 (x `control_scale`; compact preset 0.92x size / 0.86x spread, wide 1.16x spread) |
| Haptics | one short pulse (12-70 ms), at least 60 ms between pulses |

Sizes are authored in millimetres from the bottom-right usable corner and
converted with `UiScale.px_per_mm` (screen DPI from
`DisplayServer.screen_get_dpi`, divided by the canvas_items stretch factor).
That keeps buttons ~12-14 mm and the cluster in the thumb corner on iPhone
(about 460 dpi) and iPad (about 264 dpi) alike. On desktop (non-mobile, no
override) the density is floored so a 96 dpi monitor does not shrink the HUD
to a sliver. If the cluster would cross the screen midline or the top edge
(small phone, large scale, wide preset) it shrinks uniformly instead of
overlapping.

### Cancellation

`TouchControls.release_all(true)` runs on `NOTIFICATION_APPLICATION_FOCUS_OUT`,
`NOTIFICATION_APPLICATION_PAUSED`, `NOTIFICATION_WM_WINDOW_FOCUS_OUT`, when
the tree is paused (`NOTIFICATION_PAUSED`), when the overlay is hidden or
leaves the tree, and for any `InputEventScreenTouch` with `canceled == true`.
Effect: stick -> 0; held attack / guard -> `*_released`; held technique ->
`tech_cancel` (never `tech_released`); a pending pause is dropped. The
latched edges are delivered by the next poll, so a game that is paused sees
them on resume. `DesktopInput.cancel_all()` does the same for keys, mouse and
pad, and `NOTIFICATION_UNPAUSED` re-syncs edge-only actions so the Esc that
closed the pause menu cannot pause again.

### Safe area

`UiScale.safe_insets` maps `DisplayServer.get_display_safe_area()` to
viewport units (subtracting the window position and dividing by the stretch
factor). Insets that are implausibly large for a real notch / home indicator
(a window bigger than the screen on desktop) are discarded. The layout keeps
every control inside the safe rect plus a 3 mm margin; left-handed play swaps
which side the notch inset applies to. The pause button sits in the top
corner of the usable rect; the settings card is centred in it.

### Settings file

`user://settings.cfg` (ConfigFile, sections `controls`, `comfort`, `debug`).
Every value is clamped on load, so a hand-edited file cannot break the layout.

| Key | Range / values | Default |
|---|---|---|
| `control_scale` | 0.8 - 1.4 | 1.0 |
| `control_opacity` | 0.3 - 1.0 | 0.8 |
| `layout_preset` | `default`, `compact`, `wide` | `default` |
| `left_handed` | bool | false |
| `strong_labels` | bool | false |
| `camera_sensitivity` | 0.3 - 2.5 | 1.0 |
| `invert_y` | bool | false |
| `screen_shake` | 0 - 1 | 1.0 |
| `flashes` | 0 - 1 | 1.0 |
| `haptics` | bool | true |
| `reduced_motion` | bool | false |
| `slowmo_assist` | bool | false |
| `show_debug` | bool | false |

`GameSettings.current()` is the shared instance; it emits `changed` after the
panel edits it (saved 0.45 s after the last change and on close). The
presentation layer should read `screen_shake`, `flashes`, `reduced_motion`
and `slowmo_assist` from it. `show_debug` draws hit radii, the stick split,
finger roles and the live move / aim values over the HUD.

### Haptics

`Haptics.play("light" | "block" | "deflect" | "perfect" | "heavy" |
"lost_control" | "transform")` issues a single
`Input.vibrate_handheld(duration_ms, amplitude)` and respects the haptics
setting. Godot only exposes duration and amplitude; true Core Haptics patterns
(transient + continuous events, sharpness curves) need a native iOS plugin.
`Haptics._emit` is the one place to swap that in later.

### Lab code map

`game/ui/lab/`: `lab_panel.gd` (tabs, `action(name, args)` signal the Game answers), `lab_page_*.gd` (one page each),
`lab_session.gd` (time scale, freeze / step, cheats, AI choice), `spawn_catalog.gd` (every spawnable + `spawn()` through
the ledgers), `lab_script.gd` (scripted input: Try, combo demo, the rival performing a volume), `move_list_data.gd`,
`combos.gd` + `combo_tracker.gd` (28 combos, live detection), `matrix_query.gd` (`Interactions.predict` on a scratch
world), `lab_tuning.gd` (`Moves.set_override`, in-place rule edits, `user://tuning.cfg`). `Game.lab_*` carries out the
panel's actions; `PlayerInputHub.input_blocked` idles the input while the panel is open.

### Tests and screenshots

```
tools/scripts/godot.sh --headless -s res://tests/ui/run_ui_tests.gd           # exit 0 / 1
tools/scripts/godot.sh --headless -s res://tests/ui/run_ui_tests.gd -- --only=cancel
tools/scripts/godot.sh --render --resolution 2532x1170 -s res://tests/ui/render_hud_screenshot.gd -- --device=phone --out=/tmp/shots
tools/scripts/godot.sh --render --resolution 2732x2048 -s res://tests/ui/render_hud_screenshot.gd -- --device=ipad  --out=/tmp/shots
# the real Game (Lab scenario, touch HUD) and every dev-panel page:
tools/scripts/godot.sh --render --resolution 2532x1170 -s res://tests/ui/render_lab_screenshot.gd -- --device=phone --out=/tmp/shots --touchui --scenario=lab
```
After adding `class_name` scripts run `tools/scripts/godot.sh --headless --import` once.

The suites inject `InputEventScreenTouch` / `ScreenDrag` through
`Viewport.push_input`, so they run the same `_input` path as a device.

### Known limits

- Multi-touch, safe-area insets and DPI scaling are exercised synthetically;
  they still need a pass on real iPhone / iPad hardware.
- Sub-element locking is supported by the HUD / ring / keys (`unlocked_subs`) but every scenario currently unlocks all 16
  (the showcases and the Lab rely on it); gating them behind mastery is future work.
- The hold-for-charge cue is visual only; the authoritative tap/hold decision
  lives with the combat rules (`CombatWorld.attack_after_startup`, the move's
  startup and `Moves.HOLD_THRESHOLD`) and the ring mirrors it.
- Gamepad auto-hide of the touch HUD only applies on mobile platforms.
