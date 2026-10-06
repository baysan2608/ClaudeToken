# core stream - PROGRESS (checkpoint file; read first, update after every sub-step)

Scratch build dirs: /tmp/claude-0/-home-user-ClaudeToken/37fdfe41-9b78-53ea-8620-b33d5491ff57/scratchpad/ue/core/build-{gcc,clang}
Build: `cmake -S unreal/CoreTests -B $B -G Ninja -DCMAKE_CXX_COMPILER=g++ && cmake --build $B && $B/ff_tests [filter]`

## Inventory (run 1 left this; run 2 verified it compiles with g++/clang strict flags)
Ported (draft, untested): Util/{Json,Rng,GodotMath,GdUtil}, Data/{GameData,EmbeddedData + Generated/EmbeddedDataGen.cpp},
Sim/{Sim.h,Materials,MatBody,ActorState(+Intent+ActionInst),ArenaMap,Thermal,Charge,Status,FxEvents,Conduction,Agent,
Interactions,Outcomes,Hooks, CombatWorld*.cpp (Actions/Bodies/Hits/Move/Zones)}, Combat/{Moves, Verbs/*, Acts/ActCommon, Acts/ActEarth},
Combat/Kits/Water/WaterUtil.
Missing at link time (run 2, 15:30): ActWater, ActFire, ActAir, KitEarth/KitWater/KitFire/KitAir dispatchers,
Register{Core,Earth,Water,Fire,Air}Hooks. No CoreRules.cpp yet (check core_rules.gd: likely data-only now).
NOT started: Private/App (Session), Private/AI, Lab, Scenarios, Progression, PlayerController, kits (except WaterUtil).

## Done (run 2)
- CoreTests/src/ff_test.h + ff_test_main.cpp (FF_TEST / FF_TEST_F, check/near/note, filter, per-suite summary)
- CoreTests/src/sim_harness.{h,cpp} (port of sim_harness.gd)
- CoreTests/link/facade_link_test.cpp (full facade use), CoreTests/perf/perf_main.cpp (placeholder)
- CMake: g++ -Wno-maybe-uninitialized -Wno-dangling-reference (GCC13 false positives); fixed Json.cpp Pow10 bounds warning,
  Conduction unused helper, Verbs.cpp dangling ref.

- Ported ActWater, ActFire, ActAir (legacy acts complete), Kits/Fire/FireUtil, Kits/Air/AirUtil, Kits/Air/AirGust (grip_target,
  grip_tick, tech_preview only), Kits/KitDispatch (generic handler maps for Water/Fire/Air + KitKey/KitPart),
  Kits/Earth/KitEarth (part dispatch + KitEarthUtil helpers), Kits/<El>/<El>Registry.cpp (EMPTY registration stubs:
  RegisterEarthParts/EarthToSatchel/Register*Handlers/Register*Hooks - fill as kits get ported), Verbs/VerbHooks.cpp.
- ff_core_shared links with --no-undefined; ff_tests runs: tests/test_data.cpp 3/3 green (counts 162/160/1237/1264 ok).

- test_core_world ported: 8/8 green first try (run-1 sim port is faithful).
- App/: PlayerController (+frame_from_intent for duel), Progression (JSON format in header), Scenarios (from scenarios.json).
- Lab/: LabScript, LabSession, SpawnCatalog (entries from lab.json + live param_specs), LabCombos + ComboTracker,
  MoveListData (+MoveInfo), MatrixQuery. AI/AiBrain = TEMPORARY STUB (idle intents) until step 6.
- tests/test_golden_matrix.cpp: 12441/13300 match; ALL mismatches = unported kit hooks: WaterWater.tidal_scale
  (tidal_rush counter, 107 rows), WaterMist.fog_execute (mist threat, 380), WaterPlant.burr_execute (vines, 380).

- Lab/LabTuning; App/HudBuilder, App/SnapshotBuilder, App/Session.cpp (full facade incl. autoplay duel/soak, KO reset,
  launcher, vents, challenges, app_* events, Lab spawn/try/combos/matrix/tuning). Session.h/ViewModels.h ADDITIONS:
  SetLabMode/LabMode, LabRuleCells/LabRuleFields/LabSetRuleValue (+RuleCellInfo), MoveInputText/MoveCostText/
  MoveFramesText, SetSparOptions. LoadScenario("__lab_mode") toggles lab mode.
- Renamed every `ensure()` -> `ensure_ready()` (UE macro; poison build caught it).
- FULL g++ build green (ff_core, ff_tests, ff_core_shared+ff_facade_link_test (passes), ff_poison, ff_unity, ff_perf stub).
  NOTE: full build ~10 min on 4 cores; iterate with `--target ff_tests`. Don't wait with `pgrep -f "cmake --build"` (matches itself).

- tests/test_facade.cpp 5/5 green (lab load/HUD, tap/T3/flick/perfect guard/technique via input path, Try/combo/matrix/
  tuning/freeze, KO reset + progression + lab mode, spar/duel/soak 3600 ticks). Golden test prints only 5 rows now.

- AI ported: AI/AiPresets, AI/AiPlanner (typed AiThreat/AiOption/AiParams/AiObserve/AiKit), AI/AiBrain.cpp (think+legacy),
  AI/AiBrainPlanner.cpp (configure/perception/plans/offense/chain/interrupt/drills). Builds, all tests still green.
- clang full build (all targets) was green BEFORE the AI port (CLANG_DONE 0).

- Ported suites green: test_core_world 8/8, test_core_charge 5/5, test_core_ledgers 2/2, test_core_input 11/11,
  test_core_registry 7/7, test_core_interactions 8/8 (harness: filter(), ev_i/ev_f/ev_s/ev_b, HarnessCase fixture with H(seed)).
- EARTH KIT DONE (run 3): Kits/Earth/{Earth.h, EarthStone, EarthMetal, EarthSand, EarthMagma, EarthRules, EarthRegistry}.cpp;
  suites test_kit_earth_{moves 5, stone 14, metal 15, sand 12, magma 11, ledgers 2} ALL GREEN first run (helpers in
  CoreTests/src/kit_earth_util.*; use EU::keep(body) -> BodyRef when a test reads a body that may be removed).
  Perf note: 60 s two-fighter Earth exchange runs in ~20 ms.
- DECISION: core port is faithful (every ported suite passed first time) -> switch to KITS now (zero ported), port each
  element's kit suites with it; remaining core suites later (verbs, flagship, thermal, contest, waves, lightning,
  combat_rules, energy_and_soak, review_fixes, regressions_sim, water_ice, core_examples, integration_matrix).

## Current item
- KITS: Water. WRITTEN (run 3): Kits/Water/{Water.h, WaterWater.cpp (+WaterJet), WaterIce, WaterMist, WaterPlant, WaterRules,
  WaterRegistry}.cpp (15 move-id handlers + all 49 Water hook names). BUILT: ALL 154 tests green, GOLDEN MATRIX
  13300/13300, fire ledger soak green, 46/212 hook names left (Air). Tests (run 4): WRITTEN CoreTests/src/kit_water_util.{h,cpp} (WaterCase), tests/test_kit_water_{moves 3, water 17, ice 20}.cpp ALL GREEN;
  + test_kit_water_{mist 12,plant 12,cells 6,ledger 3}.cpp: ALL 73 WATER TESTS GREEN (first run).
  DECISION: no extract_cell_refs.py - the golden matrix (25 threats x 133 moves x 4 tiers = 13300 Godot predictions)
  already pins every kit move's counter at every tier; the ref-cell tests note a skip (document in PORT_STATUS).
  NEXT: Air kit. Air plan: Kits/Air/Air.h (AirOutcomes/AirVortex/AirVacuum/AirSound decls) WRITTEN; AirGust.{h,cpp}
  COMPLETE (crescent_execute/tick, crosswind, downdraft, grip, preview). WRITTEN (run 4): AirOutcomes.cpp, AirVortex.cpp,
  AirVacuum.cpp, AirSound.cpp, AirRegistry.cpp -> BUILT: 227 tests green, golden 13300/13300, 0/212 hooks unported.
  Air tests: WRITTEN CoreTests/src/kit_air_util.{h,cpp} (AirCase), tests/test_kit_air_{gust 23, vortex 19, vacuum 13, sound 16, cells 5}.cpp GREEN. ALL FOUR KITS + KIT TESTS DONE.
  NEXT: remaining core suites (see list), then AI suites, lab/scenario suites, perf, docs. Registry covers (handlers: vortex_eye after/tick, vortex_whirl after/phase/interrupt,
  vacuum_well phase/interrupt, vacuum_hop phase, vacuum_slipstream tick, sound_thunder_step+sound_boom_step phase=boom_phase,
  sound_flight start/after, sound_hover after; hooks: all 46 Air names), then port test_kit_air_* suites.
  Then Air kit (AirGust already partially in Kits/Air/AirGust.cpp; AirRegistry empty).

## Next steps
1. Lab/LabTuning (port lab_tuning.gd; JSON {schema, moves{"id/path":v}, rules{"key#idx/field":v}}).
2. App/Session.cpp (Impl: load/step/KO/launcher/vents/challenges/autoplay duel+soak/lab/script/events) + App/HudBuilder +
   App/SnapshotBuilder; add Session::SetLabMode/LabMode + LabRuleCells/LabRuleFields/LabSetRuleValue (game REQUESTS).
3. facade tests + facade_link_test; then port remaining core suites; then AI (step 6); then kits.

## Decisions
- Test framework uses `check()` names (tests are never compiled by UE).
- Generated file name: Private/Generated/EmbeddedDataGen.cpp (not .gen.cpp).
