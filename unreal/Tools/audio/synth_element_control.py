"""Element-control stage layers (first person; spec 2026-10-14 §4 "Shared", plan 06): raise (matter leaves its source
and comes to the hands), hold loops per element family whose gain and pitch the runtime drives from the held body's
speed (sfx_manifest loops.bodies gain_map / pitch_map), and the release push of the arms. Impacts reuse the impact set.
Deterministic like every synth module (make_rng(name)); loops are exactly periodic (circular noise, integer LFO cycles,
wrapped crackle / bubbles).
"""
from __future__ import annotations

import numpy as np

import synth_sfx as S
from synth_sfx import pthump as thump
from synth_sfx import (SR, TWO_PI, burst, click, crackle, curve, fnoise, grind, lfo, loop_mod, m_bp, m_hp, m_lp, mixn,
                       mprod, norm_std, ns, place, reverb, sfx, snoise, strat, swell, tv_biquad, tv_loop, warm, whoosh,
                       white)


# ---------------------------------------------------------------------------------------------------------------- raise
@sfx("raise_earth", "earth", -18, 2, "Raise (earth): the ground cracks open, the block grinds free and rises with a low "
     "rumble into the hands.", crest=16.0, fade_out=0.08)
def raise_earth(r):
    n = ns(0.9)
    gr = grind(r, 0.9, [(0, 30), (0.35, 55), (0.9, 12)], 90, 1300)
    rumble = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 180), (0.5, 420), (0.9, 220)], log=True), 0.8))
    env = swell(n, 0.5, 1.4)
    out = mixn(n, (gr * env, 0.8), (rumble * env, 0.6), (click(r, 0.004, lo=1200), 0.6))
    place(out, thump(0.25, 150, 70, 0.03, 0.08, drive=2.0), 0.0, 0.8)
    place(out, burst(r, 0.12, lo=300, hi=2600, att=0.001, tau=0.03), 0.02, 0.5)
    return reverb(r, out, 0.3, 0.12, 5000)


@sfx("raise_water", "water", -18, 2, "Raise (water): a sucking pull off the surface, a rising watery swirl, droplets "
     "falling back as the stream reaches the hands.", crest=16.0, fade_out=0.1)
def raise_water(r):
    n = ns(0.9)
    sw = whoosh(r, 0.9, [(0, 300), (0.55, 2600), (0.9, 1400)], q=2.0, p=1.4, qq=1.6, body=0.3, air=0.15)
    bub = S._bubbles(r, n, np.sort(r.uniform(0.05, 0.85, 26)), 500, 2600, 0.01, 0.035, 0.7, 0.8)
    suck = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 220), (0.3, 900), (0.9, 600)], log=True), 3.0))
    drops = np.zeros(n)
    crackle(r, drops, np.sort(r.uniform(0.45, 0.88, 14)), 1800, 5200, 0.004, 0.012, amp=0.7, power=1.5, g=0.6)
    return mixn(n, (sw, 0.9), (bub, 0.7), (suck * swell(n, 0.6, 2.2), 0.35), (drops, 0.45))


@sfx("raise_fire", "fire", -18, 2, "Raise (fire): a sharp inhale of air toward the fists, gathering ember ticks, a soft "
     "ignition swell.", crest=15.0, fade_out=0.1)
def raise_fire(r):
    n = ns(0.8)
    inhale = whoosh(r, 0.55, [(0, 3500), (0.4, 1200), (0.55, 700)], q=1.1, p=2.4, qq=0.6, air=0.4)
    roar = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 300), (0.5, 900), (0.8, 2200)], log=True), 0.7))
    roar = roar * swell(n, 2.6, 0.7) * (1 + 0.3 * fnoise(r, n, hi=30))
    tk = np.zeros(n)
    crackle(r, tk, np.sort(r.uniform(0.2, 0.78, 40)), 1200, 6500, 0.0008, 0.004, amp=0.9)
    out = mixn(n, (inhale, 0.8), (roar, 0.7), (tk, 0.5))
    place(out, thump(0.2, 220, 130, 0.02, 0.06, drive=1.6), 0.55, 0.45)
    return out


@sfx("raise_air", "air", -20, 2, "Raise (air): wind drawn in from all around, a tightening swirl that spins up toward "
     "the palms.", crest=14.0, fade_out=0.1)
def raise_air(r):
    n = ns(0.8)
    sw = whoosh(r, 0.8, [(0, 500), (0.6, 2400), (0.8, 1800)], q=1.6, p=1.6, qq=0.9, body=0.2, air=0.5, flutter=0.2)
    spin = 0.65 + 0.35 * np.sin(TWO_PI * np.cumsum(curve(n, [(0, 3.0), (0.8, 11.0)])) / SR)
    hiss = fnoise(r, n, lo=5000, order=3) * swell(n, 2.0, 0.8)
    return mixn(n, (sw * spin, 1.0), (hiss, 0.12))


# ----------------------------------------------------------------------------------------------------------------- hold
@sfx("hold_earth_loop", "earth", -24, 2, "Hold (earth): a held rock's low strain, a gritty trickle of dust and a slow "
     "creak; gain and pitch follow its speed at runtime (loop 2.0 s).", loop=True, crest=12.0)
def hold_earth_loop(r):
    L = 2.0
    n = ns(L)
    low = snoise(r, n, mprod(m_lp(320, 2), m_hp(70))) * loop_mod(r, n, 2.5, 0.35)
    grit = np.zeros(n)
    crackle(r, grit, strat(r, 70, L), 1500, 7000, 0.0006, 0.0025, amp=0.8, power=2.5, wrap=True)
    creak = snoise(r, n, m_bp(260, 900, 3)) * loop_mod(r, n, 6, 0.6)
    return mixn(n, (low, 0.9), (grit, 0.45), (creak, 0.25))


@sfx("hold_water_loop", "water", -24, 2, "Hold (water): a churning stream between the palms, rolling resonance and small "
     "bubbles; gain and pitch follow its speed at runtime (loop 2.0 s).", loop=True, crest=12.0)
def hold_water_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    churn = tv_loop(snoise(r, n, m_bp(200, 5000)), "bp", 900 * 2 ** (0.8 * lfo(n, 3, ph)), 2.6)
    flow = snoise(r, n, m_bp(400, 3000)) * loop_mod(r, n, 6, 0.3)
    bub = S._bubbles(r, n, strat(r, 40, L), 600, 2400, 0.01, 0.03, 0.7, 0.7, wrap=True)
    return mixn(n, (norm_std(churn), 0.6), (flow, 0.5), (bub, 0.8))


@sfx("hold_fire_loop", "fire", -24, 2, "Hold (fire): a contained roar at the fists with flutter, hiss and crackle; gain "
     "and pitch follow its speed at runtime (loop 2.0 s).", loop=True, crest=12.0)
def hold_fire_loop(r):
    L = 2.0
    n = ns(L)
    roar = snoise(r, n, m_bp(120, 1800, 2)) * loop_mod(r, n, 10, 0.45)
    hiss = snoise(r, n, m_bp(2800, 9000)) * loop_mod(r, n, 18, 0.7)
    cr = np.zeros(n)
    crackle(r, cr, strat(r, 60, L), 900, 6000, 0.0008, 0.005, amp=1.0, power=2.0, wrap=True)
    return warm(mixn(n, (roar, 0.9), (hiss, 0.18), (cr, 0.5)), 1.3)


@sfx("hold_air_loop", "air", -26, 2, "Hold (air): a spinning ball of wind between the hands, a whirling band over a soft "
     "body; gain and pitch follow its speed at runtime (loop 2.0 s).", loop=True, crest=12.0)
def hold_air_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    swirl = tv_loop(snoise(r, n, m_bp(250, 6000)), "bp", 1100 * 2 ** (1.1 * lfo(n, 4, ph)), 2.2)
    body = snoise(r, n, mprod(m_lp(500, 2), m_hp(90))) * loop_mod(r, n, 3, 0.4)
    air = snoise(r, n, m_hp(4500, 3)) * (0.7 + 0.3 * lfo(n, 8, ph + 0.7))
    return mixn(n, (norm_std(swirl), 0.8), (body, 0.5), (air, 0.12))


# -------------------------------------------------------------------------------------------------------------- release
@sfx("release_push", "movement", -18, 3, "Release: the arms' push that throws the held element - a short pressure thump "
     "with a cloth snap and a forward whoosh (under the element's own launch sound).", crest=16.0, fade_out=0.06,
     max_dur=0.8, bus="sfx", cat2="element")
def release_push(r):
    n = ns(0.5)
    sw = whoosh(r, 0.42, [(0, 600), (0.12, 2600), (0.42, 900)], q=1.3, p=1.0, qq=2.2, body=0.4, air=0.2)
    out = mixn(n, (sw, 0.8), (burst(r, 0.06, lo=900, hi=5000, att=0.0008, tau=0.012), 0.35))
    place(out, thump(0.2, 230, 140, 0.02, 0.05, drive=1.8), 0.0, 0.9)
    return out
