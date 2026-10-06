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
  AI SUITES (run 4): CoreTests/src/ai_util.h (AiRig + ff::AiTestAccess, a friend added to AiBrain.h), test_ai_planner 7/7 GREEN.
  + test_ai_{duel 3, offense 5, drills 5, flagship 2}, test_regressions_ai 12: ALL AI SUITES GREEN.
  API NOTE for docs: GDScript think() returns the brain's shared ActorIntent object; C++ think() returns const& and
  harness/Session copy it -> after calling brain internals between think and step, re-copy ai->intent.
  CORE SUITES (run 4): test_flagship 8, test_integration_matrix 2, test_core_examples 8, test_review_fixes 12,
  test_contest 11, test_waves 9, test_core_verbs 3, test_thermal 20, test_lightning 22, test_regressions_sim 18, test_water_ice 21, test_energy_and_soak 15, test_combat_rules 31, test_scenarios 6, test_regressions_game 8 GREEN (7 ported + 1 documented skip: quality/PerfMonitor = presentation). REFACTOR: Session::Impl body moved to Private/App/SessionCore.h (struct SessionCore; Impl derives) so tests drive scenario_tick/ko_tick/challenges like GameProbe. FIX: Session soak_frame rng compares now double (GDScript float) - 0.06f etc rounded down.(soak 36000 ticks ~0.2 s; OBSERVED: ~131k "impact" events per soak = bodies resting against arena walls re-emit impact every tick - same code path as Godot, note in PORT_STATUS).
  ALL SIM SUITES PORTED (run 4): full ff_tests = 531 tests, 0 failed, 2.1 s. Per-suite counts == Godot except:
  test_kit_earth_moves 5/6 (test_anim_clips_exist = Godot anim library, presentation), test_regressions_game 8/10
  (3 quality/PerfMonitor cases folded into 1 documented-skip test), test_regressions_views 0/21 (presentation:
  FxDirector/BodyViews/FighterView/camera - not FourfoldCore). Extra C++ suites: test_data 3, test_facade 5, test_golden_matrix 1.
  PERF DONE: CoreTests/perf/perf_main.cpp (ff_perf [all|core|air|session|determinism] [secs]); test kit moved to
  CoreTests/src/test_kit.{h,cpp} (shared by test_core_verbs + ff_perf). g++ -O2, 120 s: core mean 0.003 ms p99 0.029 max 0.13;
  air mean 0.004 p99 0.034 max 0.11 OK (exact ledgers); Session duel mean 0.019 p99 0.28 max 1.4 ms; determinism identical.
  DOCS: PORT_STATUS.md WRITTEN (run 4). test_data hook test now strict (fails on unresolved). Cells tests' comments fixed
  (no extract_cell_refs.py). Background: full g++ build -> scratch gcc_full.log (GCC_DONE), then clang -> clang_build.log.
  DOCS DONE: README.md, PORT_STATUS.md, API_NOTES.md (no REQUESTS.md needed; game REQUESTS for core all done).
  CoreTests/run_all.sh added (one command: embed --check, both compilers, ff_all, tests, link test, perf 10 s).
  g++ FULL BUILD GREEN (GCC_DONE 0) + ff_tests 531/531 + link test ok (run 4).
  CLANG FULL BUILD GREEN (CLANG_DONE 0): ff_tests 531/531, link test ok, ff_perf OK (run 4). run_all.sh validated (g++).
  STREAM COMPLETE. Only the final report remains (if resumed: just report).

## Decisions
- Test framework uses `check()` names (tests are never compiled by UE).
- Generated file name: Private/Generated/EmbeddedDataGen.cpp (not .gen.cpp).
