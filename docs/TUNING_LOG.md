# Tuning log

| # | Observed | Change | Result |
|---|---|---|---|
| 1 | Passive lava cooling (2.2 HU/s·kg^⅔ per 100 °C) set a free wave in ~1 s; uncountered waves never reached the rival | `AMBIENT_LOSS` 2.2 → 0.6 | Free 20 kg wave stays fluid ~4.6 s; travel budget, not cooling, limits range |
| 2 | Magma grip could be started 0.75 s ahead of impact (window too generous, early press never failed) | grip reach 6 → 5 m, `GRIP_WINDOW` 0.45 → 0.25 s; incoming stones selectable from 14 m | Early (telegraph) press whiffs, late press is hit, ≈0.5 s usable window on a 12 m throw |
| 3 | Thermal technique found no target when pressed at 8.5 m (search radius 8 m) | incoming-projectile search 14 m, static stones 9 m | Press-on-incoming works from any readable distance |
| 4 | Ripping a stone while running counted as a "catch" (impulse from the actor's own velocity) | catch impulse uses the body's velocity only, threshold 20 kg·m/s | No false intercept events / Focus drain on rips |
| 5 | Waves missed strafing rivals entirely (AI with no counter dodged 3/3 just by strafing) | fluid wave bends toward the pourer's target ≤32°/s × fluidity (fantasy rule) | Wave threatens; counters (draw/wall/evade) matter |
| 6 | Rival's heat draw (320 HU/s, any range) set the wave within ~2 m of the player in captures | draw 260 HU/s with distance falloff (½ at 9 m); AI contests a held blob less often | Wave travels several metres before setting; AI still counters 4/5 seeds in tests |
| 7 | AI released a "draw" hold on the first tick (action not started yet after an element switch) | 0.2 s grace before "action ended" releases a hold | AI draw holds work after switching to Fire |
| 8 | AI re-rolled its counter decision every tick while a wave was out of range (counter chance effectively ~100%) | decide once per threat, wait for range in a separate state | `counter` setting means what it says |
| 9 | Soak: per-body audio loop players were never freed (node count crept up) | idle loop players freed when off and silent | node count plateaus (see PERF.md) |
| 10 | Soak: EarthWallView.setup signature mismatch made body views fail and cascade | call matches VFX API (seed, width, height, thickness) | 0 script errors in 2-min soak |
| 11 | Tests: own rising wall stopped the guard's own redirected stone; redirect flew straight (dropped ~5 m over 12 m) | own wall never blocks own shots; redirects use a ballistic launch to the thrower's chest | Perfect redirect window = stone launched 2.0–4.5 m from the guard; reaches a 12 m thrower |
| 12 | Tests: inert stones stuck inside a rising wall ground it down (39 block events, crumble) | inert bodies never damage walls and are pushed clear | Walls only take damage from attacks |
| 13 | Tests: heavy earth gather kept the stone's temperature while adding ambient mass (heat from nothing) | gather preserves total heat (temperature dilutes) | Energy ledger exact |
| 14 | Tests: drawing from a partly frozen puddle ignored latent heat | drawn water carries the source's ice fraction and exact energy | 10-min soak ledger drift 83 HU → within tolerance |
| 15 | Ledger rounding: thermal results passed through Vector2 (32-bit floats) | `Thermal.heat()` / `boil()` return 64-bit floats; Vector2 wrappers kept for compatibility | Ledgers accumulate no float32 rounding |
