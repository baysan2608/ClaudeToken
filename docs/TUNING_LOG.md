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
| 16 | Integration (moveset): a palm gust (T0/T1, legacy cell `magma\|gust`) turned a thrown 25 kg molten blob back at its thrower — "a simple air attack" stopping lava (MOVESET §5.4 forbids it) | legacy gust redirect for class `magma` only below 4 kg (spatter); heavier molten blobs are bent; Gale / Hurricane (T2/T3, Air kit cells) cool them | T0 gust vs 25 kg blob: bend; T3: weaken (r 0.64) / an 8 kg blob set to rock (`test_integration_matrix`) |
| 17 | Integration: any hit interrupted the armor stances (Stone Skin, Iron Stance, rooted modes), so "anchored, 40 % armor" never lasted | a fighter in a stance with armor or anchor takes damage and balance without flinching; only a knockdown (balance broken) ends the stance; `hit` events carry `armored` | Stance holds through chip hits; balance still breaks it |
| 18 | Integration: the AI's projectile aim check extrapolated thrown stones in a straight line (ignored gravity), so it saw most arcing throws as misses and never countered them | `AiPlanner.body_threat` adds the ballistic drop (½ g t²) | Every element now finds a full/partial answer to every Lab threat (21 threats × 4 elements) |
| 19 | Integration: the Lab / lab-mode kit carried the legacy `lightning` flag, so a long Flame hold became the bolt and Fire Column / Inferno were unreachable | Lab kits omit the legacy flag (`Progression.LAB_OMIT`); the bolt lives on Fire / Lightning. Practice scenarios that grant it explicitly keep it | Flame T2/T3 reachable in the Lab and in Free Spar with Lab mode on |
| 20 | Review round 3: a full 6 kg waterskin made a T0 Tidal Rush of only 13.5 PU, so "make a wave back" slowed a 20 kg stone instead of carrying it | Tidal Rush T0 water 8 → 6 kg (T1-T3 unchanged); predictions scale with the water in reach | T0 from the skin captures the stone (r 1.09-1.14 in the probe); T1-T3 away from water are partial and the stone hits softer (7.2 instead of 12) |
| 21 | Review round 3: plain guards blocked a 200 kg boulder and lava with 12 % chip; a perfect press deflected anything | Plain guard chip ×TP/(2 CP) past 2×CP, overwhelmed past 4×CP, perfect only at ratio ≥ 0.5; molten eff 0.35 vs wind / fire / static / blast guards | Boulder through any plain guard: 34-37 of 38 dmg; 20 kg stone keeps the 12 % chip; lava overwhelms a held Wind Guard / Static Ward (a perfect press still blocks a weakened wave) |
| 22 | Review round 3: Static Ward stored half of every bolt and let half through (worse than a plain guard) | the ward drinks up to 2.5 × its CP of the bolt | T0 ward stores a whole T1 Bolt (0 dmg); plain guards take the bolt minus their CP |
| 23 | Review round 3: charge tiers were hard to read at gameplay distance | thrown stones drawn ×1.25 / 1.5 / 1.8 at T1-T3 with a tier-tinted streak; T2/T3 release shockwave ring (T3 double + dust + shake); charge ring / ripple / aura grow per tier, T3 keeps a glow light | Checked in the `show_owner` render: T3 reads from the default camera |

## 2026-10-09 - movement pace (owner: "a little slower, more deliberate and fluid")
- combat_world TURN_RATE 14 -> 9 rad/s, RUN_SPEED 5.5 -> 5.0 m/s, RUN_MIN 4.0 -> 3.6, WALK_MAX 1.8 -> 1.6 (Godot
  game/core/combat_world.gd and unreal sim.json together). ACCEL / DECEL stay 34 / 42: lowering them changed air-move
  hover heights (kit tests), so the softer starts belong in the presentation layer instead.
- The slower turning surfaced a latent bug: WaterRules.o_feed's early returns left the raw kit outcome "water_feed" in
  the interaction event (not in FxEvents.OUTCOMES); they now report block (volume stopped) / pass.

## 2026-10-09 - Unreal camera and combat feel (owner: "AAA cinematic, smooth, more deliberate and fluid")
All in the logic island (`unreal/Source/Fourfold/Private/Logic/FFGCamera.h`, `FFGFeel.cpp`), unit-tested.
- Camera judder: camera drags / stick were drained once per 60 Hz sim tick (steps at 120 Hz, stalls in hit-stop); now
  drained and applied every rendered frame (`DesktopInput` / `TouchControls::TakeCamDelta`, controller `OnSimFrame`).
- Framing: lock-on two-shot replaces "orbit behind + turn to the rival". Boom on the player's chest (1.35 m), rotated
  theta off the player->rival axis; aim at lerp(player, rival, w) at 1.2 m. k = clamp((sep - 3) / 13, 0, 1):
  distance 3.6 + 1.6k m, theta 24 - 10k deg, w 0.40 - 0.10k, pitch 0.08 + 0.10k rad, vFOV 50 + 4k deg (was 5.6 m,
  pitch 0.32, vFOV 62 for everything; the player filled 21-27 % of the frame height, the rival hid behind their head).
  Side hysteresis, flips when a yard wall sits behind the boom; horizontal FOV capped at 90 deg on wide phones.
  Free orbit 4.4 m / pitch 0.22 / vFOV 54. Title / watch: side-on spectator two-shot (70 deg off the axis, never
  crossing the line). While locked the stick basis is the lock axis (forward = toward the rival).
- Smoothing: critically damped springs instead of ExpK(14) everywhere: pivot 0.12 s horizontal / 0.28 s vertical with a
  0.25 m dead zone while grounded (no stride bob), framing 0.4 s, assist yaw / pitch 0.35 s (max 4 rad/s) re-engaging
  1 s after the player steered (was a constant 1.4 / 2.6 rad/s turn with an 18 deg dead zone), look-ahead 0.12 s of
  velocity (<= 0.6 m) in free orbit; the collision pull-in keeps ExpK(18) and eases back out over 0.45 s. Rival-in-frame
  safety blend is frame-rate independent (was Lerpf 0.25 per frame, snapped to 0). Round reset snaps.
- Hit-stop runs in real seconds (was rendered frames): T0 4, T1 6, T2 9, T3 12, perfect 8, clash 6 (/60 s; were 3 / 5 /
  7 / 9 / 6 / 4); budget 20/60 s per rolling second (was 12 frames); eases 0.05 -> 1 over 50 ms.
- Shake: rotational trauma model (pitch 1.2 / yaw 0.9 deg x trauma^2, 21 Hz noise, <= 1.5 cm translation, roll 1.2 deg
  on T3 / knockdown), trauma decay 2.2/s, falloff from the duel midpoint with a 0.75 floor for hits the player gives or
  takes (was two sines of 6 cm translation). Plain shakes honour their decay.
- Kick: spring impulse (5 Hz, zeta 0.6, exact solution) peaking at T1 6 / T2 9 / T3 15 cm along the hit (toward the
  impact when the player lands it, away when they take it); only for hits the player is in.
- FOV punch: T2 -2.5, T3 / knockdown -5, perfect -7 deg; eases in 60 ms, holds through hit-stop, eases out 0.3 s.
- New cinematic beat: perfect counters, full-band tier >= 2 reflect / redirect / capture / transform / shatter /
  reclaim and the player's perfect deflect -> 0.25x for 0.4 s after the hit-stop (KO 0.9 s), ease back 0.25 s, -6 deg
  FOV + 7 % dolly toward the event; 3 s cooldown (KO exempt); never with reduced motion, a Lab freeze or a Lab time
  scale. The 0.55x slow-motion assist setting is unchanged.
- Hit-stop victim shake: the struck fighter trembles along the hit (1.2-3 cm, 26 Hz) while time is frozen.
- Hair / sash SpringChain: fixed 1/120 s steps with interpolated output and per-step animated poses (the variable last
  sub-step jittered the tails at uneven frame times).
