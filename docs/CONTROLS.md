# Fourfold controls

Fourfold is played in landscape on iPhone and iPad. Controllers and a
keyboard/mouse work too (mainly for testing). This page has two parts: a
player guide, then a technical section for anyone touching the input layer
(`game/ui/`).

---

## Player guide

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
| **Left half** | Floating movement stick. Put your thumb down anywhere in the left half; the stick appears under it and moves relative to where you landed. Drag further than the ring and the ring follows your thumb. |
| **Right side, empty space** | Camera. Drag to turn and tilt. |
| **ATTACK** | Tap for a fast strike. Hold (about a fifth of a second; a quarter for Earth) for a charged attack; a ring fills around the button as the strike winds up, and releasing once it is full delivers the charge. A press made while the previous move is still recovering is kept for up to 0.15 s; the ring stays empty until the strike actually starts. |
| **GUARD** | Hold to block. The moment you press is the timed deflection window: press just before a hit lands to deflect instead of merely blocking. |
| **EVADE** | Tap. The direction you are steering decides where you go. |
| **TECHNIQUE** | Press to acquire / shape the technique for the selected element. While you keep holding, drag the *same finger* to aim (a thin ring shows the aim range). Lift to commit. |
| **CANCEL** | A red zone appears while you hold TECHNIQUE. Drag into it, or tap it with a second finger, to abort. Lifting the finger anywhere else commits. |
| **Element chips** | Four small chips in an arc by the technique button: Earth, Water, Fire, Air. Tap to switch. Locked elements are dimmed with a strike-through and do nothing. They are disabled while a technique is held. |
| **Target** (crosshair) | Cycle the lock-on target. |
| **Pause** (top corner) | Opens the pause / settings screen. It fires when you lift your finger on the button, so a stray brush while panning the camera will not pause. |

Things worth knowing:

- A finger belongs to whatever it touched first. A camera swipe that drifts
  over ATTACK will not attack; an aiming finger that wanders across the screen
  will not turn the camera.
- You can do everything at once: move with the left thumb, guard with a right
  finger and swipe the camera with another, or move with the left thumb while
  aiming a technique with the right.
- If the app loses focus, the phone gets a call, or you pull down Control
  Centre, everything is released safely: the stick returns to zero, held
  buttons let go and a half-aimed technique is **cancelled, never fired**.

### Settings (pause screen)

Control size (0.8x to 1.4x), control opacity, layout (Default / Compact /
Wide), left-handed layout (mirrors everything), strong button labels (text on
every button), camera sensitivity, invert camera Y, screen shake, flashes,
haptics, reduced motion, slow-motion assist, debug overlay. "Reset progress"
asks for confirmation first.

### Controller

| Input | Action |
|---|---|
| Left stick | Move |
| Right stick | Camera (aims the technique while RT is held) |
| X / Square | Attack (tap = fast, hold = charged) |
| RB / R1 | Guard |
| B / Circle | Evade |
| RT / R2 (hold) | Technique, release to commit |
| LB / L1 | Cancel technique |
| D-pad: left / down / right / up | Earth / Water / Fire / Air |
| Y / Triangle | Cycle target |
| Start | Pause (cancels a held technique first) |

### Keyboard and mouse

| Input | Action |
|---|---|
| W A S D | Move |
| Arrow keys | Camera (aim while L is held) |
| J or left mouse button | Attack |
| K | Guard |
| Space | Evade |
| L or right mouse button (hold) | Technique; mouse movement or arrow keys aim while held |
| Esc / Backspace | Cancel a held technique, otherwise pause |
| 1 2 3 4 | Earth / Water / Fire / Air |
| Tab | Cycle target |
| Middle mouse drag | Camera |
| F1 | Toggle captured mouse-look |

Right mouse is the technique button, so mouse-drag camera lives on the middle
button (or F1 mouse-look).

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
    hub.set_context({ "element": 2, "unlocked_elements": [0, 1, 2], "tech_label": "HEAT",
                      "tech_available": true, "holding": false,
                      "attack_charge": 0.05, "attack_element": 2,
                      "target_screen_pos": cam.unproject_position(p), "target_label": "Dummy" })
    var f := hub.poll_frame()
```

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

### Tests and screenshots

```
tools/scripts/godot.sh --headless -s res://tests/ui/run_ui_tests.gd           # exit 0 / 1
tools/scripts/godot.sh --headless -s res://tests/ui/run_ui_tests.gd -- --only=cancel
tools/scripts/godot.sh --render --resolution 2532x1170 -s res://tests/ui/render_hud_screenshot.gd -- --device=phone --out=/tmp/shots
tools/scripts/godot.sh --render --resolution 2732x2048 -s res://tests/ui/render_hud_screenshot.gd -- --device=ipad  --out=/tmp/shots
```

The suites inject `InputEventScreenTouch` / `ScreenDrag` through
`Viewport.push_input`, so they run the same `_input` path as a device.

### Known limits

- Multi-touch, safe-area insets and DPI scaling are exercised synthetically;
  they still need a pass on real iPhone / iPad hardware.
- The hold-for-charge cue is visual only; the authoritative tap/hold decision
  lives with the combat rules (`CombatWorld.attack_after_startup`, the move's
  startup and `Moves.HOLD_THRESHOLD`) and the ring mirrors it.
- Gamepad auto-hide of the touch HUD only applies on mobile platforms.
