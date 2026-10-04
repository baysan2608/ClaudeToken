# Code review log

An independent multi-agent review (5 reviewers: simulation rules, opponent AI, game integration, input contract, iOS readiness)
reported 49 problems; a separate skeptical verifier per area confirmed **48** (1 rejected). Each fix group then had its own
independent verifier re-run the original reproductions. Round 1 resolved 46; round 2 finished the remaining 2 and 7 visual
issues found while recording the element showcase videos. Regression tests: `game/tests/sim/test_regressions_*.gd`,
`game/tests/ui/test_input_regressions.gd` (each fails on the pre-fix code).

| # | Sev | Area | Finding | Status |
|---|---|---|---|---|
| 1 | high | input | Technique aim drag is discarded on the commit tick, so throws, streams and pours ignore the player's aim | fixed, verified |
| 2 | high | input | game.gd sets the input hub to PROCESS_MODE_ALWAYS, which breaks pause handling: Esc close reopens the menu over a running game and menu input leaks into gameplay | fixed, verified |
| 3 | high | ios | Handheld orientation is SENSOR (all four orientations), so iOS runs the landscape-only game in portrait | fixed, verified |
| 4 | high | sim | A queued grip still resolves after the requester whiffed or was staggered, leaving an orphan held body dragged to a stale hold_point | fixed, verified |
| 5 | medium | ai | AI picks heat-draw against heavy (45 kg) lava waves even though the draw can never set them in time, so every heavy wave hits; the wall it skips stops 100% | fixed, verified |
| 6 | medium | ai | A decided wave counter is silently dropped when the AI is mid-action (press expires in the buffer), and the AI never re-decides; it stands idle while the wave hits | fixed, verified |
| 7 | medium | ai | AI oscillates in place at the pool edge (13-21 direction reversals per second) and can never approach a foe across the pool | fixed, verified |
| 8 | medium | ai | AI throws stone after stone into the cover wall from behind it (no line-of-sight check), stalling the stone_rain drill for long stretches | fixed, verified |
| 9 | medium | ai | Lightning defence lowers its guard after a fixed 0.9 s while the visible charge continues; holding the charge >~1.3 s always beats the AI | fixed, verified |
| 10 | medium | ai | Close-range strike defence is dead code: it requires the foe's move to still be in STARTUP after the reaction delay, which never happens | fixed, verified |
| 11 | medium | input | A technique cancel during startup, buffered, or in the same tick as the press is dropped and the technique commits | fixed, verified |
| 12 | medium | input | Camera sensitivity and invert_y are applied twice (once in the producer, again in CameraRig), so invert does nothing and sensitivity is squared | fixed, verified |
| 13 | medium | input | CameraRig turns 'pitch up' (positive cam_delta.y) into raising the camera, so finger, mouse or stick up looks down, opposite to the documented contract | fixed, verified |
| 14 | medium | integration | Per-body audio loops never stop when their body dies (stuck lava/heat loops until scenario reload) | fixed, verified |
| 15 | medium | integration | Lightning charge never shows the aim line (ChargeAimFX); fire-charge FX is kept after bolt_ready | fixed, verified |
| 16 | medium | integration | Strafe and evade clips are left/right mirrored (feet step opposite to travel) | fixed, verified |
| 17 | medium | integration | No KO handling: a fighter at 0 HP becomes an inert, untargetable, unhittable statue and drills stop forever | fixed, verified |
| 18 | medium | integration | Pooled views alias when a view kind exceeds its VfxPool cap (two live bodies drive one node; released node still in use) | fixed, verified |
| 19 | medium | ios | Touch HUD keeps taking touches while paused: phantom attack/guard/evade and a camera jump on resume | fixed, verified |
| 20 | medium | ios | BodyViews rebuilds every lava-wave/rock-ridge mesh every rendered frame, including static cold ridges | fixed, verified |
| 21 | medium | ios | Adaptive quality ratchets to the lowest tier: the 600-frame p95 window is not reset after a step-down | fixed, verified |
| 22 | medium | ios | HUD text is sized in viewport units, not physical size: about 1 mm glyphs on iPhone | fixed, verified |
| 23 | medium | sim | Stale buffered press cancels the guard the player just pressed and is holding | fixed, verified |
| 24 | medium | sim | Water held through a guard cancel stays held forever when the guard is not a Water guard | fixed, verified |
| 25 | medium | sim | Pour places the wave 1.3 m ahead with no obstruction test, tunnelling through thin walls | fixed, verified |
| 26 | medium | sim | Lava resting in the pool (or a puddle) never quenches | fixed, verified |
| 27 | medium | sim | Remnant lifetime runs from spawn, so a reused stone despawns mid-throw | fixed, verified |
| 28 | medium | sim | Running guard reads the live element, so a mid-guard element switch changes its rules | fixed, verified |
| 29 | medium | sim | Stale wall_body: a non-Earth or airborne guard keeps the previous Earth wall up and can redirect with it | fixed, verified |
| 30 | low | ai | Contest-the-held-stone 'chance' is re-rolled every tick, so the AI always contests regardless of counter | fixed, verified |
| 31 | low | input | Charge-ring timing (0.22 s from touch-down) does not match when the sim decides tap vs hold (per element, from action start) | fixed, verified |
| 32 | low | integration | Cold Hands: the vent re-melts lava already 'set into rock'; the pool then vanishes 45 s after setting | fixed, verified |
| 33 | low | integration | Storm's Path challenge counts the caster as one of the '2 targets' | fixed, verified |
| 34 | low | integration | Attack clip contact frame lands early: _play_aligned re-scales speed from the full contact time every frame | fixed, verified |
| 35 | low | integration | Auto-quality steps down repeatedly on stale samples (p95 window not reset after a quality change) | fixed, verified |
| 36 | low | integration | Boulder Launcher: a seized plinth stone keeps static_body=true, so a thrown stone freezes mid-air and is re-launched | fixed, verified |
| 37 | low | integration | Puddle decal is placed once and never resized/moved, so the visible wet patch differs from the conductive puddle | fixed, verified |
| 38 | low | integration | HUD water-technique availability uses a hard-coded point and ignores puddles; button is dimmed where DRAW works | fixed, verified |
| 39 | low | ios | No 60 fps cap: ProMotion iPhones/iPads render at up to 120 fps while the sim runs at 60 Hz | fixed, verified |
| 40 | low | ios | Touch overlay is re-recorded every frame because set_context always calls queue_redraw | fixed, verified |
| 41 | low | ios | PerfMonitor.session grows without bound (one Dictionary per second, forever, in every build) | fixed, verified |
| 42 | low | ios | App icon is saved with an alpha channel; App Store Connect rejects it | fixed, verified |
| 43 | low | ios | PERF.md on-device autoplay arguments are missing the `--` separator, so the soak never runs on iOS | fixed, verified |
| 44 | low | sim | Perfect guard drives the attacker's Balance below 0 without a knockdown | fixed, verified |
| 45 | low | sim | Earth heavy throw keeps full heavy damage/balance when the heavy cost cannot be paid | fixed, verified |
| 46 | low | sim | trim_remnants decays by spawn order, deleting freshly landed stones instead of the oldest-resting remnant | fixed, verified |
| 47 | low | sim | Water moved into the waterskin drops its thermal energy from the ledger | fixed, verified |
| 48 | low | sim | _enforce_cap can decay a steam cloud without recording its water mass | fixed, verified |

## Round 2 (from showcase recordings)

| Item | Status |
|---|---|
| AI walked toward the wave while drawing heat (draw estimate assumed standing still) | fixed, verified |
| Earth technique silently ripped a stone after a 'too heavy' failure | fixed (technique ends visibly), verified |
| Touch charge ring timed from touch-down, not from the sim's action start (buffered presses) | fixed (ring follows sim), verified |
| Mastery toast wording when a practice kit already grants the technique | fixed, verified |
| Held water never resized; drawn-water shield looked tiny | fixed, verified |
| Puddles nearly invisible; no visual while drawing water | fixed (water lens + draw stream), verified; lens/ice sorting and melt-splash position fixed after verification |
| First flare of a session darkened the scene for a frame | fixed (light variants pre-compiled at load), verified |
| Air gust and cyclone push looked the same; air dash had no visual | fixed, verified |
