# Fix pass PROGRESS (review round: ue-compile + gameplay)

## Done
1. FourfoldPageBuilder.h: added `#include <string>` (std::string helpers no longer rely on unity-build include order). Verified: standalone TU with real UHT 5.8 headers + EngineSharedPCH (review harness run_real.sh): 5 errors -> 0.
2. iOS signing: XcodeProjectSettings CodeSigningTeam/BundleIdentifier are the EDIT-ME keys; IOSRuntimeSettings mirrors them; bUseModernXcode removed; MAC_SETUP 6.2, ARCHITECTURE, world/README updated.
3. Chained actions animated: anim_table.py + MARTIAL_ARTS §4.17 add lightning (l_release, T3 l_skybreak), pour (f_pour), vent (f_palm_burst), gust_grip (a_pluck/a_guard/a_palm), flare_dash (evade_*); air_tech wind-grip hold w_hold -> a_guard. Regenerated anim_map.json, COVERAGE/CLIPS.md, FFGAnimDefaults.gen.cpp. test_anim_data.py checks chained ids + element; new logic test chained_actions_resolve_to_own_element_clips scans FourfoldCore for start_action/morph_action literals (fails 24x on old data, passes now; 57 tests g++/clang).
4. CombatWorldBodies.cpp wave step: `(b.wave_dir * float(speed)) * float(dt)` like GDScript. Per-tick Godot-vs-C++ move probe (review_gameplay diff_moves, 1280 cases) now 1280/1280 (was 1271); output byte-identical to the reviewer's patched run. run_all.sh (embed check + g++ + clang++): 531/531 each, link test + perf OK.
5. HANDOFF.md written; MAC_SETUP fast path + signing row.

## Next
- all 4 issues fixed and verified; HANDOFF.md / MAC_SETUP.md updated. Nothing pending in this pass.
