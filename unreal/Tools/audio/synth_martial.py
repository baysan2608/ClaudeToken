"""Martial-arts body sounds: per-element swings and kicks, stomps, weight shifts, breath, cloth, footsteps per surface.

Registered into synth_sfx.SOUNDS (run through render_audio.py).  All sounds are original, synthesised from noise and
oscillators; the per-element character follows docs/MARTIAL_ARTS.md:

    Earth (Hung Gar)        heavy and low, rooted: saturated low-mid sweeps, a weight-drop thump before the limb moves
    Water (Tai Chi)         smooth and circular: broad low-Q arch, symmetric swell, soft liquid shimmer, no sharp edge
    Fire  (Northern Shaolin) long, extended and snapping: fast bright sweep that ends in a cloth crack
    Air   (Baguazhang)      airy spirals: breathy high band with a rotating amplitude flutter and a doppler-like arch
"""
from __future__ import annotations

import numpy as np

import synth_sfx as S
from synth_sfx import pthump as thump  # noqa: F401  (phone-safe thump)
from synth_sfx import (SR, TWO_PI, add_at, ad, burst, click, crackle, curve, dsine, filt, fit, fnoise, harm_osc,
                       lfo, loop_mod, m_bp, mixn, modal, ns, place, poisson, reverb, sat, snoise, strat, swell,
                       tarr, tone, tv_biquad, warm, white, whoosh, norm_std, sfx)

# ---------------------------------------------------------------------------------------------------------------
# swings
# ---------------------------------------------------------------------------------------------------------------


def _cloth_edge(r, n, t0, gain=1.0, lo=1800, hi=6500, tau=0.03):
    """Short sleeve / sash crack at the end of a fast limb."""
    out = np.zeros(n)
    place(out, burst(r, 0.08, lo=lo, hi=hi, att=0.0003, tau=tau), t0, gain, check=False)
    place(out, click(r, 0.0012, lo=3000), t0, 0.5 * gain, check=False)
    return out


def _swing_earth(r, heavy):
    dur = 0.62 if heavy else 0.34
    n = ns(dur)
    top = 520.0 if heavy else 760.0
    w = whoosh(r, dur, [(0, 110 if heavy else 170), (dur * 0.5, top), (dur, top * 0.45)], q=0.95, p=1.9, qq=1.7,
               body=0.9 if heavy else 0.5, air=0.08, flutter=0.1, comp=0.6)
    w = filt(warm(w, 1.7 if heavy else 1.3), "lp", 3200, 2)
    out = mixn(n, (w, 1.0))
    # weight dropping into the horse stance: a short rooted thump just before the limb leaves
    place(out, thump(0.2, 105, 52, 0.03, 0.06, drive=2.2), 0.0, 0.55 if heavy else 0.3, check=False)
    cl = fnoise(r, n, lo=1400, hi=4200) * swell(n, 1.8, 2.4)
    out += (0.12 if heavy else 0.08) * cl
    return out


def _swing_water(r, heavy):
    dur = 0.78 if heavy else 0.44
    n = ns(dur)
    t = tarr(n)
    w = whoosh(r, dur, [(0, 330), (dur * 0.5, 1700 if heavy else 2100), (dur, 620)], q=0.8, p=2.3, qq=2.3,
               body=0.35, air=0.1, flutter=0.06, comp=0.4)
    # slow liquid shimmer: two soft resonances drifting against each other (a round, rolling feel)
    sh = tv_biquad(white(r, n), "bp", 900 * 2 ** (0.5 * np.sin(TWO_PI * 2.2 * t)), 4.0)
    sh2 = tv_biquad(white(r, n), "bp", 1500 * 2 ** (0.5 * np.sin(TWO_PI * 1.6 * t + 1.0)), 4.0)
    env = swell(n, 2.2, 2.2)
    out = mixn(n, (w, 1.0), (norm_std(sh) * env, 0.18), (norm_std(sh2) * env, 0.12))
    return out


def _swing_fire(r, heavy):
    dur = 0.4 if heavy else 0.24
    n = ns(dur)
    w = whoosh(r, dur, [(0, 1300), (dur * 0.62, 6200 if heavy else 7000), (dur, 3600)], q=2.3, p=1.15, qq=3.4,
               body=0.18, air=0.35, flutter=0.1, comp=0.45)
    out = mixn(n, (w, 1.0))
    tip = dur * 0.62
    out += _cloth_edge(r, n, tip, 0.9 if heavy else 0.65)
    if heavy:
        place(out, thump(0.12, 190, 90, 0.015, 0.035, drive=1.8), tip, 0.35, check=False)
    return out


def _swing_air(r, heavy):
    dur = 0.66 if heavy else 0.4
    n = ns(dur)
    t = tarr(n)
    w = whoosh(r, dur, [(0, 800), (dur * 0.45, 3600), (dur, 1300)], q=2.6, p=1.6, qq=2.1, body=0.1, air=0.55,
               flutter=0.14, comp=0.5)
    spin = 0.78 + 0.22 * np.sin(TWO_PI * (7.0 if heavy else 9.0) * t * (1 - 0.35 * t / dur) + 0.6)
    air = fnoise(r, n, lo=6000, hi=13000, order=2) * swell(n, 1.7, 1.9)
    return mixn(n, (w * spin, 1.0), (air, 0.22))


_SWING = {"earth": _swing_earth, "water": _swing_water, "fire": _swing_fire, "air": _swing_air}
_SWING_LEVEL = {("earth", False): -22, ("earth", True): -17, ("water", False): -24, ("water", True): -20,
                ("fire", False): -23, ("fire", True): -19, ("air", False): -25, ("air", True): -21}
_SWING_NOTE = {
    "earth": "Hung Gar: low rooted sweep with a weight-drop thump",
    "water": "Tai Chi: smooth rolling arch, no hard edge",
    "fire": "Northern Shaolin: fast bright sweep ending in a cloth crack",
    "air": "Baguazhang: airy spiral with a rotating flutter",
}
for _el, _fn in _SWING.items():
    for _heavy in (False, True):
        _nm = f"swing_{_el}_{'heavy' if _heavy else 'light'}"
        sfx(_nm, "combat", _SWING_LEVEL[(_el, _heavy)], 3,
            f"{'Heavy' if _heavy else 'Light'} {_el} limb swing, {_SWING_NOTE[_el]}.",
            fade_out=0.06 if _heavy else 0.04, crest=16.0, max_dur=0.9, bus="sfx", cat2="element")(
            lambda r, f=_fn, h=_heavy: f(r, h))

# ---------------------------------------------------------------------------------------------------------------
# kicks, stomps, weight shifts
# ---------------------------------------------------------------------------------------------------------------


def _kick(r, el):
    p = {"earth": dict(dur=0.58, pts=[(0, 90), (0.25, 460), (0.58, 160)], q=0.9, body=1.0, air=0.05),
         "water": dict(dur=0.72, pts=[(0, 260), (0.34, 1400), (0.72, 420)], q=0.75, body=0.4, air=0.1),
         "fire": dict(dur=0.5, pts=[(0, 900), (0.3, 5200), (0.5, 2600)], q=2.0, body=0.25, air=0.3),
         "air": dict(dur=0.66, pts=[(0, 600), (0.3, 3000), (0.66, 900)], q=2.4, body=0.15, air=0.5)}[el]
    n = ns(p["dur"])
    w = whoosh(r, p["dur"], p["pts"], q=p["q"], p=1.7, qq=2.0, body=p["body"], air=p["air"], flutter=0.12, comp=0.5)
    out = mixn(n, (w, 1.0))
    # trouser / sash flutter along the leg
    t = tarr(n)
    flap = fnoise(r, n, lo=900, hi=3500) * swell(n, 1.6, 2.2) * (0.6 + 0.4 * np.sin(TWO_PI * 22 * t))
    out += 0.14 * flap
    if el == "earth":
        out = warm(out, 1.6)
        place(out, thump(0.25, 100, 50, 0.03, 0.08, drive=2.4), 0.0, 0.5, check=False)
    if el == "fire":
        out += _cloth_edge(r, n, p["dur"] * 0.6, 0.7, lo=1500, hi=6000)
    return out


for _el, _lvl in (("earth", -16), ("water", -22), ("fire", -20), ("air", -22)):
    sfx(f"kick_{_el}", "combat", _lvl, 2, f"{_el.capitalize()} leg sweep / kick: {_SWING_NOTE[_el]}, longer with trouser flutter.",
        fade_out=0.08, crest=16.0, max_dur=0.9, bus="sfx", cat2="element")(lambda r, e=_el: _kick(r, e))


@sfx("stomp_earth", "combat", -13, 2, "Hung Gar stomp: rooted low-mid thud, stone crack, dust scatter and a short ground "
     "rumble.", crest=17.0, fade_out=0.12, max_dur=0.9, bus="sfx", cat2="impact", lowcut=130.0)
def stomp_earth(r):
    n = ns(0.8)
    g = np.zeros(n)
    crackle(r, g, np.sort(r.uniform(0.01, 0.4, 22) ** 1.3), 400, 2800, 0.002, 0.008, amp=0.5)
    out = mixn(n,
               (thump(0.45, 135, 52, 0.035, 0.1, drive=3.0), 0.85),
               (thump(0.15, 270, 150, 0.012, 0.045, drive=2.0), 0.9),
               (burst(r, 0.25, lo=150, hi=1800, att=0.0008, tau=0.06), 0.9),
               (click(r, 0.003, lo=1700), 0.4),
               (S._stone_ring(300, 0.8, 0.3), 0.35),
               (g, 0.45))
    return reverb(r, out, 0.3, 0.12, 4200)


@sfx("stomp_light", "combat", -22, 2, "Short foot stamp for a stance change or a ground slap: tight thud and a little grit.",
     crest=14.0, fade_out=0.06, max_dur=0.5, bus="sfx", cat2="impact", lowcut=140.0)
def stomp_light(r):
    n = ns(0.3)
    g = np.zeros(n)
    crackle(r, g, np.sort(r.uniform(0.005, 0.12, 8)), 800, 3600, 0.001, 0.004, amp=0.5)
    return mixn(n, (thump(0.15, 180, 80, 0.02, 0.04, drive=2.0), 0.9), (burst(r, 0.08, lo=300, hi=2200, att=0.0006, tau=0.014), 0.8),
                (click(r, 0.002, lo=2000), 0.35), (g, 0.35))


@sfx("weight_shift", "movement", -32, 2, "Tai Chi weight transfer: a soft press of the foot with a slow cloth swish; very "
     "quiet, for stance changes.", fade_out=0.08, crest=12.0, max_dur=0.6, bus="sfx", cat2="element")
def weight_shift(r):
    n = ns(0.42)
    sw = whoosh(r, 0.42, [(0, 380), (0.2, 900), (0.42, 520)], q=0.8, p=2.2, qq=2.2, body=0.3, air=0.0, flutter=0.05)
    return mixn(n, (sw, 0.7), (thump(0.2, 140, 85, 0.03, 0.05, drive=1.3), 0.45),
                (fnoise(r, n, lo=2000, hi=5000) * swell(n, 1.8, 2.0), 0.1))


@sfx("swirl_air", "movement", -28, 2, "Baguazhang circle-walk pivot: cloth swirl with a rotating flutter and a soft footfall.",
     fade_out=0.08, crest=12.0, max_dur=0.8, bus="sfx", cat2="element")
def swirl_air(r):
    n = ns(0.62)
    t = tarr(n)
    sw = whoosh(r, 0.62, [(0, 700), (0.28, 2400), (0.62, 900)], q=1.8, p=2.0, qq=2.0, body=0.15, air=0.4, flutter=0.15)
    rot = 0.7 + 0.3 * np.sin(TWO_PI * 6.5 * t + 0.4)
    out = mixn(n, (sw * rot, 1.0), (thump(0.15, 170, 90, 0.015, 0.035, drive=1.6), 0.28))
    place(out, thump(0.12, 160, 88, 0.015, 0.03, drive=1.5), 0.3, 0.22, check=False)
    return out


# ---------------------------------------------------------------------------------------------------------------
# breath (non-vocal: shaped noise only) and cloth
# ---------------------------------------------------------------------------------------------------------------


def _breath(r, dur, formants, qs, att, tau, lo, hi, pressure=0.0, rise=False):
    n = ns(dur)
    t = tarr(n)
    base = fnoise(r, n, lo=lo, hi=hi)
    voc = np.zeros(n)
    for f, q in zip(formants, qs):
        fv = f * (1 + 0.12 * (t / dur)) if rise else f
        voc += norm_std(tv_biquad(base, "bp", fv, q))
    env = ad(t, att, tau)
    out = (0.5 * norm_std(base) + 0.8 * norm_std(voc)) * env
    if pressure:
        out += pressure * fit(thump(0.1, 160, 95, 0.015, 0.03, drive=1.4), n)
    return out


@sfx("breath_exhale_light", "movement", -32, 2, "Short light exhale (shaped noise, no voice): 'hah' formants.",
     fade_out=0.05, max_dur=0.5, bus="sfx", cat2="element")
def breath_exhale_light(r):
    return _breath(r, 0.28, (850, 1500, 2700), (2.6, 3.0, 3.5), 0.012, 0.07, 350, 6500)


@sfx("breath_exhale_heavy", "movement", -28, 2, "Heavy forced exhale on a hard hit or a big strike: low 'huh' with pressure.",
     fade_out=0.08, max_dur=0.7, bus="sfx", cat2="element")
def breath_exhale_heavy(r):
    return _breath(r, 0.5, (600, 1100, 2300), (2.2, 2.6, 3.0), 0.02, 0.14, 250, 5200, pressure=0.35)


@sfx("breath_inhale", "movement", -34, 1, "Quick inhale before a charged move: rising soft hiss.",
     fade_out=0.08, max_dur=0.6, bus="sfx", cat2="element")
def breath_inhale(r):
    n = ns(0.4)
    t = tarr(n)
    env = swell(n, 1.8, 1.4)
    f = 900 * 2 ** (0.9 * t / 0.4)
    return norm_std(tv_biquad(white(r, n), "bp", f, 1.8)) * env + 0.3 * fnoise(r, n, lo=3000, hi=8000) * env


@sfx("breath_sharp", "movement", -29, 3, "Fire breath burst: a sharp 'tsh' of air with a clipped onset, for snapping strikes.",
     fade_out=0.03, max_dur=0.3, bus="sfx", cat2="element")
def breath_sharp(r):
    n = ns(0.18)
    return mixn(n, (burst(r, 0.16, lo=1800, hi=9000, att=0.001, tau=0.03), 0.9),
                (_breath(r, 0.16, (1100, 2400), (2.5, 3.0), 0.004, 0.035, 400, 7000), 0.35))


def _rustle(r, dur, lo, hi, rate, tau_pts, level=1.0):
    n = ns(dur)
    t = tarr(n)
    body = fnoise(r, n, lo=lo, hi=hi) * swell(n, 1.4, 1.8) * (0.5 + 0.5 * np.clip(fnoise(r, n, hi=30), -1, 2))
    g = np.zeros(n)
    times = np.sort(r.uniform(0.0, dur * 0.95, int(rate * dur)))
    crackle(r, g, times, lo * 1.5, hi, tau_pts[0], tau_pts[1], amp=0.8, power=1.6)
    return level * (body + 0.8 * g)


for _i in (1, 2, 3):
    sfx(f"cloth_rustle_{_i}", "movement", -36, 3, "Soft cloth movement (fabric friction ticks over a shaped hiss); rotate the variants.",
        fade_out=0.06, crest=12.0, max_dur=0.6, bus="sfx", cat2="element")(
        lambda r, i=_i: _rustle(r, 0.24 + 0.05 * i, 1200 + 300 * i, 6500, 90 - 10 * i, (0.0006, 0.003)))


@sfx("cloth_snap", "movement", -26, 2, "Sleeve / sash snap at the end of a fast strike: crisp cloth crack.", fade_out=0.04,
     crest=14.0, max_dur=0.3, bus="sfx", cat2="element")
def cloth_snap(r):
    n = ns(0.2)
    return mixn(n, (click(r, 0.0012, lo=2800), 0.8), (burst(r, 0.1, lo=1500, hi=7500, att=0.0003, tau=0.025), 0.9),
                (dsine(1900, 0.012), 0.3), (_rustle(r, 0.2, 2000, 7000, 70, (0.0006, 0.002)), 0.3))


@sfx("cloth_flap", "movement", -30, 2, "Loose fabric flutter (sash and hem) after a dash or a fast turn.", fade_out=0.08,
     crest=12.0, max_dur=0.6, bus="sfx", cat2="element")
def cloth_flap(r):
    n = ns(0.5)
    t = tarr(n)
    fl = fnoise(r, n, lo=700, hi=4200) * (0.55 + 0.45 * np.sin(TWO_PI * (24 - 10 * t / 0.5) * t)) * ad(t, 0.01, 0.16)
    return fl + 0.4 * _rustle(r, 0.5, 1500, 6000, 80, (0.0006, 0.002), 0.5) * ad(t, 0.005, 0.2)


@sfx("cloth_wrap", "movement", -30, 2, "Getting up / taking a stance: heavier fabric rustle with a soft body press.", fade_out=0.08,
     crest=12.0, max_dur=0.7, bus="sfx", cat2="element")
def cloth_wrap(r):
    n = ns(0.5)
    out = mixn(n, (_rustle(r, 0.5, 700, 4200, 70, (0.001, 0.004)), 1.0),
               (thump(0.2, 130, 80, 0.03, 0.05, drive=1.3), 0.4))
    return out


# ---------------------------------------------------------------------------------------------------------------
# footsteps per surface (stone steps are the 4 base sounds step_stone_1..4)
# ---------------------------------------------------------------------------------------------------------------


def _grains(r, n, t0, t1, count, lo, hi, tau, amp=1.0, power=1.4):
    g = np.zeros(n)
    times = t0 + (t1 - t0) * np.sort(r.random(count) ** power)
    crackle(r, g, times, lo, hi, tau[0], tau[1], amp=amp, power=1.5)
    return g


def _step_sand(r, v):
    n = ns(0.3)
    base = 95 + 12 * v
    out = mixn(n, (thump(0.14, base * 1.3, base * 0.8, 0.02, 0.04, drive=1.6), 0.55),
               (burst(r, 0.14, lo=500, hi=2800, att=0.004, tau=0.04), 0.6),
               (_grains(r, n, 0.0, 0.2, 90, 2200, 7500, (0.0004, 0.0016), 0.7), 0.55),
               (burst(r, 0.2, lo=3500, hi=9000, att=0.01, tau=0.05), 0.12))
    toe = mixn(n, (burst(r, 0.1, lo=700, hi=3500, att=0.003, tau=0.03), 0.4),
               (_grains(r, n, 0.0, 0.08, 40, 2500, 8000, (0.0004, 0.0012), 0.6), 0.5))
    place(out, toe[:ns(0.12)], 0.07, 0.55, check=False)
    return out


def _step_wood(r, v):
    n = ns(0.3)
    f0 = (150, 175, 132)[v - 1]
    ring = modal([f0 * 1.0, f0 * 2.31, f0 * 3.9, f0 * 5.6], [0.8, 0.45, 0.22, 0.1], [0.05, 0.035, 0.022, 0.014], dur=0.25)
    out = mixn(n, (thump(0.12, f0 * 1.4, f0 * 0.8, 0.012, 0.03, drive=1.5), 0.6), (ring, 0.7),
               (click(r, 0.0018, lo=1800), 0.5), (burst(r, 0.06, lo=500, hi=3200, att=0.0004, tau=0.012), 0.5))
    place(out, mixn(ns(0.12), (thump(0.08, f0 * 1.6, f0, 0.01, 0.02, drive=1.3), 0.4), (click(r, 0.0015, lo=2400), 0.3)),
          0.06, 0.45, check=False)
    return reverb(r, out, 0.18, 0.1, 5000, 0.004)


def _step_metal(r, v):
    n = ns(0.36)
    f0 = (210, 245, 188)[v - 1]
    # thin plate: inharmonic modal ring (hollow, a little 'tonk')
    ring = modal([f0, f0 * 2.43, f0 * 3.87, f0 * 5.6, f0 * 8.1], [0.7, 0.5, 0.3, 0.2, 0.1],
                 [0.09, 0.06, 0.04, 0.025, 0.015], dur=0.32)
    out = mixn(n, (thump(0.1, f0 * 1.2, f0 * 0.7, 0.01, 0.025, drive=1.4), 0.45), (ring, 0.7),
               (click(r, 0.0015, lo=3000), 0.55), (burst(r, 0.05, lo=1500, hi=6500, att=0.0003, tau=0.009), 0.5))
    place(out, mixn(ns(0.14), (ring[:ns(0.14)], 0.3), (click(r, 0.0012, lo=3300), 0.35)), 0.065, 0.55, check=False)
    return reverb(r, out, 0.22, 0.12, 7000, 0.003)


def _bubble_cluster(r, n, t0, count, f_lo, f_hi, amp=0.6, span=0.15):
    g = np.zeros(n)
    for _ in range(count):
        f = float(np.exp(r.uniform(np.log(f_lo), np.log(f_hi))))
        add_at(g, dsine(f, r.uniform(0.008, 0.025), g=r.uniform(0.3, 1.0), ln=6.0),
               ns(t0 + r.uniform(0, span)), amp * r.uniform(0.3, 1.0), check=False)
    return g


def _step_water(r, v):
    n = ns(0.55)
    out = mixn(n, (thump(0.2, 150 + 20 * v, 70, 0.03, 0.07, drive=1.2), 0.35),
               (burst(r, 0.3, lo=300, hi=3800, att=0.004, tau=0.09), 0.9),
               (burst(r, 0.3, lo=3500, hi=9500, att=0.003, tau=0.05), 0.25),
               (_bubble_cluster(r, n, 0.01, 7, 350, 1400, 0.7, 0.2), 0.8),
               (_bubble_cluster(r, n, 0.12, 6, 900, 3000, 0.4, 0.3), 0.5))
    drops = np.zeros(n)
    for _ in range(5):
        add_at(drops, dsine(r.uniform(2500, 5500), 0.01, g=0.5), ns(r.uniform(0.1, 0.45)), r.uniform(0.15, 0.4), check=False)
    return out + 0.7 * drops


def _step_puddle(r, v):
    n = ns(0.3)
    return mixn(n, (burst(r, 0.14, lo=700, hi=5200, att=0.002, tau=0.035), 0.9),
                (_bubble_cluster(r, n, 0.005, 4, 900, 2800, 0.6, 0.1), 0.7),
                (thump(0.1, 200, 110, 0.015, 0.03, drive=1.2), 0.4), (click(r, 0.002, lo=2800), 0.3))


def _step_ice(r, v):
    n = ns(0.3)
    sq = tone(2400 + 300 * v, 0.1, 0.02, ((1, 1.0, 1.0), (2.3, 0.3, 0.5)), att=0.004, bend=-0.12, bend_tau=0.03)
    return mixn(n, (thump(0.1, 220, 120, 0.012, 0.025, drive=1.3), 0.4), (click(r, 0.0015, lo=3500), 0.6),
                (burst(r, 0.12, lo=2500, hi=10000, att=0.002, tau=0.03), 0.65),
                (_grains(r, n, 0.0, 0.14, 40, 3500, 11000, (0.0005, 0.0018), 0.7), 0.6), (sq, 0.14))


def _step_mud(r, v):
    n = ns(0.4)
    t = tarr(n)
    f = 260 * 2 ** (1.2 * np.clip(t / 0.18, 0, 1)) * (1 + 0.1 * v)
    sl = norm_std(tv_biquad(white(r, n), "bp", f, 3.0)) * ad(t, 0.01, 0.07)
    return mixn(n, (thump(0.2, 130, 62, 0.03, 0.08, drive=2.0), 0.7), (sl, 0.6),
                (burst(r, 0.2, lo=200, hi=1600, att=0.004, tau=0.05), 0.6),
                (_bubble_cluster(r, n, 0.04, 4, 250, 700, 0.8, 0.2), 0.5))


_SURF = {
    "sand": (_step_sand, 3, -29, "Dry sand step: soft thud with a grain hiss."),
    "wood": (_step_wood, 3, -27, "Wooden boards: hollow knock with a short ring (pavilion floors)."),
    "metal": (_step_metal, 3, -28, "Metal plate: thin hollow ring with a click."),
    "water": (_step_water, 3, -24, "Wading step in the pool: splash, bubbles and droplets."),
    "puddle": (_step_puddle, 2, -29, "Shallow puddle step: thin slap with a few bubbles."),
    "ice": (_step_ice, 2, -29, "Ice step: dry crunch with a glassy squeak."),
    "mud": (_step_mud, 2, -26, "Mud step: low thud with a wet squelch."),
}
for _s, (_fn, _cnt, _lvl, _txt) in _SURF.items():
    for _v in range(1, _cnt + 1):
        sfx(f"step_{_s}_{_v}", "movement", _lvl, 2, _txt + " Rotate the variants.", fade_out=0.04, crest=14.0,
            max_dur=0.7, bus="sfx", cat2="element", lowcut=110.0)(lambda r, f=_fn, v=_v: f(r, v))


def _land(r, kind):
    base = {"sand": 110, "wood": 210, "metal": 240, "ice": 210, "mud": 110, "water": 90}[kind]
    n = ns(0.7)
    if kind == "water":
        out = mixn(n, (thump(0.4, 140, 60, 0.04, 0.1, drive=1.6), 0.6), (burst(r, 0.4, lo=250, hi=4200, att=0.006, tau=0.12), 0.95),
                   (_bubble_cluster(r, n, 0.01, 12, 300, 2200, 0.7, 0.35), 0.8), (burst(r, 0.4, lo=3000, hi=9000, att=0.004, tau=0.07), 0.25))
        return reverb(r, out, 0.3, 0.12, 6000)
    if kind == "sand":
        return mixn(n, (thump(0.35, base * 1.4, base * 0.6, 0.04, 0.09, drive=2.0), 0.7),
                    (burst(r, 0.3, lo=250, hi=3200, att=0.004, tau=0.08), 0.8),
                    (_grains(r, n, 0.0, 0.4, 160, 1800, 7000, (0.0005, 0.002), 0.7), 0.6))
    if kind == "mud":
        return mixn(n, (thump(0.35, 150, 55, 0.04, 0.1, drive=2.4), 0.8), (burst(r, 0.3, lo=150, hi=1500, att=0.006, tau=0.08), 0.8),
                    (_bubble_cluster(r, n, 0.05, 6, 200, 700, 0.8, 0.3), 0.6))
    ring = modal([base, base * 2.43, base * 3.87, base * 5.6], [0.7, 0.5, 0.3, 0.15], [0.12, 0.08, 0.05, 0.03], dur=0.5)
    out = mixn(n, (thump(0.3, base * 1.5, base * 0.6, 0.03, 0.08, drive=2.2), 0.8), (ring, 0.5 if kind != "wood" else 0.6),
               (click(r, 0.003, lo=1800), 0.5), (burst(r, 0.14, lo=300, hi=4500, att=0.0006, tau=0.025), 0.7))
    if kind == "ice":
        out += 0.5 * _grains(r, n, 0.0, 0.25, 40, 3500, 11000, (0.0005, 0.002), 0.8)
    return reverb(r, out, 0.22, 0.12, 6000, 0.004)


for _k, _lvl in (("sand", -22), ("wood", -21), ("metal", -20), ("ice", -21), ("mud", -22), ("water", -19)):
    sfx(f"land_{_k}", "movement", _lvl, 3, f"Landing on {_k}: body weight thud with a {_k}-specific tail.", fade_out=0.08,
        crest=15.0, max_dur=0.9, bus="sfx", cat2="element")(lambda r, k=_k: _land(r, k))
