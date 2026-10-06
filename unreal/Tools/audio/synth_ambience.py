"""Ambience beds for the mountain courtyard by a lake: open air, wind in the trees, distant water.

Long (12-16 s) exactly periodic loops (circular spectral noise, integer-cycle modulators, wrapped event scatter) so the
seam is continuous and the beds repeat rarely; the runtime plays them together with the short 3 s base bed and random
accents (wind chimes, bamboo knock).  No animals, no voices.
"""
from __future__ import annotations

import numpy as np

import synth_sfx as S
from synth_sfx import (SR, TWO_PI, add_at, crackle, dsine, lfo, loop_mod, m_bp, m_hp, m_lp, m_pow, mixn, mprod, ns, norm_std,
                       snoise, strat, tarr, tv_loop, warm, sfx)

# the base 3 s courtyard bed becomes a normal -3 dBFS file like everything else (its level lives in the manifest gain)
for _s in S.SOUNDS:
    if _s.name == "amb_courtyard_loop":
        _s.peak = -3.0
        _s.bus = "ambience"
        _s.cat2 = "ambience"


def _gust(r, n, fc=0.3, depth=0.9, floor=0.12):
    """Periodic slow gust envelope 0..1."""
    g = np.exp(depth * snoise(r, n, m_lp(fc, 2)))
    g = (g - g.min()) / max(float(g.max() - g.min()), 1e-9)
    return floor + (1 - floor) * g


@sfx("amb_courtyard_air_loop", "ambience", -44, 1, "Open courtyard air: soft room-tone hush with slow gust swells and a faint "
     "high shimmer (loop 12 s).", loop=True, crest=12.0, max_loop=16.0, max_dur=16.0, bus="ambience", cat2="ambience")
def amb_courtyard_air_loop(r):
    L = 12.0
    n = ns(L)
    g = _gust(r, n, 0.22, 0.8, 0.25)
    hush = snoise(r, n, mprod(m_pow(-0.45), m_lp(2600, 2), m_hp(110, 2)))
    body = snoise(r, n, m_bp(160, 700)) * g ** 1.3
    shim = snoise(r, n, m_bp(4200, 9500)) * loop_mod(r, n, 0.6, 0.7) * g ** 1.6
    return mixn(n, (norm_std(hush), 0.7), (norm_std(body), 0.55), (norm_std(shim), 0.08))


@sfx("amb_wind_trees_loop", "ambience", -44, 1, "Wind in the trees: leaf rustle riding on gusts, a woody low body and a faint "
     "whistle through branches (loop 14 s).", loop=True, crest=12.0, max_loop=16.0, max_dur=16.0, bus="ambience", cat2="ambience")
def amb_wind_trees_loop(r):
    L = 14.0
    n = ns(L)
    g = _gust(r, n, 0.32, 1.0, 0.1)
    rust = snoise(r, n, mprod(m_bp(1500, 7500), m_pow(-0.15))) * (g ** 1.7) * loop_mod(r, n, 40, 0.95)
    rust2 = snoise(r, n, m_bp(3000, 9500)) * (g ** 2.0) * loop_mod(r, n, 70, 1.0)
    body = tv_loop(snoise(r, n, m_bp(120, 900)), "bp", 360 * 2 ** (0.7 * (g - 0.5)), 1.1, block=240) * g ** 1.2
    whistle = tv_loop(snoise(r, n, m_bp(900, 3200)), "bp", 1700 * 2 ** (0.25 * np.sin(TWO_PI * 5 * np.arange(n) / n + 1.0)),
                      14.0, block=240) * (g ** 3.0)
    return mixn(n, (norm_std(rust), 0.8), (norm_std(rust2), 0.3), (norm_std(body), 0.6), (norm_std(whistle), 0.07))


@sfx("amb_water_distant_loop", "ambience", -46, 1, "Lake water far away: low lapping swells with foamy crests, a faint trickle and "
     "sparse small plinks (loop 16 s).", loop=True, crest=12.0, max_loop=18.0, max_dur=18.0, bus="ambience", cat2="ambience")
def amb_water_distant_loop(r):
    L = 16.0
    n = ns(L)
    t = np.arange(n) / n
    ph = r.uniform(0, TWO_PI, 3)
    sw = 0.55 + 0.3 * np.sin(TWO_PI * 4 * t + ph[0]) + 0.2 * np.sin(TWO_PI * 5 * t + ph[1]) + 0.12 * np.sin(TWO_PI * 7 * t + ph[2])
    sw = np.clip(sw, 0.05, None)
    crest = np.clip(np.diff(np.concatenate([sw, sw[:1]])) * n / 40.0, 0, None)       # rising flanks make the foam
    crest = crest / max(float(crest.max()), 1e-9)
    low = snoise(r, n, mprod(m_bp(100, 800), m_pow(-0.3))) * sw ** 1.4
    lap = snoise(r, n, m_bp(350, 2800)) * (sw ** 2.2) * loop_mod(r, n, 6, 0.5)
    foam = snoise(r, n, m_bp(3000, 8000)) * (crest ** 1.2) * loop_mod(r, n, 25, 0.6)
    trickle = snoise(r, n, m_bp(2500, 7000)) * loop_mod(r, n, 90, 0.55)
    pl = np.zeros(n)
    for ts in strat(r, 22, L):
        f = float(np.exp(r.uniform(np.log(700), np.log(3200))))
        add_at(pl, dsine(f, r.uniform(0.012, 0.03), g=r.uniform(0.3, 1.0), ln=6.0), int(ts * SR), r.uniform(0.3, 1.0), wrap=True)
    return mixn(n, (norm_std(low), 0.8), (norm_std(lap), 0.5), (norm_std(foam), 0.16), (norm_std(trickle), 0.05), (pl, 0.07))
