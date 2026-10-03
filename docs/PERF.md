# Performance

## What was measured here (Linux container, no GPU, no iOS device)

| Run | Scene / settings | Result |
|---|---|---|
| Headless 10-min soak (`--headless --fixed-fps 60 -- --autoplay=soak:600`) | Free Spar, random inputs across all elements, all techniques, AI opponent | Sim step p95 **0.13–0.20 ms**/tick (60 Hz budget 16.7 ms). Static memory 48.8 MB → 53.4 MB after 10 min, nodes 217 → ~410 (pools filling to their caps, then flat). 0 script errors after fixes. |
| Sim invariants under soak (tests) | `test_energy_and_soak`, `test_scenarios` | Body count ≤ 32, mass/energy ledgers balance, deterministic. |

Frame-time numbers from `--render` runs here are **not meaningful**: rendering is done on the CPU by Mesa lavapipe.
They only prove the Mobile renderer path compiles and draws.

## Not verified (needs hardware)
GPU frame time, thermal throttling, touch latency, haptics, iPad workload, Metal-specific shader issues.

## Minimum target device (proposal)
iPhone 12 / A14 (and iPad 9th gen / A13 as the iPad floor). Rationale: Godot 4.7 Mobile renderer over Metal,
current budgets (≤32 bodies, ≤2 transparent layers per effect, one shadowed directional light, 0.85 render scale fallback).
Target sustained 60 fps; adaptive quality steps down (render scale 1.0 → 0.85 → 0.7, shadows/glow/MSAA off)
when frame p95 > 18.5 ms for 4 s. After a step the p95 window restarts, so the next decision uses only frames
rendered at the new tier (at least 240 of them). Input handling and attack telegraphs are never degraded.
ProMotion devices are capped at 60 Hz (`display/window/ios/allow_high_refresh_rate=false`): the sim runs at 60 Hz,
so 120 Hz rendering would double the per-second cost for interpolation only.

## On-device checklist (do this on the Mac)
1. `tools/scripts/export_ios.sh <TEAM_ID>` → open `build/ios/Fourfold.xcodeproj`, scheme Release, run on device.
2. Settings ▸ show debug: the overlay shows fps / avg / p95 / p99 frame time and sim p95.
3. Play **Molten Exchange** 10–15 min continuously (or autoplay: Edit Scheme ▸ Run ▸ Arguments Passed On Launch, add
   `--`, `--autoplay=flagship:900` and `--perf=user://perf.json` in that order; the game reads only arguments after `--`).
   Note quality step-downs printed as `[quality]` in the Xcode console. `user://` is not visible in the Files app
   (`user_data/accessible_from_files_app=false`): fetch `perf.json` with Xcode ▸ Devices and Simulators ▸ the app ▸
   Download Container… (it is under `AppData/Documents/`).
4. Xcode ▸ Debug Navigator: watch CPU, GPU, memory trend (should flatten after ~1 min) and Thermal State.
   The per-second perf log is kept only for `--perf` runs, so it adds no growth to a normal session.
5. Instruments ▸ Game Performance (or Metal System Trace) for 2 minutes of combat: report average, p95, p99 frame time,
   longest hitches, GPU vs CPU bound.
6. Repeat on iPad. Check layout (safe areas, control size), touch: move + guard + camera simultaneously, technique drag + cancel.
7. Haptics: block vs deflect vs perfect vs lost control must feel distinct and never continuous.
8. Background/foreground mid-technique: game pauses, nothing stays held.
