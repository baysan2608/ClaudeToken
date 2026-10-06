"""Material impact set (light / heavy) from one modal + transient + body model, plus status sounds.

Model of a struck object:  contact transient (hardness sets brightness and length)  +  modal ring of the object (material
sets mode ratios and damping, size sets the base frequency)  +  body thud (mass)  +  debris / particulate tail.
The hit event of the sim carries the threat's material (`mat`), so the runtime layers `impact_<mat>_{light,heavy}` on top
of the generic hit_light / hit_heavy body sound.
"""
from __future__ import annotations

import numpy as np

import synth_sfx as S
from synth_sfx import pthump as thump  # noqa: F401  (phone-safe thump)
from synth_sfx import (SR, TWO_PI, add_at, ad, burst, click, crackle, curve, dsine, filt, fit, fnoise, lfo, loop_mod,
                       m_bp, mixn, modal, ns, place, poisson, reverb, snoise, strat, swell, tarr, tone, tv_biquad,
                       tv_loop, warm, white, norm_std, sfx)

# material -> (mode ratios, mode amps, mode taus (s) at light size, base Hz light, base Hz heavy)
_MODES = {
    "stone": ([1.0, 1.62, 2.41, 3.35, 4.6], [0.5, 0.4, 0.28, 0.18, 0.1], [0.06, 0.045, 0.032, 0.024, 0.016], 430.0, 300.0),
    "metal": ([1.0, 2.76, 5.4, 8.93, 13.3], [0.8, 0.55, 0.35, 0.2, 0.1], [0.34, 0.22, 0.13, 0.07, 0.04], 720.0, 430.0),
    "glass": ([1.0, 2.32, 4.25, 6.63, 9.4], [0.8, 0.6, 0.4, 0.25, 0.12], [0.13, 0.09, 0.055, 0.03, 0.02], 2300.0, 1500.0),
    "ice": ([1.0, 1.59, 2.14, 3.0, 4.3], [0.7, 0.5, 0.4, 0.25, 0.15], [0.07, 0.05, 0.04, 0.028, 0.018], 1500.0, 900.0),
    "wood": ([1.0, 2.14, 3.62, 5.4], [0.8, 0.5, 0.3, 0.15], [0.05, 0.034, 0.022, 0.014], 360.0, 260.0),
    "plant": ([1.0, 2.05, 3.3, 5.1], [0.7, 0.4, 0.22, 0.1], [0.028, 0.02, 0.014, 0.009], 330.0, 240.0),
}


def _ring(kind, heavy, tilt=1.0, dur=0.5):
    ratios, amps, taus, b_l, b_h = _MODES[kind]
    base = (b_h if heavy else b_l) * tilt
    scale = 1.35 if heavy else 1.0
    return modal([base * m for m in ratios], amps, [t * scale for t in taus], dur=dur)


def _impact_modal(r, kind, heavy):
    dur = 0.62 if heavy else 0.4
    n = ns(dur + 0.1)
    hard = {"stone": 1.0, "metal": 1.3, "glass": 1.5, "ice": 1.2, "wood": 0.9, "plant": 0.6}[kind]
    ring = _ring(kind, heavy, tilt=r.uniform(0.98, 1.02), dur=dur)
    tr = mixn(n, (click(r, 0.0018 / hard, lo=1800 * hard), 0.7),
              (burst(r, 0.1, lo=500 * hard, hi=7000 * hard, att=0.0004, tau=0.012 + 0.012 * heavy), 0.8))
    g = np.zeros(n)
    if kind in ("stone", "ice"):
        crackle(r, g, np.sort(r.uniform(0.005, 0.25, 26 if heavy else 12) ** 1.2), 500, 5000, 0.002, 0.01, amp=0.5)
    if kind == "glass":
        crackle(r, g, np.sort(r.uniform(0.0, 0.2, 18 if heavy else 9)), 3000, 11000, 0.01, 0.05, amp=0.5, power=1.4)
    if kind == "plant":
        crackle(r, g, np.sort(r.uniform(0.0, 0.2, 30)), 1500, 6000, 0.0008, 0.003, amp=0.5)
    body = {"stone": 1.0, "metal": 0.45, "glass": 0.15, "ice": 0.5, "wood": 0.9, "plant": 0.7}[kind]
    th = thump(0.3, 220 if heavy else 300, 80 if heavy else 130, 0.02, 0.07 if heavy else 0.045, drive=2.0)
    out = mixn(n, (ring, 0.8 if kind != "metal" else 0.9), (tr, 1.0), (th, body), (g, 0.55))
    if kind == "metal":
        # a detuned twin gives the shimmering beat of a struck plate
        out += 0.35 * fit(modal([720.0 * 1.006 * m for m in _MODES["metal"][0][:3]] if not heavy else
                                [430.0 * 1.006 * m for m in _MODES["metal"][0][:3]], [0.5, 0.3, 0.2], [0.3, 0.2, 0.1], dur=dur), n)
    return reverb(r, out, 0.25 if heavy else 0.15, 0.12, 7000, 0.004)


def _impact_sand(r, heavy):
    n = ns(0.55 if heavy else 0.35)
    out = mixn(n, (thump(0.3, 150 if heavy else 210, 70 if heavy else 110, 0.025, 0.06, drive=1.6), 0.7),
               (burst(r, 0.3, lo=300, hi=3200, att=0.003, tau=0.07 if heavy else 0.045), 0.75),
               (burst(r, 0.3, lo=3500, hi=9000, att=0.004, tau=0.06), 0.2))
    g = np.zeros(n)
    crackle(r, g, np.sort(r.uniform(0.0, 0.4 if heavy else 0.25, 160 if heavy else 80) ** 1.1), 1800, 8000, 0.0004, 0.0016, amp=0.7)
    return out + 0.5 * g


def _impact_water(r, heavy):
    n = ns(0.6 if heavy else 0.4)
    out = mixn(n, (thump(0.25, 160 if heavy else 220, 70, 0.025, 0.06, drive=1.3), 0.5),
               (burst(r, 0.3, lo=250, hi=4200, att=0.003, tau=0.09 if heavy else 0.05), 0.95),
               (burst(r, 0.3, lo=3500, hi=9500, att=0.003, tau=0.04), 0.22))
    for _ in range(9 if heavy else 5):
        f = float(np.exp(r.uniform(np.log(350), np.log(2800))))
        add_at(out, dsine(f, r.uniform(0.01, 0.03), g=r.uniform(0.3, 1.0), ln=6.0), ns(r.uniform(0.01, 0.3)),
               r.uniform(0.3, 0.8), check=False)
    return out


def _impact_fire(r, heavy):
    n = ns(0.62 if heavy else 0.4)
    t = tarr(n)
    sz = np.zeros(n)
    crackle(r, sz, np.sort(r.uniform(0.0, 0.4, 28 if heavy else 14)), 1500, 6500, 0.0006, 0.003, amp=0.8)
    whomp = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 4000), (0.3, 700)], log=True), 0.8)) * ad(t, 0.004, 0.1 if heavy else 0.06)
    return mixn(n, (thump(0.3, 140 if heavy else 190, 70, 0.03, 0.08, drive=2.0), 0.8), (whomp, 0.9),
                (burst(r, 0.2, lo=800, hi=7000, att=0.001, tau=0.02), 0.5), (sz, 0.5),
                (fnoise(r, n, lo=4000, hi=10000) * ad(t, 0.01, 0.12), 0.1))


def _impact_wind(r, heavy):
    n = ns(0.55 if heavy else 0.34)
    t = tarr(n)
    slap = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 2600), (0.3, 500)], log=True), 0.9)) * ad(t, 0.002, 0.07 if heavy else 0.04)
    return mixn(n, (thump(0.25, 130 if heavy else 180, 70, 0.02, 0.06, drive=1.6), 0.7), (slap, 1.0),
                (burst(r, 0.12, lo=700, hi=6000, att=0.0005, tau=0.015), 0.5),
                (fnoise(r, n, lo=3500, hi=9000) * ad(t, 0.004, 0.07), 0.12))


_IMPACTS = {
    "stone": lambda r, h: _impact_modal(r, "stone", h),
    "metal": lambda r, h: _impact_modal(r, "metal", h),
    "glass": lambda r, h: _impact_modal(r, "glass", h),
    "ice": lambda r, h: _impact_modal(r, "ice", h),
    "wood": lambda r, h: _impact_modal(r, "wood", h),
    "plant": lambda r, h: _impact_modal(r, "plant", h),
    "sand": _impact_sand, "water": _impact_water, "fire": _impact_fire, "wind": _impact_wind,
}
_IMPACT_NOTE = {
    "stone": "dead stone ring, thud and chips", "metal": "inharmonic bar ring with a beating twin, bright click",
    "glass": "bright high modes and sparkle, no body", "ice": "glassy mid modes, a dull thud and crackle",
    "wood": "hollow knock with woody modes", "plant": "short fibrous thwack with leaf ticks",
    "sand": "soft thud and a grain spray", "water": "slap, spray and bubbles", "fire": "scorching whomp with crackle",
    "wind": "air slap: soft pressure thump and a falling whip",
}
for _m, _fn in _IMPACTS.items():
    for _heavy in (False, True):
        sfx(f"impact_{_m}_{'heavy' if _heavy else 'light'}", "combat", -13 if _heavy else -19, 3 if not _heavy else 2,
            f"{'Heavy' if _heavy else 'Light'} {_m} impact layer for a hit that carries the {_m} material: {_IMPACT_NOTE[_m]}.",
            crest=16.0, fade_out=0.1 if _heavy else 0.06, max_dur=0.8, bus="sfx", cat2="impact", lowcut=110.0)(
            lambda r, f=_fn, h=_heavy: f(r, h))

# ---------------------------------------------------------------------------------------------------------------
# status sounds
# ---------------------------------------------------------------------------------------------------------------


@sfx("status_burn_loop", "fire", -34, 1, "Burning on a fighter: small steady flame, low fluttering roar with sparse crackle "
     "(loop 2.0 s).", loop=True, crest=12.0, bus="sfx", cat2="loop")
def status_burn_loop(r):
    L = 2.0
    n = ns(L)
    cr = np.zeros(n)
    crackle(r, cr, np.sort(r.uniform(0.0, L, 46)), 1200, 6500, 0.0005, 0.0025, amp=0.9, power=1.8, wrap=True)
    roar = snoise(r, n, m_bp(120, 900)) * loop_mod(r, n, 9, 0.6)
    hiss = snoise(r, n, m_bp(3000, 8500)) * loop_mod(r, n, 12, 0.7)
    return mixn(n, (roar, 0.8), (cr, 0.5), (hiss, 0.14))


@sfx("status_off", "movement", -28, 2, "A status ending: a soft breath of air and a faint settling tick.", fade_out=0.08,
     crest=12.0, max_dur=0.5, bus="sfx", cat2="element")
def status_off(r):
    n = ns(0.3)
    t = tarr(n)
    w = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 2400), (0.25, 900)], log=True), 1.4)) * ad(t, 0.01, 0.07)
    return mixn(n, (w, 1.0), (click(r, 0.002, lo=2500), 0.15), (thump(0.1, 160, 90, 0.015, 0.03, drive=1.2), 0.2))


@sfx("status_wet_on", "water", -27, 2, "Getting soaked: a soft splash with a few droplets (quieter than water_splash).",
     fade_out=0.08, crest=14.0, max_dur=0.6, bus="sfx", cat2="element")
def status_wet_on(r):
    n = ns(0.45)
    out = mixn(n, (burst(r, 0.25, lo=400, hi=4200, att=0.006, tau=0.07), 0.8),
               (thump(0.2, 170, 90, 0.025, 0.05, drive=1.1), 0.3))
    for _ in range(7):
        f = float(np.exp(r.uniform(np.log(600), np.log(3500))))
        add_at(out, dsine(f, r.uniform(0.01, 0.025), g=r.uniform(0.3, 1.0), ln=6.0), ns(r.uniform(0.02, 0.38)),
               r.uniform(0.2, 0.6), check=False)
    return out


@sfx("armor_up", "earth", -20, 2, "Stone skin / anchor stance taking hold: grit, a low stone ring and a rooted press.",
     fade_out=0.1, crest=14.0, max_dur=0.8, bus="sfx", cat2="element")
def armor_up(r):
    n = ns(0.65)
    g = np.zeros(n)
    crackle(r, g, np.sort(r.uniform(0.0, 0.4, 36) ** 1.2), 500, 3500, 0.002, 0.008, amp=0.5)
    return mixn(n, (thump(0.4, 120, 55, 0.04, 0.1, drive=2.2), 0.8), (S._stone_ring(260, 1.0, 0.5), 0.5),
                (burst(r, 0.4, lo=150, hi=1600, att=0.03, tau=0.12), 0.5), (g, 0.45))


@sfx("stance_settle", "movement", -30, 2, "Entering a stance: cloth settling and a small foot press (quiet).", fade_out=0.08,
     crest=12.0, max_dur=0.6, bus="sfx", cat2="element")
def stance_settle(r):
    n = ns(0.45)
    t = tarr(n)
    cl = fnoise(r, n, lo=1200, hi=5000) * swell(n, 1.4, 2.0) * (0.55 + 0.45 * np.clip(fnoise(r, n, hi=40), -1, 2))
    return mixn(n, (cl, 0.7), (thump(0.2, 130, 80, 0.03, 0.05, drive=1.3), 0.5))


# ---------------------------------------------------------------------------------------------------------------
# one-shot / loop variants of base loops the runtime needs (a looping asset must never be played as a one-shot)
# ---------------------------------------------------------------------------------------------------------------


@sfx("water_jet", "water", -24, 2, "A burst of pressurised water: sharp onset, tight 2.5-9 kHz hiss with a narrow resonant "
     "band, falling away (one-shot twin of water_jet_loop).", crest=12.0, fade_out=0.15, max_dur=1.0, bus="sfx", cat2="element")
def water_jet(r):
    n = ns(0.8)
    t = tarr(n)
    hs = fnoise(r, n, lo=2500, hi=9500) * ad(t, 0.01, 0.28)
    nb = norm_std(tv_biquad(white(r, n), "bp", 4200 * 2 ** (0.1 * np.sin(TWO_PI * 3 * t)), 4.0)) * ad(t, 0.01, 0.3)
    body = fnoise(r, n, lo=300, hi=1400) * ad(t, 0.006, 0.12)
    return mixn(n, (hs, 0.8), (nb, 0.5), (body, 0.35), (click(r, 0.002, lo=2500), 0.25))


@sfx("geyser_loop", "water", -28, 1, "Geyser / steam column venting: broad roaring hiss over a low rumble with slow surges "
     "(loop 2.0 s).", loop=True, crest=12.0, bus="sfx", cat2="loop")
def geyser_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    roar = snoise(r, n, m_bp(700, 4200)) * loop_mod(r, n, 12, 0.35) * (0.8 + 0.2 * lfo(n, 2, ph))
    hiss = snoise(r, n, m_bp(3500, 9500)) * loop_mod(r, n, 30, 0.4)
    low = snoise(r, n, m_bp(120, 600)) * loop_mod(r, n, 4, 0.3)
    return mixn(n, (roar, 0.8), (hiss, 0.4), (low, 0.45))


@sfx("fuse_loop", "fire", -34, 2, "A burning fuse counting down: regular woody ticks over a thin sizzle (loop 2.0 s).",
     loop=True, crest=12.0, bus="sfx", cat2="loop")
def fuse_loop(r):
    L = 2.0
    n = ns(L)
    out = np.zeros(n)
    for i in range(8):
        tt = i * L / 8.0
        add_at(out, mixn(ns(0.1), (click(r, 0.002, lo=2000), 0.8), (dsine(1800, 0.01), 0.5)), ns(tt), 1.0, wrap=True)
    sz = snoise(r, n, m_bp(3000, 9000)) * loop_mod(r, n, 20, 0.6)
    cr = np.zeros(n)
    crackle(r, cr, np.sort(r.uniform(0.0, L, 24)), 1800, 6000, 0.0006, 0.002, amp=0.7, wrap=True)
    return mixn(n, (out, 0.7), (sz, 0.15), (cr, 0.3))
