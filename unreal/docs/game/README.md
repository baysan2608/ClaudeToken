# Stream `game` - the Unreal game module `Fourfold`

The game module covers:
- running the engine-free sim (`ff::Session`) every frame,
- presenting the fighters with a native animation runtime,
- the camera, the feel layer (hit-stop, shake, haptics, flashes),
- input from touch, keyboard / mouse and gamepad,
- the Slate UI (HUD, touch controls, menus, Lab panel),
- the game flow.

The contracts are in ARCHITECTURE §7 and §8.4. Related docs: [API_NOTES.md](API_NOTES.md) (every Unreal API used) and
[REQUESTS.md](REQUESTS.md) (config and other streams).

## Files

| Path (`Source/Fourfold/`) | What |
|---|---|
| `Public/FourfoldSimSubsystem.h` + `Private/FourfoldSimSubsystem.cpp` | Owns `ff::Session` and steps it at 60 Hz: accumulator x Lab time scale, max 4 ticks per frame, Lab freeze uses `TakeStepRequests`. Keeps the prev / curr snapshots and broadcasts `OnFrame` once per frame. Also: hit-stop / slow-motion through global time dilation, fighter spawn / despawn, persistence (`Saved/Fourfold/progress.json`, `lab_tuning.json`). |
| `Public/FourfoldSettings.h` + `Private/FourfoldSettings.cpp` | `UFourfoldSettingsSubsystem`: clamps settings, saves them as JSON (`Saved/Fourfold/settings.json`), sets the frame cap (`t.MaxFPS`) and the scalability tier (auto, stepped down when frames run slow). |
| `Public/FourfoldGameMode.h`, `Private/FourfoldGameMode.cpp` | No pawn, no spectator pawn. |
| `Public/FourfoldPlayerController.h` + `.cpp` | Game flow, device sampling, `PollInput`, the camera rig as view target, the UI root, the feel director, the placeholder arena, app lifecycle. |
| `Public/FourfoldFighter.h` + `Private/FourfoldFighter.cpp` | One actor per sim actor: `SK_Fighter` (or an engine-shape stand-in), palette / status MIDs, runs the anim director every frame. |
| `Private/FourfoldAnimInstance.*` | `UFourfoldAnimInstance` + `FFourfoldAnimProxy`. The proxy evaluates the director's recipe on the worker thread. There is no Anim Blueprint. |
| `Private/FourfoldAnimLibrary.*` | Loads `clips.json` / `anim_map.json` / `character.json` and the `UAnimSequence` assets once per game instance. |
| `Private/FourfoldCameraRig.*` | Third-person rig driven by `ffg::CameraLogic`. |
| `Private/FourfoldFeel.*` | Feel director: events -> hit-stop, slow-motion, shake, kick, FOV punch, zoom, haptics, flashes, toasts. |
| `Private/FourfoldPlaceholderArena.*` | Engine-cube arena when the level has no `FourfoldArena` actor, camera see-through, debug overlay. |
| `Private/UI/` | Slate: `FourfoldUi` (palette, mm sizing, painter, glyphs, button / toggle / slider / choice widgets), `FourfoldPageBuilder`, `SFourfoldTouchOverlay`, `SFourfoldHud`, `SFourfoldMenus`, `SFourfoldLabPanel`, `SFourfoldUiRoot`. |
| `Private/Logic/` | **Logic island:** no Unreal headers, unit-tested here. Touch layout + controls + flick recognizer, desktop input grammar, UI scale, settings JSON, feel policy, camera logic + arena collision, anim library / director / timing / locomotion / IK / springs / inertial fades. `tools/gen_anim_defaults.py` writes `FFGAnimDefaults.gen.cpp` from MARTIAL_ARTS §3-4. |
| `Private/Logic/tests/` | `logic_tests.cpp`, `layout_dump.cpp` + `draw_layouts.py`, CMake (guarded by `FF_LOGIC_TESTS`, so UBT ignores it). |
| `Private/Logic/tools/ue_syntax_check/` | Syntax check of the module against real UE public headers (see Testing). |

## Flow

1. **Title.** At startup the controller loads `spar` with `autoplay=duel`: the AI drives both fighters behind the
   title card. Buttons: **Lab** (all tools), **Free Spar** (rival Easy / Normal / Hard, rival kit, your element),
   **Practice** (`Session::PracticeItems`, with the option rows under each item), **Watch** (an AI duel with a small
   Back bar), **Settings**.
2. **Play** (Lab, Free Spar, drills): you get the touch HUD or the keyboard / pad, plus the HUD. The pause menu opens
   from the pause button, Esc / Backspace or Start. It holds Resume, Restart scenario, Practice, Settings, Lab tools
   and Quit to title.
3. **Lab panel:** F2 or "Lab tools" in the pause menu. The sim keeps running behind it. Gameplay input is blocked and
   every held control is released.
4. **App lifecycle:** when the app deactivates or goes to the background, everything is released (a held technique
   is cancelled, never fired), the game pauses, progress is saved and the haptic engine is released.
5. **Command line:** `-scenario=<id>` skips the title. Add `-autoplay=duel` to watch that scenario.

## Frame order (what runs when)

1. `AFourfoldPlayerController::PlayerTick` (TG_PrePhysics, after the engine processed input):
   - UI keys (F1 / F2, menu navigation);
   - input routing (gameplay input only in Play, not paused, no modal menu, Lab panel closed);
   - `DesktopInput::Sample` from the key state;
   - touch outputs: haptics, ring cues, pause requests.
2. `UFourfoldSimSubsystem::Tick` (after TG_PostPhysics). It polls `PollInput` once per 60 Hz tick, which:
   - merges the touch frame with the device frame (`InputFrame::MergeFrom`), with edges latched until consumed;
   - carries the tick's `cam_delta` over to the next camera update (it never turns the camera itself);
   - returns `MoveYaw()` (the view yaw, or the lock axis while locked).

   Then it calls `Session::Step` and broadcasts `OnFrame` once per frame.
3. `OnFrame` consumers:
   - each `AFourfoldFighter` interpolates prev -> curr, runs `AnimDirector::Update` and hands the recipe to its anim
     instance;
   - the controller drains the camera input of this rendered frame (`TakeCamDelta` on the desktop grammar and the
     touch controls, plus what a tick carried), applies it, updates the camera rig, the feel (the hit-stop it
     requests applies from the *next* frame), the HUD and touch contexts from `BuildHud()`, and the debug draw;
   - FourfoldFX / FourfoldAudio bind their own handlers.
4. Skeletal meshes evaluate in TG_PostUpdateWork, then Slate paints.

## Input map

**Touch** (docs/CONTROLS.md, a full port of `touch_controls.gd` / `touch_layout.gd` / `flick_recognizer.gd`):
- left half: floating stick; right half: camera drag;
- ATTACK: tap, hold (charge ring that follows the sim), or flick up / down / side (labelled petals);
- GUARD: hold, flick up = push, flick down = sink;
- TECHNIQUE: press-drag-aim-lift. There is a cancel zone, and a second-finger tap on ATTACK shapes the held body
  (the shape pill appears);
- EVADE: tap or hold;
- element chips: tap to select. Tap the active chip again, or long-press and slide, to open the sub-element ring;
  or touch any chip and flick toward the petal column: the ring opens on the slide and the petal under the finger
  (or nearest its height) is chosen on lift - element + sub-element in one stroke (`TouchLayout::RingAim`);
- target and pause buttons.

Sizes are in millimetres (`ffg::PxPerMm` from the physical screen density), inside the safe area. Settings cover
left-handed mirroring, control scale / opacity / preset and strong labels. "Touch controls: Always" lets the mouse
act as one finger on a Mac.

**Keyboard / mouse:**

| Input | Action |
|---|---|
| WASD | Move |
| Arrows | Camera, or aim while the technique is held |
| J / LMB | Strike (hold = charge) |
| U / N / H | Thrust / ground / sweep |
| K | Guard. K+J = push, K+N = sink |
| L / RMB | Technique. J while held = shape |
| Space | Evade |
| 1-4 | Element. The active element's key again cycles its subs |
| Q / E | Previous / next sub-element |
| Tab | Target |
| Esc / Backspace | Cancel the technique, otherwise pause |
| MMB drag | Camera |
| F1 | Captured mouse-look |
| F2 | Lab |

**Gamepad:**

| Input | Action |
|---|---|
| Sticks | Move / camera |
| X | Strike |
| Y | Thrust |
| LT | Ground |
| B | Sweep |
| RB | Guard. RB+X = push, RB+LT = sink |
| RT | Technique |
| A | Evade |
| LB | Cancel. LB + d-pad = sub-element |
| D-pad | Element |
| R3 | Target |
| Start | Pause |

**Menus:** arrows / WASD / d-pad / left stick move, Enter / A accepts, Esc / B goes back. Touch and mouse use the
widgets directly; scroll lists by dragging.

The grammar (chords, latching, "the Esc that closed the menu does not pause again") is in `FFGDesktopInput` and
`FFGTouchControls` and is unit-tested.

## Animation runtime (ARCHITECTURE §8.4)

Each frame, per fighter, `ffg::AnimDirector` (logic island) turns the sim state into an `AnimRecipe`. The priority
order is:
1. stun reactions (from `stun_kind` and `last_hit_dir`);
2. one-shot overlays (perfect guard...);
3. the action (startup -> hold / charge loop -> active -> recovery);
4. airborne;
5. movement-mode loops;
6. stride-matched locomotion (idle / walk / run, directional).

The recipe also carries hand-shape overrides, the legs-only gait layer under upper-body actions, the block impact
additive, and procedural parameters (lean, landing, aim, look-at, foot IK, breathing).

The proxy (`FFourfoldAnimProxy::Evaluate`) samples the clips, blends them, applies hand shapes and the additive, then
runs an inertial cross-fade from the last output pose on every switch (3-6 frames, `FFGInertial`). The procedural
layers follow:
- pelvis drop / landing spring, hip tilt, lean;
- hit springs (torso, head with a whiplash delay, arms);
- chest aim, head look-at at the lock target;
- breathing;
- two-bone foot IK on the sim ground height, with planting and locking from `foot_plants`;
- verlet spring chains for `ff_hair_*`, `ff_sash_*`, `ff_hem_*`.

Missing clips fall back to the stance / reference pose. Missing JSON falls back to the built-in defaults from
`FFGAnimDefaults.gen.cpp`. Missing bones skip their layer. Each case logs one warning.

**Timing** (`FFGAnimTiming`): the startup clip is time-mapped so its contact frame lands exactly at the end of the
sim's effective startup. The rate is contact / startup, clamped to 0.6-1.6. If the startup is too fast, the
anticipation is shortened. If it is too slow, the clip holds at the chamber. Active runs at rate 1. Recovery re-aims
the rate to end with the sim's recovery and fades back to locomotion.

**How to tune:**
- Per-clip data (frames, duration, loop, contact frames, gait speed, hand shapes, foot-plant ranges) lives in
  `Content/Fourfold/Data/clips.json`. The move -> clip map (startup / hold / release / perfect per tier and mode)
  lives in `anim_map.json`. Both belong to stream animation and override the defaults key by key, so no rebuild is
  needed.
- Global feel constants are in the logic headers:
  - `AnimTiming::kRateMin/kRateMax/kChamberFrac`;
  - `Locomotion` (`kMoveStart kMoveFull kRunFrom kRunTo kWeightRate`);
  - `FootPlanter` (`kPlantFrom/To kMaxDrop kLockMaxDist kLockRelease`);
  - `HitReactor` spring frequencies / damping, `LandingSpring`, `SpringChain` stiffness / drag;
  - the cross-fade frame counts passed to `AnimDirector::SetKey` (3-6 frames).

  Change a constant, run the logic tests, rebuild.
- Lab > Moves > **Try** plays any move through the real input path at a chosen tier. Use it with Lab time scale 0.1
  and freeze / step to check contact frames.

## Camera and feel

`ffg::CameraLogic` (Logic/FFGCamera.h; every tunable is a named constant there) handles:
- **free orbit** behind the player (4.4 m, pitch 0.22, vFOV 54); drags rotate it; an incoming threat turns it gently
  when the player isn't steering; the pivot leads the player's velocity by 0.12 s (at most 0.6 m);
- **lock-on two-shot**: the boom hangs off the player's chest (1.35 m) rotated `theta` off the player -> rival axis and
  aims at `lerp(player, rival, w)` 1.2 m up. With `k = clamp((sep - 3) / 13, 0, 1)`: distance 3.6 + 1.6k m,
  theta 24 - 10k deg, w 0.40 - 0.10k, pitch 0.08 + 0.10k rad, vFOV 50 + 4k deg. The side keeps hysteresis: it follows
  the side the player turned the view to, and flips when a yard wall sits behind the boom. 1 s after the player last
  steered, a spring (0.35 s, max 4 rad/s) brings the view back onto the two-shot. While locked, the stick basis handed
  to the sim is the lock axis (forward = toward the rival), blended back to the view yaw when the player has turned
  the view more than 40-70 deg away;
- **spectator two-shot** (title / watch): side-on to the fighters' axis (70 deg), around their midpoint, staying on the
  side of the line the camera is on, pulled back until both fit;
- collision against the analytic arena: swing (or side flip), then lift, never closer than 3 m to a yard wall, pitch at
  most 0.62 with a rival; see-through for interior solids; horizontal FOV capped at 90 deg (wide phones);
- smoothing: critically damped springs (`SmoothDamp`): pivot 0.12 s horizontal / 0.28 s vertical with a 0.25 m dead
  zone while grounded, framing (distance / FOV / aim) 0.4 s; the collision pull-in is the only fast exponential
  (ExpK 18) and eases back out over 0.45 s. A scenario start or round reset snaps onto the framing;
- the feel layer in real time: rotational trauma shake (pitch 1.2 / yaw 0.9 deg x trauma^2, 21 Hz noise, <= 1.5 cm of
  translation, roll 1.2 deg on T3 / knockdown) with falloff from the duel midpoint (floor 0.75 for hits the player gives
  or takes); a spring kick (5 Hz, zeta 0.6) along the hit; an FOV punch that eases in over 60 ms, holds through hit-stop
  and eases out over 0.3 s; the cinematic dolly (7 % toward the event) + -6 deg punch; 3 % zoom on transformations.
  Reduced motion: shake x 0.3, no roll, no kick / FOV punch / dolly / zoom.

Camera input is drained and applied once per rendered frame (not per sim tick), so drags stay smooth at 120 Hz and
through hit-stop.

The feel table is `ffg::FeelFor` (MOVESET §10.2, retuned in docs/TUNING_LOG.md). It sets hit-stop, trauma, kick, FOV
and haptic per event class: T0 4 / T1 6 / T2 9 / T3 12, perfect 8, clash 6 (in 1/60 s). Hit-stop runs global dilation
0.05 for that long in **real** seconds (any frame rate), at most 20/60 s frozen per rolling second, then eases back to
full speed over 50 ms; reduced motion caps one request at 3/60 s. Big counters the player is part of (every perfect;
full-band tier >= 2 reflect / redirect / capture / transform / shatter / reclaim; the player's perfect deflect) and a
KO add the cinematic beat: 0.25x for 0.4 s (KO 0.9 s) after the hit-stop, an ease back over 0.25 s, at most once every
3 s (a KO always), never with reduced motion, in a Lab freeze or at a Lab time scale. The optional slow-motion assist
(0.55x for 0.22 s on a perfect deflect) is a setting. While time is frozen the struck fighter trembles along the hit
(1.2-3 cm). Haptics are at least 60 ms apart and off when the setting is off. Flashes are scaled by the Flashes setting.

## UI

- Everything is Slate with the engine default font. Every size is in millimetres through `FFUi::Mm`: touch targets
  are at least 9 mm and HUD text has millimetre floors on phones. Colours are the Godot palette, converted from sRGB.
- One root widget (`SFourfoldUiRoot`) holds, from back to front: HUD, touch overlay, Lab panel, menus, fade. It
  updates the physical metrics and the safe area every frame and rebuilds the menus on rotation or resize.
- The HUD shows:
  - vitals that fade when calm, the element / sub line, only the resources that matter, and status chips with a
    time fill;
  - the rival panel, the Lab status, and the charge bar with the move name and tier;
  - the objective and challenge, toasts and flashes;
  - off-screen threat arrows and the eased lock-on marker;
  - the Lab overlay labels and, with debug, timing bars, debug lines and a perf line.

## Testing here (Linux)

- **Logic island:** `cmake -S Source/Fourfold/Private/Logic/tests -B build -G Ninja && cmake --build build &&
  build/ffg_logic_tests`.
  - It builds with g++ 13 and clang 18 at `-Wall -Wextra -Wshadow -Wconversion -Wundef -Werror`.
  - Every source is compiled once more against `CoreTests/ue_macro_poison.h`, so no Unreal macro name is used.
  - Current result: 56 tests, 2544 checks, 0 failures on both compilers.
- **Touch layout review:** the same build makes `ffg_layout_dump`. Run
  `build/ffg_layout_dump > layouts.json && python3 Source/Fourfold/Private/Logic/tests/draw_layouts.py layouts.json
  <out_dir>` (needs Pillow). It draws the touch HUD of iPhone 15 Pro (also left-handed), iPhone SE, iPhone 15 Pro Max
  (compact), iPad Air 11, iPad Pro 13 (wide, scale 1.2) and a Mac window, with the safe area, hit radii, aim ring,
  cancel zone, sub-element ring and a 10 mm bar. It flags any hit target under 9 mm. Current result: none. The
  geometry matches `touch_layout.gd`.
- **Unreal syntax check:** `Private/Logic/tools/ue_syntax_check/`.
  - `FF_UE_CHECK_DIR=<scratch> ./setup.sh` sparse-clones the public headers of a UE 5.8.2 source mirror (~150 MB;
    `FF_UE_MIRROR` picks another) and writes mock UHT headers.
  - `FF_JOBS=3 ./check_all.sh` runs `clang -fsyntax-only` on every module source plus one unity TU, against the real
    engine headers with the engine shared PCH.
  - Current result on 5.8.2: 0 errors and 0 warnings in `Source/Fourfold` (`-Wshadow-all -Wundef
    -Wunused-variable`), and no deprecated API in our code (`-Wdeprecated-declarations`).
  - It cannot catch link errors or UHT rules (UPROPERTY types, specifiers).

## Owner steps on the Mac

1. Follow MAC_SETUP §3: generate the project, build `FourfoldEditor`, open the editor.
2. Press Play in any map. With no arena actors you get the cube arena and the title over the AI duel.
3. If something fails to compile, send the first errors back (MAC_SETUP §9). API_NOTES lists what the header check
   cannot see.
4. On iPhone / iPad: apply the two config requests in REQUESTS.md (the four-finger console tap, 120 Hz), then follow
   MAC_SETUP §6.

## Integration notes for other streams

- `UFourfoldSimSubsystem::OnFrame` fires once per frame, also while paused (`bPaused`). Snapshots and events are valid
  only during the broadcast. `FindFighter(id)->GetBoneLocation(bone)` gives hand / foot anchors for VFX.
- `OnUiCue` names are listed in REQUESTS.md. The Settings fields are read by FX (Flashes, ScreenShake,
  ReducedMotion) and by audio (volumes).
- The world level must carry the `FourfoldArena` and `FFSolid_<name>` tags (REQUESTS.md).
- Hit-stop belongs to the feel director. FX should not call `RequestHitStop` for the same events.

## Known gaps

- Nothing was compiled or run by Unreal itself. The syntax check uses 5.8.2 headers (the target is 5.8.3) and mock
  UHT.
- Touch layout, multi-touch, the safe area and the density heuristics were only tested synthetically. They need a
  pass on a real iPhone / iPad.
- The Lab Tuning page has no counter-rule thresholds, and the Lab-mode toggle depends on core (REQUESTS.md).
- The lock-on marker and the HUD labels use a pinhole projection of the rig camera. It assumes the default
  MaintainXFOV axis constraint.
- 120 Hz needs the iOS config change. Adaptive quality only steps down (as the Godot build).
