# Fourfold — Unreal Engine 5.8 architecture and stream contracts

_Lead-architect document · 2026-10-06 · target: Unreal Engine **5.8.3** on an Apple-silicon Mac (Xcode 26.x), iPhone /
iPad from day one, Mac for development. Source of truth for gameplay: the Godot build in `game/` (546 sim tests) and
`docs/MOVESET.md`, `docs/COMBAT_SPEC.md`, `docs/kits/*.md`, `docs/CONTROLS.md`, `docs/AI.md`._

Read with: `MAC_SETUP.md` (what the owner does on the Mac), `MARTIAL_ARTS.md` (movement bible, clip catalogue, move →
clip map). Frozen contract files are marked **FROZEN** in their headers: their declarations never change; the owning
stream may only add.

---

## 0. Decisions at a glance

| Topic | Decision | Why |
|---|---|---|
| Gameplay code | **FourfoldCore**: an engine-free C++20 library (namespace `ff`), a faithful port of the Godot sim (`game/core`, `game/combat`, kits, AI, scenario + Lab logic). Built as a UE runtime module **and** with CMake here, where every test runs. | The Linux container cannot compile Unreal code; the sim is the part that must be exactly right, so it lives where it can be tested (Godot tests ported + a 13,300-row golden counter matrix exported from Godot). |
| Data | Move defs (162), bindings (160), rules (1,264 in 1,237 cells), statuses, scenarios, Lab tables **exported from the Godot registries** to JSON (`Source/FourfoldCore/Data/*.json`, `Tools/godot_export/export_core_data.gd`) and embedded into the binary. Code hooks are referenced by name (`"fn:EarthMetal.disc_execute"`, 212 names). | No hand re-typing of 1,400 tables; the C++ registries load exactly what Godot has; Lab live tuning keeps working on the same dynamic defs. |
| Sim ↔ UE contract | One facade, `ff::Session` + POD-ish `ff::Snapshot` / `ff::Event` / `ff::HudModel` (`Source/FourfoldCore/Public/ff/*.h`, FROZEN), consumed only through `UFourfoldSimSubsystem` (FROZEN header). | Every UE stream codes against fixed headers while the core is being written. |
| UE modules | `Fourfold` (game + Slate UI), `FourfoldFX` (all VFX), `FourfoldAudio` (all sound), `FourfoldShaders` (shader path), `FourfoldCore` (sim). No editor C++ module. | Disjoint ownership; no module cycles (FX / Audio depend on Fourfold, never the reverse). |
| Rendering on iOS | Mobile **forward** renderer, **4× MSAA**, mobile HDR, stationary sun + baked GI (Lightmass, built by the setup script) + CSM for movables, ≤ 4 dynamic point lights. No Lumen / Nanite / VSM on iOS (Lumen does not run on iOS/iPadOS in 5.8). Mac uses the desktop renderer (Lumen) for development. | Crisp thin effects (lightning, wind streaks) without TAA ghosting; cheap on Apple TBDR GPUs. |
| VFX | **C++-driven procedural-mesh VFX** (`UProceduralMeshComponent` ribbons, strips, shells, cones, CPU particles in one mesh per effect) + **Python-built materials** whose Custom nodes `#include "/Fourfold/..."` HLSL files (`unreal/Shaders`), + Blender-made hero meshes (rocks, crystals, metal pieces) and **Blender-rendered fire / smoke / explosion flipbooks** (Mantaflow → Cycles). Niagara is optional later. | Niagara emitters cannot be authored from Python; everything above can be written and partly tested here, and is mobile-safe (≤ 2 transparent layers, ≤ 32 particles per one-shot — the budgets of the Godot build). |
| Character | One original stylized-realistic martial artist on a **UE5-Mannequin-compatible skeleton** (the 89 bones of SK_Mannequin, same names and hierarchy, + 15 optional `ff_*` cloth/hair bones), built and skinned in Blender (`Tools/blender/character`), FBX + PNG textures committed, imported by Python. | IK Retargeter auto-characterises Manny names, so bought mocap packs / Epic's free animation samples retarget in minutes. |
| Animation | ~60–120 **hand-keyed** clips at **60 fps** authored by script in Blender on the frozen rig (`Tools/blender/common/ff_rig_spec.py`), grounded in Hung Gar / Tai Chi / Northern Shaolin / Baguazhang (`MARTIAL_ARTS.md`). Runtime: a **native C++ anim instance** (custom `FAnimInstanceProxy::Evaluate`) that blends locomotion, plays move clips time-aligned to the sim (contact at the end of startup), overrides hand shapes, foot IK, look-at, hit springs and secondary chains — no Anim Blueprint needed. Motion Matching can be added later on the same skeleton. | Anim Blueprints cannot be authored from Python; the native path gives frame-exact sync with the sim. |
| UI | Slate (C++), touch overlay ported from `game/ui/touch_controls.gd` (flicks, petals, rings, safe area, mm-based sizes); keyboard / gamepad read from `APlayerController` key state; Enhanced Input stays the engine default. | Code-only, testable logic in the core (view models), exact port of the tuned touch grammar. |
| Audio | Synthesised SFX (port + upgrade of `tools/audio/synth_sfx.py`, deterministic numpy) → WAV → Python import; C++ `FourfoldAudio` maps events to sounds (port of the Godot AudioDirector / FxDirector cues), loops per body / zone, voice caps, ambience. MetaSounds optional. | Original sound, regenerable, no editor graph work. |
| Editor automation | `Content/Python/fourfold_setup.py` (one run: imports, materials, level, lighting build) + `init_unreal.py` menu "Fourfold ▸ Build all". Scripts are idempotent, defensive and write a report to `Saved/Fourfold/setup_report.json`. | The owner runs one script; failures are reported, never silent. |

---

## 1. Goal, constraints and rules

* **Goal**: rebuild Fourfold in Unreal 5.8 at much higher quality: physical, smooth martial-arts animation, gorgeous
  fire / water / earth / air effects, great sound — on iPhone / iPad first — while keeping every gameplay rule of the
  Godot build (16 sub-elements × 10 slots, T0–T3 charge, the power-vs-threat counter matrix, the heat and mass ledgers,
  the 60 Hz deterministic sim, the counter-picking AI, Lab sandbox and 1v1 vs AI).
* **IP (absolute)**: 100 % original. No names, terms, characters, designs, symbols or scenes of any existing franchise
  anywhere (code identifiers, assets, docs, commit text, asset metadata, sound names). Movement comes from real martial
  arts (public traditions) and physics; visuals and sounds are original.
* **Environment**: streams work in a Linux container without Unreal. Engine-independent work (sim, data, Blender
  assets, audio synthesis, shader text) is tested here. Unreal C++ and editor Python are written blind against verified
  APIs (§15) and checked by the owner on the Mac. No state-changing git commands in streams.
* **Godot stays the oracle** until the port is proven: `/home/user/tools/godot` (4.7.2) runs the original headless.

---

## 2. Repository layout and ownership

```
unreal/                                  UE project root (Fourfold.uproject)                    [architect, FROZEN]
├── Fourfold.uproject, .gitignore                                                                 [architect, FROZEN]
├── Config/                     Default*.ini, IOS/IOSEngine.ini                                   [world_audio]
├── Source/
│   ├── Fourfold.Target.cs, FourfoldEditor.Target.cs                                              [architect, FROZEN]
│   ├── FourfoldCore/           engine-free sim (UE module + CMake library)                       [core]
│   │   ├── Public/ff/*.h       FACADE (FROZEN API: Config Math Value Json Types Snapshot Events Input ViewModels Session)
│   │   ├── Private/**          sim, combat, kits, AI, app (scenarios / Lab), data loader, Util/Json.cpp
│   │   ├── Private/UE/         FourfoldCoreModule.cpp (the only UE file; excluded from CMake)
│   │   └── Data/*.json         Godot export (moves rules hooks sim scenarios lab move_index golden_matrix)
│   ├── Fourfold/               game module + Slate UI                                            [game]
│   │   └── Public/             FourfoldCoords.h FourfoldSimSubsystem.h FourfoldSettings.h FourfoldFighter.h (FROZEN API)
│   ├── FourfoldFX/             VFX module                                                        [fx]
│   ├── FourfoldShaders/        /Fourfold virtual shader path (PostConfigInit)                    [fx]
│   └── FourfoldAudio/          audio module                                                      [world_audio]
├── Shaders/                    HLSL includes for material Custom nodes (/Fourfold/...)
│   ├── Common/, FX/                                                                              [fx]
│   ├── Env/                                                                                      [world_audio]
│   └── Character/                                                                                [character]
├── CoreTests/                  CMake project: tests, golden checks, poison + export checks, perf [core]
├── Content/
│   ├── Python/
│   │   ├── fourfold_setup.py, init_unreal.py                                                     [world_audio]
│   │   └── fourfold/__init__.py                                                                  [architect, FROZEN]
│   │       ├── fx/  character/  animation/  world/  audio/       one package per stream (owner = that stream)
│   └── Fourfold/Data/          runtime JSON staged as UFS (clips.json, anim_map.json → animation;
│                               sfx_manifest.json → world_audio; fx_config.json → fx; character.json → character)
├── SourceArt/                  generated binaries committed for the owner (FBX, PNG, WAV)
│   ├── Character/ [character]  Animation/ [animation]  VFX/ [fx]  Environment/ Audio/ [world_audio]
├── Tools/
│   ├── godot_export/           Godot → JSON exporter                                             [core]
│   ├── blender/common/         ff_rig_spec.py, ff_fbx_export.py, test_rig_spec.py               [architect, FROZEN]
│   ├── blender/character/ [character]   blender/animation/ [animation]
│   ├── vfx/ [fx]   world/ [world_audio]   audio/ [world_audio]
│   ├── py_mock/                mock `unreal` runner for dry runs                                 [architect, FROZEN]
│   └── py_stub/                real unreal.py stub (owner copies it after the first launch)      [owner]
└── docs/
    ├── ARCHITECTURE.md, MAC_SETUP.md, MARTIAL_ARTS.md                                            [architect]
    └── core/ game/ character/ animation/ fx/ world/ audio/      per-stream docs (owner = that stream)
```

Rules: a stream writes **only** inside its own paths. Frozen files are read-only for everyone. A stream that needs a
change elsewhere writes it into its own `docs/<stream>/REQUESTS.md` (file, exact change, reason); the integrator applies it.
`Content/**/*.uasset|umap` are produced on the Mac by the setup script (not by streams).

---

## 3. Modules and dependencies

```
FourfoldCore  (Runtime, Default; Core only; NoPCHs; no unity; no exceptions / RTTI)
     ▲
Fourfold      (Runtime, Default; Core CoreUObject Engine InputCore FourfoldCore | Slate SlateCore ApplicationCore EnhancedInput AnimationCore)
     ▲                      ▲
FourfoldFX (… FourfoldCore Fourfold | ProceduralMeshComponent RenderCore)     FourfoldAudio (… FourfoldCore Fourfold)
FourfoldShaders (Runtime, PostConfigInit; Core RenderCore) — maps unreal/Shaders → /Fourfold
```
Plugins enabled in the .uproject: PythonScriptPlugin, EditorScriptingUtilities, ProceduralMeshComponent, EnhancedInput.
A stream that needs another module adds it to **its own** Build.cs; a new plugin is a REQUEST (the .uproject is frozen).

---

## 4. Frame loop and data flow

```
             touch (Slate overlay)        keyboard / gamepad (PlayerController key state)
                     \                      /
                      ff::InputFrame latched (edges kept until a tick consumes them)        [game]
                                   │
UFourfoldSimSubsystem::Tick(dt) ── acc += dt_dilated × Session.TimeScale(); while acc ≥ 1/60 (max 4/frame):
                                   │     prev ← curr;  Session.Step(input, cameraYawSim);  input.ClearEdges()
                                   │   (Lab freeze: only Session.TakeStepRequests() steps)
                                   ▼
             ff::Session (engine-free) : CombatWorld 60 Hz + AI + scenario / Lab logic          [core]
                                   │
                 Snapshot prev / curr, alpha = acc · 60, events (std::vector<ff::Event>)
                                   ▼
                OnFrame.Broadcast(FFourfoldFrame)  — once per rendered frame
       ┌───────────────┬──────────────────┬───────────────────┬───────────────────────┐
  fighters + anim   camera + feel     HUD / menus / Lab       FourfoldFX              FourfoldAudio
  (game)            (game: hit-stop   (game: Slate, reads    (body views from        (event → sounds,
                    shake haptics)    Session.BuildHud())    snapshot, cues from     loops from bodies /
                                                             events)                 zones, ambience)
```
* The sim is the only movement authority (no physics, no root motion). Visuals interpolate positions between `Prev` and
  `Curr` with `Alpha`; facing uses the shortest-arc interpolation.
* **Persistent visuals come from body state only** (mat / form / tag / heat / liquid / zone_radius / tier / spin …);
  **one-shot cues come from events only** — the Godot rule, unchanged.
* Hit-stop is presentation: the game's feel director sets global time dilation (0.05) for N real frames (≤ 12 per
  second); the sim slows with it because the accumulator uses dilated time — determinism holds because the sim only
  counts ticks. Camera shake, UI and audio fades use undilated time.

---

## 5. Coordinates and units

Sim (Godot): metres, right-handed, +Y up; forward of facing `f` = (sin f, 0, cos f); seconds; kg; °C; HU.
Unreal: centimetres, left-handed, +Z up, yaw degrees. **Only** `Source/Fourfold/Public/FourfoldCoords.h` converts:
`UE(X,Y,Z) = 100·(x, z, y)`, `yawUE = 90° − deg(f)`, camera yaw likewise (`FF::UEYawDegToSimYaw`). The Y/Z swap is the
reflection between the two handednesses, so the arena is not mirrored. Wall half extents: `FF::HalfExtentsToUE`.
Skeletal meshes face +Y in the asset (UE5 Manny convention); the fighter rotates its mesh −90° yaw (a `MeshYawOffset`
property, default −90, so the owner can correct it in the editor if an import differs).

---

## 6. FourfoldCore — the engine-free simulation  (stream `core`)

### 6.1 Facade (FROZEN API, `Source/FourfoldCore/Public/ff/`)
| Header | Contents |
|---|---|
| `Config.h` | `FOURFOLDCORE_API` handling, `FF_WITH_UE` (from Build.cs), list of identifiers that are Unreal macros (never use) |
| `Math.h` | `ff::Vec2`, `ff::Vec3` — float32, Godot semantics (normalized of zero = zero, limit_length, …) |
| `Value.h` | `ff::Value` / `ff::Array` / `ff::Dict`: GDScript semantics (reference arrays / dicts, insertion-ordered string keys, int / float numeric equality, `get(k, def)`, `duplicate(deep)`), header-only |
| `Json.h` | `ff::ParseJson` / `ff::ToJson` (locale-independent, no exceptions, decodes the exporter's `$v3 $v2 $v3a $color $map $f $fn` tags). **All UE modules parse their runtime JSON with it** (FJsonObject's API changed in 5.8). |
| `Types.h` | enums with the Godot integer values (`Element Gesture Mat Phase Form ActionPhase Slot`), name tables, `kSimHz`, `kSimDt` |
| `Snapshot.h` | `ActorView` (pose, resources, element/sub, action incl. effective startup / active / recovery, stun, statuses, charge), `BodyView` (every field views read + `props` copy), `ArenaView`, `Snapshot` |
| `Events.h` | `Event {type, tick, data}` — `type` / `data` keys are exactly the Godot event names / fields (159 sim types, `fx_events.gd`, MOVESET §15.6) + `app_*` session events |
| `Input.h` | `InputFrame` (port of `input_frame.gd`, incl. `MergeFrom`, `ClearEdges`) |
| `ViewModels.h` | `HudModel` (port of `Game._hud_context` and friends), `PracticeItem`, `MoveInfo`, Lab structs (`LabState`, `LabSpawnEntry`, `LabCombo`, `ComboTrackerView`, `MatrixCounter`, `MatrixResult`, `TuningField`) |
| `Session.h` | `ff::Session`: scenarios, `Step(InputFrame, cameraYaw)`, snapshot, events, HUD, move registry queries, rival AI, Lab (spawn / try / combos / matrix / tuning), progression save / load |
| `FourfoldCore.h` | umbrella include |
`Value.h`, `Json.h` / `Private/Util/Json.cpp` are already implemented and tested (g++ 13 / clang 18, `-Wall -Wextra
-Wshadow -Wconversion -Wshorten-64-to-32 -Werror`, no exceptions / RTTI, Unreal-macro poison build; every exported
JSON file parses and round-trips).

### 6.2 Port strategy
* **Faithful, readable port** of `game/core`, `game/combat` (verbs, acts, registry), the 15 kit modules, `game/actors/ai_*`,
  and the engine-free parts of `game/game.gd`, `game/scenarios/scenarios.gd`, `game/ui/lab/*` (session, spawn catalogue,
  scripts, combos, tracker, matrix query, tuning, move-list data), `game/ui/input_frame.gd`, `player_controller.gd`.
  Same names in snake_case (`split_body`, `heat_body`, `Charge::param`) so a reader can diff C++ against GDScript.
  Dictionaries stay `ff::Dict` where the GDScript is data-driven (defs, `inst.data`, `body.props`, rule tables, events);
  typed structs everywhere else (`MatBody`, `ActorState`, `ActionInst`, ledgers, typed id maps).
* **Data, not code, for tables**: load `Data/moves.json`, `rules.json`, `hooks.json`, `sim.json`, `scenarios.json`,
  `lab.json` at first use (embedded, §6.3). `"fn:Class.method"` strings resolve through a **hook registry**
  (`name → C++ function pointer`) per hook kind: `hook_execute`, `hook_tick`, `hook_impact`, outcome handlers,
  channel hooks, body ticks, zone effects, tech previews, status hooks. Unresolved names are listed by a test and in
  `docs/core/PORT_STATUS.md`; the move then runs its verb's default behaviour (playable, approximate).
* **Order of work** (each step ends green): foundations (Value, Json, Godot-exact RNG and math) → data load + registries
  → CombatWorld (actors, movement, bodies, thermal, waves, walls, ledgers, contests) → Interactions / Agent / Outcomes /
  CoreRules → verbs → legacy acts (sub-0 kits) → **Session facade with Lab + Spar playable** (the UE streams need it)
  → AI brain + planner + presets → kits by element (Earth, Fire, Water, Air: hooks, outcomes, body ticks, zones,
  previews) → Lab tooling logic → progression / challenges → perf soak. If time runs out, the remaining kit hooks are
  a second wave split by element (one stream per element, disjoint `Private/Combat/Kits/<Element>/`).

### 6.3 Data pipeline
1. `Tools/godot_export/export_core_data.gd` (run: `/home/user/tools/godot --headless --path game -s
   "$PWD/unreal/Tools/godot_export/export_core_data.gd" -- --out="$PWD/unreal/Source/FourfoldCore/Data"`) writes the
   JSON files (format in its header; ints stay ints, floats keep a '.', vectors / maps / callables are tagged).
2. `CoreTests/tools/embed_data.py` turns them into `Source/FourfoldCore/Private/Generated/EmbeddedData.gen.cpp`
   (string chunks ≤ 8 KB, concatenated at runtime; deterministic output) — committed, so the Mac build needs no Python.
   `golden_matrix.json` is test-only (not embedded).
3. `ff::Session::DataOk()` reports parse failures; a test asserts every def / rule / hook loads.

### 6.4 Determinism and numerics
* Godot's `RandomNumberGenerator` is PCG32 — port it exactly (seed, `randi`, `randf`, `randf_range`, `randi_range`,
  `randfn`) from Godot's MIT sources (`core/math/random_pcg.*`), so seeded tests behave like Godot.
* GDScript `float` is double, `Vector3` components are float32: keep that split (ff::Vec3 is float32) and port Godot's
  Vector3 / math helpers (normalized, rotated, slerp, move_toward, smoothstep, lerp_angle, is_equal_approx …) from
  Godot's sources.
* Same seed + same inputs ⇒ same state hash on one platform. Cross-platform bit equality is not required (tests use
  tolerances against Godot goldens). Never rely on NaN / Inf checks for control flow (fast-math safety); guard
  divisions instead.

### 6.5 Rules that keep the core compiling inside Unreal
* No exceptions, no RTTI (`dynamic_cast`, `typeid`, `std::any`), no `<iostream>` in shipping code, no thread-local
  magic; C++20 only from the standard library; no third-party code.
* Never use Unreal macro names as identifiers (`check verify ensure TEXT PI IN OUT INDEX_NONE TRUE FALSE nil Nil YES NO`
  … full list in `Config.h`); never define `int32` / `uint8` style typedefs. The poison build enforces it.
* Every non-inline public class / free function carries `FOURFOLDCORE_API` (the Mac editor is a modular build; missing
  exports only fail there). The CMake shared-library build with `-fvisibility=hidden` enforces it.
* `#if defined(X)` only (UE uses `-Wundef` as an error); no unnamed-namespace name collisions across files (the module
  is non-unity, but keep names unique anyway); no `using namespace` at file scope in headers.
* Clean under `-Wall -Wextra -Wshadow -Wconversion -Wshorten-64-to-32 -Werror` with clang **and** g++.

### 6.6 CMake project and tests (`unreal/CoreTests`)
Targets: `ff_core` (static, all `Private/**/*.cpp` except `Private/UE/`), `ff_core_shared` (shared, hidden visibility,
`FOURFOLDCORE_API=__attribute__((visibility("default")))`) + `ff_facade_link_test` (uses only `Public/ff`, links the
shared lib), `ff_poison` (every source compiled with `-include ue_macro_poison.h`), `ff_tests` (runner:
`ff_tests [filter]`), `ff_unity` (CMake `UNITY_BUILD ON`, compile only), `ff_perf` (soak). Test suites: ported Godot
suites (`test_core_*`, flagship, thermal, lightning, contest, waves, combat rules, energy / soak, review fixes, kit
suites, AI suites, scenarios), **golden matrix** (every row of `golden_matrix.json` through the C++ MatrixQuery: band
and outcome exact, numbers within 1e-3 relative), facade tests (Session load → step → snapshot → events → HUD), data
completeness (all defs / rules / hooks resolve). One command: `cmake -S unreal/CoreTests -B <build> -G Ninja &&
cmake --build <build> && <build>/ff_tests`.

---

## 7. Fourfold game module  (stream `game`)

### 7.1 Public contract (FROZEN API, `Source/Fourfold/Public/`)
* `FourfoldCoords.h` — conversions (§5), header-only, complete.
* `FourfoldSimSubsystem.h` — `UFourfoldSimSubsystem : UTickableWorldSubsystem`: `Get(ctx)`, `GetSession()`,
  `GetSnapshot()`, `GetPrevSnapshot()`, `GetAlpha()`, `LoadScenario(id, opts)`, `GetScenarioId()`, `FindFighter(id)`,
  `GetPlayerActorId()`, `SetPaused / IsPaused`, `RequestHitStop(frames)`, delegates **`OnFrame(const FFourfoldFrame&)`**
  (once per rendered frame: prev / curr snapshot, alpha, `const std::vector<ff::Event>*` of the ticks stepped, real and
  dilated delta, frame index, paused), `OnScenarioLoaded(FString)`, **`OnUiCue(FName)`** (UI sounds:
  `ui_tap ui_select ui_back ui_open ui_close ui_toggle ui_ring_open ui_ring_pick ui_error ui_pause ui_resume ui_toast`).
* `FourfoldSettings.h` — `FFourfoldSettings` (touch layout, camera, comfort: shake / flashes / haptics / reduced motion /
  slow-mo assist, audio volumes, quality −1..2, frame-rate cap, debug) + `UFourfoldSettingsSubsystem`
  (`Get`, `GetSettings`, `SetSettings(s, bSave)`, `GetEffectiveQuality`, `OnChanged`); saved as JSON under
  `Saved/Fourfold/settings.json` (parsed with `ff::ParseJson`).
* `FourfoldFighter.h` — `AFourfoldFighter`: `GetSimActorId()`, `GetBodyMesh()`, `GetBoneLocation(FName)`.
Consumers (fx, world_audio) use only these four headers plus `ff/*.h`.

### 7.2 Flow
Boot into `/Game/Fourfold/Maps/L_Lab` with `AFourfoldGameMode` (no default pawn; the subsystem spawns one
`AFourfoldFighter` per sim actor; the PlayerController views through the camera rig). Title overlay over an AI-vs-AI
attract duel (`ScenarioOptions.autoplay = "duel"`): **Lab** · **Free Spar** (rival difficulty Easy / Normal / Hard,
kit) · **Practice** (scenario list, `Session::PracticeItems`) · **Watch** (AI duel) · **Settings**. Pause menu:
resume, restart, practice, settings, dev panel (Lab), quit to title. KO / round reset, toasts and challenges come
from `app_*` events. Progression and Lab tuning are saved under `Saved/Fourfold/` (`Session::SaveProgress`,
`LabSaveTuning`).

### 7.3 Input
* Keyboard / mouse / gamepad exactly as `docs/CONTROLS.md` (WASD, arrows, J/U/N/H/K/L/Space, 1–4 + same key cycles
  subs, Q/E, Tab, Esc, mouse buttons; pad X/Y/LT/B/A/RB/RT/LB/D-pad/R3/Start), with the chord rules (K+J push, K+N sink,
  J while L held = shape). The dev panel moves to **F2** (backquote is Unreal's console key).
* Touch: a Slate overlay (`SLeafWidget` + `OnTouchStarted/Moved/Ended`, multi-finger by pointer index, added with
  `UGameViewportClient::AddViewportWidgetContent`) porting `touch_controls.gd`, `touch_layout.gd`,
  `flick_recognizer.gd`, `ui_scale.gd`: floating stick, camera drag, ATTACK tap / hold / flick petals, GUARD with
  push / sink flicks, TECHNIQUE drag-aim + cancel zone + second-finger shape tap, EVADE tap / hold, element chips +
  sub-element ring (tap-again and long-press-slide), target, pause; sizes in millimetres from the device DPI; safe-area
  insets; left-handed mirror; release-all on focus loss (never fires a half-aimed technique).
* Both produce `ff::InputFrame`s merged with `MergeFrom`; edges latched until the subsystem consumes them in a tick.
  The engine's virtual joystick is disabled (`DefaultTouchInterface=None` + `ActivateTouchInterface(nullptr)`).

### 7.4 Camera
Port of `presentation/camera_rig.gd`: third-person orbit behind the player, framing the locked target, collision
against `Session::Arena()` boxes (no physics), smoothing, plus the feel layer (shake with distance falloff, kick along
the hit, FOV punch, 3 % zoom on transformations; reduced-motion rules).

### 7.5 Feel (MOVESET §10.2)
| Event | Hit-stop (60 Hz frames) | Camera | Haptic (`FPlatformMisc::PrepareMobileHaptics` / `TriggerMobileHaptics`) |
|---|---|---|---|
| T0 / T1 / T2 / T3-knockdown hit | 3 / 5 / 7 / 9 | shake 0.10 / 0.18 + kick / 0.28 + kick / 0.40 + kick + FOV −3° | ImpactLight / Light / Heavy / Heavy |
| block light / heavy (TP ≥ 25) | 2 / 4 | 0.08 / 0.15 | ImpactMedium |
| perfect counter (full, in your favour) | 6 + white flash (flashes setting) | 0.2 | FeedbackSuccess |
| clash / shatter | 4 / 3 | 0.15 / 0.12 | ImpactLight |
| transformation (stone → lava, slump, glass, ice ridge) | 0 | 3 % zoom 0.3 s | ImpactMedium |
Caps: ≤ 12 hit-stop frames per second, haptics ≥ 60 ms apart, reduced motion → shake × 0.3, no FOV / zoom, hit-stop ≤ 3.

### 7.6 HUD, menus, Lab panel (Slate)
HUD from `Session::BuildHud()` (bars, element / sub, resources that matter, status chips, charge bar with move name,
target marker projected with `ProjectWorldLocationToScreen`, rival panel, objective / challenge text, toasts). Lab
panel pages Dev / Spawn / Moves / Combos / Matrix / Tuning on the Lab facade calls. Fonts: engine default (Roboto).

---

## 8. Character and animation contracts  (streams `character`, `animation`, runtime by `game`)

### 8.1 Skeleton (FROZEN: `Tools/blender/common/ff_rig_spec.py`, `ff_fbx_export.py`, self-test `test_rig_spec.py`)
* The 89 UE5 SK_Mannequin bones with identical names / hierarchy (root, pelvis, spine_01–05, neck_01–02, head,
  clavicle / upperarm / lowerarm / hand + 4 twist bones per arm, 4 metacarpals + 12 phalanges + 3 thumb bones per hand,
  thigh / calf / foot / ball + 4 twist bones per leg, ik_foot_root / ik_foot_l / ik_foot_r, ik_hand_root / ik_hand_gun /
  ik_hand_l / ik_hand_r, interaction, center_of_mass) + 15 secondary bones `ff_hair_01..03`, `ff_sash_{l,r}_01..02`,
  `ff_hem_{fl,fr,bl,br}_01..02` (springs at runtime, never keyed).
* Rest pose: A-pose (arms 45° down, palms toward the thighs), 1.80 m to the head-bone tail. Local axes: +Y along the
  bone; the roll makes every primary flexion a **positive rotation about local +X** (table in the spec header).
* FBX: the armature object is named **`root`** (becomes UE's root bone; there is no Blender bone called root), identity
  transform, scene unit scale 1.0, `apply_scale_options='FBX_SCALE_ALL'`, Blender default axes and bone axes, no leaf
  bones, all bones exported. Animations: one action per FBX, 60 fps, baked every frame, first key at frame 0.
  Verified here: build → validate → flexion convention → export mesh + animation → re-import: 103 bones, no missing /
  extra / parent mismatches.
* Bought mocap / Epic sample animations: IK Retargeter with auto-generated IK Rigs (Manny names are auto-characterised);
  bone axes differ from Manny's (Blender convention), so retarget rather than share the Skeleton asset.

### 8.2 Character assets (stream `character`)
| Asset | Path |
|---|---|
| Skeletal mesh / skeleton / physics asset | `/Game/Fourfold/Characters/Fighter/SK_Fighter`, `SKEL_Fighter`, `PA_Fighter` |
| Materials | `/Game/Fourfold/Characters/Fighter/Materials/M_Fighter_*` (+ instances `MI_Fighter_<slot>`) |
| Source | `unreal/SourceArt/Character/SK_Fighter.fbx`, `T_Fighter_<slot>_{BC,N,ORM}.png`, previews |
| Data | `Content/Fourfold/Data/character.json`: material slots, palettes (player / rival / dummy), bone list check, LOD info |
`character.json` schema:
```json
{ "schema": "fourfold.character/1", "rig": "ff-manny-1.0",
  "mesh": "/Game/Fourfold/Characters/Fighter/SK_Fighter", "skeleton": "/Game/Fourfold/Characters/Fighter/SKEL_Fighter",
  "mesh_yaw_offset_deg": -90.0, "height_m": 1.79,
  "slots": ["skin", "hair", "eyes", "cloth_main", "cloth_accent", "wraps", "sash", "shoes"],
  "palettes": { "player": {"FF_Main": [0.19, 0.23, 0.31, 1.0], "FF_Accent": [0.62, 0.50, 0.32, 1.0], "FF_Trim": [0.85, 0.82, 0.74, 1.0]},
                "rival":  {"FF_Main": [0.40, 0.20, 0.15, 1.0], "FF_Accent": [0.78, 0.58, 0.28, 1.0], "FF_Trim": [0.20, 0.18, 0.17, 1.0]},
                "dummy":  {"FF_Main": [0.56, 0.50, 0.40, 1.0], "FF_Accent": [0.40, 0.36, 0.30, 1.0], "FF_Trim": [0.70, 0.66, 0.58, 1.0]} } }
```
The game reads it at runtime (palettes per fighter role, mesh yaw offset); values are linear colours.
Material slots (names fixed): `skin`, `hair`, `eyes`, `cloth_main`, `cloth_accent`, `wraps`, `sash`, `shoes`.
Parameters every fighter material understands (missing ones are ignored by the game): vectors `FF_Main`, `FF_Accent`,
`FF_Trim` (palette tints), `FF_ElementColor`; scalars `FF_Wet` (0..1 darkening + gloss from sim `wetness`), `FF_Frost`
(chilled / frozen), `FF_Burn` (burning char), `FF_ElementGlow` (charge rim 0..1). The game creates one dynamic
instance per slot per fighter and drives them from the snapshot. Budget: ≤ 30 k triangles LOD0, LOD1 ≤ 15 k, LOD2 ≤ 6 k;
≤ 4 material slots drawn per LOD where possible; textures 2048² (skin / cloth) and 1024² (others), mobile-friendly
(Default Lit, no subsurface profile on iOS; fake translucency in the skin shader).

### 8.3 Animation data (stream `animation`)
Assets `/Game/Fourfold/Characters/Fighter/Anims/A_<clip>` (UAnimSequence on SKEL_Fighter, no root motion, looping flag
from the JSON). Sources `unreal/SourceArt/Animation/A_<clip>.fbx`.

`Content/Fourfold/Data/clips.json`
```json
{ "schema": "fourfold.clips/1", "fps": 60, "rig": "ff-manny-1.0",
  "asset_root": "/Game/Fourfold/Characters/Fighter/Anims",
  "clips": { "e_strike": { "asset": "A_e_strike", "frames": 24, "duration": 0.4, "loop": false, "contact": 8,
                           "contacts": [8], "base": "e_stance", "speed": 0.0, "hands": {"l": "fist", "r": "tiger"},
                           "foot_plants": {"l": [[0, 24]], "r": [[0, 3], [10, 24]]}, "priority": "P0",
                           "technique": "iron bridge drive" } },
  "hand_poses": { "fist": "hand_fist", "palm": "hand_palm", "willow": "hand_willow", "tiger": "hand_tiger",
                  "crane": "hand_crane", "sword": "hand_sword", "oxtongue": "hand_oxtongue", "relaxed": "hand_relaxed",
                  "cup": "hand_cup", "spread": "hand_spread" } }
```
`contact` = frame of the power point (null for loops); `speed` = design ground speed for gait clips (m/s);
`foot_plants` = planted frame ranges per foot (inclusive start, exclusive end).

`Content/Fourfold/Data/anim_map.json`
```json
{ "schema": "fourfold.anim_map/1",
  "locomotion": { "idle": "idle", "stance": ["e_stance", "w_stance", "f_stance", "a_stance"],
                  "walk": "walk", "run": "run", "strafe_l": "strafe_l", "strafe_r": "strafe_r", "back": "walk_back" },
  "reactions": { "light": "hit_light_front", "light_back": "hit_light_back", "heavy": "hit_heavy",
                 "knockdown": "knockdown", "getup": "getup", "guard_break": "guard_break", "bound": "stagger",
                 "stagger": "stagger", "block": "block_impact", "perfect": "deflect" },
  "air": { "jump": "jump", "fall": "fall", "land": "land", "glide": "glide" },
  "guard": { "default": "e_guard", "by_element": ["e_guard", "w_shield", "guard", "a_guard"] },
  "modes": { "flight": "flight", "hover": "hover", "skate": "skate", "surf": "surf" },
  "moves": { "earth_attack": { "startup": "e_lift", "release": "e_strike",
                               "tiers": { "1": {"release": "e_heave"}, "2": {"release": "e_heave"}, "3": {"release": "e_heave"} },
                               "hands": {"l": "tiger", "r": "fist"} },
             "fire_attack": { "startup": "f_jab", "hold": "f_charge", "release": "f_palm_burst",
                              "tiers": { "2": {"release": "f_column"}, "3": {"release": "f_inferno"} } } },
  "fallbacks": { "slot": { "strike": "f_jab", "thrust": "a_pierce", "ground": "e_ground_slap", "sweep": "e_sweep",
                           "guard": "e_wall", "push": "w_push", "sink": "e_sink", "tech": "w_hold",
                           "evade": "evade_fwd", "evade_hold": "run" } } }
```
Keys of `moves` are sim move ids (every id of `MARTIAL_ARTS.md` §4 / `Data/move_index.json`). A running guard has
`action.id == "guard"`; its sub-element def id is `action.data.spec` — look that id up (e.g. `aegis_plate`, `ice_wall`).
Optional per-move `"modes": {"heat": {...}, "draw": {...}, "vent": {...}, "scorch": {...}}` override the clips by
`action.data.mode` (lower-case; Fire Thermal, grips with RECALL / RIP …).

### 8.4 Runtime playback rules (stream `game`, `UFourfoldAnimInstance`)
* Native `UAnimInstance` subclass with a custom proxy: `CreateAnimInstanceProxy` / `DestroyAnimInstanceProxy`,
  `FAnimInstanceProxy::PreUpdate` copies the fighter's view state, `Evaluate(FPoseContext&)` samples clips with
  `UAnimSequence::GetAnimationPose(FAnimationPoseData, FAnimExtractContext(double time, false[, FDeltaTimeRecord, bLooping]))`
  and blends bone transforms itself. Assigned with `SetAnimInstanceClass` (no Anim Blueprint needed).
* **Locomotion**: idle / element stance when still; walk / run / strafe / back by speed and direction relative to
  facing, playback rate = actual speed ÷ design speed (stride-matched, no foot sliding), smooth 0.15 s blends.
* **Actions** (from `ActorView.action`): STARTUP plays `startup` time-scaled so its `contact` lands at the end of the
  effective startup (rate clamped 0.6–1.6; if clamped, the contact is held for the remaining frames) → CHARGE /
  CHANNEL loops `hold` → ACTIVE continues the startup clip past contact or plays `release` (tier override) from its
  contact → RECOVERY plays to the end and blends back to the stance by the end of recovery. Inertial-style cross-fade
  of 3–6 frames on every switch (blend from the last output pose). Unmapped moves use the slot fallback.
* **Hand shapes** per move (`hands`) override finger bones with weight 1 from startup to recovery end.
* **Reactions** from `stun_kind` + `last_hit_dir` (front / back); knockdown → getup; guard loops while `guarding`;
  `block_impact` additive on block events.
* **Procedural layers**: two-bone foot IK to the sim ground height with planting from `foot_plants`; head / spine
  look-at toward the lock target (±60° yaw, ±30° pitch, smoothed); hit springs (port of `hit_reactor.gd`); spring chains
  for `ff_hair_*`, `ff_sash_*`, `ff_hem_*` (port of `fighter_secondary_motion.gd`); breathing additive in stances.
* Charge-tier tremble and the T3 "ready" glint are the fx stream's (materials / VFX), not bones.

### 8.5 Later: mocap and Motion Matching
Same skeleton names → IK Retargeter → replace locomotion clips (or add a Pose Search database driven by the same
velocity inputs in an Anim Blueprint whose parent class is `UFourfoldAnimInstance`, switching the proxy to graph
evaluation). Measure CPU on the oldest target iPhone before adopting Motion Matching.

---

## 9. VFX strategy and contract  (stream `fx`)

* **Why not Niagara now**: Python cannot author emitters / modules, the editor is not available here, and the Godot
  look (mesh + shader effects, ≤ 2 transparent layers) maps cleanly onto C++-driven geometry. Niagara can be added later
  for sparks / dust once the owner can author in the editor.
* **Module** `FourfoldFX`: `UFourfoldFxSubsystem` (world subsystem) binds `UFourfoldSimSubsystem::OnFrame` at
  `OnWorldBeginPlay`, and owns pools of view components:
  * **Body views** (keyed by body id, created on first sight, released with a fade when the body disappears),
    mapped exactly like `presentation/body_views.gd` (table in `docs/VFX.md` "Body → view map"): stone chunks /
    spears / crags (Blender rock set, tumble from velocity, heat / melt / crust / frost via material params),
    walls (stone / obsidian / sand / mud / ice / glass / vine / plate, rise / damage / heat / slump), spikes, metal
    (disc spin + blur, lance, rod, plate, caltrops, red-hot), sand slug / clouds / surge, ground strips (lava wave,
    water wave with foam lip, sand surge, rime), clouds (fog / mist / steam / sandstorm / geyser), ground decals
    (quicksand, ice floor, mud, melt pit), crystals (ice / glass), vines, fireballs / comets / embers, flame fields /
    fire lines, shells (bubbles, wells, corona, static fields), ground current crackle, wind blades, vortices
    (layered funnels with debris, infusion tints), charged-body crackle.
  * **One-shot cues** from events: `fx` (cast, release, cone, beam, burst, ring, erupt, trail, splash, aura),
    `interaction` outcome cues (MOVESET §11.3), `charge` tiers (hand ring T1, + ground ripple and rim T2, + aura shell,
    light pulse, 2-frame glint T3), `status` visuals on fighters (attached to bones: burning, wet drips, frost shell,
    crackle, grit, roots, mist veil, anchor dust, armor aura, levitation rings), `zone` open / close, `clash`,
    `morph`, `slump`, `convert`, `capture`, `ricochet`, `stance`, `mode`, `inrush`, `extinguish` …
  * **Primitives** (C++): ribbon (streams, bolts with branches and re-strike flicker, trails), path strip (waves),
    tube (vines), shell / sphere, cone stack (flames, tornado layers spinning at different rates), CPU particles drawn as
    camera-facing quads in **one** procedural mesh section per effect (vertex colour = tint / alpha / age, UVs = flipbook
    frame), ground quads instead of deferred decals.
* **Materials** built by `Content/Python/fourfold/fx/` with `MaterialEditingLibrary`; heavy logic lives in HLSL files
  `unreal/Shaders/Common/*.ush` (noise, fbm, flow, fresnel, flipbook helpers) and `unreal/Shaders/FX/*.ush`
  (flame, lava crust, water, ice, lightning, vortex, smoke) included from Custom nodes via
  `include_file_paths = ["/Fourfold/FX/FFFlame.ush"]`. Parameters drive state (Heat, Melt, Crust, Frost, Tier, Age …).
  Asset root `/Game/Fourfold/FX/` (Materials, Textures, Meshes, Flipbooks).
* **Offline assets** (`Tools/vfx/`, outputs `SourceArt/VFX/`): Blender Mantaflow fire / smoke / explosion / steam
  simulations rendered with Cycles to 8×8 flipbooks (512–1024² atlases), noise textures (numpy), rock / crystal / metal
  meshes with baked normal maps (FBX).
* **Budgets** (per effect, iOS): ≤ 2 transparent layers, ≤ 32 particles per one-shot, ≤ 4 dynamic point lights in the
  scene (shadowless, short), meshes rebuilt at most once per frame, everything pooled and pre-warmed; a quality level
  (0..2 from `UFourfoldSettingsSubsystem::GetEffectiveQuality`) scales counts. Flashes respect the `Flashes` setting.
* The character's status looks on the body itself (wet / frost / burn / glow) are material params of §8.2 driven by
  the game; the fx stream adds the volumetric parts (drips, steam, flames, crackle).

---

## 10. World contract  (stream `world_audio`)

* Level `/Game/Fourfold/Maps/L_Lab` built by `Content/Python/fourfold/world/` from `Source/FourfoldCore/Data/sim.json`
  (`arena_lab`: boundary walls, cover wall, terrace, step block, high ledge, two pillars, pool rectangle, metal plate) —
  **exact sim geometry** (the sim does collision analytically; visual meshes have no collision). Dressing outside the
  play area (original courtyard architecture: stone, timber, cloth banners with an original four-part emblem, terraces,
  distant mountains, sky) — no recognisable franchise architecture.
* The builder tags the arena's root actor with the Actor tag **`FourfoldArena`**. When a level has no such actor (setup
  not run yet, or any other map) the game spawns a plain placeholder arena from `Session::Arena()` (engine cubes), so
  the game is playable before the world assets exist.
* Lighting: stationary directional (sun) + static or stationary sky light + reflection captures + height fog + post
  process (bloom, mild grading, no auto exposure), lightmaps built by the setup script; a dynamic-only fallback
  if the build fails. Pool water surface and metal plate materials match the sim rectangles.
* Environment materials (ground flagstones, walls, wood, cloth, water, metal, foliage) built in Python, HLSL in
  `unreal/Shaders/Env/`, textures generated by `Tools/world/` (numpy / Blender) into `SourceArt/Environment/`.
* Settings ownership: all `Config/*.ini` (rendering, iOS bundle / orientation / signing placeholders, packaging,
  input defaults, editor Python developer mode).

---

## 11. Audio strategy and contract  (stream `world_audio`)

* Synthesis: `Tools/audio/` (port + upgrade of `tools/audio/synth_sfx.py`: transient + body + tail, layered noise /
  modal impacts / granular textures, deterministic seeds, motifs per element, −3 dBFS peaks, 48 kHz 16-bit WAV) →
  `SourceArt/Audio/SFX/*.wav`; ambience beds (wind, distant water, birds-free courtyard air) → `SourceArt/Audio/Ambience/`.
* `Content/Fourfold/Data/sfx_manifest.json`:
  `{ "schema": "fourfold.sfx/1", "sounds": { "<name>": { "asset": "/Game/Fourfold/Audio/SFX/S_<name>", "gain_db": -3.0,
  "pitch_var": 0.05, "max_voices": 4, "loop": false, "category": "impact|element|ui|ambience|loop", "attenuation":
  "near|mid|far|2d" } }, "events": { … event → sound rules … } }`.
* Import: `Content/Python/fourfold/audio/` creates SoundWaves (looping flags), optional sound classes / submixes;
  `FourfoldAudio` C++: `UFourfoldAudioSubsystem` binds `OnFrame` (events → one-shots via the Godot FxDirector /
  AudioDirector cue logic, per-body / zone loops keyed by id, stinger rate limit ≤ 1 per 0.1 s, voice caps, distance
  attenuation from the camera, volumes from settings) and `OnUiCue` (UI sounds). Haptics stay in the game module.

---

## 12. Editor-Python contract (all asset streams)

* Each stream owns `Content/Python/fourfold/<pkg>/` with `__init__.py` exposing
  **`build_all(force: bool = False) -> dict`** returning `{"created": [...], "skipped": [...], "failed": [{"item",
  "error"}], "notes": [...]}`. Idempotent: skip existing assets unless `force`. Never raise out of `build_all`
  (catch per item, log with `unreal.log_error`, continue).
* Orchestrator `Content/Python/fourfold_setup.py` (world_audio): order **fx → character → animation → world → audio →
  lighting build → save all**, writes `Saved/Fourfold/setup_report.json` and prints a summary; tolerates missing
  packages. `init_unreal.py` adds a "Fourfold" main-menu entry (Build all / Build one package / Open report).
* Verified API patterns (use these; verify anything else with web docs / open-source projects, never guess):
  materials — `unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, path, unreal.Material,
  unreal.MaterialFactoryNew())`, `unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionCustom,
  x, y)`, Custom node properties `code`, `output_type` (`unreal.CustomMaterialOutputType.CMOT_FLOAT3`), `inputs`,
  `additional_outputs`, `include_file_paths`, `connect_material_expressions`, `connect_material_property`,
  `recompile_material`; FBX / PNG / WAV import through Interchange: `pipeline = unreal.new_object(unreal.InterchangeGenericAssetsPipeline)`,
  `pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties").set_editor_property("skeleton", skel)`,
  `mesh_pipeline` (`import_skeletal_meshes`, `import_static_meshes`, `create_physics_asset`), `animation_pipeline`
  (`import_animations`), `params = unreal.ImportAssetParameters()` (`is_automated`, `replace_existing`,
  `override_pipelines = [unreal.SoftObjectPath(pipeline.get_path_name())]`), `mgr = unreal.InterchangeManager.get_interchange_manager_scripted()`,
  `mgr.import_asset(dest, mgr.create_source_data(file), params)`, `mgr.wait_until_all_tasks_done(True)`; fallback: legacy
  `unreal.AssetImportTask` + `unreal.FbxImportUI` with `Interchange.FeatureFlags.Import.FBX 0`; levels —
  `unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level(path)` / `save_current_level()`, actors —
  `unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(cls, location, rotation)`.
* Dry run here: `python3 unreal/Tools/py_mock/run_with_mock_unreal.py <script> [--call pkg.mod:fn] [--stub]` (catches
  Python errors in our code; `--stub` lints `unreal.X` names once the owner commits `Tools/py_stub/unreal.py`).

---

## 13. Config ownership
`Config/*.ini` belong to `world_audio`. Settings other streams rely on are already in place: game mode / maps
(`/Script/Fourfold.FourfoldGameMode`, `/Game/Fourfold/Maps/L_Lab`), mobile forward + MSAA, cook `/Game/Fourfold`,
stage `Content/Fourfold/Data` as UFS, Enhanced Input classes, virtual joystick off, Python developer mode on.

---

## 14. iOS performance budgets (target: 60 fps on iPhone 12 / A14 and newer, iPad Air 4+)
| Budget | Value |
|---|---|
| Frame | 16.6 ms; game thread ≤ 7 ms, render thread ≤ 8 ms, GPU ≤ 12 ms at `r.MobileContentScaleFactor=2` |
| Sim | ≤ 0.6 ms per 60 Hz tick (Godot p95 0.3–0.4 ms; C++ should be faster) |
| Animation | ≤ 0.5 ms per fighter (2 fighters + 3 dummies, dummies at reduced rate) |
| VFX CPU | ≤ 2 ms per frame (mesh rebuilds included) |
| Draw calls | ≤ 250 typical, ≤ 400 worst |
| Triangles | ≤ 600 k visible (fighter LOD0 ≤ 30 k) |
| Translucency | ≤ 2 layers per effect; screen-covering translucency only for flashes (≤ 0.1 s) |
| Lights | 1 directional (CSM 2 cascades) + ≤ 4 dynamic point lights, shadowless |
| Memory | textures ≤ 200 MB resident, total app ≤ 1.2 GB |
| Thermal | 10-minute Free Spar without throttling below 50 fps (owner checks on device) |

---

## 15. UE 5.8 notes and pitfalls (verified)
* Target files use `BuildSettingsVersion.Latest` / `EngineIncludeOrderVersion.Latest` (5.8 = V7 / Unreal5_8).
* iOS: minimum iOS / iPadOS 17; recommended Xcode 26.1.1 with the iOS 26 SDK (App Store uploads require Xcode 26 since
  April 2026); modern Xcode is the only workflow (`bUseModernXcode` deprecated in 5.8); the Xcode project takes bundle id and team from
  `XcodeProjectSettings` `BundleIdentifier` / `CodeSigningTeam` (automatic signing); `IOSRuntimeSettings` mirrors them for Turnkey.
* Lumen does not run on iOS / iPadOS; 5.8 adds Lumen Lite and production MegaLights for consoles / PC (not our iOS path).
* FBX import goes through **Interchange** (default since 5.5): `AssetImportTask.options = FbxImportUI` is ignored
  unless `Interchange.FeatureFlags.Import.FBX 0`; prefer the Interchange pipeline calls of §12.
* `FJsonObject` changed its key type in 5.8 (shared strings) → parse JSON with `ff::ParseJson`.
* `UE_LOG` arguments are validated against the format at compile time in 5.8; pass `*FString` for `%s`.
* `PLATFORM_64BITS` is deprecated; `TObjectPtr` for UPROPERTY object members; include the full header (not a forward
  declaration) when an inline function converts a `TObjectPtr<T>` to `T*`.
* Custom HLSL: map the folder in a PostConfigInit module (`AddShaderSourceDirectoryMapping`, guard with
  `AllShaderSourceDirectoryMappings().Contains`); write portable HLSL (no wave ops, no SM6-only features) so it
  compiles for Metal (mobile) and SM6 (Mac).
* Mobile renderer: forward supports MSAA (deferred does not); refraction / distortion is limited on Metal — fake it.
* Haptics: `FPlatformMisc::PrepareMobileHaptics(EMobileHapticsType::ImpactHeavy)` then `TriggerMobileHaptics()`,
  `ReleaseMobileHaptics()` when done (UIFeedbackGenerator; no-op elsewhere).
* Slate drawing signatures changed in 5.x (`FGeometry::ToPaintGeometry(FVector2f size, FSlateLayoutTransform)`):
  verify each draw call against current docs / open-source code.
* Python: `Content/Python` is on `sys.path`; `init_unreal.py` runs at editor start; developer mode writes the API stub.

---

## 16. Verification matrix
| What | Where | How |
|---|---|---|
| Sim rules, kits, AI, Lab logic, golden counter matrix, ledgers, determinism, perf | container | `unreal/CoreTests` (CMake, g++ and clang, poison / shared-lib / unity builds) |
| Facade compiles and links like a UE module | container | shared-lib hidden-visibility build + facade link test |
| Skeleton, FBX conventions, clips (poses, foot plants, loops), character mesh | container | Blender scripts + validators + rendered previews (agents LOOK at them) |
| Shader text | container | HLSL syntax check (DXC / glslang if available) |
| Editor Python | container | mock dry run; stub lint once available |
| Audio | container | spectra / loudness checks; renders listened to by the owner |
| UE C++ compiles, editor Python runs, visuals, feel, device perf, haptics, sound | owner's Mac + iPhone | `MAC_SETUP.md` checklist; logs sent back |

## 17. Open risks
1. Blind UE C++ (game / fx / audio) will need compile-fix iterations on the Mac — mitigated by frozen headers, a small
   verified API surface, and `MAC_SETUP.md` telling the owner exactly what to send back.
2. Editor-Python API drift in 5.8 — mitigated by defensive scripts, the mock, and the owner-provided stub.
3. Core scope (≈ 23 k GDScript lines + 15 k test lines) — mitigated by data export, the priority order (§6.2) and a
   ready second-wave split by element.
4. Character quality from procedural modelling — mitigated by a dedicated stream, review renders, and the option of a
   CC0 base mesh (documented in its manifest) if procedural modelling cannot reach the bar.
5. Device performance unknown until the owner runs it — budgets (§14) + quality levels from day one.

## Sources
* UE 5.8 release / Lumen Lite / MegaLights / iOS Lumen: https://www.unrealengine.com/news/unreal-engine-5-8-is-now-available ·
  https://www.guru3d.com/story/unreal-engine-58-debuts-lumen-lite-and-productionready-megalights/ ·
  https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes
* iOS requirements (5.8: iOS 17+, Xcode 26.1.1 / iOS 26 SDK): https://dev.epicgames.com/documentation/en-us/unreal-engine/ios-ipados-and-tvos-development-requirements-for-unreal-engine
* 5.8 migration (BuildSettingsVersion V7 / Unreal5_8, FJsonObject keys, UE_LOG validation, PLATFORM_64BITS): https://jakubpradeniak.com/posts/dev-notes/migrating-unreal-engine-5-8-cpp-project/ · https://www.spongehammer.com/unreal-engine-5-8-upgrade/
* Mobile rendering modes, MSAA vs deferred, mobile deferred features: https://dev.epicgames.com/documentation/en-us/unreal-engine/mobile-rendering-and-shading-modes-for-unreal-engine · https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-mobile-deferred-shading-mode-in-unreal-engine
* Modern Xcode workflow / signing: https://dev.epicgames.com/documentation/en-us/unreal-engine/using-modern-xcode-in-unreal-engine · https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-ios-projects-in-unreal-engine
* Niagara lightweight emitters (5.4+): https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-lightweight-emitters-overview
* Interchange Python import (pipelines, override_pipelines): https://dev.epicgames.com/documentation/en-us/unreal-engine/importing-assets-using-interchange-in-unreal-engine ·
  open-source usage: github.com/Twoos123/Clockworks (Tools/SKImport/import_armor.py), github.com/xavier150/Blender-For-UnrealEngine-Addons
* Interchange FBX default in 5.5 and `Interchange.FeatureFlags.Import.FBX`: https://forums.unrealengine.com/t/possible-5-5-fbx-import-bug-with-asset-interchange/2326622 · https://impromptu.zone/t/disable-the-new-fbx-importer-in-ue-5-5/154
* MaterialEditingLibrary / Custom node `include_file_paths` from Python: https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialEditingLibrary · open-source usage: github.com/carla-simulator/carla (Util/ContentRepair/build_rain_lens.py)
* Shader directory mapping: https://docs.unrealengine.com/4.26/API/Runtime/RenderCore/AddShaderSourceDirectoryMapping/index.html · github.com/carla-simulator/carla (Carla.cpp, AllShaderSourceDirectoryMappings guard)
* Native anim-instance proxy evaluation (GetAnimationPose / FAnimExtractContext): open-source usage, e.g. github.com/alexrios/ArenaGame (ArenaRigAnimInstance.cpp)
* Level / actor Python APIs: https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/LevelEditorSubsystem
* Mobile haptics API: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FGenericPlatformMisc/TriggerMobileHaptics
* Python stub / developer mode: https://dev.epicgames.com/documentation/unreal-engine/setting-up-autocomplete-for-unreal-editor-python-scripting
* IK Retargeter auto-retargeting: https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-retargeting-in-unreal-engine
* Blender → Unreal FBX conventions: https://docs.blender.org/manual/en/4.1/addons/import_export/scene_fbx.html · https://app.cinevva.com/guides/blender-to-unreal-export-checklist
* Game feel (hit-stop, anticipation): MOVESET.md sources S9–S12; Mariel Cartwright GDC: https://gdcvault.com/play/1021657/Powerful-and-Effective-Animation-for
