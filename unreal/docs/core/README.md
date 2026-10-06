# FourfoldCore (stream `core`)

The engine-free C++20 port of the Fourfold simulation: the 60 Hz combat sim, moves / verbs / the four element kits, the
counter matrix, AI, scenarios, progression and the Lab, behind the frozen facade `ff::Session`
(`Source/FourfoldCore/Public/ff/*.h`). The same sources build as the Unreal runtime module `FourfoldCore` and, here,
as a CMake library that runs the ported Godot tests. Status and numbers: `PORT_STATUS.md`. UE API use: `API_NOTES.md`.

## Build and test here

One command (both compilers, every target, all checks):

```
unreal/CoreTests/run_all.sh            # FF_COMPILERS="g++ clang++"  FF_JOBS=4  FF_BUILD_ROOT=/tmp/fourfold-core
```

By hand:

```
cmake -S unreal/CoreTests -B <build> -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build <build> --target ff_all          # or --target ff_tests while iterating (full build ~10 min on 4 cores)
<build>/ff_tests -q                            # all suites, summary per suite; ff_tests <substring> runs a subset
<build>/ff_facade_link_test                    # facade used through Public/ff only, against the hidden-visibility .so
<build>/ff_perf all 120                        # perf_core / perf_air / Session duel timings + determinism hash
```

| Target | What it proves |
|---|---|
| `ff_core` | static library: `Private/**` except `Private/UE` |
| `ff_tests` | 531 tests: 521 ported Godot sim tests (+1 documented skip), the 13,300-row golden counter matrix, data and facade tests |
| `ff_core_shared` + `ff_facade_link_test` | `-fvisibility=hidden` build where only `FOURFOLDCORE_API` symbols are exported (the Mac editor's modular build) |
| `ff_poison` | every source compiled with `-include ue_macro_poison.h` (no identifier collides with an Unreal macro) |
| `ff_unity` | every source in unity batches of 16 (no file-local name clashes) |
| `ff_perf` | timings and same-seed determinism |

Library flags: `-std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Wundef
-Werror` (+ `-Wshorten-64-to-32` on clang). Test code keeps the language rules but not `-Wconversion`.

## Data

- `Source/FourfoldCore/Data/*.json` is exported from the Godot registries by `Tools/godot_export/export_core_data.gd`:
  `/home/user/tools/godot --headless --path game -s "$PWD/unreal/Tools/godot_export/export_core_data.gd" -- --out="$PWD/unreal/Source/FourfoldCore/Data"`.
- `CoreTests/tools/embed_data.py` embeds every file except the test-only `golden_matrix.json` into
  `Private/Generated/EmbeddedDataGen.cpp` (committed; the Mac build needs no Python and reads no files at runtime).
  Re-run it after an export; `--check` fails when the generated file is stale (`run_all.sh` does this).
- Formats added by this stream (JSON text the host stores wherever it likes):
  - Progression (`Session::SaveProgress` / `LoadProgress`):
    `{"schema": 1, "unlocked": ["magma", ...], "done": ["m_stone_reader", ...], "lab_mode": false, "last_scenario": "lab", "spar_difficulty": "adept", "spar_kit": "mixed"}`
  - Lab tuning (`Session::LabSaveTuning` / `LabLoadTuning`):
    `{"schema": 1, "moves": {"<move id>/<path>": value, ...}, "rules": {"<threat|counter>#<idx>/<field>": value, ...}}`
    (`<path>` is a flat key or `tiers.t1.<key>`; rule fields are `eff`, `full_at`, `partial_at`, `perfect_mult`, `absorb_on_fail`, `w.K`..`w.P`).

## Module map (C++ -> Godot source)

| Folder | Contents | Godot |
|---|---|---|
| `Public/ff` | facade (FROZEN; additions marked "additions (core stream, additive)") | - |
| `Private/Util` | Json, Rng (Godot PCG32 `RandomNumberGenerator`), GodotMath, GdUtil (Dict / Array helpers) | engine built-ins |
| `Private/Data` | GameData: loads the embedded JSON into the registries | `export_core_data.gd` |
| `Private/Sim` | Sim constants, Materials, MatBody, ActorState (+ intent, action), ArenaMap, Thermal, Charge, Status, FxEvents, Conduction, Agent, Interactions, Outcomes, Hooks (name -> function tables), CombatWorld split into `CombatWorld{,Actions,Bodies,Hits,Move,Zones}.cpp` | `game/core/*.gd` |
| `Private/Combat` | Moves registry, Verbs (`Verbs/Verb*.cpp`), legacy acts (`Acts/Act*.cpp`), Kits (`Kits/<Element>/`: one file per sub-element + `*Rules` outcomes + `*Registry` hook / handler registration; `KitDispatch` routes per-move lifecycle stages) | `game/combat/**` |
| `Private/AI` | AiBrain (`AiBrain.cpp` legacy rules + think, `AiBrainPlanner.cpp` planner mode), AiPlanner, AiPresets | `game/actors/ai_*.gd` |
| `Private/App` | Session (facade), SessionCore (state + tick: scenario load, KO / round reset, dummies, launcher, vents, challenges, Lab actions, autoplay duel / soak), PlayerController, HudBuilder, SnapshotBuilder, Scenarios, Progression | `game/game.gd` logic, `game/actors/{player_controller,autoplay}.gd`, `game/scenarios/*.gd` |
| `Private/Lab` | LabSession, LabScript, SpawnCatalog, LabCombos + ComboTracker, MatrixQuery, LabTuning, MoveListData | `game/ui/lab/*.gd` |
| `Private/UE` | `FourfoldCoreModule.cpp` (the only Unreal file; excluded from CMake) | - |
| `CoreTests/src` | test framework (`ff_test.h`), SimHarness port, per-kit helpers, the shared verb test kit | `game/tests/sim/sim_harness.gd`, `test_kit_*_util.gd` |
| `CoreTests/tests` | one file per Godot suite, same test names | `game/tests/sim/test_*.gd` |

Names follow the GDScript in snake_case (`split_body`, `heat_body`, `Interactions::predict`) so a reviewer can diff the
two. `ff::Value` / `Array` / `Dict` keep GDScript reference semantics (copy = shared handle, `duplicate()` copies).

## Integration notes for the other streams

- Call `Session::Step(input, camera_yaw)` exactly once per 60 Hz tick (use `TimeScale()` / `Frozen()` /
  `TakeStepRequests()` for the Lab's slow motion, freeze and frame step), then read `GetSnapshot()`, `TakeEvents()`
  and `BuildHud()`. Event `type` and `data` keys are the Godot names; app events are `app_scenario_loaded`,
  `app_round_reset`, `app_ko`, `app_toast`, `app_challenge`, `app_combo`, `app_lab`.
- `ScenarioOptions.autoplay = "duel"` lets the AI drive the player too (attract mode); `"soak"` drives random input.
  `LoadScenario("__lab_mode")` toggles Lab mode and reloads the current scenario.
- Bodies resting against an arena solid emit an `impact` event every tick (Godot does the same): rate-limit impact
  cues per body in FX / audio.
- Everything is single-threaded and deterministic for a seed (`SessionConfig.seed`).
- `docs/game/REQUESTS.md` items for core are done: `LoadScenario("__lab_mode")` toggles Lab mode and
  `SetLabMode` / `LabMode` exist; spar difficulty / kit from `ScenarioOptions` or `SetSparOptions` persist in
  `SaveProgress()`; `LabRuleCells` / `LabRuleFields` / `LabSetRuleValue` tune the counter-rule cells.
- Core needs nothing from other streams (no `REQUESTS.md`).

## On the Mac

Nothing special: `FourfoldCore.Build.cs` builds the module with `PCHUsage = NoPCHs`, `bUseUnity = false`, no
exceptions, no RTTI. If a UE toolchain warning appears that g++ / clang here did not flag, the fix belongs in the
named file; `run_all.sh` also runs on macOS (Apple clang) for a quick check outside Unreal.

## Known gaps

- Presentation tests are not ported (views, FX director, camera, animation clip tables, adaptive quality); see
  `PORT_STATUS.md`.
- The kit reference-cell metadata of the Godot `*_cells` suites is not exported; the golden matrix covers those cells.
- Deviations from Godot (all behaviour-neutral in the tests) are listed in `PORT_STATUS.md`.
