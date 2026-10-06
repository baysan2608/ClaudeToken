"""Tonal sounds: per-element charge risers (tier 1-3), the UI family, round / KO / combo / challenge cues, wind chimes.

Everything shares the D-pentatonic centre of the base set (D E F# A B) so cues sit together as one family:
    earth  low and woody (marimba-like bars, A3 - D4 - A4)         water  falling droplet ripple (A5 - F#5 - D5, FM bells)
    fire   quick bright pluck arpeggio (D4 - F#4 - A4 - D5)        air    breathy flute-like line with a whistle rise (A5 - E6 - A6)
UI sounds are soft, wooden and bell-like (a mallet on a hollow block, a small bowl bell): never a beep.
"""
from __future__ import annotations

import numpy as np

import synth_sfx as S
from synth_sfx import pthump as thump  # noqa: F401  (phone-safe thump)
from synth_sfx import (SR, TWO_PI, add_at, ad, burst, click, crackle, curve, dsine, filt, fit, fm_bell, fnoise, mixn, modal,
                       ns, place, reverb, swell, tarr, tone, tv_biquad, warm, white, norm_std, sfx)

D3, A3, D4, E4, FS4, A4, B4 = 146.83, 220.0, 293.66, 329.63, 369.99, 440.0, 493.88
D5, E5, FS5, A5, B5 = 587.33, 659.26, 739.99, 880.0, 987.77
D6, E6, FS6, A6 = 1174.66, 1318.51, 1479.98, 1760.0


def wood_note(r, f, dur=0.35, tau=0.16, bright=1.0):
    """Mallet on a tuned wooden bar: 1 : 3.9 : 9.2 modes and a soft click."""
    ring = modal([f, f * 3.9, f * 9.2], [1.0, 0.28 * bright, 0.07 * bright], [tau, tau * 0.3, tau * 0.12], dur=dur)
    tk = burst(r, 0.02, lo=1500, hi=5000, att=0.0002, tau=0.004)
    return mixn(len(ring), (ring, 1.0), (tk, 0.18 * bright))


def bowl_bell(f, dur=1.0, tau=0.5, bright=1.0):
    """Soft small bell / singing-bowl: warm partials 1 : 2.01 : 2.99 : 4.2 with a beating twin."""
    parts = [(1.0, 1.0, 1.0), (1.004, 0.55, 0.9), (2.01, 0.45 * bright, 0.6), (2.99, 0.25 * bright, 0.4),
             (4.2, 0.12 * bright, 0.25), (5.4, 0.06 * bright, 0.15)]
    return modal([f * m for m, _, _ in parts], [a for _, a, _ in parts], [tau * k for _, _, k in parts], dur=dur)


def flute(r, f, dur=0.4, att=0.04, tau=0.15, breath=0.35, vib=5.5, vib_depth=0.004):
    n = ns(dur)
    t = tarr(n)
    fr = f * (1 + vib_depth * np.sin(TWO_PI * vib * t) * np.clip(t / 0.1, 0, 1))
    ph = TWO_PI * np.cumsum(fr) / SR
    y = np.sin(ph) + 0.18 * np.sin(2 * ph) + 0.05 * np.sin(3 * ph)
    env = ad(t, att, tau)
    br = norm_std(tv_biquad(white(r, n), "bp", f * 2.0, 3.0)) * env
    return (y * env + breath * 0.5 * br)


def pluck(r, f, dur=0.35, tau=0.11, nh=6):
    harm = tuple((k, 1.0 / k ** 1.1, 1.0 / (0.8 + 0.5 * k)) for k in range(1, nh + 1))
    return mixn(ns(max(dur, 6 * tau)), (tone(f, dur, tau, harm, att=0.003, bend=0.02, bend_tau=0.02), 1.0),
                (burst(r, 0.02, lo=2500, hi=9000, att=0.0002, tau=0.004), 0.12))


def droplet(r, f, dur=0.5, tau=0.14):
    n = ns(dur)
    t = tarr(n)
    bell = fm_bell(f, dur, tau, 1.0, 1.4, 0.07, 0.002)
    chirp = dsine(f * 1.5, 0.02, g=0.5, ln=6.0)
    return mixn(n, (bell, 1.0), (chirp, 0.25))


# ---------------------------------------------------------------------------------------------------------------
# charge tiers per element
# ---------------------------------------------------------------------------------------------------------------


def _riser(r, n, t0, t1, f0, f1, gain):
    """Band-passed noise sweep rising from f0 to f1 over [t0, t1] (the 'energy gathers' layer)."""
    m = ns(t1 - t0)
    sw = norm_std(tv_biquad(white(r, m), "bp", curve(m, [(0, f0), (t1 - t0, f1)], log=True), 3.0)) * swell(m, 1.8, 2.2)
    out = np.zeros(n)
    place(out, sw, t0, gain, check=False)
    return out


def _charge(r, el, tier):
    seqs = {
        "earth": ([A3], [A3, D4], [A3, D4, A4]),
        "water": ([A5], [A5, FS5], [A5, FS5, D5]),
        "fire": ([FS4], [D4, FS4, A4], [D4, FS4, A4, D5]),
        "air": ([A5], [A5, E6], [A5, E6, A6]),
    }
    gap = {"earth": 0.115, "water": 0.12, "fire": 0.055, "air": 0.10}[el]
    notes = seqs[el][tier - 1]
    dur = {1: 0.38, 2: 0.55, 3: 0.85}[tier]
    n = ns(dur)
    out = np.zeros(n)
    for i, f in enumerate(notes):
        g = 0.75 + 0.25 * i / max(len(notes) - 1, 1)
        if el == "earth":
            v = wood_note(r, f, 0.5, 0.22)
        elif el == "water":
            v = droplet(r, f, 0.55, 0.15 + 0.03 * tier)
        elif el == "fire":
            v = pluck(r, f, 0.4, 0.1 + 0.02 * tier)
        else:
            v = flute(r, f, 0.5, 0.035, 0.16 + 0.04 * tier)
        place(out, v, gap * i, g * 0.9)
    if el == "earth":
        place(out, thump(0.3, 150, 62, 0.03, 0.08 + 0.02 * tier, drive=2.0), 0.0, 0.25 + 0.12 * tier)
        if tier >= 3:
            place(out, bowl_bell(A3, 0.8, 0.3, 0.6), 0.2, 0.25)
    elif el == "water":
        for k in range(tier):
            place(out, dsine(2200 * (1 + 0.25 * k), 0.012, g=0.6, ln=6.0), 0.05 + 0.09 * k, 0.12)
        if tier >= 3:
            for f in (D6, FS6, A6):
                place(out, dsine(f, 0.09), 0.3 + 0.05 * (f > D6), 0.12)
    elif el == "fire":
        sp = np.zeros(n)
        crackle(r, sp, np.sort(r.uniform(0.0, dur * 0.8, 6 * tier)), 2500, 9000, 0.001, 0.006, amp=0.8, power=1.5)
        out += (0.1 + 0.05 * tier) * sp
    else:
        if tier >= 3:
            place(out, tone(A6 * 1.0, 0.45, 0.2, ((1, 1.0, 1.0),), att=0.12, bend=-0.03, bend_tau=0.4), 0.2, 0.12)
    if tier >= 2:
        f0, f1 = {"earth": (300, 900), "water": (1200, 4500), "fire": (1500, 7000), "air": (2500, 9000)}[el]
        out += _riser(r, n, 0.0, dur * 0.85, f0, f1, 0.025 + 0.015 * tier)
    if tier >= 3:
        # the 'ready' glint: a bright two-partial shimmer at the end
        gl = np.zeros(n)
        for i, f in enumerate((D6 * 2, A6 * 1.5, FS6 * 2)):
            place(gl, dsine(f, 0.12 - 0.02 * i), dur * 0.55 + 0.04 * i, 0.5, check=False)
        out += 0.18 * gl
        place(out, thump(0.2, 200, 100, 0.02, 0.05, drive=1.8), 0.0, 0.25)
    rt = {"earth": 0.35, "water": 0.5, "fire": 0.3, "air": 0.45}[el]
    return reverb(r, out, rt, 0.14 + 0.03 * tier, 8000, 0.004)


_CH_LEVEL = {1: -25, 2: -22, 3: -18}
_CH_MAXD = {1: 0.5, 2: 0.7, 3: 1.1}
_CH_NOTE = {"earth": "low woody bars (A3, D4, A4) over a rooted thump", "water": "falling droplet bells (A5, F#5, D5) with ripple glints",
            "fire": "quick bright plucks (D4, F#4, A4, D5) with a crackle of sparks", "air": "breathy flute line (A5, E6, A6) and a whistle rise"}
for _el in ("earth", "water", "fire", "air"):
    for _t in (1, 2, 3):
        sfx(f"charge_{_el}_t{_t}", "system", _CH_LEVEL[_t], 2,
            f"Charge tier {_t} for {_el}: {_CH_NOTE[_el]}; one cue per tier reached.", fade_out=0.06 + 0.03 * _t, crest=16.0,
            max_dur=_CH_MAXD[_t], bus="sfx", cat2="element", gap=0.1)(lambda r, e=_el, t=_t: _charge(r, e, t))

# ---------------------------------------------------------------------------------------------------------------
# UI family
# ---------------------------------------------------------------------------------------------------------------


def _ui(name, level, notes, voices=3, **kw):
    def deco(fn):
        sfx(name, "ui", level, voices, notes, bus="ui", cat2="ui", **kw)(fn)
        return fn
    return deco


@_ui("ui_tap", -25, "Soft wooden tick for any tap.", voices=4, fade_out=0.03, max_dur=0.3, crest=10.0)
def ui_tap(r):
    n = ns(0.14)
    return mixn(n, (wood_note(r, 1050.0, 0.12, 0.03, 0.9), 1.0), (click(r, 0.001, lo=3500), 0.15))


@_ui("ui_select", -24, "Confirm: wooden tok followed by a small warm bell (A5).", fade_out=0.1, max_dur=0.6, crest=12.0)
def ui_select(r):
    n = ns(0.42)
    out = mixn(n, (wood_note(r, 760.0, 0.14, 0.04, 0.9), 0.9))
    place(out, bowl_bell(A5, 0.4, 0.22, 0.7), 0.05, 0.6)
    return reverb(r, out, 0.2, 0.1, 7000, 0.003)


@_ui("ui_back", -26, "Back / cancel: a lower tok and a short falling bell (E5 to D5).", fade_out=0.1, max_dur=0.6, crest=12.0)
def ui_back(r):
    n = ns(0.38)
    out = mixn(n, (wood_note(r, 540.0, 0.12, 0.035, 0.8), 0.9))
    place(out, bowl_bell(E5, 0.3, 0.13, 0.6), 0.04, 0.4)
    place(out, bowl_bell(D5, 0.3, 0.16, 0.6), 0.1, 0.5)
    return reverb(r, out, 0.2, 0.1, 6000, 0.003)


@_ui("ui_open", -25, "Panel open: rising two-note soft bell (D5, A5) with a light air swish.", fade_out=0.12, max_dur=0.7, crest=12.0)
def ui_open(r):
    n = ns(0.5)
    out = np.zeros(n)
    place(out, bowl_bell(D5, 0.45, 0.2, 0.7), 0.0, 0.8)
    place(out, bowl_bell(A5, 0.4, 0.2, 0.7), 0.09, 0.8)
    sw = norm_std(tv_biquad(white(r, ns(0.3)), "bp", curve(ns(0.3), [(0, 900), (0.3, 3200)], log=True), 1.6)) * swell(ns(0.3), 1.6, 1.8)
    place(out, sw, 0.0, 0.045, check=False)
    return reverb(r, out, 0.25, 0.12, 7000, 0.003)


@_ui("ui_close", -26, "Panel close: falling two-note soft bell (A5, D5) and a short swish.", fade_out=0.12, max_dur=0.7, crest=12.0)
def ui_close(r):
    n = ns(0.45)
    out = np.zeros(n)
    place(out, bowl_bell(A5, 0.35, 0.16, 0.7), 0.0, 0.7)
    place(out, bowl_bell(D5, 0.4, 0.2, 0.7), 0.08, 0.8)
    sw = norm_std(tv_biquad(white(r, ns(0.25)), "bp", curve(ns(0.25), [(0, 3000), (0.25, 900)], log=True), 1.6)) * swell(ns(0.25), 1.2, 1.9)
    place(out, sw, 0.0, 0.04, check=False)
    return reverb(r, out, 0.25, 0.12, 6500, 0.003)


@_ui("ui_toggle", -26, "Toggle: two tiny wooden ticks, the second a little higher (switch click).", voices=4, fade_out=0.04,
     max_dur=0.3, crest=10.0)
def ui_toggle(r):
    n = ns(0.16)
    out = mixn(n, (wood_note(r, 880.0, 0.1, 0.025, 0.8), 0.8))
    place(out, wood_note(r, 1320.0, 0.1, 0.025, 0.8), 0.045, 0.7)
    return out


@_ui("ui_ring_open", -25, "Sub-element ring opens: a quick rising roll of four wooden ticks with a shimmer.", fade_out=0.08,
     max_dur=0.5, crest=12.0)
def ui_ring_open(r):
    n = ns(0.34)
    out = np.zeros(n)
    for i, f in enumerate((D5, FS5, A5, D6)):
        place(out, wood_note(r, f, 0.15, 0.05, 0.8), 0.045 * i, 0.55 + 0.1 * i)
    place(out, bowl_bell(D6, 0.3, 0.14, 0.5), 0.14, 0.25)
    return reverb(r, out, 0.2, 0.1, 7000, 0.003)


@_ui("ui_ring_pick", -24, "Sub-element picked from the ring: a crisp small bell (F#5).", voices=3, fade_out=0.08, max_dur=0.5, crest=12.0)
def ui_ring_pick(r):
    n = ns(0.32)
    out = mixn(n, (bowl_bell(FS5, 0.3, 0.14, 0.9), 0.9), (wood_note(r, 900.0, 0.1, 0.02, 0.8), 0.4))
    return reverb(r, out, 0.15, 0.1, 7000, 0.003)


@_ui("ui_error", -25, "Not allowed: two soft low wooden knocks, no buzz.", fade_out=0.08, max_dur=0.5, crest=12.0)
def ui_error(r):
    n = ns(0.3)
    out = np.zeros(n)
    place(out, wood_note(r, 330.0, 0.14, 0.05, 0.6), 0.0, 0.9)
    place(out, wood_note(r, 300.0, 0.14, 0.05, 0.6), 0.12, 0.8)
    return reverb(r, out, 0.12, 0.08, 4500, 0.003)


@_ui("ui_pause", -24, "Pause: descending soft bell chord (A5, D5, A4) settling.", voices=1, fade_out=0.2, max_dur=0.9, crest=12.0)
def ui_pause(r):
    n = ns(0.7)
    out = np.zeros(n)
    for i, (f, g) in enumerate(((A5, 0.7), (D5, 0.8), (A4, 0.9))):
        place(out, bowl_bell(f, 0.55, 0.25, 0.7), 0.1 * i, g)
    return reverb(r, out, 0.4, 0.15, 6500, 0.004)


@_ui("ui_resume", -24, "Resume: ascending soft bell chord (A4, D5, A5).", voices=1, fade_out=0.2, max_dur=0.9, crest=12.0)
def ui_resume(r):
    n = ns(0.6)
    out = np.zeros(n)
    for i, (f, g) in enumerate(((A4, 0.8), (D5, 0.8), (A5, 0.9))):
        place(out, bowl_bell(f, 0.45, 0.2, 0.8), 0.08 * i, g)
    return reverb(r, out, 0.35, 0.14, 7000, 0.004)


@_ui("ui_toast", -26, "Toast / notice: one soft bell with a wooden tick (D6).", voices=2, fade_out=0.12, max_dur=0.7, crest=12.0)
def ui_toast(r):
    n = ns(0.5)
    out = mixn(n, (bowl_bell(D6, 0.5, 0.25, 0.8), 0.8), (wood_note(r, 1200.0, 0.08, 0.02, 0.7), 0.4))
    return reverb(r, out, 0.25, 0.12, 7500, 0.003)


# --- round / KO / combo / challenge ---------------------------------------------------------------------------------


def church_bell(f, dur, tau):
    """Low hand-bell-like voice with the minor-third 'tierce' that makes a bell a bell (partials of a real bell)."""
    ratios = [0.5, 1.0, 1.183, 1.506, 2.0, 2.514, 3.011, 4.166]
    amps = [0.35, 1.0, 0.75, 0.5, 0.6, 0.3, 0.2, 0.1]
    taus = [tau * k for k in (1.3, 1.0, 0.85, 0.6, 0.5, 0.3, 0.22, 0.12)]
    return modal([f * m for m in ratios], amps, taus, dur=dur)


@sfx("ko_bell", "system", -17, 1, "Knock-out: a deep soft temple bell (D3 with the minor-third partial) over a body thud and "
     "a low swell. Heavy but not harsh.", fade_out=0.35, crest=14.0, max_dur=2.2, bus="sfx", cat2="element", gap=1.0)
def ko_bell(r):
    n = ns(2.0)
    out = mixn(n, (church_bell(D3 * 1.5, 1.9, 0.55), 0.9), (thump(0.5, 150, 58, 0.04, 0.12, drive=2.2), 0.7),
               (burst(r, 0.3, lo=150, hi=1500, att=0.004, tau=0.06), 0.4))
    sw = fnoise(r, n, lo=100, hi=900) * swell(n, 0.7, 1.3)
    out += 0.12 * sw
    return reverb(r, out, 0.9, 0.2, 5000, 0.006)


@sfx("round_start", "system", -20, 1, "Round start: two wooden clapper knocks, then a bell chord (D5, A5) over a soft air swell.",
     fade_out=0.2, crest=14.0, max_dur=1.4, bus="sfx", cat2="element", gap=1.0)
def round_start(r):
    n = ns(1.1)
    out = np.zeros(n)
    place(out, wood_note(r, 620.0, 0.2, 0.06, 1.0), 0.0, 0.9)
    place(out, wood_note(r, 620.0, 0.2, 0.06, 1.0), 0.15, 1.0)
    place(out, bowl_bell(D5, 0.9, 0.4, 0.8), 0.32, 0.8)
    place(out, bowl_bell(A5, 0.8, 0.35, 0.8), 0.36, 0.7)
    sw = fnoise(r, ns(0.6), lo=800, hi=4000) * swell(ns(0.6), 1.6, 1.8)
    place(out, sw, 0.2, 0.08, check=False)
    return reverb(r, out, 0.6, 0.18, 7000, 0.005)


@sfx("round_reset", "system", -23, 1, "Round reset: a soft falling chime (A5, F#5, D5).", fade_out=0.15, crest=12.0, max_dur=1.0,
     bus="sfx", cat2="element", gap=1.0)
def round_reset(r):
    n = ns(0.8)
    out = np.zeros(n)
    for i, f in enumerate((A5, FS5, D5)):
        place(out, bowl_bell(f, 0.6, 0.25, 0.7), 0.13 * i, 0.9 - 0.1 * i)
    return reverb(r, out, 0.45, 0.16, 7000, 0.004)


@sfx("victory", "system", -19, 1, "Match won: a warm bell phrase D5 F#5 A5 D6 over a soft sustained triad.", fade_out=0.25,
     crest=14.0, max_dur=2.0, bus="sfx", cat2="element", gap=2.0)
def victory(r):
    n = ns(1.7)
    out = np.zeros(n)
    for i, f in enumerate((D5, FS5, A5, D6)):
        place(out, bowl_bell(f, 1.0, 0.45, 0.9), 0.13 * i, 0.8)
    for f in (D4, A4, FS5):
        place(out, tone(f, 1.4, 0.7, ((1, 1.0, 1.0), (2, 0.15, 0.5)), att=0.15), 0.3, 0.12)
    return reverb(r, out, 0.8, 0.2, 7500, 0.006)


@sfx("scenario_start", "ui", -24, 1, "A scenario / level starts: a soft bell (D5) and a breath of air.", fade_out=0.15, crest=12.0,
     max_dur=1.0, bus="ui", cat2="ui", gap=0.5)
def scenario_start(r):
    n = ns(0.8)
    out = mixn(n, (bowl_bell(D5, 0.7, 0.35, 0.8), 0.8))
    place(out, fnoise(r, ns(0.4), lo=900, hi=3500) * swell(ns(0.4), 1.5, 1.7), 0.0, 0.08, check=False)
    return reverb(r, out, 0.4, 0.14, 7000, 0.004)


@sfx("combo_start", "ui", -27, 2, "Combo practice started: a small rising wooden tick pair.", fade_out=0.05, crest=10.0,
     max_dur=0.4, bus="ui", cat2="ui")
def combo_start(r):
    n = ns(0.22)
    out = mixn(n, (wood_note(r, 700.0, 0.1, 0.03), 0.8))
    place(out, wood_note(r, 1050.0, 0.1, 0.03), 0.07, 0.8)
    return out


@sfx("combo_step", "ui", -27, 3, "Combo step landed: one short bell (A5).", fade_out=0.06, crest=10.0, max_dur=0.4, bus="ui", cat2="ui")
def combo_step(r):
    return mixn(ns(0.26), (bowl_bell(A5, 0.25, 0.1, 0.8), 0.8), (wood_note(r, 1100.0, 0.06, 0.015), 0.3))


@sfx("combo_success", "ui", -23, 1, "Combo completed: three rising bells (D5, F#5, A5) with a bright top.", fade_out=0.12, crest=12.0,
     max_dur=0.9, bus="ui", cat2="ui")
def combo_success(r):
    n = ns(0.7)
    out = np.zeros(n)
    for i, f in enumerate((D5, FS5, A5)):
        place(out, bowl_bell(f, 0.5, 0.22, 0.9), 0.08 * i, 0.85)
    place(out, bowl_bell(D6, 0.4, 0.2, 0.7), 0.24, 0.45)
    return reverb(r, out, 0.35, 0.14, 7500, 0.004)


@sfx("combo_fail", "ui", -27, 1, "Combo dropped: two soft low knocks, descending.", fade_out=0.08, crest=10.0, max_dur=0.5,
     bus="ui", cat2="ui")
def combo_fail(r):
    n = ns(0.3)
    out = mixn(n, (wood_note(r, 420.0, 0.12, 0.04, 0.6), 0.8))
    place(out, wood_note(r, 330.0, 0.12, 0.045, 0.6), 0.1, 0.8)
    return out


@sfx("challenge_done", "ui", -22, 1, "Challenge completed: wooden tick, then bells F#5, A5 and D6.", fade_out=0.15, crest=12.0,
     max_dur=1.1, bus="ui", cat2="ui")
def challenge_done(r):
    n = ns(0.9)
    out = mixn(n, (wood_note(r, 900.0, 0.1, 0.02), 0.5))
    for i, f in enumerate((FS5, A5, D6)):
        place(out, bowl_bell(f, 0.7, 0.3, 0.9), 0.07 + 0.1 * i, 0.8)
    return reverb(r, out, 0.5, 0.16, 7500, 0.005)


# --- ambient accents (played at random by the ambience layer) ---------------------------------------------------------


def _tube_chime(f, dur=1.4):
    return modal([f, f * 2.76, f * 5.4], [1.0, 0.45, 0.2], [0.9, 0.5, 0.22], dur=dur)


for _i, _f in enumerate((D5, FS5, A5, E5), 1):
    @sfx(f"chime_wind_{_i}", "ambience", -36, 2, f"Distant wind chime tube ({_f:.0f} Hz), soft strike and a long shimmer.",
         fade_out=0.3, crest=12.0, max_dur=1.8, bus="ambience", cat2="ambience", gap=3.0)
    def _chime(r, f=_f):
        n = ns(1.5)
        out = mixn(n, (_tube_chime(f, 1.4), 1.0), (click(r, 0.001, lo=3000), 0.08))
        return reverb(r, out, 0.8, 0.3, 8000, 0.01)


@sfx("bamboo_knock", "ambience", -34, 1, "A bamboo fountain tipping: a short water trickle then a hollow bamboo knock.", fade_out=0.15,
     crest=12.0, max_dur=1.0, bus="ambience", cat2="ambience", gap=8.0)
def bamboo_knock(r):
    n = ns(0.8)
    t = tarr(n)
    tr = fnoise(r, n, lo=1500, hi=6000) * swell(n, 0.8, 3.0) * 0.3
    out = tr * (t < 0.35)
    kn = modal([520.0, 520 * 2.12, 520 * 3.7, 520 * 5.6], [1.0, 0.45, 0.2, 0.08], [0.07, 0.04, 0.025, 0.015], dur=0.4)
    place(out, kn, 0.34, 1.0)
    place(out, burst(r, 0.03, lo=1200, hi=5000, att=0.0003, tau=0.006), 0.34, 0.4, check=False)
    return reverb(r, out, 0.5, 0.2, 7000, 0.006)
