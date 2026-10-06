# FourfoldCore - port status

Stream `core`, 2026-10-06. Source of truth: the Godot game in `game/` (GDScript). Everything below was built and run
here with g++ 13 and clang 18 (CMake + Ninja, `-std=c++20 -fno-exceptions -fno-rtti`, warnings as errors).

## Summary

| Item | State |
|---|---|
| Sim core (`game/core/*.gd`, 2,673-line `combat_world.gd` split over `CombatWorld*.cpp`) | ported |
| Moves registry, verbs, legacy acts (`game/combat/**`) | ported |
| Kits: Earth, Water, Fire, Air (15 sub-element kits) | ported; **212 / 212** code-hook names resolve (`test_data.test_hook_names_listed` fails on any unresolved name) |
| Golden matrix (`Data/golden_matrix.json`, 13,300 Godot `MatrixQuery.predict` rows) | **13,300 / 13,300** match (band, outcome, rule_id, to, classes exact; tp / cp / cp_eff / ratio within 1e-3 rel.) |
| AI (`ai_brain.gd`, `ai_planner.gd`, `ai_presets.gd`) | ported; every Godot AI suite green |
| Session facade (scenarios, KO / round reset, dummies, launcher, vents, challenges, HUD, snapshot, Lab, autoplay duel / soak) | ported; facade tests + `test_regressions_game` green |
| Lab logic (LabSession, LabScript, SpawnCatalog, LabCombos + ComboTracker, MatrixQuery, LabTuning, MoveListData), Progression, Scenarios | ported |
| Godot sim tests | **521 of 546** Godot tests ported; the other 25 are presentation-only (21 `test_regressions_views`, 1 anim-clip check, 3 quality / PerfMonitor). `ff_tests` runs **531 tests, 0 failed** (521 ported + 1 documented skip + 9 C++-only) in about 2 s |
| Perf | see "Perf" below: p95 0.011 ms per tick (Godot p95 0.32 ms) |

## Per-suite results (`ff_tests -q`, g++ and clang)

Godot = `func test_` count in `game/tests/sim/<suite>.gd`; C++ = tests in `CoreTests/tests/<suite>.cpp`. All C++ tests pass.

| Suite | Godot | C++ | Notes |
|---|---|---|---|
| test_ai_drills | 5 | 5 | |
| test_ai_duel | 3 | 3 | |
| test_ai_flagship | 2 | 2 | |
| test_ai_offense | 5 | 5 | |
| test_ai_planner | 7 | 7 | |
| test_combat_rules | 31 | 31 | |
| test_contest | 11 | 11 | |
| test_core_charge | 5 | 5 | |
| test_core_examples | 8 | 8 | |
| test_core_input | 11 | 11 | |
| test_core_interactions | 8 | 8 | |
| test_core_ledgers | 2 | 2 | |
| test_core_registry | 7 | 7 | |
| test_core_verbs | 3 | 3 | test kit shared with ff_perf (`CoreTests/src/test_kit.*`) |
| test_core_world | 8 | 8 | |
| test_energy_and_soak | 15 | 15 | 36,000-tick soak about 0.2 s |
| test_flagship | 8 | 8 | |
| test_integration_matrix | 2 | 2 | |
| test_kit_air_cells | 5 | 5 | reference-cell test notes a skip (see below) |
| test_kit_air_gust | 23 | 23 | |
| test_kit_air_sound | 16 | 16 | |
| test_kit_air_vacuum | 13 | 13 | |
| test_kit_air_vortex | 19 | 19 | |
| test_kit_earth_ledgers | 2 | 2 | |
| test_kit_earth_magma | 11 | 11 | |
| test_kit_earth_metal | 15 | 15 | |
| test_kit_earth_moves | 6 | 5 | `test_anim_clips_exist` not ported (Godot animation library) |
| test_kit_earth_sand | 12 | 12 | |
| test_kit_earth_stone | 14 | 14 | |
| test_kit_fire_blue | 7 | 7 | |
| test_kit_fire_cells | 5 | 5 | reference-cell test notes a skip |
| test_kit_fire_combustion | 8 | 8 | |
| test_kit_fire_flame | 10 | 10 | |
| test_kit_fire_ledger | 2 | 2 | |
| test_kit_fire_lightning | 8 | 8 | |
| test_kit_fire_moves | 5 | 5 | clip-existence lines inside tests not ported |
| test_kit_water_cells | 6 | 6 | reference-cell test notes a skip |
| test_kit_water_ice | 20 | 20 | |
| test_kit_water_ledger | 3 | 3 | |
| test_kit_water_mist | 12 | 12 | |
| test_kit_water_moves | 3 | 3 | clip-existence lines inside tests not ported |
| test_kit_water_plant | 12 | 12 | |
| test_kit_water_water | 17 | 17 | |
| test_lightning | 22 | 22 | |
| test_regressions_ai | 12 | 12 | |
| test_regressions_game | 10 | 8 | 7 ported; the 3 quality / PerfMonitor cases are one documented-skip test |
| test_regressions_sim | 18 | 18 | |
| test_regressions_views | 21 | 0 | presentation (FxDirector, BodyViews, FighterView, camera): not FourfoldCore |
| test_review_fixes | 12 | 12 | |
| test_scenarios | 6 | 6 | progression persistence via `to_json` / `from_json` instead of a `user://` file |
| test_thermal | 20 | 20 | |
| test_water_ice | 21 | 21 | |
| test_waves | 9 | 9 | |
| test_data (C++ only) | - | 3 | embedded data parses, registry counts, every hook name resolves |
| test_golden_matrix (C++ only) | - | 1 | 13,300 rows |
| test_facade (C++ only) | - | 5 | Session through `Public/ff` only: Lab inputs, Try / combos / matrix / tuning, KO + progression, spar / duel / soak |

## Not ported, on purpose

- **Presentation tests**: `test_regressions_views.gd` (21), `test_anim_clips_exist` and the clip-existence lines of the
  kit `*_moves` / Air suites (the clip table belongs to the animation stream), and the adaptive-quality / PerfMonitor
  cases of `test_regressions_game.gd` (game stream).
- **Kit reference-cell metadata**: the Godot `*_cells` suites read per-cell `move / tier / expect` comments that are not
  in the exported data. `kit_cell_refs.json` was not generated: the golden matrix already pins every kit move's counter
  result at every tier (25 threats x 133 moves x 4 tiers), so the reference tests note a skip. The other cell tests
  (column shape, registration order, outcome handlers) run.

## Deviations from Godot (behaviour is identical unless stated)

- `AiBrain::think` returns `const ActorIntent&` and callers copy it; GDScript returns the brain's shared intent object.
  Code that pokes brain internals between `think` and `step` must re-copy `ai->intent` (tests do).
- GDScript `hash()` is not reproduced: determinism checks use an FNV-1a digest of the same state fields (compared only
  against itself).
- Godot `sort_custom` is unstable; C++ uses `std::stable_sort` and keeps every GDScript tie-break. Results differ only
  for exact ties the GDScript comparator leaves open (none observed in any test or the golden matrix).
- Random draws: GDScript compares `randf()` (float32 widened to double) against double literals; C++ does the same
  (`static_cast<double>(rng.randf()) < 0.06`). Fixed in `Session` soak autoplay during this run.
- `Moves.ensure()` is `Moves::ensure_ready()` (`ensure` is an Unreal macro).
- Int-keyed GDScript dictionaries (e.g. the spar AI config `subs = {2: [1]}`) use string keys (`"2"`) in `ff::Dict`.
- Progression and Lab tuning persistence are JSON text handed to the host (`Session::SaveProgress`,
  `Session::LabSaveTuning`); formats in README.md.
- `Session::Impl` derives from `SessionCore` (`Private/App/SessionCore.h`) so tests drive `scenario_tick` / `ko_tick` /
  `challenges` directly, as Godot's `GameProbe` drives `Game`.

## Observations worth knowing

- A body resting against an arena solid re-emits an `impact` event every tick (same code path in Godot:
  `combat_world.gd` `_body_impact(b, "wall")` for inert bodies). The 10-minute soak sees about 131,000 of them;
  consumers should rate-limit impact effects per body.

## Perf (`ff_perf all 120`, g++ -O2, one core of the build container)

| Run | mean | p95 | p99 | max |
|---|---|---|---|---|
| core: 2 fighters, random input, legacy kits + every verb (perf_core.gd) | 0.003 ms | 0.011 ms | 0.029 ms | 0.13 ms |
| air: Air fighter vs all elements, ledgers checked every second (perf_air.gd) | 0.004 ms | 0.013 ms | 0.034 ms | 0.11 ms |
| Session::Step, spar autoplay duel (two master AIs + snapshot) | 0.019 ms | 0.049 ms | 0.28 ms | 1.4 ms |

clang 18 gives the same numbers within 10 %. Godot reference (perf_core.gd): mean 0.20 ms, p95 0.32 ms. Target was p95 <= 0.15 ms. Determinism: the core soak
run twice with seed 42 gives the same state hash; seed 43 differs. The air soak keeps energy and all four mass ledgers
exact (drift 0.000000).
