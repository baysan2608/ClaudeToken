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
| `Private/Logic/tests/` | `logic_tests.cpp` + CMake (guarded by `FF_LOGIC_TESTS`, so UBT ignores it). |
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
   - feeds `cam_delta` to the camera;
   - returns `ViewYaw()`.

   Then it calls `Session::Step` and broadcasts `OnFrame` once per frame.
3. `OnFrame` consumers:
   - each `AFourfoldFighter` interpolates prev -> curr, runs `AnimDirector::Update` and hands the recipe to its anim
     instance;
   - the controller updates the camera rig, the feel (the hit-stop it requests applies from the *next* frame), the
     HUD and touch contexts from `BuildHud()`, and the debug draw;
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

`ffg::CameraLogic` (a port of camera_rig.gd) handles:
- orbiting behind the player;
- lock-on framing, using the horizontal FOV from the viewport aspect;
- swing / lift / see-through against the analytic arena boxes;
- smoothing;
- the feel layer in real time: shake with distance falloff, kick, FOV punch, 3 % zoom on transformations, and the
  reduced-motion rules.

The feel table is `ffg::FeelFor` (MOVESET §10.2). It sets hit-stop frames, shake, kick, FOV and haptic per event
class. Hit-stop runs global dilation 0.05 for N rendered frames, at most 12 frozen frames per rolling second; reduced
motion caps it at 3. Haptics are at least 60 ms apart and off when the setting is off. Flashes are scaled by the
Flashes setting.

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
- **Unreal syntax check:** `Private/Logic/tools/ue_syntax_check/`.
  - `FF_UE_CHECK_DIR=<scratch> ./setup.sh` sparse-clones the UE 5.5 public headers (~80 MB) and writes mock UHT
    headers.
  - `./check_all.sh` runs `clang -fsyntax-only` on every module source plus one unity TU, against the real engine
    headers with the engine shared PCH.
  - Current result: 0 errors and 0 warnings in `Source/Fourfold` (`-Wshadow-all -Wundef -Wunused-variable`).
  - It cannot catch link errors, UHT rules (UPROPERTY types, specifiers) or 5.5 -> 5.8 API changes.

## Owner steps on the Mac

1. Follow MAC_SETUP §3: generate the project, build `FourfoldEditor`, open the editor.
2. Press Play in any map. With no arena actors you get the cube arena and the title over the AI duel.
3. If something fails to compile, send the first errors back (MAC_SETUP §9). API_NOTES lists the likeliest 5.8
   differences first.
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

- Nothing was compiled or run by Unreal itself. The syntax check uses 5.5 headers and mock UHT.
- Touch layout, multi-touch, the safe area and the density heuristics were only tested synthetically. They need a
  pass on a real iPhone / iPad.
- The Lab Tuning page has no counter-rule thresholds, and the Lab-mode toggle depends on core (REQUESTS.md).
- The lock-on marker and the HUD labels use a pinhole projection of the rig camera. It assumes the default
  MaintainXFOV axis constraint.
- 120 Hz needs the iOS config change. Adaptive quality only steps down (as the Godot build).
