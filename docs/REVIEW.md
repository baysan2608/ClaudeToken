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

## Round 3 (counter coherence, bugs and visuals review, 2026-10-06)

Reproductions: the reviewers' scripts (headless sim sweeps, guard / ledger matrices, renders). Regression tests:
`game/tests/sim/test_review_fixes.gd` (12 tests), the camera test in `game/tests/ui/test_input_regressions.gd`, and updated
kit tests. All suites green after the fixes: 546 sim · 96 UI · 182 scripts · 16 anim · VFX smoke; 5-min soak clean.

| # | Sev | Issue | Fix | Verification |
|---|---|---|---|---|
| 1 | high | Plain-guard cells skip the counter rule (12 % chip from anything; a perfect press deflects anything) | `CoreRules.plain_guard` (core + Fire kit copies): chip / balance / knock ×TP/(2·CP_eff) past 2×CP (`chip_scale`, `CombatWorld.guard_chip`), `overwhelm` below ratio 0.25, perfect only from ratio 0.5. Molten eff 0.35 vs Wind Guard / Flame Guard / Static Ward / Reactive Blast / Blue Aegis; heavy stones overwhelm a Wind Guard | Guard probe: 200 kg boulder vs every plain guard 34-37 dmg held or perfect (was 4.5 / 0); 20 kg stone keeps 12 % chip; test `test_plain_guards_scale_...`, `test_a_heavy_stone_chips_harder_...` |
| 2 | high | Water Shield and Wind Guard block lightning; Static Ward does worst | `lightning\|shield_water` = conduct ×1.5 into the holder, `lightning\|guard_wind` = pass; plain guards vs lightning take only their CP off the bolt (`plain_guard_electric`); Static Ward drinks up to 2.5×CP | Probe (T1 Bolt 24): Water shield 36, Wind Guard 24, Static Ward 0 (held), plain guard 14; `test_lightning_conducts_...`, `test_static_ward_stores_the_bolt_...` |
| 3 | high | Partials stack on continuous contact (weak zones / walls fully stop heavy threats) | `Interactions.resolve`: after a weaken / slow, the same (threat, counter) pair passes for the rest of the contact (zones, walls, wave-walls; `CONTACT_TICKS`); a weakened shot carries on through a wall; `Outcomes.weaken` caps the heat removed by the counter body's own capacity (`heat_capacity`) | Probe: Creeping Fog T0-T3 vs 45 / 80 kg lava waves now 13-18 dmg (was 0); Dune Wall T0 vs 80 kg stone weakens it once, it passes and the guard chips 1.1 (no capture); `test_a_small_fog_does_not_stop_...`, `test_a_partial_applies_once_...` |
| 4 | high | T0/T1 palm gust sends back anything under 30 kg | Legacy gust cell banded: pass < 0.5, bend ≥ 0.5, redirect ≥ 1 (eff 2 vs light solids, 0.6 heavy); fire left to the Air kit's fire bands at every tier | Probe: 20 kg stone vs Palm Gust T0 bend (r 0.85), Cyclone T1 redirect (r 1.34); 29 kg @30 m/s passes; palm gust vs a 19 PU fireball fans it; tests in `test_kit_air_gust`, `test_integration_matrix` |
| 5 | high | Sound clap disrupts any channelled guard | `Outcomes.disruptable`: a held guard is never disrupted (the sound meets the guard's own cell through the hit); charges and technique channels still are | `test_a_sound_clap_does_not_disrupt_a_held_null_bubble` (no disrupt, 0 dmg) |
| 6 | high | Grounded stance stacks with the plain-guard chip (~5 % from any bolt) | `Conduction.discharge`: the stance (plain Earth guard on stone) lands as an unblockable 40 % hit; kit guards with their own cell (Aegis) answer themselves | Storm Bolt through a fresh Bulwark vs the guarding builder: shatter r0.83 → E 21 → 7.0 dmg (was 0.8); Skybreak 15.2 (was 1.8); `test_the_grounded_stance_replaces_the_guard_chip` |
| 7 | medium | Partial bands never lower hit damage | `props.dmg_scale` multiplied by weaken / slow / overwhelm; `CombatWorld.hit_scale` applied to wave and projectile contact damage / balance; lava waves also scale with the melt they keep below 60 % | Probe: Tidal Rush T1 slows a 20 kg stone → 7.2 dmg (was 12); test `test_a_partial_applies_once_...` |
| 8 | medium | Tidal Rush power shrinks with the water but predictions use nominal | `counter_scale` hook on the def (`WaterWater.tidal_scale`) applied by `Agent.of_move` (AI, Lab matrix, predictions); T0 takes 6 kg so a full waterskin makes a full T0 wave | Probe: T0 from the skin captures the 20 kg stone; dry T3 prediction < 18 PU (`test_kit_water_cells`) |
| 9 | medium | merge_bodies divides by zero mass (Dew Fall + held guard) | `merge_bodies` / `_set_energy` skip the division for massless bodies; `Outcomes.absorb` decays a massless threat instead of merging | `test_dew_fall_then_a_held_guard_stays_finite`; reviewer's `repro_nan` no NaN |
| 10 | medium | Lab Try plays push / sink at T0 whatever tier | `LabScript.for_move` holds the guard for the tier's hold time before the flick | `test_lab_script_push_and_sink_reach_the_requested_tier` (every push / sink tier reached) |
| 11 | high | A guard that fully stops a threat still breaks the guard and staggers | `hit_actor`: `res.stopped` with nothing passing → clean `block` (no damage, balance or stagger); `disperse` handles vapour volumes | `test_a_guard_that_absorbs_the_threat_stays_up`; reviewer's guard sweep: the listed cases (Vortex Wall vs gust / fire jab, Null Bubble vs gust / blast, Wind Guard vs ember) no longer stagger |
| 12 | high | Outcomes.extinguish drops a heat volume's heat unbooked | `extinguish` / `neutralize` / kit snuff, reactive, skin and burn paths book the volume's unspent heat (`ledger.spent` / `ambient`); body heat is left in the body | Reviewer's `repro_ledger` drift 0.0000; `test_the_guard_matrix_keeps_the_energy_ledger` (16 guards × 16 strikes, |drift| < 0.01) |
| 13 | medium | merge_bodies NaN: Vortex Wall absorbing a Gust Crescent | as #9 | `test_vortex_wall_absorbing_a_gust_crescent_stays_finite` |
| 14 | medium | earth_glassify heats the wall without taking the heat from the body | `Outcomes.draw_heat` / `move_heat` take heat from a body threat's real energy (or a volume's budget); used by glassify, `heat` and the steam / water / lava transforms | Reviewer's case (Sand wall vs Blue Comet) drift 0 in the guard sweep |
| 15 | medium | Other unbooked energy (steam vs guard_blast, flame vs wall_vine) | Reactive Blast volume branch and `water_burn` volume branch book the heat | Guard sweep (2 distances × 2 gestures × 256 cells): every drift < 0.01 HU |
| 16 | high | Camera collapses to a ~1 m overhead close-up near a wall | `CameraRig`: never closer than 3 m; a wall behind swings the camera sideways (rival kept in frame, up to 100° when flush against a wall), then lifts (pitch ≤ 0.62 with a rival); interior solids in the way are drawn shadows-only, the yard's boundary walls are never crossed; a framing turn keeps the rival on screen; movement follows the view yaw | `test_camera_near_a_wall_keeps_its_distance_and_the_rival_in_frame`; `show_water_ice:30` re-rendered: pressed against the east wall the view runs along it with both fighters in frame (was a floor close-up) |
| 17 | medium | Charge tiers hard to tell apart | Stones ×1.25 / 1.5 / 1.8 at T1-T3 + tier-tinted streak (`GlideTrailFX.set_look`, `VfxPalette.tier_color`); T2/T3 release shockwave ring (T3 double ring, dust, shake); `ChargeFX` ring / ground ripple / aura grow per tier, aura from T2, steady T3 light | `show_owner` render frames: T1 heave visibly larger, T3 Crag Breaker big ring + aura while charging, double white-hot ring on release and a long streak |

Rule changes the owner may want to review (legacy cells changed by this round): plain guards now scale with the threat
(#1), lightning through a plain guard is weakened rather than chip-blocked (#2; `test_lightning.test_late_guard_does_not_redirect`
updated), the palm gust follows the counter rule (#4), Static Ward stores whole bolts (#2).
