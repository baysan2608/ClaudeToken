#!/usr/bin/env python3
"""Fourfold procedural SFX synthesizer.

Generates the complete Fourfold sound-effect library from scratch (no samples,
no recordings, no third-party audio) into ``game/assets/audio/``:

* 16-bit mono PCM WAV, 44.1 kHz
* peak-normalised to -3 dBFS (ambience bed: -18 dBFS), short fades, silence trimmed
* loops are periodic by construction (circular spectral noise, integer-cycle LFOs,
  wrapped event scatter, warm-started time-varying filters, phase-aligned Shepard
  glissandi) so the loop seam is continuous without an audible crossfade; each loop
  WAV also carries a ``smpl`` chunk (loop 0..N-1) which Godot reads on import
* fully deterministic: every sound owns a fixed seed (crc32 of its name), so two runs
  produce byte-identical files

Usage (from the repo root):

    python3 tools/audio/synth_sfx.py                 # render everything
    python3 tools/audio/synth_sfx.py --only deflect block
    python3 tools/audio/synth_sfx.py --out /tmp/sfx  # render somewhere else
    python3 tools/audio/synth_sfx.py --list

Requirements: numpy, scipy (see tools/audio/requirements.txt).

Sound design notes
------------------
Everything is layered transient + body + tail.  Low "thuds" are deliberately
saturated (tanh) so that they carry harmonics in the 150-600 Hz range; phone and
tablet speakers cannot reproduce true sub-bass, so the weight has to be implied.
The musical UI/reward material shares one tonal centre (D, pentatonic) so the
element motifs, deflect ping and unlock fanfare sit together as one family.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.interpolate import PchipInterpolator

SR = 44100
MASTER_SEED = 0x4F0F01D
TWO_PI = 2.0 * np.pi

# ---------------------------------------------------------------------------
# registry
# ---------------------------------------------------------------------------


@dataclass
class Spec:
    name: str
    category: str
    fn: callable
    level: float          # target playback level: loudest-100 ms RMS in dBFS (drives suggested_volume_db)
    voices: int           # suggested max simultaneous voices
    notes: str
    loop: bool = False
    peak: float = -3.0    # target peak (dBFS)
    crest: float | None = None  # soft-limit crest factor (dB) before normalising
    trim: float = -50.0   # silence threshold (dB below peak) for one-shots
    fade_in: float = 0.0007
    fade_out: float | None = None
    max_dur: float = 1.2  # one-shots are hard-capped (with a fade) at this length


SOUNDS: list[Spec] = []


def sfx(name, category, vol, voices, notes, **kw):
    def deco(fn):
        SOUNDS.append(Spec(name, category, fn, vol, voices, notes, **kw))
        return fn
    return deco


def make_rng(name: str) -> np.random.Generator:
    return np.random.default_rng([MASTER_SEED, zlib.crc32(name.encode())])


# ---------------------------------------------------------------------------
# primitives
# ---------------------------------------------------------------------------

AUDIT: list | None = None   # filled when --audit is given: layers that get truncated while still audible
_CUR = ""
_LOOP = False               # loops are periodic: never taper/fade their buffers


def _lvl_db(y, k):
    """Level of the last k samples of y relative to y's own peak (dB)."""
    pk = float(np.max(np.abs(y)))
    if pk < 1e-12 or len(y) < k:
        return -200.0
    return 20 * np.log10(max(float(np.sqrt(np.mean(y[-k:] ** 2))), 1e-12) / pk)


def taper(y, tag="", ms=10.0, audit=True):
    """Cosine-taper the end of a finite layer so a truncated decay can never click."""
    k = min(int(ms * SR / 1000), len(y) // 4)
    if k < 2 or _LOOP:
        return y
    if AUDIT is not None and audit and len(y) > int(0.02 * SR):
        lv = _lvl_db(y, max(int(0.004 * SR), 1))
        if lv > -38:
            AUDIT.append((_CUR, tag, round(lv, 1), round(len(y) / SR, 3)))
    y = y.copy()
    y[-k:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(k) / k)
    return y


def ns(sec: float) -> int:
    return int(round(sec * SR))


def tarr(n: int) -> np.ndarray:
    return np.arange(n, dtype=np.float64) / SR


def norm_std(x):
    s = float(np.std(x))
    return x / s if s > 1e-12 else x


def white(r, n):
    return r.standard_normal(n)


def filt(x, kind, fc, order=2):
    nyq = SR / 2
    if kind == "bp":
        lo, hi = fc
        sos = signal.butter(order, [max(lo, 5.0), min(hi, nyq * 0.97)], "bandpass", fs=SR, output="sos")
    else:
        sos = signal.butter(order, min(max(fc, 5.0), nyq * 0.97), "lowpass" if kind == "lp" else "highpass",
                            fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def fnoise(r, n, lo=None, hi=None, order=2):
    """Causal Butterworth-filtered white noise, unit std."""
    x = white(r, n)
    if lo and hi:
        x = filt(x, "bp", (lo, hi), order)
    elif lo:
        x = filt(x, "hp", lo, order)
    elif hi:
        x = filt(x, "lp", hi, order)
    return norm_std(x)


# --- circular (exactly periodic) spectral noise ----------------------------


def m_bp(lo, hi, order=2):
    def fn(f):
        f = np.maximum(f, 1e-3)
        return 1.0 / np.sqrt(1 + (f / hi) ** (2 * order)) / np.sqrt(1 + (lo / f) ** (2 * order))
    return fn


def m_lp(fc, order=2):
    return lambda f: 1.0 / np.sqrt(1 + (np.maximum(f, 1e-3) / fc) ** (2 * order))


def m_hp(fc, order=2):
    return lambda f: 1.0 / np.sqrt(1 + (fc / np.maximum(f, 1e-3)) ** (2 * order))


def m_pow(slope):
    return lambda f: np.maximum(f, 1.0) ** slope


def mprod(*fns):
    def fn(f):
        out = np.ones_like(f)
        for g in fns:
            out = out * g(f)
        return out
    return fn


def snoise(r, n, mag):
    """White noise shaped in the frequency domain; periodic over n samples."""
    spec = np.fft.rfft(white(r, n))
    f = np.fft.rfftfreq(n, 1.0 / SR)
    spec = spec * mag(f)
    spec[0] = 0.0
    return norm_std(np.fft.irfft(spec, n))


def loop_mod(r, n, fc, depth):
    """Periodic log-normal amplitude modulator (slow random flutter)."""
    return np.exp(depth * snoise(r, n, m_lp(fc, 2)))


def lfo(n, cycles, phase=0.0):
    return np.sin(TWO_PI * cycles * np.arange(n) / n + phase)


def circ_hp(x, fc):
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    u = np.clip((f - fc * 0.5) / (fc * 0.5), 0, 1)
    spec *= u * u * (3 - 2 * u)
    return np.fft.irfft(spec, len(x))


# --- time-varying biquad ----------------------------------------------------


def _rbj(kind, fc, q):
    fc = min(max(fc, 20.0), 0.45 * SR)
    w0 = TWO_PI * fc / SR
    c, s = np.cos(w0), np.sin(w0)
    al = s / (2.0 * q)
    if kind == "bp":
        b = np.array([al, 0.0, -al])
    elif kind == "lp":
        b = np.array([(1 - c) / 2, 1 - c, (1 - c) / 2])
    else:
        b = np.array([(1 + c) / 2, -(1 + c), (1 + c) / 2])
    a = np.array([1 + al, -2 * c, 1 - al])
    return b / a[0], a / a[0]


def tv_biquad(x, kind, f, q, block=48):
    """Biquad whose centre/cut-off frequency (and Q) vary per block."""
    n = len(x)
    f = np.broadcast_to(np.asarray(f, float), (n,))
    q = np.broadcast_to(np.asarray(q, float), (n,))
    y = np.empty(n)
    z = np.zeros(2)
    for s in range(0, n, block):
        e = min(n, s + block)
        m = (s + e) // 2
        b, a = _rbj(kind, f[m], q[m])
        y[s:e], z = signal.lfilter(b, a, x[s:e], zi=z)
    return y


def filt_loop(x, kind, fc, order=2):
    """Static causal filter for periodic material (warm-up period discarded, so the loop stays seamless)."""
    return filt(np.tile(x, 2), kind, fc, order)[len(x):]


def tv_loop(x, kind, f, q, block=48):
    """tv_biquad for periodic material: warm up one period, keep the second."""
    n = len(x)
    f = np.broadcast_to(np.asarray(f, float), (n,))
    q = np.broadcast_to(np.asarray(q, float), (n,))
    return tv_biquad(np.tile(x, 2), kind, np.tile(f, 2), np.tile(q, 2), block)[n:]


# --- envelopes / contours ---------------------------------------------------


def ad(t, att, tau):
    up = np.sin(0.5 * np.pi * np.clip(t / att, 0, 1)) if att > 0 else 1.0
    return up * np.exp(-np.maximum(t - att, 0.0) / tau)


def swell(n, p=2.0, q=2.0):
    x = np.linspace(0, 1, n)
    b = x ** p * (1 - x) ** q
    return b / max(b.max(), 1e-12)


def curve(n, pts, log=False):
    """Smooth monotone contour through (time_s, value) knots."""
    xs = np.array([p[0] for p in pts], float)
    ys = np.array([p[1] for p in pts], float)
    if log:
        ys = np.log(ys)
    out = PchipInterpolator(xs, ys, extrapolate=False)(np.clip(tarr(n), xs[0], xs[-1]))
    return np.exp(out) if log else out


def sat(y, d):
    if d <= 0:
        return y
    return np.tanh(d * y) / np.tanh(d)


def warm(x, d=1.5, pct=99.5):
    """Soft saturation scaled to the signal's own level (percentile of |x| maps to 1.0)."""
    u = x / max(float(np.percentile(np.abs(x), pct)), 1e-12)
    return np.tanh(d * u) / np.tanh(d)


# --- placement ---------------------------------------------------------------


def add_at(buf, sig, start, gain=1.0, wrap=False, check=True):
    n, m = len(buf), len(sig)
    if wrap:
        start %= n
        k = min(m, n - start)
        buf[start:start + k] += gain * sig[:k]
        rest = m - k
        while rest > 0:
            k2 = min(rest, n)
            buf[:k2] += gain * sig[m - rest:m - rest + k2]
            rest -= k2
        return
    if start >= n or start + m <= 0:
        return
    s0 = max(start, 0)
    e0 = min(start + m, n)
    part = sig[s0 - start:e0 - start]
    if check:
        part = taper(part, "placed layer cut/ends audibly", 5.0)
    buf[s0:e0] += gain * part


def place(buf, sig, t, gain=1.0, wrap=False, check=True):
    add_at(buf, sig, ns(t), gain, wrap, check)


# --- oscillators / voices ---------------------------------------------------


def dsine(f, tau, phase=0.0, g=0.0, att=0.0004, ln=7.0, g_tau=None):
    """Damped sinusoid; optional upward chirp (bubbles / droplets)."""
    m = max(int(ln * tau * SR), int(0.004 * SR))
    t = tarr(m)
    if g:
        fr = f * (1 + g * (1 - np.exp(-t / (g_tau or tau * 0.8))))
        ph = TWO_PI * np.cumsum(fr) / SR
    else:
        ph = TWO_PI * f * t
    return taper(np.sin(ph + phase) * np.exp(-t / tau) * (1 - np.exp(-t / att)), "dsine", 2.0, audit=False)


def thump(dur, f0, f1, tau_f=0.02, tau_a=0.08, att=0.0015, drive=0.0, phase=0.0):
    n = ns(max(dur, 6.0 * tau_a + att))   # dur is a minimum: the decay always runs its course
    t = tarr(n)
    f = f1 + (f0 - f1) * np.exp(-t / tau_f)
    y = np.sin(TWO_PI * np.cumsum(f) / SR + phase) * ad(t, att, tau_a)
    return taper(sat(y, drive), audit=False)


def burst(r, dur, lo=None, hi=None, att=0.0005, tau=0.02, order=2):
    n = ns(max(dur, 6.0 * tau + att))
    return taper(fnoise(r, n, lo, hi, order) * ad(tarr(n), att, tau), audit=False)


def click(r, tau=0.0025, lo=2500, dur=0.03):
    return burst(r, dur, lo=lo, att=0.0002, tau=tau)


def tone(f, dur, tau, harm=((1, 1.0, 1.0),), att=0.003, bend=0.0, bend_tau=0.02):
    n = ns(max(dur, 6.0 * tau * max(h[2] for h in harm)))
    t = tarr(n)
    fr = f * (1 + bend * np.exp(-t / bend_tau))
    ph = TWO_PI * np.cumsum(fr) / SR
    y = np.zeros(n)
    for mult, amp, tm in harm:
        y += amp * np.sin(mult * ph) * np.exp(-t / (tau * tm))
    return taper(y * np.sin(0.5 * np.pi * np.clip(t / att, 0, 1)), audit=False)


def fm_bell(f, dur, tau, ratio=1.0, index=2.0, index_tau=0.08, att=0.002):
    n = ns(max(dur, 6.0 * tau))
    t = tarr(n)
    mod = index * np.exp(-t / index_tau) * np.sin(TWO_PI * f * ratio * t)
    return taper(np.sin(TWO_PI * f * t + mod) * ad(t, att, tau), audit=False)


def harm_osc(f, nh=30, rolloff=1.0, odd=False, maxf=18000.0, phase0=0.0):
    f = np.asarray(f, float)
    ph = TWO_PI * np.cumsum(f) / SR + phase0
    y = np.zeros(len(f))
    for k in range(1, nh + 1):
        if odd and k % 2 == 0:
            continue
        y += (k ** -rolloff) * (k * f < maxf) * np.sin(k * ph)
    return y


def modal(freqs, amps, taus, dur=None, phase=0.0):
    dur = max(dur or 0.0, 6.0 * max(taus))
    n = ns(dur)
    t = tarr(n)
    y = np.zeros(n)
    for f, a, tau in zip(freqs, amps, taus):
        y += a * np.sin(TWO_PI * f * t + phase) * np.exp(-t / tau)
    return taper(y * (1 - np.exp(-t / 0.0003)), audit=False)


# --- event scatter -----------------------------------------------------------


def poisson(r, dur, rate, shape=None):
    """Event times from an (optionally thinned) Poisson process."""
    out, t = [], 0.0
    while True:
        t += r.exponential(1.0 / rate)
        if t >= dur:
            break
        if shape is None or r.random() < shape(t):
            out.append(t)
    return np.array(out)


def strat(r, m, dur):
    """m event times spread evenly over dur with random jitter inside each slot (periodic-friendly)."""
    return (np.arange(m) + r.random(m)) / m * dur


def crackle(r, buf, times, fmin, fmax, tau_min, tau_max, amp=1.0, power=2.0, wrap=False, g=0.0):
    for ts in times:
        f = float(np.exp(r.uniform(np.log(fmin), np.log(fmax))))
        tau = r.uniform(tau_min, tau_max)
        a = amp * (0.08 + 0.92 * r.random() ** power)
        add_at(buf, dsine(f, tau, phase=r.uniform(0, TWO_PI), g=g), int(ts * SR), a, wrap)
    return buf


def reverb(r, x, rt60=0.4, wet=0.2, lp_hz=6000.0, pre=0.006):
    x = taper(x, "reverb input", 15.0)
    ln = ns(rt60 * 1.15)
    t = tarr(ln)
    ir = r.standard_normal(ln) * np.exp(-6.9078 * t / rt60) * (1 - np.exp(-t / 0.004))
    ir = filt(ir, "lp", lp_hz, 2)
    ir /= np.sqrt(np.sum(ir ** 2))
    ir = np.concatenate([np.zeros(ns(pre)), ir])
    w = signal.fftconvolve(x, ir)
    out = wet * w
    out[:len(x)] += x
    return out


def fit(x, n, tag="mix layer"):
    if len(x) > n:
        return taper(x[:n], tag + " cut by buffer")
    return np.concatenate([taper(x, tag + " ends audibly") if len(x) < n else x, np.zeros(n - len(x))])


def mixn(n, *layers):
    """Sum (signal, gain) layers into a buffer of n samples."""
    out = np.zeros(n)
    for sig, gain in layers:
        out += gain * fit(sig, n)
    return out


# --- shared recipes -----------------------------------------------------------


def whoosh(r, dur, f_pts, q, p=2.0, qq=2.5, body=0.0, air=0.0, flutter=0.0, comp=0.5):
    """Band-passed noise sweep: resonant main band + low body + airy top."""
    n = ns(dur)
    f = curve(n, f_pts, log=True)
    env = swell(n, p, qq)
    fref = float(np.exp(np.mean(np.log(f))))
    w = tv_biquad(white(r, n), "bp", f, q * 1.35)
    main = tv_biquad(w, "bp", f, q * 1.35) * (fref / f) ** comp   # two cascaded sections: steeper skirts
    main = norm_std(main)
    if flutter:
        main = main * (1 + flutter * fnoise(r, n, hi=38))
    out = main * env
    if body:
        lo = tv_biquad(white(r, n), "lp", f * 0.55, 0.8)
        out = out + body * norm_std(lo) * env ** 1.4
    if air:
        out = out + air * fnoise(r, n, lo=4500, order=3) * env ** 2.2
    return out


def grind(r, dur, rate_pts, lo, hi, tau=(0.006, 0.02), base=0.22, grit=0.45):
    """Stone-on-stone grinding: irregular stick-slip events (random times and strengths) driving band-limited
    noise, over a quiet continuous scrape. rate_pts: (time, events/s) contour."""
    n = ns(dur)
    rate = curve(n, rate_pts)
    peak = float(rate.max())
    times = poisson(r, dur, peak, lambda t: rate[min(int(t * SR), n - 1)] / peak)
    env = np.zeros(n)
    for ts in times:
        tau_i = r.uniform(*tau)
        k = tarr(int(6 * tau_i * SR))
        add_at(env, (1 - np.exp(-k / 0.0012)) * np.exp(-k / tau_i), int(ts * SR), r.uniform(0.25, 1.0) ** 1.5,
               check=False)
    env = base + env / max(float(np.percentile(env, 97)), 1e-9)
    body = fnoise(r, n, lo, hi) + grit * fnoise(r, n, lo=hi, hi=hi * 4)
    return body * env


def shepard(L, f0=27.5, noct=10, center=500.0, sigma=1.1, harm=((1, 1.0),)):
    """Endlessly rising Shepard-Risset glissando, exactly periodic over L seconds."""
    n = ns(L)
    x = np.arange(n) / n
    y = np.zeros(n)
    c = f0 * L / np.log(2.0)
    for k in range(noct):
        fk0 = f0 * 2 ** k
        phase0 = TWO_PI * c * (2 ** k - 1)
        ph = phase0 + TWO_PI * fk0 * L / np.log(2.0) * (2 ** x - 1)
        fr = fk0 * 2 ** x
        a = np.exp(-0.5 * (np.log2(fr / center) / sigma) ** 2)
        for mult, amp in harm:
            y += amp * a * (mult * fr < 18000) * np.sin(mult * ph)
    return y


# ---------------------------------------------------------------------------
# MOVEMENT
# ---------------------------------------------------------------------------


def _step(r, tap, scuff_lo, scuff_hi, ring, toe_dt, toe_gain, grit):
    n = ns(0.26)
    heel = mixn(n,
                (thump(0.12, tap * 1.25, tap * 0.7, 0.010, 0.026, drive=2.0), 0.55),
                (burst(r, 0.08, lo=300, hi=1800, att=0.0006, tau=0.013), 0.85),
                (click(r, 0.002, lo=2200), 0.45),
                (dsine(ring, 0.020, phase=1.0), 0.10),
                (burst(r, 0.12, lo=scuff_lo, hi=scuff_hi, att=0.012, tau=0.035), 0.20 * grit))
    out = np.zeros(n)
    out += heel
    toe = mixn(n,
               (thump(0.08, tap * 1.5, tap * 0.95, 0.008, 0.02, drive=1.5), 0.40),
               (burst(r, 0.05, lo=600, hi=3500, att=0.0005, tau=0.009), 0.45),
               (dsine(ring * 1.33, 0.012, phase=0.4), 0.06))
    place(out, toe, toe_dt, toe_gain)
    g = np.zeros(n)
    crackle(r, g, r.uniform(0.0, 0.11, 7), 1800, 5500, 0.0008, 0.003, amp=0.5)
    return out + 0.22 * grit * g


_STEPS = [
    dict(tap=215, scuff_lo=1800, scuff_hi=4500, ring=1250, toe_dt=0.062, toe_gain=0.50, grit=1.0),
    dict(tap=190, scuff_lo=1400, scuff_hi=3600, ring=980, toe_dt=0.070, toe_gain=0.42, grit=1.2),
    dict(tap=240, scuff_lo=2200, scuff_hi=5200, ring=1560, toe_dt=0.056, toe_gain=0.55, grit=0.8),
    dict(tap=203, scuff_lo=1600, scuff_hi=4000, ring=1120, toe_dt=0.066, toe_gain=0.46, grit=1.4),
]
for _i, _p in enumerate(_STEPS, 1):
    sfx(f"step_stone_{_i}", "movement", -28, 2,
        "Light stone footstep (heel tap + toe tap + grit); rotate the 4 variants, add +/-1.5 dB random volume and "
        "+/-4% pitch.", fade_out=0.02)(lambda r, p=_p: _step(r, **p))


@sfx("land", "movement", -20, 3, "Landing from a jump/knockback: low thump, dust and a small scuff.")
def land(r):
    n = ns(0.8)
    g = np.zeros(n)
    crackle(r, g, r.uniform(0.01, 0.22, 22), 400, 2800, 0.002, 0.008, amp=0.5)
    out = mixn(n,
               (thump(0.4, 125, 46, 0.04, 0.09, drive=2.6), 0.6),
               (thump(0.15, 250, 140, 0.015, 0.05, drive=1.8), 0.9),
               (burst(r, 0.3, lo=150, hi=1500, att=0.001, tau=0.07), 0.95),
               (click(r, 0.003, lo=1800), 0.22),
               (burst(r, 0.2, lo=900, hi=4200, att=0.02, tau=0.05), 0.18),
               (g, 0.4))
    return reverb(r, out, 0.25, 0.14, 4000)


@sfx("evade_whoosh", "movement", -22, 2, "Quick body-evade swish: narrow cloth-like band sweep, low in level.",
     fade_out=0.04)
def evade_whoosh(r):
    w = whoosh(r, 0.42, [(0, 520), (0.13, 2500), (0.42, 900)], q=2.4, p=1.5, qq=2.4, body=0.25, air=0.0,
               flutter=0.22)
    cloth = fnoise(r, ns(0.42), lo=3000, hi=7500) * swell(ns(0.42), 1.8, 2.8)
    return w + 0.18 * cloth


@sfx("dash_air", "movement", -20, 2, "Air-propelled dash: pressure pop, bright rising sweep and a hissing tail.",
     fade_out=0.05)
def dash_air(r):
    n = ns(0.58)
    w = whoosh(r, 0.58, [(0, 320), (0.17, 4200), (0.58, 1500)], q=1.5, p=1.6, qq=2.2, body=0.35, air=0.4)
    pop = burst(r, 0.12, lo=700, hi=5000, att=0.002, tau=0.018)
    tail = fnoise(r, n, lo=3200, hi=9500) * ad(tarr(n), 0.12, 0.16)
    return mixn(n, (w, 1.0), (pop, 0.7), (tail, 0.14),
                (thump(0.12, 120, 70, 0.02, 0.03, drive=1.4), 0.3))


# ---------------------------------------------------------------------------
# GENERIC COMBAT
# ---------------------------------------------------------------------------


@sfx("whoosh_light", "combat", -22, 3, "Fast light strike swish; mid-high band sweep.", fade_out=0.03)
def whoosh_light(r):
    return whoosh(r, 0.3, [(0, 950), (0.1, 3600), (0.3, 1800)], q=2.0, p=1.7, qq=2.0, body=0.12, air=0.22,
                  flutter=0.12)


@sfx("whoosh_heavy", "combat", -18, 3, "Heavy strike swing: lower, wider, with body and saturation.", fade_out=0.06)
def whoosh_heavy(r):
    w = whoosh(r, 0.6, [(0, 240), (0.22, 1500), (0.6, 560)], q=1.3, p=1.8, qq=2.0, body=0.6, air=0.18,
               flutter=0.18)
    return warm(w, 1.6)


@sfx("hit_light", "combat", -18, 4, "Light impact: crisp click, slap and short body knock.", crest=18.0)
def hit_light(r):
    n = ns(0.4)
    out = mixn(n,
               (click(r, 0.0022, lo=2600), 1.0),
               (burst(r, 0.1, lo=700, hi=5200, att=0.0005, tau=0.017), 0.75),
               (thump(0.15, 290, 130, 0.012, 0.038, drive=2.0), 0.85),
               (burst(r, 0.1, lo=250, hi=1100, att=0.0006, tau=0.026), 0.55),
               (thump(0.15, 105, 70, 0.02, 0.035, drive=1.5), 0.16))
    return reverb(r, out, 0.15, 0.10, 6000, 0.004)


@sfx("hit_heavy", "combat", -14, 3, "Heavy impact: crack, saturated body thud, crunch and room tail.", crest=18.0)
def hit_heavy(r):
    n = ns(0.95)
    g = np.zeros(n)
    crackle(r, g, r.uniform(0.004, 0.14, 26), 300, 3200, 0.002, 0.007, amp=0.6)
    out = mixn(n,
               (click(r, 0.0028, lo=2000), 0.8),
               (burst(r, 0.12, lo=550, hi=6000, att=0.0004, tau=0.028), 0.85),
               (thump(0.5, 155, 52, 0.03, 0.085, drive=3.2), 0.75),
               (thump(0.2, 310, 170, 0.015, 0.055, drive=2.0), 1.0),
               (burst(r, 0.2, lo=180, hi=1200, att=0.0008, tau=0.06), 0.85),
               (g, 0.5))
    return reverb(r, out, 0.35, 0.20, 5000)


@sfx("block", "combat", -18, 3, "Dull solid thud for a blocked hit. Dark, no ring, no top end; contrasts with deflect.",
     trim=-50, fade_out=0.03)
def block(r):
    n = ns(0.45)
    out = mixn(n,
               (thump(0.22, 235, 140, 0.02, 0.06, drive=1.7), 1.0),
               (burst(r, 0.1, hi=1300, att=0.0008, tau=0.024, order=3), 0.8),
               (burst(r, 0.15, lo=200, hi=800, att=0.002, tau=0.045), 0.55),
               (thump(0.2, 100, 70, 0.03, 0.06, drive=1.3), 0.18))
    return filt(out, "lp", 1700, 2)


def _ping(r, f, n, bright=1.0):
    """Clean, bright glassy ping (slightly inharmonic, with detuned shimmer pair)."""
    parts = [(1.0, 1.00, 0.17), (1.0035, 0.55, 0.15), (2.0, 0.34, 0.10), (2.76, 0.26, 0.055),
             (3.0, 0.16, 0.07), (5.40, 0.10, 0.03)]
    freqs = [f * m for m, _, _ in parts]
    amps = [a for _, a, _ in parts]
    taus = [t for _, _, t in parts]
    ring = modal(freqs, amps, taus, dur=n / SR)
    tink = burst(r, 0.03, lo=5200, hi=13000, att=0.0002, tau=0.0035)
    tick = burst(r, 0.03, lo=2200, hi=4200, att=0.0002, tau=0.004)
    return mixn(n, (ring, 1.0), (tink, 0.55 * bright), (tick, 0.35))


@sfx("deflect", "combat", -16, 3, "Bright clean ring/ping for a successful deflect. Rewarding, distinct from block.",
     crest=20.0, fade_out=0.05)
def deflect(r):
    n = ns(1.15)
    x = _ping(r, 1760.0, n)
    return reverb(r, x, 0.5, 0.16, 9500, 0.004)


@sfx("perfect_deflect", "combat", -14, 2, "Deflect ping plus a short rising crystalline shimmer; the premium variant.",
     crest=20.0, fade_out=0.06)
def perfect_deflect(r):
    n = ns(1.4)
    x = _ping(r, 1760.0, n, bright=1.2)
    sh = np.zeros(n)
    for i, f in enumerate([1760, 2217, 2637, 3520, 4435, 5274, 7040]):
        place(sh, dsine(f, 0.14 - 0.010 * i, phase=0.7 * i), 0.035 + 0.036 * i, 0.55 - 0.045 * i)
    m = ns(0.5)
    sweep = tv_biquad(white(r, m), "bp", curve(m, [(0, 2200), (0.4, 8500), (0.5, 9000)], log=True), 3.5)
    sweep = norm_std(sweep) * swell(m, 1.4, 1.8) * (1 + 0.5 * np.sin(TWO_PI * 15 * tarr(m)))
    out = mixn(n, (x, 1.0), (sh, 0.75))
    place(out, sweep, 0.03, 0.07)
    return reverb(r, out, 0.75, 0.22, 9500, 0.006)


@sfx("control_lost", "combat", -20, 2, "Strained descending grind: jittery detuned buzz with a downward formant, for "
     "loss of elemental control.", crest=14.0, fade_out=0.08)
def control_lost(r):
    n = ns(0.6)
    t = tarr(n)
    f = curve(n, [(0, 320), (0.14, 250), (0.55, 95)], log=True)
    f = f * np.exp(0.035 * fnoise(r, n, hi=18))
    osc = harm_osc(f, 22, 1.1) + 0.7 * harm_osc(f * 1.011, 20, 1.2) + 0.5 * harm_osc(f * 0.5, 10, 1.0)
    form = tv_biquad(osc, "bp", curve(n, [(0, 1700), (0.55, 420)], log=True), 2.2)
    am = 0.7 + 0.3 * np.sin(TWO_PI * np.cumsum(curve(n, [(0, 26), (0.55, 44)])) / SR)
    rasp = fnoise(r, n, lo=700, hi=2600) * (0.4 + 0.6 * np.clip(fnoise(r, n, hi=40), 0, None))
    env = ad(t, 0.012, 1.0) * np.clip((0.58 - t) / 0.16, 0, 1)
    out = warm(norm_std(form) * am + 0.14 * rasp, 1.8) * env
    out += 0.45 * fit(thump(0.2, 130, 55, 0.04, 0.07, drive=2.0), n)
    return out


@sfx("guard_break", "combat", -13, 2, "Guard shattered: sharp crack, stone/glass fragments, descending break tone and "
     "low boom.", crest=18.0)
def guard_break(r):
    n = ns(1.3)
    g = np.zeros(n)
    crackle(r, g, np.sort(r.uniform(0.02, 0.5, 90) ** 1.3 * 0.7), 800, 6500, 0.001, 0.005, amp=0.7)
    brk = thump(0.3, 780, 190, 0.07, 0.09, drive=2.0)
    rum = fnoise(r, n, hi=700, order=3) * ad(tarr(n), 0.01, 0.22)
    out = mixn(n,
               (click(r, 0.003, lo=1500), 1.0),
               (burst(r, 0.12, lo=1000, hi=8000, att=0.0003, tau=0.02), 1.0),
               (g, 0.8),
               (brk, 0.70),
               (thump(0.6, 118, 40, 0.05, 0.12, drive=3.4), 0.75),
               (thump(0.3, 260, 140, 0.02, 0.07, drive=2.0), 0.7),
               (rum, 0.40))
    place(out, burst(r, 0.14, lo=400, hi=2800, att=0.0004, tau=0.04), 0.034, 0.75)
    return reverb(r, out, 0.5, 0.22, 6000)


@sfx("knockdown", "combat", -13, 2, "Body slam to the ground: big thud, smaller bounce, gravel slide and dust.",
     crest=17.0)
def knockdown(r):
    n = ns(1.4)
    g = np.zeros(n)
    crackle(r, g, np.sort(r.uniform(0.04, 0.8, 55) ** 1.4 * 0.8 / 0.8 ** 0.4), 350, 3200, 0.002, 0.008, amp=0.5)
    slide = fnoise(r, n, lo=150, hi=1800) * swell(n, 0.5, 2.2) * np.exp(0.6 * fnoise(r, n, hi=25))
    out = mixn(n,
               (thump(0.7, 102, 38, 0.05, 0.15, drive=3.2), 0.8),
               (thump(0.3, 230, 120, 0.02, 0.07, drive=1.8), 0.6),
               (burst(r, 0.3, lo=120, hi=1600, att=0.001, tau=0.08), 0.8),
               (click(r, 0.004, lo=1400), 0.3),
               (slide, 0.28), (g, 0.45))
    place(out, thump(0.3, 140, 62, 0.03, 0.09, drive=2.4), 0.19, 0.45)
    place(out, burst(r, 0.15, lo=100, hi=1400, att=0.001, tau=0.05), 0.19, 0.3)
    return reverb(r, out, 0.5, 0.2, 4500)


# ---------------------------------------------------------------------------
# EARTH
# ---------------------------------------------------------------------------


def _debris(r, n, t0, t1, count, fmin, fmax, tau_min, tau_max, amp=0.5, skew=1.0):
    g = np.zeros(n)
    times = t0 + (t1 - t0) * np.sort(r.random(count) ** skew)
    crackle(r, g, times, fmin, fmax, tau_min, tau_max, amp=amp)
    return g


STONE_MODES = np.array([1.0, 1.62, 2.41, 3.35, 4.6])


def _stone_ring(base, gain=1.0, dur=0.3):
    return gain * modal(base * STONE_MODES, [0.35, 0.26, 0.16, 0.09, 0.05], [0.07, 0.05, 0.036, 0.026, 0.018],
                        dur=dur)


@sfx("stone_rip", "earth", -18, 2, "Stone pulled out of the ground: initial crack then stick-slip grind and a release pop.",
     crest=16.0, fade_out=0.05)
def stone_rip(r):
    n = ns(1.0)
    t = tarr(n)
    gr = grind(r, 1.0, [(0, 22), (0.6, 45), (1.0, 60)], 70, 1100)
    env = swell(n, 0.45, 1.7) * (1 + 0.7 * (t < 0.62)) * np.clip((0.97 - t) / 0.1, 0, 1)
    out = mixn(n, (gr * env, 0.9),
               (click(r, 0.003, lo=1400), 0.7),
               (_stone_ring(620, 0.7, 0.3), 0.5),
               (_debris(r, n, 0.05, 0.9, 70, 400, 2600, 0.002, 0.009, 0.5, 1.0), 0.5))
    place(out, thump(0.2, 135, 68, 0.03, 0.06, drive=2.5), 0.62, 0.7)
    place(out, click(r, 0.004, lo=1000), 0.62, 0.55)
    place(out, burst(r, 0.15, lo=200, hi=2200, att=0.001, tau=0.04), 0.62, 0.5)
    return reverb(r, out, 0.3, 0.12, 5000)


@sfx("stone_launch", "earth", -18, 3, "Stone thrown: low release thump with grit, then a receding whoosh.",
     crest=16.0, fade_out=0.06)
def stone_launch(r):
    n = ns(0.78)
    fl = whoosh(r, 0.78, [(0, 1500), (0.1, 1300), (0.78, 420)], q=1.3, p=0.7, qq=2.6, body=0.5, air=0.1)
    out = mixn(n,
               (thump(0.35, 142, 55, 0.03, 0.1, drive=3.0), 1.0),
               (burst(r, 0.12, lo=160, hi=2600, att=0.001, tau=0.03), 0.8),
               (click(r, 0.003, lo=1500), 0.45),
               (fl, 0.8),
               (_debris(r, n, 0.0, 0.3, 24, 400, 3000, 0.002, 0.007, 0.5, 1.4), 0.5))
    return reverb(r, out, 0.3, 0.12, 5000)


def _impact(r, thump_f, ring_base, n_debris, debris_hi, shatter, dur=0.9):
    n = ns(dur)
    out = mixn(n,
               (click(r, 0.003, lo=1800), 0.75),
               (burst(r, 0.12, lo=500, hi=5200, att=0.0004, tau=0.02), 0.85),
               (thump(0.5, thump_f, thump_f * 0.41, 0.026, 0.10, drive=3.2), 0.55),
               (thump(0.3, thump_f * 0.55, thump_f * 0.28, 0.03, 0.08, drive=2.2), 0.25),
               (thump(0.2, thump_f * 2.0, thump_f * 1.2, 0.012, 0.045, drive=1.8), 0.9),
               (burst(r, 0.2, lo=200, hi=1100, att=0.0008, tau=0.05), 0.6),
               (_stone_ring(ring_base, 1.0, 0.4), 0.55),
               (_debris(r, n, 0.03, 0.55, n_debris, 300, debris_hi, 0.002, 0.011, 0.55, 1.2), 0.55))
    if shatter:
        place(out, click(r, 0.003, lo=1200), 0.012, 0.5)
        place(out, click(r, 0.003, lo=900), 0.031, 0.4)
        place(out, burst(r, 0.12, lo=700, hi=4800, att=0.0004, tau=0.03), 0.014, 0.5)
        place(out, burst(r, 0.3, lo=3200, att=0.01, tau=0.1), 0.02, 0.12)
    return reverb(r, out, 0.4, 0.2, 5200)


@sfx("stone_impact_1", "earth", -14, 4, "Boulder/stone hit: solid body thud, dead stone ring, a spray of debris.",
     crest=17.0)
def stone_impact_1(r):
    return _impact(r, 170, 520, 42, 2600, False)


@sfx("stone_impact_2", "earth", -14, 4, "Stone hit, cracking/crumbling variant: split-crack, lower thump, more debris.",
     crest=17.0)
def stone_impact_2(r):
    return _impact(r, 128, 740, 105, 4600, True, 0.9)


@sfx("stone_roll_loop", "earth", -24, 2, "Boulder rolling: lumpy low rumble, gritty mids, pebble ticks (loop 2.0 s).",
     loop=True, crest=12.0)
def stone_roll_loop(r):
    L = 2.0
    n = ns(L)
    k = 7
    rot = 0.5 + 0.5 * lfo(n, k, r.uniform(0, TWO_PI))
    low = snoise(r, n, mprod(m_lp(170, 3), m_hp(30, 2))) * (0.55 + 0.45 * rot) * loop_mod(r, n, 4, 0.3)
    mid = snoise(r, n, m_bp(250, 1900)) * (0.30 + 0.70 * rot ** 1.5) * loop_mod(r, n, 12, 0.45)
    grit = snoise(r, n, m_bp(1800, 6000)) * (0.25 + 0.75 * rot ** 2) * loop_mod(r, n, 25, 0.6)
    th = np.zeros(n)
    for i in range(k):
        tt = (i + r.uniform(-0.12, 0.12)) / k * L
        place(th, thump(0.18, r.uniform(95, 125), r.uniform(58, 72), 0.02, 0.05, drive=2.0), tt,
              r.uniform(0.55, 1.0), wrap=True)
    peb = np.zeros(n)
    crackle(r, peb, strat(r, 50, L), 700, 4200, 0.002, 0.006, amp=0.5, wrap=True)
    out = mixn(n, (low, 1.0), (mid, 0.55), (grit, 0.17), (th, 0.9), (peb, 0.35))
    return warm(out, 1.6)


@sfx("wall_raise", "earth", -14, 2, "Stone wall rising: building rumble and grind, then a heavy slam with debris.",
     crest=17.0, fade_out=0.07)
def wall_raise(r):
    n = ns(1.5)
    t = tarr(n)
    slam_t = 0.62
    pre = np.clip(t / slam_t, 0, 1) ** 1.6 * np.clip((slam_t + 0.1 - t) / 0.1, 0, 1)
    rum = fnoise(r, n, hi=240, order=3) * pre * np.exp(0.5 * fnoise(r, n, hi=30))
    sub = sat(np.sin(TWO_PI * np.cumsum(curve(n, [(0, 42), (slam_t, 68)])) / SR), 3.0) * pre
    gr = grind(r, 1.5, [(0, 9), (slam_t, 34), (1.5, 34)], 90, 1500, tau=(0.01, 0.035)) * pre ** 1.2
    out = mixn(n, (rum, 1.0), (sub, 0.55), (gr, 0.55),
               (_debris(r, n, 0.05, slam_t, 50, 300, 2400, 0.002, 0.009, 0.4, 0.7), 0.5))
    place(out, thump(0.5, 118, 38, 0.04, 0.10, drive=3.6), slam_t, 1.4)
    place(out, thump(0.2, 260, 130, 0.02, 0.07, drive=2.0), slam_t, 0.9)
    place(out, click(r, 0.004, lo=1400), slam_t, 0.65)
    place(out, burst(r, 0.3, lo=90, hi=2600, att=0.0008, tau=0.09), slam_t, 1.0)
    place(out, _stone_ring(300, 1.0, 0.4), slam_t, 0.5)
    place(out, _debris(r, ns(0.45), 0.0, 0.35, 36, 300, 3000, 0.002, 0.01, 0.5, 1.5), slam_t + 0.02, 0.6)
    return reverb(r, out, 0.3, 0.14, 4500)


@sfx("wall_crumble", "earth", -16, 2, "Wall collapsing: cracks, falling blocks, dense then thinning rubble and dust.",
     crest=17.0, fade_out=0.08)
def wall_crumble(r):
    n = ns(1.3)
    t = tarr(n)
    rum = fnoise(r, n, hi=320, order=2) * ad(t, 0.03, 0.30) * np.exp(0.5 * fnoise(r, n, hi=40))
    out = mixn(n, (rum, 0.9),
               (_debris(r, n, 0.0, 1.0, 230, 250, 3600, 0.002, 0.012, 0.6, 1.9), 0.75),
               (fnoise(r, n, lo=2500, hi=8000) * ad(t, 0.1, 0.3), 0.07))
    for tt in (0.0, 0.05, 0.11):
        place(out, click(r, 0.004, lo=1200), tt, 0.7)
        place(out, _stone_ring(r.uniform(420, 700), 0.8, 0.25), tt, 0.4)
    for tt, g in zip((0.08, 0.22, 0.4, 0.62), (1.0, 0.7, 0.5, 0.32)):
        place(out, thump(0.3, r.uniform(95, 125), r.uniform(48, 62), 0.03, 0.09, drive=2.8), tt, g)
        place(out, burst(r, 0.12, lo=100, hi=1800, att=0.001, tau=0.03), tt, 0.6 * g)
    return reverb(r, out, 0.3, 0.12, 4500)


# ---------------------------------------------------------------------------
# THERMAL / LAVA
# ---------------------------------------------------------------------------


@sfx("heat_crackle_loop", "thermal", -28, 2, "Stone heating: ticks, resonant cracks, faint sizzle (loop 2.0 s).",
     loop=True, crest=14.0)
def heat_crackle_loop(r):
    L = 2.0
    n = ns(L)
    ticks = np.zeros(n)
    crackle(r, ticks, strat(r, 36, L), 2200, 7500, 0.0006, 0.0025, amp=1.0, power=2.5, wrap=True)
    cracks = np.zeros(n)
    crackle(r, cracks, strat(r, 14, L), 500, 1700, 0.005, 0.016, amp=1.0, power=1.0, wrap=True)
    big = np.zeros(n)
    crackle(r, big, strat(r, 6, L), 380, 900, 0.018, 0.035, amp=0.8, power=0.8, wrap=True)
    sizzle = snoise(r, n, m_bp(4500, 10500)) * loop_mod(r, n, 18, 0.9)
    warm = snoise(r, n, mprod(m_lp(140, 3), m_hp(45))) * loop_mod(r, n, 1.5, 0.4)
    out = mixn(n, (ticks, 0.55), (cracks, 0.65), (big, 0.5), (sizzle, 0.045), (warm, 0.10))
    return out


@sfx("melt_rise", "thermal", -20, 2, "Stone becoming molten: heat swell opening up, glowing tone, bubbles emerging "
     "(~1.1 s).", fade_out=0.12)
def melt_rise(r):
    n = ns(1.15)
    t = tarr(n)
    env = swell(n, 2.2, 1.0) * np.clip((1.15 - t) / 0.15, 0, 1)
    body = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 130), (0.8, 1500), (1.15, 900)], log=True), 0.9))
    glow = sat(np.sin(TWO_PI * np.cumsum(curve(n, [(0, 66), (0.85, 150)], )) / SR), 2.2)
    out = mixn(n, (body * env, 0.9), (glow * swell(n, 1.6, 0.8), 0.55),
               (fnoise(r, n, lo=3000, hi=8000) * env ** 2, 0.07))
    bub = np.zeros(n)
    for tt in np.sort(r.uniform(0.32, 0.85, 9)):
        place(bub, dsine(r.uniform(90, 250), r.uniform(0.04, 0.08), g=0.4), tt, r.uniform(0.5, 1.0))
    tk = np.zeros(n)
    crackle(r, tk, r.uniform(0.0, 0.55, 14), 1500, 5500, 0.001, 0.004, amp=0.5)
    return mixn(n, (out, 1.0), (sat(bub, 1.5), 0.6), (tk, 0.35))


def _bubbles(r, n, times, flo, fhi, tau_lo, tau_hi, g, amp=1.0, wrap=False, pop=0.0):
    b = np.zeros(n)
    for tt in times:
        f = float(np.exp(r.uniform(np.log(flo), np.log(fhi))))
        tau = r.uniform(tau_lo, tau_hi)
        a = amp * r.uniform(0.45, 1.0)
        add_at(b, dsine(f, tau, g=g), int(tt * SR), a, wrap)
        if pop:
            add_at(b, burst(r, 0.05, hi=700, att=0.001, tau=0.012), int((tt + tau * 0.9) * SR), a * pop, wrap)
    return b


@sfx("lava_bubble_loop", "thermal", -24, 2, "Thick, low viscous bubbling with soft pops and faint crackle (loop 2.0 s).",
     loop=True, crest=12.0)
def lava_bubble_loop(r):
    L = 2.0
    n = ns(L)
    m = 19
    times = (np.arange(m) + r.uniform(0.1, 0.9, m)) / m * L
    bub = _bubbles(r, n, times, 85, 300, 0.05, 0.12, 0.4, 1.0, wrap=True, pop=0.5)
    big = _bubbles(r, n, (np.arange(4) + r.uniform(0.2, 0.8, 4)) / 4 * L, 55, 95, 0.12, 0.18, 0.3, 0.8, wrap=True,
                   pop=0.3)
    drone = snoise(r, n, mprod(m_lp(130, 3), m_hp(35))) * loop_mod(r, n, 1.2, 0.35)
    gurgle = snoise(r, n, m_bp(180, 700)) * loop_mod(r, n, 6, 0.9)
    cr = np.zeros(n)
    crackle(r, cr, strat(r, 12, L), 1200, 4200, 0.001, 0.004, amp=0.7, wrap=True)
    out = mixn(n, (bub, 1.0), (big, 0.85), (drone, 0.55), (gurgle, 0.14), (cr, 0.10))
    return filt_loop(warm(out, 1.8), "lp", 2600, 2)


@sfx("lava_wave_loop", "thermal", -22, 2, "Rushing viscous lava flow: dark thick surge with slow swells and slow "
     "gurgle (loop 2.0 s).", loop=True, crest=12.0)
def lava_wave_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    surge = 0.62 + 0.38 * lfo(n, 1, ph) + 0.18 * lfo(n, 3, ph * 1.7)
    brown = snoise(r, n, mprod(m_pow(-1.0), m_lp(900, 2), m_hp(28, 2)))
    body = tv_loop(brown, "lp", 260 + 520 * (0.5 + 0.5 * lfo(n, 1, ph + 1.1)), 0.9)
    mid = snoise(r, n, m_bp(130, 520)) * loop_mod(r, n, 9, 0.9)
    thick = tv_loop(snoise(r, n, m_bp(60, 400)), "bp", 120 + 140 * (0.5 + 0.5 * lfo(n, 2, ph)), 3.0)
    bub = _bubbles(r, n, (np.arange(6) + r.uniform(0.2, 0.8, 6)) / 6 * L, 60, 200, 0.06, 0.14, 0.35, 1.0, wrap=True,
                   pop=0.2)
    cr = np.zeros(n)
    crackle(r, cr, strat(r, 9, L), 1000, 3500, 0.001, 0.004, amp=0.6, wrap=True)
    out = mixn(n, (body, 1.0), (mid, 0.38), (thick, 0.42), (bub, 0.45), (cr, 0.06))
    out = out * np.clip(surge, 0.05, None)
    return filt_loop(warm(out, 1.9), "lp", 2200, 2)


@sfx("lava_splat", "thermal", -16, 3, "Lava poured onto the ground: wet slap, low thud, viscous blobs and sizzle.",
     crest=15.0, fade_out=0.06)
def lava_splat(r):
    n = ns(0.7)
    t = tarr(n)
    bub = np.zeros(n)
    for tt, g in zip((0.06, 0.13, 0.2, 0.3, 0.42), (1.0, 0.8, 0.7, 0.5, 0.35)):
        place(bub, dsine(r.uniform(90, 240), r.uniform(0.05, 0.09), g=0.4), tt, g)
        place(bub, burst(r, 0.05, hi=700, att=0.001, tau=0.012), tt + 0.05, 0.3 * g)
    tk = np.zeros(n)
    crackle(r, tk, np.sort(r.uniform(0.05, 0.6, 18)), 1300, 5000, 0.001, 0.004, amp=0.6)
    out = mixn(n,
               (burst(r, 0.25, lo=150, hi=2300, att=0.002, tau=0.05), 1.0),
               (thump(0.4, 108, 48, 0.035, 0.10, drive=2.6), 0.55),
               (sat(bub, 1.5), 0.6),
               (fnoise(r, n, lo=3500, hi=9500) * ad(t, 0.01, 0.2), 0.18),
               (tk, 0.35),
               (_debris(r, n, 0.01, 0.2, 18, 300, 2400, 0.003, 0.012, 0.4, 1.0), 0.4))
    return reverb(r, filt(out, "lp", 6000, 2), 0.25, 0.1, 4000)


@sfx("crust_hiss", "thermal", -26, 2, "Cooling crust forming: hiss with sparse ticks over a dull warm body (~1 s). "
     "Darker and more crackly than steam_hiss.", fade_out=0.1)
def crust_hiss(r):
    n = ns(1.05)
    t = tarr(n)
    env = ad(t, 0.035, 0.42) * np.exp(0.35 * fnoise(r, n, hi=14))
    hiss = tv_biquad(white(r, n), "bp", curve(n, [(0, 6500), (1.05, 3200)], log=True), 0.8)
    tk = np.zeros(n)
    crackle(r, tk, poisson(r, 1.0, 36, lambda x: np.exp(-x / 0.45)), 1500, 6200, 0.0006, 0.003, amp=1.0,
            power=2.0)
    body = fnoise(r, n, lo=350, hi=1500) * ad(t, 0.02, 0.22)
    return mixn(n, (norm_std(hiss) * env, 0.8), (tk, 0.55), (body, 0.18))


@sfx("cool_crack", "thermal", -20, 3, "Rock cracking as it sets: quick series of dwindling resonant cracks and a tiny "
     "settle.", crest=15.0, fade_out=0.05)
def cool_crack(r):
    n = ns(0.85)
    out = np.zeros(n)
    for tt, g in zip((0.0, 0.07, 0.152, 0.3), (1.0, 0.72, 0.5, 0.3)):
        place(out, click(r, 0.002, lo=1800), tt, 0.8 * g)
        place(out, dsine(r.uniform(1200, 3200), r.uniform(0.012, 0.03), phase=r.uniform(0, 6)), tt, 0.6 * g)
        place(out, dsine(r.uniform(280, 420), 0.03, phase=1.0), tt, 0.45 * g)
        place(out, dsine(r.uniform(4200, 6500), 0.05), tt, 0.10 * g)
    tk = np.zeros(n)
    crackle(r, tk, r.uniform(0.0, 0.4, 14), 2000, 6500, 0.0006, 0.002, amp=0.5)
    place(out, thump(0.2, 100, 62, 0.03, 0.07, drive=2.0), 0.34, 0.3)
    return out + 0.5 * tk


@sfx("heat_draw_loop", "thermal", -24, 2, "Tonal inward-pulling whoosh: endlessly rising Shepard glide under a "
     "breathy inhale (loop 2.0 s).", loop=True, crest=12.0)
def heat_draw_loop(r):
    L = 2.0
    n = ns(L)
    tonal = shepard(L, 27.5, 10, 520.0, 1.05, harm=((1, 1.0), (2, 0.18), (3, 0.08)))
    br = snoise(r, n, m_bp(350, 2800)) * (0.7 + 0.3 * lfo(n, 1, 0.8)) * loop_mod(r, n, 7, 0.25)
    xs = np.arange(n) / n
    sw = np.zeros(n)
    for off in (0.0, 0.5):   # two sweeps half a loop apart, each windowed to silence where it wraps
        xo = (xs + off) % 1.0
        sw += tv_loop(snoise(r, n, m_bp(200, 5000)), "bp", 300 * 2 ** (3.2 * xo), 4.0) * np.sin(np.pi * xo)
    hot = snoise(r, n, m_bp(2500, 7000)) * loop_mod(r, n, 12, 0.5)
    out = mixn(n, (norm_std(tonal), 1.0), (br, 0.22), (norm_std(sw), 0.22), (hot, 0.02))
    return warm(out, 1.3)


@sfx("vent_heat", "thermal", -18, 2, "Exhaling flame burst: soft-attack breathy roar with pressure thump and "
     "dwindling crackle.", crest=15.0, fade_out=0.1)
def vent_heat(r):
    n = ns(0.88)
    t = tarr(n)
    env = ad(t, 0.07, 0.27)
    roar = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 2600), (0.12, 2400), (0.88, 600)], log=True), 0.7))
    roar *= env * (1 + 0.4 * fnoise(r, n, hi=25))
    low = fnoise(r, n, hi=420, order=2) * ad(t, 0.05, 0.3)
    breath = fnoise(r, n, lo=2500, hi=8500) * ad(t, 0.04, 0.2)
    tk = np.zeros(n)
    crackle(r, tk, poisson(r, 0.8, 40, lambda x: 0.2 + 0.8 * np.exp(-(x - 0.15) / 0.3) if x > 0.12 else 0.0), 1400,
            5200, 0.001, 0.004, amp=0.8)
    out = mixn(n, (roar, 1.0), (low, 0.7), (breath, 0.28), (tk, 0.5))
    place(out, thump(0.3, 78, 48, 0.04, 0.11, drive=2.6), 0.015, 0.75)
    return reverb(r, out, 0.3, 0.1, 4500)


# ---------------------------------------------------------------------------
# WATER / ICE
# ---------------------------------------------------------------------------


@sfx("water_draw_loop", "water", -24, 2, "Water being drawn in: flowing band, rolling swirl resonance, bubbly plinks "
     "(loop 2.0 s).", loop=True, crest=12.0)
def water_draw_loop(r):
    L = 2.0
    n = ns(L)
    flow = snoise(r, n, m_bp(350, 3600)) * loop_mod(r, n, 7, 0.35)
    ph = r.uniform(0, TWO_PI)
    sw = tv_loop(snoise(r, n, m_bp(150, 6000)), "bp", 1000 * 2 ** (1.0 * lfo(n, 2, ph)), 3.2)
    times = []
    while len(times) < 56:
        tt = r.uniform(0, L)
        if r.random() < 0.25 + 0.75 * (0.5 + 0.5 * np.sin(TWO_PI * 2 * tt / L + ph - 1.2)):
            times.append(tt)
    bub = _bubbles(r, n, np.array(times), 500, 2800, 0.012, 0.04, 0.7, 0.8, wrap=True)
    low = snoise(r, n, m_bp(90, 450)) * loop_mod(r, n, 3, 0.4)
    out = mixn(n, (flow, 0.6), (norm_std(sw), 0.45), (bub, 0.95), (low, 0.25))
    return out * (0.8 + 0.2 * lfo(n, 4, ph + 0.4))


@sfx("water_whip", "water", -18, 3, "Water lash: fast rising whoosh into a sharp wet crack, then droplet spray.",
     crest=17.0, fade_out=0.06)
def water_whip(r):
    n = ns(0.46)
    cr = 0.145
    pre = whoosh(r, 0.145, [(0, 650), (0.145, 4800)], q=2.2, p=2.4, qq=0.35, body=0.0, air=0.1)
    out = np.zeros(n)
    k = ns(0.012)
    pre[-k:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(k) / k)   # the lash whoosh stops at the crack
    place(out, pre, 0.0, 0.8, check=False)
    place(out, click(r, 0.002, lo=2000), cr, 1.0)
    place(out, burst(r, 0.12, lo=800, hi=8000, att=0.0003, tau=0.016), cr, 0.85)
    place(out, dsine(2300, 0.012), cr, 0.3)
    place(out, thump(0.1, 380, 190, 0.01, 0.03, drive=1.5), cr, 0.35)
    place(out, burst(r, 0.3, lo=3200, hi=11000, att=0.003, tau=0.07), cr, 0.35)
    dr = _bubbles(r, n, np.sort(r.uniform(cr + 0.01, 0.42, 16)), 1500, 5200, 0.006, 0.016, 0.5, 0.35)
    return out + dr


@sfx("water_splash", "water", -16, 3, "Splash: bright noise burst that darkens, droplet/bubble scatter, watery body.",
     crest=15.0, fade_out=0.08)
def water_splash(r):
    n = ns(0.8)
    t = tarr(n)
    sp = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 8500), (0.15, 4200), (0.8, 900)], log=True), 0.8))
    sp = sp * ad(t, 0.002, 0.17) * (1 + 0.4 * fnoise(r, n, hi=60))
    times = np.sort(r.exponential(0.22, 70))
    times = times[times < 0.7]
    bub = _bubbles(r, n, times, 600, 3600, 0.01, 0.04, 0.65, 0.7)
    out = mixn(n, (sp, 0.9), (bub, 0.6),
               (thump(0.25, 210, 105, 0.02, 0.06, drive=1.4), 0.35),
               (fnoise(r, n, lo=2500, hi=9000) * ad(t, 0.01, 0.22), 0.14),
               (burst(r, 0.15, lo=200, hi=1500, att=0.002, tau=0.05), 0.4))
    return reverb(r, out, 0.3, 0.12, 6500)


@sfx("water_shield_loop", "water", -26, 1, "Water barrier: smooth shimmering flow, glassy partials and liquid wobble "
     "(loop 2.0 s).", loop=True, crest=11.0)
def water_shield_loop(r):
    L = 2.0
    n = ns(L)
    t = np.arange(n) / SR
    ph = r.uniform(0, TWO_PI)
    flow = snoise(r, n, m_bp(220, 2600)) * loop_mod(r, n, 2.5, 0.3) * (0.8 + 0.2 * lfo(n, 1, ph))
    wob = tv_loop(snoise(r, n, m_bp(200, 5000)), "bp", 700 * 2 ** (0.7 * lfo(n, 1, ph + 2.0)), 5.0) * 0.5
    wob += tv_loop(snoise(r, n, m_bp(200, 6000)), "bp", 1500 * 2 ** (0.6 * lfo(n, 2, ph + 0.5)), 5.0) * 0.4
    shim = np.zeros(n)
    for f, c, a in ((587.5, 3, 1.0), (880.0, 5, 0.7), (1318.5, 4, 0.5), (1760.0, 6, 0.3)):
        f = round(f * L) / L
        shim += a * np.sin(TWO_PI * f * t + r.uniform(0, 6)) * (0.55 + 0.45 * lfo(n, c, r.uniform(0, 6)))
    bub = _bubbles(r, n, strat(r, 14, L), 1200, 4200, 0.008, 0.025, 0.7, 0.35, wrap=True)
    air = snoise(r, n, m_bp(4000, 9000)) * loop_mod(r, n, 9, 0.5)
    out = mixn(n, (flow, 0.7), (norm_std(wob), 0.45), (shim, 0.14), (bub, 0.4), (air, 0.05))
    return out


@sfx("freeze", "water", -20, 2, "Water crystallizing: dense glassy crunch grains, icy twinkles, cold hiss "
     "(~0.9 s).", crest=15.0, fade_out=0.08)
def freeze(r):
    n = ns(1.3)
    t = tarr(n)
    g = np.zeros(n)
    times = []
    while len(times) < 220:
        tt = r.uniform(0, 0.85)
        if r.random() < np.interp(tt, [0, 0.2, 0.55, 0.85], [0.5, 1.0, 0.5, 0.1]):
            times.append(tt)
    crackle(r, g, np.sort(times), 2500, 9500, 0.0005, 0.003, amp=1.0, power=1.6)
    crunch = fnoise(r, n, lo=2200, hi=8000) * np.clip(fnoise(r, n, hi=140) * 1.2 + 0.1, 0, None) ** 1.5
    crunch *= np.interp(t, [0, 0.2, 0.6, 0.9], [0.5, 1.0, 0.5, 0.0])
    tw = np.zeros(n)
    for tt in np.sort(r.uniform(0.08, 0.7, 9)):
        place(tw, dsine(float(r.choice([3520.0, 4699.0, 5274.0, 7040.0, 3951.0])), r.uniform(0.05, 0.12)), tt,
              r.uniform(0.3, 0.8))
    low = fnoise(r, n, lo=300, hi=1500) * ad(t, 0.01, 0.18)
    cold = fnoise(r, n, lo=5000, hi=10000) * swell(n, 1.0, 1.4)
    out = mixn(n, (g, 0.55), (crunch, 0.26), (tw, 0.40), (low, 0.2), (cold, 0.07))
    return reverb(r, out, 0.4, 0.15, 9000)


@sfx("ice_shatter", "water", -15, 3, "Ice breaking: bright crack, small body knock, shower of glassy shards.",
     crest=17.0, fade_out=0.08)
def ice_shatter(r):
    n = ns(0.86)
    t = tarr(n)
    times = np.sort(r.exponential(0.2, 130))
    times = times[times < 0.72]
    shards = np.zeros(n)
    crackle(r, shards, times, 2000, 11000, 0.003, 0.025, amp=1.0, power=1.4)
    tink = np.zeros(n)
    crackle(r, tink, np.sort(r.uniform(0.02, 0.5, 13)), 3000, 7500, 0.05, 0.12, amp=1.0, power=0.8)
    out = mixn(n,
               (click(r, 0.002, lo=2500), 1.0),
               (burst(r, 0.1, lo=1500, hi=10000, att=0.0003, tau=0.014), 1.0),
               (thump(0.2, 210, 90, 0.015, 0.05, drive=1.6), 0.5),
               (shards, 0.6), (tink, 0.22),
               (fnoise(r, n, lo=6000) * ad(t, 0.002, 0.12), 0.14))
    return reverb(r, out, 0.45, 0.2, 9000)


@sfx("ice_melt_drip", "water", -26, 3, "Three melting drips with a faint trickle and small-room echo.",
     fade_out=0.08)
def ice_melt_drip(r):
    n = ns(0.78)
    t = tarr(n)
    out = np.zeros(n)
    for tt, f, g in ((0.02, 1050, 1.0), (0.27, 1450, 0.72), (0.52, 880, 0.52)):
        place(out, dsine(f, 0.045, g=0.9, g_tau=0.03), tt, g)
        place(out, dsine(f * 2.01, 0.022, g=0.9, g_tau=0.03), tt, 0.28 * g)
        place(out, burst(r, 0.02, lo=2500, hi=7000, att=0.0002, tau=0.0025), tt, 0.25 * g)
    trick = fnoise(r, n, lo=1200, hi=5000) * np.exp(0.8 * fnoise(r, n, hi=25)) * ad(t, 0.02, 0.4)
    return reverb(r, out + 0.06 * trick, 0.35, 0.28, 6500, 0.012)


@sfx("steam_hiss", "water", -24, 2, "Pure breathy steam release: bright hiss with air band and gentle flutter "
     "(~1 s).", fade_out=0.1)
def steam_hiss(r):
    n = ns(1.0)
    t = tarr(n)
    env = ad(t, 0.06, 0.4) * (1 + 0.25 * fnoise(r, n, hi=10))
    hiss = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 5600), (1.0, 3600)], log=True), 0.6))
    air = fnoise(r, n, lo=600, hi=2400) * ad(t, 0.08, 0.3)
    pops = _bubbles(r, n, np.sort(r.uniform(0.1, 0.8, 5)), 900, 2600, 0.01, 0.025, 0.6, 0.12)
    return mixn(n, (hiss * env, 0.85), (air, 0.22), (pops, 0.5))


# ---------------------------------------------------------------------------
# FIRE
# ---------------------------------------------------------------------------


@sfx("fire_jab", "fire", -18, 3, "Quick flame punch: sharp flare burst, snappy thump, pops and a short ember tail.",
     crest=16.0, fade_out=0.04)
def fire_jab(r):
    n = ns(0.32)
    t = tarr(n)
    flare = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 7000), (0.12, 2200), (0.32, 700)], log=True), 0.8))
    flare *= ad(t, 0.0012, 0.07)
    tk = np.zeros(n)
    crackle(r, tk, np.sort(r.uniform(0.01, 0.22, 10)), 1500, 5500, 0.0008, 0.004, amp=0.8)
    return mixn(n, (flare, 1.0),
                (thump(0.2, 150, 75, 0.02, 0.05, drive=2.5), 0.85),
                (click(r, 0.002, lo=2800), 0.45), (tk, 0.5),
                (fnoise(r, n, lo=2200, hi=7500) * ad(t, 0.01, 0.07), 0.14))


@sfx("fire_charge_loop", "fire", -22, 1, "Fire energy building: low roar with flutter, flame hiss and crackle, "
     "subtle rising tone (loop 2.0 s).", loop=True, crest=12.0)
def fire_charge_loop(r):
    L = 2.0
    n = ns(L)
    roar = snoise(r, n, m_bp(110, 1700, 2)) * loop_mod(r, n, 12, 0.5) * loop_mod(r, n, 4, 0.25)
    low = snoise(r, n, mprod(m_lp(260, 2), m_hp(40))) * loop_mod(r, n, 2, 0.35)
    hiss = snoise(r, n, m_bp(3200, 9000)) * loop_mod(r, n, 20, 0.8)
    cr = np.zeros(n)
    crackle(r, cr, strat(r, 46, L), 1000, 6000, 0.0008, 0.005, amp=1.0, power=2.0, wrap=True)
    rise = shepard(L, 27.5, 10, 330.0, 0.9, harm=((1, 1.0), (2, 0.3)))
    out = mixn(n, (roar, 0.9), (low, 0.8), (hiss, 0.2), (cr, 0.55), (norm_std(rise), 0.16))
    return warm(out, 1.4)


@sfx("fire_release", "fire", -12, 2, "Fire blast release: pressure thump, wide roar that falls in pitch, whoosh, "
     "crackle tail.", crest=16.0, fade_out=0.1)
def fire_release(r):
    n = ns(1.4)
    t = tarr(n)
    roar = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 5600), (0.3, 1900), (1.05, 520)], log=True), 0.7))
    roar *= ad(t, 0.012, 0.3) * (1 + 0.35 * fnoise(r, n, hi=40))
    low = fnoise(r, n, hi=420, order=2) * ad(t, 0.01, 0.36)
    sw = whoosh(r, 0.3, [(0, 500), (0.14, 3200), (0.3, 1500)], q=1.2, p=1.2, qq=1.8)
    tk = np.zeros(n)
    crackle(r, tk, poisson(r, 1.0, 55, lambda x: np.exp(-x / 0.4)), 1200, 6000, 0.0008, 0.005, amp=0.9)
    out = mixn(n, (roar, 1.0), (low, 0.8), (sw, 0.5), (tk, 0.55),
               (thump(0.5, 98, 42, 0.04, 0.14, drive=3.4), 1.15),
               (click(r, 0.003, lo=2000), 0.35),
               (fnoise(r, n, lo=5000) * ad(t, 0.01, 0.25), 0.12))
    return reverb(r, out, 0.5, 0.2, 5000)


@sfx("fire_ignite", "fire", -18, 3, "Ignition: spark flick, a soft gas 'whump' flare, settling crackle.",
     crest=15.0, fade_out=0.08)
def fire_ignite(r):
    n = ns(0.9)
    t = tarr(n)
    sp = np.zeros(n)
    for tt, f, g in ((0.0, 4800, 1.0), (0.006, 5600, 0.7), (0.014, 4100, 0.55), (0.021, 6200, 0.4)):
        place(sp, dsine(f, 0.002), tt, g)
    place(sp, click(r, 0.0015, lo=3000), 0.0, 0.8)
    flare = norm_std(tv_biquad(white(r, n), "lp",
                               curve(n, [(0, 250), (0.07, 3600), (0.25, 1300), (0.68, 600)], log=True), 0.8))
    flare *= ad(t, 0.045, 0.2) * (1 + 0.3 * fnoise(r, n, hi=18))
    tk = np.zeros(n)
    crackle(r, tk, np.sort(r.uniform(0.09, 0.6, 22)), 1300, 5500, 0.0008, 0.004, amp=0.8)
    out = mixn(n, (sp, 0.7), (flare, 1.0), (tk, 0.5),
               (fnoise(r, n, lo=3000, hi=8000) * ad(t, 0.05, 0.1), 0.12))
    place(out, thump(0.3, 72, 46, 0.05, 0.11, drive=2.6), 0.03, 0.9)
    return reverb(r, out, 0.25, 0.08, 5000)


# ---------------------------------------------------------------------------
# LIGHTNING
# ---------------------------------------------------------------------------


def _crack(r, n, at=0.0, gain=1.0, zap_up=False):
    out = np.zeros(n)
    place(out, burst(r, 0.02, lo=500, att=0.0001, tau=0.0004), at, 1.0 * gain)
    place(out, burst(r, 0.15, lo=1200, hi=15000, att=0.0002, tau=0.011), at, 1.0 * gain)
    place(out, burst(r, 0.2, lo=600, hi=9000, att=0.0003, tau=0.034), at + 0.018, 0.65 * gain)
    zl = ns(0.25)
    tz = tarr(zl)
    fz = 1200 * 5 ** (np.exp(-tz / 0.02)) if not zap_up else 1000 * 5 ** (1 - np.exp(-tz / 0.03))
    zap = np.sin(TWO_PI * np.cumsum(fz) / SR) * ad(tz, 0.0005, 0.04)
    place(out, zap, at, 0.38 * gain)
    return out


@sfx("lightning_charge_loop", "lightning", -24, 1, "Building electrical buzz: beating mains-style hum, bright spark "
     "ticks and a rising whine (loop 2.0 s).", loop=True, crest=12.0)
def lightning_charge_loop(r):
    L = 2.0
    n = ns(L)
    t = np.arange(n) / SR
    ph = r.uniform(0, TWO_PI)
    b1 = harm_osc(np.full(n, 100.0), 45, 0.75, phase0=2.1)
    b2 = harm_osc(np.full(n, 123.0), 38, 0.8, phase0=1.3)
    form = 900 * 2 ** (1.3 * (0.5 + 0.5 * lfo(n, 2, ph)))
    buzz = tv_loop(b1 + 0.8 * b2, "bp", form, 1.6)
    grit = snoise(r, n, m_bp(1500, 8000)) * (0.5 + 0.5 * loop_mod(r, n, 60, 1.0))
    sp = np.zeros(n)
    times = []
    while len(times) < 70:
        tt = r.uniform(0, L)
        if r.random() < 0.2 + 0.8 * (0.5 + 0.5 * np.sin(TWO_PI * tt / L + ph)):
            times.append(tt)
    crackle(r, sp, np.array(times), 2500, 11000, 0.0004, 0.0015, amp=1.0, power=1.8, wrap=True)
    rise = shepard(L, 27.5, 10, 1700.0, 0.8)
    whine = np.sin(TWO_PI * 3200.0 * t) * (0.4 + 0.6 * lfo(n, 8, 0.5) ** 2)
    out = mixn(n, (norm_std(buzz), 1.0), (grit, 0.10), (sp, 0.5), (norm_std(rise), 0.22), (whine, 0.02))
    out = out * (0.8 + 0.2 * lfo(n, 4, ph))
    return warm(out, 1.6)


def _buzz_tail(r, n, f_pts, form_pts, q, tau, am_depth, nh=40):
    t = tarr(n)
    f = curve(n, f_pts, log=True)
    osc = harm_osc(f, nh, 0.7)
    form = tv_biquad(osc, "bp", curve(n, form_pts, log=True), q)
    am = 1 - am_depth + am_depth * np.clip(0.5 + fnoise(r, n, hi=220), 0, 1.5)
    return norm_std(form) * am * ad(t, 0.004, tau)


@sfx("lightning_strike", "lightning", -14, 2, "Sharp crack with zap, low boom and a buzzy sizzling tail.",
     crest=17.0, fade_out=0.1)
def lightning_strike(r):
    n = ns(1.4)
    tail = _buzz_tail(r, n, [(0, 112), (0.5, 100), (1.0, 92)], [(0, 1900), (1.0, 650)], 1.5, 0.22, 0.8)
    sz = np.zeros(n)
    crackle(r, sz, poisson(r, 0.9, 75, lambda x: np.exp(-x / 0.35)), 2000, 9500, 0.0004, 0.0015, amp=1.0,
            power=1.8)
    out = mixn(n, (_crack(r, n), 1.0), (warm(tail, 2.0), 0.75), (sz, 0.45),
               (thump(0.5, 88, 44, 0.04, 0.14, drive=3.0), 0.35))
    return reverb(r, out, 0.35, 0.15, 7500, 0.004)


@sfx("lightning_redirect", "lightning", -14, 2, "Strike that twists mid-air: first crack, warbling pitch-bent buzz, "
     "a second rising zap.", crest=17.0, fade_out=0.1)
def lightning_redirect(r):
    n = ns(1.4)
    f_pts = [(0, 135), (0.14, 430), (0.3, 190), (0.5, 68), (0.72, 260), (1.0, 150)]
    tail = _buzz_tail(r, n, f_pts, [(0, 2100), (0.4, 900), (1.0, 1300)], 1.6, 0.3, 0.7)
    twist = np.sin(TWO_PI * np.cumsum(curve(n, [(0, 300), (0.3, 1500), (0.6, 400), (1.0, 700)])) / SR)
    tail = tail * (0.6 + 0.4 * twist)
    sz = np.zeros(n)
    crackle(r, sz, poisson(r, 0.9, 70, lambda x: np.exp(-x / 0.4)), 1800, 9000, 0.0004, 0.0015, amp=1.0,
            power=1.8)
    out = mixn(n, (_crack(r, n, 0.0, 0.9), 1.0), (_crack(r, n, 0.3, 0.65, zap_up=True), 1.0),
               (warm(tail, 2.2), 0.8), (sz, 0.4),
               (thump(0.5, 95, 46, 0.04, 0.14, drive=3.0), 0.3))
    return reverb(r, out, 0.35, 0.15, 7500, 0.004)


@sfx("conduct_buzz", "lightning", -20, 2, "Short electrical buzz crossing water/metal: gated harmonic buzz, "
     "metallic ring, sparks.", crest=14.0, fade_out=0.07)
def conduct_buzz(r):
    n = ns(0.6)
    t = tarr(n)
    f = curve(n, [(0, 128), (0.42, 112)], log=True)
    buzz = tv_biquad(harm_osc(f, 36, 0.7), "bp", curve(n, [(0, 2600), (0.42, 1500)], log=True), 1.8)
    gate = np.clip(0.55 + 0.45 * np.sign(np.sin(TWO_PI * np.cumsum(curve(n, [(0, 46), (0.42, 62)])) / SR)), 0.15, 1)
    gate = filt(gate, "lp", 400, 2)
    out = norm_std(buzz) * gate * ad(t, 0.004, 0.2)
    ring = np.zeros(n)
    for tt in (0.0, 0.11, 0.2):
        place(ring, dsine(2200, 0.05), tt, 0.5)
        place(ring, dsine(3450, 0.035), tt, 0.35)
        place(ring, dsine(5100, 0.02), tt, 0.2)
    sp = np.zeros(n)
    crackle(r, sp, r.uniform(0, 0.38, 26), 2500, 10000, 0.0004, 0.0015, amp=1.0, power=1.6)
    grit = fnoise(r, n, lo=2500, hi=8000) * ad(t, 0.003, 0.12)
    return mixn(n, (warm(out, 1.8), 1.0), (ring, 0.35), (sp, 0.5), (grit, 0.12))


# ---------------------------------------------------------------------------
# AIR
# ---------------------------------------------------------------------------


@sfx("air_push", "air", -18, 3, "Palm air push: low pressure pulse, quick band-passed gust, airy tail.",
     crest=15.0, fade_out=0.06)
def air_push(r):
    n = ns(0.56)
    t = tarr(n)
    g = whoosh(r, 0.56, [(0, 650), (0.11, 2500), (0.56, 600)], q=1.5, p=0.9, qq=2.0, body=0.35, air=0.25)
    return mixn(n, (g, 1.0),
                (thump(0.2, 108, 58, 0.03, 0.07, drive=1.8), 0.75),
                (burst(r, 0.1, lo=1500, hi=8000, att=0.004, tau=0.05), 0.35),
                (fnoise(r, n, lo=200, hi=900) * ad(t, 0.02, 0.2), 0.25))


@sfx("air_gust", "air", -20, 2, "Wide wind gust: swelling resonant band with turbulence, breathy body and airy top.",
     crest=14.0, fade_out=0.12)
def air_gust(r):
    n = ns(0.95)
    f = curve(n, [(0, 360), (0.3, 1500), (0.95, 560)], log=True) * np.exp(0.12 * fnoise(r, n, hi=9))
    main = norm_std(tv_biquad(white(r, n), "bp", f, 1.7)) * (1 + 0.25 * fnoise(r, n, hi=22))
    env = swell(n, 1.8, 2.2)
    out = main * env
    out += 0.35 * norm_std(fnoise(r, n, lo=120, hi=520)) * env ** 1.2
    out += 0.25 * fnoise(r, n, lo=3000, hi=9500) * env ** 2.0
    return out


@sfx("glide_loop", "air", -28, 1, "Gentle gliding wind: smooth wandering resonances, no events (loop 2.0 s).",
     loop=True, crest=11.0)
def glide_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    body = snoise(r, n, m_bp(160, 900)) * loop_mod(r, n, 1.5, 0.35)
    mid = snoise(r, n, m_bp(700, 3400)) * loop_mod(r, n, 2.5, 0.4)
    wander = tv_loop(snoise(r, n, m_bp(200, 5000)), "bp", 850 * 2 ** (0.55 * lfo(n, 1, ph)), 2.6)
    wander2 = tv_loop(snoise(r, n, m_bp(300, 6000)), "bp", 1900 * 2 ** (0.5 * lfo(n, 2, ph + 1.0)), 3.0)
    air = snoise(r, n, m_bp(3500, 8000)) * loop_mod(r, n, 3, 0.5)
    out = mixn(n, (body, 0.8), (mid, 0.5), (norm_std(wander), 0.5), (norm_std(wander2), 0.28), (air, 0.12))
    return out * (0.85 + 0.15 * lfo(n, 1, ph + 2.2))


# ---------------------------------------------------------------------------
# UI
# ---------------------------------------------------------------------------

D4, E4, FS4, A4, B4 = 293.66, 329.63, 369.99, 440.00, 493.88
D5, E5, FS5, A5, B5 = 587.33, 659.26, 739.99, 880.00, 987.77
D6, E6, A6 = 1174.66, 1318.51, 1760.0


@sfx("ui_tap", "ui", -28, 4, "Soft tick for taps and hovers.", fade_out=0.015, trim=-50)
def ui_tap(r):
    n = ns(0.14)
    return mixn(n,
                (burst(r, 0.04, lo=2200, hi=5200, att=0.0003, tau=0.004), 0.7),
                (dsine(1450, 0.022), 0.6),
                (dsine(2900, 0.012), 0.15),
                (thump(0.06, 420, 230, 0.01, 0.018, drive=1.2), 0.35))


@sfx("ui_select", "ui", -22, 3, "Confirm: soft rising pair (D5 to A5).", fade_out=0.04)
def ui_select(r):
    n = ns(0.4)
    a = tone(D5, 0.2, 0.05, ((1, 1.0, 1.0), (2, 0.25, 0.6), (3, 0.08, 0.4)), att=0.004)
    b = tone(A5, 0.2, 0.07, ((1, 1.0, 1.0), (2, 0.2, 0.6), (3, 0.06, 0.4)), att=0.004)
    out = np.zeros(n)
    place(out, a, 0.0, 0.8)
    place(out, b, 0.07, 1.0)
    place(out, burst(r, 0.02, lo=2500, hi=6000, att=0.0003, tau=0.003), 0.07, 0.15)
    return reverb(r, out, 0.18, 0.1, 6000, 0.004)


@sfx("ui_back", "ui", -24, 3, "Back/cancel: softer falling pair (A5 to D5), a little darker.", fade_out=0.04)
def ui_back(r):
    n = ns(0.4)
    a = tone(A5, 0.2, 0.045, ((1, 1.0, 1.0), (2, 0.18, 0.5)), att=0.004)
    b = tone(D5, 0.2, 0.06, ((1, 1.0, 1.0), (2, 0.2, 0.6), (3, 0.05, 0.4)), att=0.004)
    out = np.zeros(n)
    place(out, a, 0.0, 0.7)
    place(out, b, 0.065, 1.0)
    return filt(reverb(r, out, 0.15, 0.08, 5000, 0.004), "lp", 4200, 2)


@sfx("element_earth", "ui", -22, 2, "Earth motif: low warm A3-D4 pluck pair with a stone knock.", fade_out=0.04,
     max_dur=0.34, trim=-45)
def element_earth(r):
    n = ns(0.5)
    h = ((1, 1.0, 1.0), (2, 0.55, 0.7), (3, 0.28, 0.5), (4, 0.12, 0.4))
    out = np.zeros(n)
    place(out, sat(tone(220.0, 0.28, 0.07, h, att=0.003), 1.4), 0.0, 0.9)
    place(out, sat(tone(D4, 0.28, 0.05, h, att=0.003), 1.4), 0.075, 1.0)
    place(out, thump(0.12, 160, 90, 0.015, 0.035, drive=1.8), 0.0, 0.5)
    place(out, burst(r, 0.05, lo=500, hi=2200, att=0.0005, tau=0.01), 0.075, 0.25)
    return filt(out, "lp", 3600, 2)


@sfx("element_water", "ui", -22, 2, "Water motif: falling droplet-like D5-A4 ripple, soft vibrato glass.",
     fade_out=0.04, max_dur=0.34, trim=-45)
def element_water(r):
    n = ns(0.5)
    h = ((1, 1.0, 1.0), (2, 0.22, 0.5), (3, 0.06, 0.4))
    out = np.zeros(n)
    place(out, tone(A5, 0.2, 0.055, h, att=0.003, bend=0.05, bend_tau=0.02), 0.0, 0.8)
    place(out, tone(FS5, 0.2, 0.06, h, att=0.003, bend=0.05, bend_tau=0.02), 0.06, 0.9)
    place(out, tone(D5, 0.25, 0.045, h, att=0.003, bend=0.05, bend_tau=0.02), 0.12, 1.0)
    place(out, _bubbles(r, ns(0.2), [0.0, 0.05], 1500, 2800, 0.008, 0.016, 0.6, 0.18), 0.0, 1.0)
    return reverb(r, out, 0.2, 0.12, 7000, 0.004)


@sfx("element_fire", "ui", -22, 2, "Fire motif: quick rising D4-F#4-A4-D5 arpeggio, bright with a crackle accent.",
     fade_out=0.04, max_dur=0.34, trim=-45)
def element_fire(r):
    n = ns(0.5)
    h = ((1, 1.0, 1.0), (2, 0.5, 0.6), (3, 0.38, 0.5), (4, 0.25, 0.4), (5, 0.12, 0.3))
    out = np.zeros(n)
    for i, f in enumerate((D4, FS4, A4, D5)):
        place(out, tone(f, 0.2, 0.04 + 0.006 * i, h, att=0.002), 0.048 * i, 0.75 + 0.1 * i)
    tk = np.zeros(n)
    crackle(r, tk, r.uniform(0.0, 0.2, 9), 2000, 6000, 0.0008, 0.003, amp=0.8)
    return filt(out + 0.35 * tk, "lp", 7000, 2)


@sfx("element_air", "ui", -22, 2, "Air motif: high breathy A5-E6 sigh with a soft upward glide.", fade_out=0.04,
     max_dur=0.34, trim=-45)
def element_air(r):
    n = ns(0.5)
    h = ((1, 1.0, 1.0), (2, 0.12, 0.5))
    out = np.zeros(n)
    place(out, tone(A5, 0.25, 0.06, h, att=0.01, bend=-0.04, bend_tau=0.04), 0.0, 0.9)
    place(out, tone(E6, 0.25, 0.06, h, att=0.01, bend=-0.03, bend_tau=0.04), 0.075, 1.0)
    br = tv_biquad(white(r, n), "bp", curve(n, [(0, 2800), (0.3, 5200)], log=True), 1.2) * swell(n, 1.2, 1.8)
    out += 0.03 * norm_std(br)
    return reverb(r, out, 0.22, 0.14, 8000, 0.004)


@sfx("unlock", "ui", -16, 1, "Warm reward motif: rising D-major pentatonic bell arpeggio resolving to a soft chord.",
     fade_out=0.35, max_dur=1.45)
def unlock(r):
    n = ns(1.9)
    out = np.zeros(n)
    notes = [(0.000, D4, 0.9), (0.085, A4, 0.9), (0.170, D5, 0.95), (0.255, FS5, 1.0), (0.340, A5, 1.05)]
    for tt, f, g in notes:
        place(out, fm_bell(f, 0.8, 0.22, 1.0, 1.4, 0.07), tt, 0.6 * g)
        place(out, tone(f, 0.8, 0.17, ((1, 1.0, 1.0), (2, 0.2, 0.5))), tt, 0.4 * g)
    for f, g in ((D5, 1.0), (A5, 0.8), (FS5, 0.6), (D6, 0.5), (E6, 0.25)):
        place(out, tone(f, 0.9, 0.27, ((1, 1.0, 1.0), (2, 0.15, 0.5)), att=0.02), 0.43, 0.5 * g)
    place(out, tone(146.83, 1.0, 0.30, ((1, 1.0, 1.0), (2, 0.4, 0.6), (3, 0.15, 0.4)), att=0.01), 0.0, 0.40)
    place(out, tone(146.83, 1.0, 0.32, ((1, 1.0, 1.0), (2, 0.4, 0.6), (3, 0.15, 0.4)), att=0.02), 0.43, 0.38)
    place(out, burst(r, 0.03, lo=3000, hi=9000, att=0.0003, tau=0.004), 0.34, 0.12)
    return reverb(r, out, 0.6, 0.22, 7000, 0.008)


@sfx("challenge_fail", "ui", -20, 1, "Restrained fail cue: muted falling F4-D4 pluck pair over a soft low knock; "
     "no buzzer.", fade_out=0.1, max_dur=0.9)
def challenge_fail(r):
    n = ns(1.0)
    h = ((1, 1.0, 1.0), (2, 0.5, 0.6), (3, 0.3, 0.4), (4, 0.15, 0.3))
    out = np.zeros(n)
    place(out, filt(tone(349.23, 0.4, 0.1, h, att=0.004), "lp", 1400, 2), 0.0, 0.9)
    place(out, filt(tone(D4, 0.6, 0.16, h, att=0.004), "lp", 1100, 2), 0.18, 1.0)
    place(out, filt(tone(D4 * 1.007, 0.6, 0.16, h, att=0.004), "lp", 1100, 2), 0.18, 0.5)
    place(out, thump(0.25, 120, 65, 0.03, 0.08, drive=2.0), 0.18, 0.55)
    place(out, burst(r, 0.08, lo=300, hi=1500, att=0.001, tau=0.02), 0.18, 0.2)
    return reverb(r, out, 0.4, 0.14, 3500, 0.006)


# ---------------------------------------------------------------------------
# AMBIENCE
# ---------------------------------------------------------------------------


@sfx("amb_courtyard_loop", "ambience", -32, 1, "Quiet courtyard bed: distant airy wind and soft leaf-rustle air, very low "
     "level (-18 dBFS peak) (loop 3.0 s).", loop=True, peak=-18.0, crest=10.0)
def amb_courtyard_loop(r):
    L = 3.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    gust = np.clip(0.55 + 0.35 * lfo(n, 1, ph) + 0.2 * lfo(n, 2, ph * 1.3), 0.12, None)
    low = snoise(r, n, mprod(m_pow(-0.5), m_bp(55, 380, 2))) * loop_mod(r, n, 0.7, 0.4)
    mid = tv_loop(snoise(r, n, m_bp(250, 2200)), "bp", 700 * 2 ** (0.45 * lfo(n, 1, ph + 0.8)), 1.2)
    air = snoise(r, n, m_bp(2500, 7500)) * loop_mod(r, n, 2.0, 0.9)
    leaf = snoise(r, n, m_bp(4500, 9500)) * loop_mod(r, n, 10, 0.45)
    out = mixn(n, (low, 0.9), (norm_std(mid), 0.6), (air, 0.22), (leaf, 0.05))
    return out * gust


# ---------------------------------------------------------------------------
# MOVESET EXTENSION (docs/MOVESET.md section 12): metal, sand/glass, magma, water/ice/mist/plant, fire, air, system.
# Every sound owns a fixed seed (crc32 of its name), so adding or editing one never changes another.
# ---------------------------------------------------------------------------

D3, A3 = 146.83, 220.0
_BAR = (1.0, 2.756, 5.404, 8.933, 13.34)     # free-bar (metal) inharmonic partial ratios


def _bar(f, tau, dur, amps=(1.0, 0.75, 0.5, 0.3, 0.15)):
    """Struck metal bar: inharmonic modal ring, upper partials die faster."""
    sc = (1.0, 0.55, 0.32, 0.2, 0.12)
    pr = [(f * m, a, tau * s) for m, a, s in zip(_BAR, amps, sc) if f * m < 16000]
    return modal([p[0] for p in pr], [p[1] for p in pr], [p[2] for p in pr], dur=dur)


def _grains(r, n, count, t0, t1, flo, fhi, tau_lo=0.0005, tau_hi=0.002, amp=1.0, power=2.0):
    g = np.zeros(n)
    crackle(r, g, np.sort(r.uniform(t0, t1, count)), flo, fhi, tau_lo, tau_hi, amp=amp, power=power)
    return g


# --- metal ------------------------------------------------------------------


@sfx("metal_clang", "metal", -16, 3, "Struck metal: sharp tink, inharmonic bar ring (ratios 1/2.76/5.4/8.9) with a "
     "detuned shimmer. Bright and metallic, unlike the harmonic glassy deflect ping.", crest=18.0, fade_out=0.08,
     max_dur=1.0)
def metal_clang(r):
    n = ns(0.95)
    out = mixn(n, (_bar(640.0, 0.16, 0.9), 1.0), (_bar(640.0 * 1.0042, 0.14, 0.9), 0.5),
               (click(r, 0.0025, lo=3000), 0.7),
               (burst(r, 0.06, lo=1500, hi=9000, att=0.0003, tau=0.01), 0.6),
               (thump(0.1, 380, 210, 0.01, 0.03, drive=1.6), 0.35))
    return reverb(r, out, 0.3, 0.14, 8000, 0.003)


@sfx("metal_scrape", "metal", -20, 2, "Blade on plate: stick-slip grind in the 1.2-5 kHz band over a ringing, "
     "wandering squeal.", crest=14.0, fade_out=0.08, max_dur=0.9)
def metal_scrape(r):
    n = ns(0.75)
    g = grind(r, 0.75, [(0, 18), (0.2, 60), (0.55, 45), (0.75, 10)], 1200, 5200, tau=(0.003, 0.012), base=0.35, grit=0.6)
    fq = curve(n, [(0, 1800), (0.3, 2600), (0.75, 1500)], log=True) * (1 + 0.03 * fnoise(r, n, hi=30))
    sq = norm_std(tv_biquad(white(r, n), "bp", fq, 16.0))
    env = swell(n, 1.0, 1.4)
    return mixn(n, (g * env, 1.0), (sq * env, 0.55))


@sfx("disc_whirr_loop", "metal", -26, 2, "Spinning disc: blade-pass buzz (11 Hz) on a 540 Hz harmonic stack with air "
     "and a high ring (loop 2.0 s).", loop=True, crest=12.0)
def disc_whirr_loop(r):
    L = 2.0
    n = ns(L)
    t = tarr(n)
    ph = r.uniform(0, TWO_PI)
    osc = harm_osc(np.full(n, 540.0), 22, 0.9, maxf=12000.0)
    am = 0.62 + 0.38 * lfo(n, 22, ph)
    air = snoise(r, n, m_bp(1800, 7500)) * loop_mod(r, n, 6, 0.4) * (0.6 + 0.4 * lfo(n, 22, ph + 0.7))
    ring = np.sin(TWO_PI * 1480.0 * t) * (0.5 + 0.5 * lfo(n, 22, ph + 1.4))
    return warm(mixn(n, (norm_std(osc) * am, 0.8), (air, 0.7), (ring, 0.07)), 1.3)


@sfx("magnet_hum_loop", "metal", -28, 1, "Magnetic field: 147 Hz mains-style hum with a slow 1.5 Hz beat, strong "
     "2nd-6th harmonics (phone-safe) and a faint mid band (loop 2.0 s).", loop=True, crest=10.0)
def magnet_hum_loop(r):
    L = 2.0
    n = ns(L)
    t = tarr(n)
    ph = r.uniform(0, TWO_PI)
    w = (0.35, 1.0, 0.8, 0.55, 0.4, 0.25, 0.15, 0.1, 0.06)
    a = sum(g * np.sin(TWO_PI * 147.0 * (k + 1) * t + ph * k) for k, g in enumerate(w))
    b = sum(g * np.sin(TWO_PI * 148.5 * (k + 1) * t + ph * k) for k, g in enumerate(w[1:4], 1))
    mid = snoise(r, n, m_bp(300, 1500)) * loop_mod(r, n, 2, 0.3)
    out = mixn(n, (a, 0.8), (b, 0.5), (mid, 0.35))
    return warm(out * (0.85 + 0.15 * lfo(n, 3, ph)), 1.5)


@sfx("metal_recall", "metal", -18, 2, "Metal called back to the hand: rising metallic zip with a shimmering tone and "
     "a small clink on arrival.", crest=16.0, fade_out=0.08, max_dur=0.8)
def metal_recall(r):
    n = ns(0.7)
    z = whoosh(r, 0.4, [(0, 500), (0.3, 5500), (0.4, 6000)], q=3.0, p=1.6, qq=1.2, air=0.15)
    t4 = tarr(ns(0.4))
    fr = 900 * 4 ** (t4 / 0.4)
    sh = np.sin(TWO_PI * np.cumsum(fr) / SR) * swell(len(t4), 1.4, 1.0)
    out = mixn(n, (z, 1.0), (sh, 0.22))
    place(out, _bar(1250.0, 0.07, 0.3), 0.37, 0.9)
    place(out, click(r, 0.002, lo=3500), 0.37, 0.5)
    return reverb(r, out, 0.2, 0.1, 8000, 0.003)


@sfx("rod_hum_loop", "metal", -30, 2, "Resonant rod: pure 587.5 Hz tone with vibrato, a detuned beating twin and thin "
     "high shimmer (loop 2.0 s).", loop=True, crest=10.0)
def rod_hum_loop(r):
    L = 2.0
    n = ns(L)
    t = tarr(n)
    ph = r.uniform(0, TWO_PI)
    vib = 0.9 * np.sin(TWO_PI * 3 * t / L)
    y = np.zeros(n)
    for k, g in ((1, 1.0), (2, 0.35), (3, 0.2), (4, 0.08)):
        y += g * np.sin(TWO_PI * 587.5 * k * t + k * vib + ph)
        y += 0.55 * g * np.sin(TWO_PI * 589.5 * k * t + ph * 0.7)
    sh = snoise(r, n, m_bp(5000, 9500)) * loop_mod(r, n, 5, 0.6)
    return mixn(n, (y, 0.8), (sh, 0.06)) * (0.88 + 0.12 * lfo(n, 2, ph))


# --- sand / glass ------------------------------------------------------------


@sfx("sand_hiss", "sand", -24, 2, "Sand pouring: dry bright hiss (3.5-5 kHz) with a grain tick scatter.",
     fade_out=0.1, max_dur=0.9)
def sand_hiss(r):
    n = ns(0.8)
    hiss = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 5200), (0.8, 3600)], log=True), 0.7)) * swell(n, 0.8, 1.6)
    body = fnoise(r, n, lo=400, hi=1800) * swell(n, 1.0, 1.6)
    return mixn(n, (hiss, 0.9), (body, 0.18), (_grains(r, n, 70, 0.02, 0.7, 2000, 7000), 0.5))


@sfx("sand_burst", "sand", -16, 3, "Sand blast: wide dry noise puff that darkens quickly, a spray of grains and a soft "
     "pressure thump.", crest=16.0, fade_out=0.06, max_dur=0.7)
def sand_burst(r):
    n = ns(0.6)
    t = tarr(n)
    puff = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 6500), (0.3, 2200)], log=True), 0.7)) * ad(t, 0.0008, 0.07)
    gr = np.zeros(n)
    crackle(r, gr, poisson(r, 0.45, 160, lambda x: np.exp(-x / 0.2)), 1500, 8000, 0.0005, 0.002, amp=0.8)
    return mixn(n, (puff, 1.0), (gr, 0.55), (thump(0.2, 200, 110, 0.015, 0.04, drive=1.5), 0.55),
                (fnoise(r, n, lo=5000) * ad(t, 0.002, 0.04), 0.2))


@sfx("sand_surge_loop", "sand", -26, 2, "Sand stream rushing: dense grain wash with slow surges (loop 2.0 s).",
     loop=True, crest=12.0)
def sand_surge_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    sg = 0.65 + 0.35 * lfo(n, 2, ph)
    wash = snoise(r, n, m_bp(800, 6500)) * loop_mod(r, n, 15, 0.5)
    gr = np.zeros(n)
    crackle(r, gr, strat(r, 380, L), 1500, 8000, 0.0005, 0.002, amp=0.8, wrap=True)
    low = snoise(r, n, m_bp(180, 700)) * loop_mod(r, n, 3, 0.4)
    return warm(mixn(n, (wash, 0.8), (gr, 0.5), (low, 0.35)) * sg, 1.3)


@sfx("sandstorm_loop", "sand", -24, 1, "Sandstorm: wide howling wind with wandering formants, driving grit and a low "
     "roar; slow gusts (loop 3.0 s).", loop=True, crest=12.0)
def sandstorm_loop(r):
    L = 3.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    gust = np.clip(0.7 + 0.25 * lfo(n, 1, ph) + 0.15 * lfo(n, 3, ph * 1.4), 0.2, None)
    h1 = tv_loop(snoise(r, n, m_bp(200, 3500)), "bp", 900 * 2 ** (0.7 * lfo(n, 3, ph)), 3.0)
    h2 = tv_loop(snoise(r, n, m_bp(300, 5000)), "bp", 1700 * 2 ** (0.6 * lfo(n, 3, ph + 2.1)), 3.5)
    rum = snoise(r, n, m_bp(90, 450)) * loop_mod(r, n, 2, 0.4)
    grit = np.zeros(n)
    crackle(r, grit, strat(r, 700, L), 800, 6500, 0.0005, 0.002, amp=0.7, wrap=True)
    roar = snoise(r, n, m_bp(500, 3000)) * (0.6 + 0.4 * lfo(n, 3, ph + 1.0))
    out = mixn(n, (norm_std(h1), 0.7), (norm_std(h2), 0.45), (rum, 0.45), (grit, 0.5), (roar, 0.4))
    return warm(out * gust, 1.3)


@sfx("quicksand_loop", "sand", -26, 1, "Quicksand: hollow suction gurgle, slow sucking sweeps, wet gulps and a little "
     "grit (loop 2.0 s).", loop=True, crest=12.0)
def quicksand_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    low = snoise(r, n, mprod(m_lp(900, 2), m_hp(120, 2))) * (0.6 + 0.4 * lfo(n, 2, ph)) * loop_mod(r, n, 4, 0.4)
    suck = tv_loop(snoise(r, n, m_bp(200, 2500)), "bp", 450 * 2 ** (0.8 * lfo(n, 2, ph + 0.6)), 3.0)
    gulp = _bubbles(r, n, (np.arange(14) + r.uniform(0.1, 0.9, 14)) / 14 * L, 400, 1300, 0.02, 0.05, 0.5, 0.9, wrap=True)
    gr = np.zeros(n)
    crackle(r, gr, strat(r, 120, L), 2000, 6000, 0.0005, 0.002, amp=0.5, wrap=True)
    return warm(mixn(n, (low, 0.8), (norm_std(suck), 0.55), (gulp, 0.9), (gr, 0.3)), 1.4)


@sfx("glass_fuse", "sand", -20, 2, "Sand fusing to glass: accelerating grain ticks, a rising crystalline shimmer and a "
     "bright glassy ring as it sets.", crest=18.0, fade_out=0.1, max_dur=1.1)
def glass_fuse(r):
    n = ns(1.0)
    t = tarr(n)
    tk = np.zeros(n)
    crackle(r, tk, poisson(r, 0.65, 130, lambda x: 0.15 + 0.85 * (x / 0.65) ** 1.5), 2000, 8500, 0.0004, 0.0015, amp=0.9)
    sw = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 2500), (0.6, 9000), (1.0, 9000)], log=True), 2.5)) * \
        swell(n, 2.0, 1.4)
    sh = np.zeros(n)
    for i, f in enumerate((D6, FS5 * 2, A5 * 2, D6 * 2)):
        place(sh, tone(f, 0.5, 0.12, ((1, 1.0, 1.0), (2, 0.2, 0.5)), att=0.05, bend=-0.08, bend_tau=0.15), 0.2 + 0.07 * i, 0.5)
    out = mixn(n, (tk, 0.8), (sw, 0.5), (sh, 0.35))
    place(out, _ping(r, D6 * 2, ns(0.5), 0.9), 0.6, 0.9)
    return reverb(r, out, 0.4, 0.15, 9500, 0.004)


@sfx("glass_shatter", "sand", -14, 3, "Glass breaking: sharp bright crack and a shower of pitched shards (2.5-9.5 kHz) "
     "with sparse chimes. No body knock, which separates it from ice_shatter.", crest=18.0, fade_out=0.1, max_dur=1.0)
def glass_shatter(r):
    n = ns(0.95)
    crk = mixn(n, (burst(r, 0.02, lo=2000, att=0.0001, tau=0.0005), 1.0),
               (burst(r, 0.1, lo=3500, hi=14000, att=0.0002, tau=0.008), 0.9))
    sh = np.zeros(n)
    crackle(r, sh, np.sort(r.uniform(0.01, 0.6, 46) ** 1.3), 2500, 9500, 0.02, 0.09, amp=0.9, power=1.5)
    ch = np.zeros(n)
    for f in (D6, FS5 * 2, A5 * 2):
        place(ch, dsine(f * r.uniform(0.98, 1.02), 0.09), r.uniform(0.03, 0.3), r.uniform(0.3, 0.6))
    out = mixn(n, (crk, 1.0), (sh, 0.6), (ch, 0.45), (burst(r, 0.3, lo=5000, att=0.005, tau=0.07), 0.1))
    return reverb(r, out, 0.4, 0.16, 9500, 0.004)


# --- magma -------------------------------------------------------------------


@sfx("magma_glob", "magma", -18, 3, "Molten blob: wet slap, a viscous low-mid bloop that sags in pitch, and a quick "
     "sizzle.", crest=14.0, fade_out=0.08, max_dur=0.7)
def magma_glob(r):
    n = ns(0.6)
    t = tarr(n)
    sz = np.zeros(n)
    crackle(r, sz, np.sort(r.uniform(0.03, 0.4, 14)), 1500, 5000, 0.0008, 0.003, amp=0.6)
    out = mixn(n, (dsine(190, 0.06, g=0.6, ln=6.0), 0.9), (dsine(320, 0.035, g=0.8, ln=6.0), 0.5),
               (thump(0.25, 190, 95, 0.02, 0.06, drive=2.5), 0.7),
               (burst(r, 0.12, lo=300, hi=2200, att=0.001, tau=0.03), 0.8),
               (click(r, 0.003, lo=1500), 0.3), (sz, 0.4),
               (fnoise(r, n, lo=3000, hi=7000) * ad(t, 0.01, 0.06), 0.12))
    return filt(filt(out, "lp", 4200, 2), "hp", 130, 2)


@sfx("magma_surge", "magma", -15, 2, "Magma surging: dark roar swelling upward with a heavy thump, thick bubbles and "
     "crackle.", crest=16.0, fade_out=0.12, max_dur=1.2)
def magma_surge(r):
    n = ns(1.2)
    t = tarr(n)
    roar = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 300), (0.4, 1800), (1.2, 500)], log=True), 0.8)) * swell(n, 1.3, 1.1)
    mid = fnoise(r, n, lo=200, hi=900) * swell(n, 1.2, 1.4)
    bub = _bubbles(r, n, np.sort(r.uniform(0.1, 1.0, 9)), 90, 300, 0.05, 0.12, 0.35, 0.9, pop=0.2)
    cr = np.zeros(n)
    crackle(r, cr, poisson(r, 1.1, 40), 1000, 4000, 0.001, 0.004, amp=0.6)
    out = mixn(n, (roar, 1.0), (mid, 0.5), (bub, 0.5), (cr, 0.3),
               (thump(0.5, 140, 55, 0.05, 0.14, drive=3.0), 0.8),
               (thump(0.25, 280, 160, 0.015, 0.05, drive=2.0), 0.5))
    return reverb(r, out, 0.4, 0.15, 4000)


@sfx("obsidian_set", "magma", -20, 2, "Lava setting to black glass: dwindling resonant ticks and a dark glassy ring "
     "(single fall, fewer shards than glass_shatter).", crest=16.0, fade_out=0.1, max_dur=0.9)
def obsidian_set(r):
    n = ns(0.8)
    tk = np.zeros(n)
    for i, (tt, f) in enumerate(((0.0, 2400), (0.07, 1900), (0.16, 1500), (0.28, 1200), (0.42, 1000))):
        place(tk, dsine(f, 0.012 + 0.003 * i, phase=i), tt, 0.9 - 0.14 * i)
        place(tk, click(r, 0.002, lo=1600), tt, 0.4 - 0.05 * i)
    ring = modal([1046, 1630, 2400, 3100], [1.0, 0.6, 0.4, 0.2], [0.12, 0.09, 0.06, 0.04], dur=0.5)
    out = mixn(n, (tk, 0.9), (ring, 0.45), (burst(r, 0.2, lo=700, hi=3500, att=0.001, tau=0.04), 0.25))
    return filt(reverb(r, out, 0.3, 0.14, 6000, 0.004), "lp", 5500, 2)


@sfx("lava_hiss_quench", "magma", -16, 2, "Lava hit by water: big hiss burst with a sputter of low pops, a dull thump "
     "and a crackle tail (steam_jet is cleaner, higher and has no pops).", crest=16.0, fade_out=0.12, max_dur=1.2)
def lava_hiss_quench(r):
    n = ns(1.1)
    t = tarr(n)
    hiss = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 2000), (0.4, 5500), (1.1, 7000)], log=True), 0.6)) * \
        ad(t, 0.01, 0.38) * (1 + 0.3 * fnoise(r, n, hi=30))
    pops = _bubbles(r, n, np.sort(r.uniform(0.0, 0.5, 14)), 150, 600, 0.01, 0.03, 0.3, 0.8, pop=0.3)
    cr = np.zeros(n)
    crackle(r, cr, poisson(r, 0.9, 60, lambda x: np.exp(-x / 0.35)), 1200, 5500, 0.0008, 0.004, amp=0.7)
    out = mixn(n, (hiss, 1.0), (pops, 0.6), (cr, 0.45), (fnoise(r, n, lo=300, hi=1200) * ad(t, 0.01, 0.2), 0.3),
               (thump(0.3, 190, 80, 0.03, 0.08, drive=2.6), 0.6))
    return reverb(r, out, 0.35, 0.14, 6000)


# --- water / ice / mist / plant -----------------------------------------------


@sfx("wave_rush_loop", "water", -24, 2, "Tidal wave rushing: broad churning water with slow surges, foam top and a "
     "weighty low band (loop 2.0 s).", loop=True, crest=12.0)
def wave_rush_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    sg = np.clip(0.65 + 0.35 * lfo(n, 1, ph) + 0.12 * lfo(n, 3, ph * 1.5), 0.15, None)
    flow = snoise(r, n, m_bp(150, 3200)) * loop_mod(r, n, 6, 0.4)
    mid = snoise(r, n, m_bp(600, 2500)) * loop_mod(r, n, 12, 0.5)
    foam = snoise(r, n, m_bp(3000, 9000)) * loop_mod(r, n, 18, 0.7) * (0.5 + 0.5 * lfo(n, 1, ph + 0.8))
    low = snoise(r, n, m_bp(70, 320)) * loop_mod(r, n, 2, 0.4)
    bub = _bubbles(r, n, (np.arange(18) + r.uniform(0.1, 0.9, 18)) / 18 * L, 600, 2600, 0.01, 0.03, 0.6, 0.5, wrap=True)
    return mixn(n, (flow, 0.8), (mid, 0.4), (foam, 0.3), (low, 0.35), (bub, 0.5)) * sg


@sfx("water_jet_loop", "water", -26, 2, "Pressurised water jet: tight steady 2.5-9 kHz hiss with a narrow resonant "
     "band and a light flutter (loop 2.0 s).", loop=True, crest=10.0)
def water_jet_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    hs = snoise(r, n, m_bp(2500, 9500)) * loop_mod(r, n, 25, 0.15)
    nb = tv_loop(snoise(r, n, m_bp(1500, 6000)), "bp", 4200 * 2 ** (0.12 * lfo(n, 2, ph)), 4.0)
    body = snoise(r, n, m_bp(300, 1200)) * loop_mod(r, n, 8, 0.2)
    return mixn(n, (hs, 0.8), (norm_std(nb), 0.5), (body, 0.3)) * (0.9 + 0.1 * lfo(n, 5, ph))


@sfx("ice_wall_raise", "ice", -16, 2, "Ice wall growing: rising stream of crystal ticks, a creaking resonance, then a "
     "solid thunk and glassy ring.", crest=16.0, fade_out=0.12, max_dur=1.1)
def ice_wall_raise(r):
    n = ns(1.0)
    tk = np.zeros(n)
    crackle(r, tk, poisson(r, 0.7, 150, lambda x: 0.15 + 0.85 * (x / 0.7)), 2500, 9000, 0.004, 0.02, amp=0.8, g=0.0)
    cq = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 400), (0.35, 900), (0.7, 1400)], log=True) *
                            (1 + 0.04 * fnoise(r, n, hi=25)), 8.0)) * swell(n, 1.0, 1.2)
    out = mixn(n, (tk, 0.8), (cq, 0.4), (fnoise(r, n, lo=4500) * swell(n, 1.4, 1.4), 0.08))
    place(out, thump(0.25, 260, 120, 0.015, 0.05, drive=2.0), 0.7, 0.9)
    place(out, click(r, 0.003, lo=2000), 0.7, 0.6)
    place(out, modal([1568, 2400, 3500], [1.0, 0.5, 0.3], [0.1, 0.07, 0.05], dur=0.3), 0.7, 0.5)
    return reverb(r, out, 0.35, 0.14, 8000)


@sfx("ice_crack", "ice", -17, 3, "Ice cracking: sharp crack with a falling resonant chirp, then two smaller dwindling "
     "cracks.", crest=18.0, fade_out=0.08, max_dur=0.6)
def ice_crack(r):
    n = ns(0.5)
    out = np.zeros(n)
    for tt, g in ((0.0, 1.0), (0.055, 0.5), (0.12, 0.28)):
        place(out, click(r, 0.002, lo=2500), tt, 0.7 * g)
        place(out, burst(r, 0.08, lo=1500, hi=12000, att=0.0002, tau=0.006), tt, g)
        place(out, dsine(2800 * (1 - 0.1 * tt * 8), 0.04, g=-0.5), tt, 0.6 * g)
    place(out, thump(0.1, 300, 160, 0.01, 0.03, drive=1.4), 0.0, 0.3)
    return reverb(r, out, 0.3, 0.14, 9000, 0.003)


@sfx("ice_slide_loop", "ice", -28, 1, "Sliding on ice: smooth silky hiss, a squeaking resonance that drifts, and sparse "
     "scrape ticks (loop 2.0 s).", loop=True, crest=12.0)
def ice_slide_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    hs = snoise(r, n, m_bp(2500, 8000)) * loop_mod(r, n, 8, 0.3) * (0.7 + 0.3 * lfo(n, 2, ph))
    sq = tv_loop(snoise(r, n, m_bp(1500, 5000)), "bp", 3200 * 2 ** (0.3 * lfo(n, 3, ph)), 12.0)
    lowb = snoise(r, n, m_bp(150, 700)) * loop_mod(r, n, 4, 0.4)
    tk = np.zeros(n)
    crackle(r, tk, strat(r, 24, L), 2000, 7000, 0.001, 0.004, amp=0.6, wrap=True)
    return mixn(n, (hs, 0.8), (norm_std(sq), 0.35), (lowb, 0.35), (tk, 0.4))


@sfx("frost_hiss", "ice", -26, 2, "Frost forming: cold 5.5-12 kHz hiss with twinkling sparkles; colder and thinner "
     "than steam_hiss.", fade_out=0.1, max_dur=0.9)
def frost_hiss(r):
    n = ns(0.8)
    t = tarr(n)
    hs = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 7000), (0.8, 9000)], log=True), 0.8)) * swell(n, 0.9, 1.5)
    sp = np.zeros(n)
    crackle(r, sp, np.sort(r.uniform(0.05, 0.7, 24)), 4000, 10000, 0.01, 0.03, amp=0.8, power=1.5)
    tw = tone(D6 * 2, 0.4, 0.1, ((1, 1.0, 1.0),), att=0.03) * 0.5
    out = mixn(n, (hs, 0.8), (sp, 0.45), (fnoise(r, n, lo=500, hi=2000) * ad(t, 0.1, 0.2), 0.08))
    place(out, tw, 0.25, 0.12)
    return out


@sfx("mist_loop", "mist", -34, 1, "Fog bank: soft diffuse hush, slowly drifting; very quiet shimmering top and rare "
     "tiny droplets (loop 3.0 s).", loop=True, crest=10.0)
def mist_loop(r):
    L = 3.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    hush = snoise(r, n, m_bp(1200, 7000)) * loop_mod(r, n, 0.7, 0.3)
    shim = snoise(r, n, m_bp(3500, 9000)) * (0.6 + 0.4 * lfo(n, 2, ph))
    dr = np.zeros(n)
    crackle(r, dr, strat(r, 7, L), 3000, 7000, 0.01, 0.03, amp=0.5, power=1.5, wrap=True)
    return mixn(n, (hush, 0.8), (shim, 0.4), (dr, 0.18)) * (0.8 + 0.2 * lfo(n, 1, ph + 1.0))


@sfx("steam_jet", "mist", -17, 3, "Hard steam jet: sharp-onset 1.8-8 kHz hiss with fast flutter and a tiny pressure "
     "pop; cleaner and brighter than lava_hiss_quench.", crest=14.0, fade_out=0.1, max_dur=1.0)
def steam_jet(r):
    n = ns(0.9)
    t = tarr(n)
    hs = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 6500), (0.9, 3200)], log=True), 0.6)) * ad(t, 0.004, 0.3)
    hs *= 1 + 0.35 * fnoise(r, n, hi=60)
    return mixn(n, (hs, 1.0), (fnoise(r, n, lo=400, hi=1600) * ad(t, 0.006, 0.12), 0.3),
                (thump(0.1, 220, 120, 0.01, 0.03, drive=1.2), 0.35), (click(r, 0.002, lo=3000), 0.3))


@sfx("geyser", "water", -14, 2, "Geyser: rising rumble, a sudden roaring water column, a splash and falling droplets "
     "with steam.", crest=16.0, fade_out=0.15, max_dur=1.2)
def geyser(r):
    n = ns(1.3)
    t = tarr(n)
    rum = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 200), (0.4, 900), (1.3, 500)], log=True), 0.8)) * ad(t, 0.15, 0.5)
    col = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0.3, 800), (0.5, 2800), (1.2, 1600)], log=True), 0.9)) * \
        np.clip(ad(t - 0.3, 0.05, 0.4), 0, None) * (t > 0.3)
    st = fnoise(r, n, lo=3500, hi=9000) * np.clip(ad(t - 0.35, 0.1, 0.4), 0, None) * (t > 0.35)
    dr = _bubbles(r, n, np.sort(r.uniform(0.55, 1.2, 22)), 900, 3000, 0.01, 0.03, 0.6, 0.55)
    out = mixn(n, (rum, 0.7), (col, 1.0), (st, 0.3), (dr, 0.55))
    place(out, burst(r, 0.2, lo=800, hi=9000, att=0.002, tau=0.05), 0.42, 0.7)
    place(out, thump(0.3, 170, 70, 0.03, 0.08, drive=2.4), 0.4, 0.6)
    return reverb(r, out, 0.3, 0.12, 7000)


@sfx("vine_creak", "plant", -22, 2, "Vine straining: slow stick-slip woody creak (pulse train through a drifting "
     "~600 Hz formant) with small fibre ticks.", crest=14.0, fade_out=0.1, max_dur=0.9)
def vine_creak(r):
    n = ns(0.8)
    rate = curve(n, [(0, 80), (0.25, 55), (0.5, 90), (0.8, 40)], log=True) * (1 + 0.05 * fnoise(r, n, hi=20))
    pulse = harm_osc(rate, 45, 0.6)
    fm = curve(n, [(0, 500), (0.15, 800), (0.3, 620), (0.5, 1100), (0.8, 640)], log=True)
    cr = norm_std(tv_biquad(pulse, "bp", fm, 5.0)) * swell(n, 0.8, 1.0) * (1 + 0.3 * fnoise(r, n, hi=18))
    return mixn(n, (cr, 1.0), (_grains(r, n, 14, 0.05, 0.7, 1500, 4500, 0.001, 0.004, 0.5), 0.35),
                (fnoise(r, n, lo=1800, hi=5000) * swell(n, 1.2, 1.2), 0.1))


@sfx("vine_snap", "plant", -17, 3, "Vine snapping: sharp woody crack, a brief twang and a fibrous tear.", crest=16.0,
     fade_out=0.06, max_dur=0.5)
def vine_snap(r):
    n = ns(0.35)
    out = mixn(n, (click(r, 0.002, lo=1500), 0.8), (burst(r, 0.06, lo=1200, hi=8000, att=0.0003, tau=0.004), 1.0),
               (dsine(780, 0.03, g=-0.3), 0.5), (thump(0.1, 260, 140, 0.01, 0.03, drive=1.5), 0.35))
    place(out, burst(r, 0.12, lo=2000, hi=9000, att=0.003, tau=0.025), 0.015, 0.45)
    return out


@sfx("vine_burn", "plant", -22, 2, "Green vine burning: wet sizzle and steam hiss with popping sap and crackle.",
     crest=14.0, fade_out=0.12, max_dur=1.1)
def vine_burn(r):
    n = ns(1.0)
    t = tarr(n)
    hs = norm_std(tv_biquad(white(r, n), "bp", curve(n, [(0, 6000), (1.0, 3500)], log=True), 0.6)) * ad(t, 0.03, 0.4)
    hs *= 1 + 0.4 * fnoise(r, n, hi=40)
    cr = np.zeros(n)
    crackle(r, cr, poisson(r, 0.9, 55, lambda x: np.exp(-x / 0.5)), 1000, 6000, 0.0008, 0.004, amp=0.9)
    pp = _bubbles(r, n, np.sort(r.uniform(0.03, 0.8, 9)), 600, 1800, 0.008, 0.02, 0.5, 0.6)
    return filt(mixn(n, (hs, 0.9), (cr, 0.6), (pp, 0.5)), "lp", 8000, 2)


# --- fire ---------------------------------------------------------------------


@sfx("fireball_whoosh", "fire", -17, 3, "Fireball passing: roaring whoosh that swells and falls with a low-mid body "
     "and crackle in its wake.", crest=16.0, fade_out=0.12, max_dur=1.0)
def fireball_whoosh(r):
    n = ns(0.9)
    w = whoosh(r, 0.85, [(0, 350), (0.35, 1800), (0.85, 700)], q=0.9, p=1.4, qq=1.6, body=0.9, air=0.15)
    cr = np.zeros(n)
    crackle(r, cr, np.sort(r.uniform(0.05, 0.85, 50)), 1200, 6000, 0.0008, 0.004, amp=0.8)
    roar = fnoise(r, n, lo=150, hi=900) * swell(n, 1.5, 1.8)
    return mixn(n, (w, 1.0), (cr, 0.5), (roar, 0.4)) * (1 + 0.15 * fnoise(r, n, hi=30))


@sfx("fire_field_loop", "fire", -26, 2, "Burning ground: dense crackle over a low breathing roar and faint hiss "
     "(loop 2.0 s).", loop=True, crest=12.0)
def fire_field_loop(r):
    L = 2.0
    n = ns(L)
    roar = snoise(r, n, m_bp(100, 1000)) * loop_mod(r, n, 3, 0.45)
    mid = snoise(r, n, m_bp(800, 3000)) * loop_mod(r, n, 14, 0.7)
    hiss = snoise(r, n, m_bp(4000, 9000)) * loop_mod(r, n, 20, 0.8)
    cr = np.zeros(n)
    crackle(r, cr, strat(r, 130, L), 800, 6000, 0.0008, 0.005, amp=1.0, power=2.2, wrap=True)
    return warm(mixn(n, (roar, 0.7), (mid, 0.3), (hiss, 0.15), (cr, 0.8)), 1.4)


@sfx("blue_roar_loop", "fire", -24, 1, "Blue flame: smooth turbine-like jet hiss with a resonant 2.2 kHz ring, a "
     "faint 1.65 kHz whine and almost no crackle (loop 2.0 s).", loop=True, crest=10.0)
def blue_roar_loop(r):
    L = 2.0
    n = ns(L)
    t = tarr(n)
    ph = r.uniform(0, TWO_PI)
    jet = snoise(r, n, m_bp(1500, 6500)) * loop_mod(r, n, 10, 0.15)
    ring = tv_loop(snoise(r, n, m_bp(800, 5000)), "bp", 2200 * 2 ** (0.2 * lfo(n, 2, ph)), 4.0)
    body = snoise(r, n, m_bp(200, 900)) * loop_mod(r, n, 3, 0.3)
    wh = np.sin(TWO_PI * 1650.0 * t) * (0.6 + 0.4 * lfo(n, 3, ph))
    cr = np.zeros(n)
    crackle(r, cr, strat(r, 14, L), 2500, 7000, 0.0006, 0.002, amp=0.4, wrap=True)
    return warm(mixn(n, (jet, 0.8), (norm_std(ring), 0.6), (body, 0.4), (wh, 0.05), (cr, 0.2)), 1.2)


@sfx("blue_ignite", "fire", -17, 3, "Blue flame lighting: spark tick, a tight gas 'fwoomp' and a rising whine that "
     "settles into a jet hiss.", crest=15.0, fade_out=0.1, max_dur=0.8)
def blue_ignite(r):
    n = ns(0.65)
    t = tarr(n)
    fw = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 1200), (0.08, 3500), (0.4, 1500)], log=True), 0.8)) * ad(t, 0.012, 0.12)
    wh = np.sin(TWO_PI * np.cumsum(900 * 4 ** (1 - np.exp(-t / 0.15))) / SR) * ad(t, 0.03, 0.15)
    jet = fnoise(r, n, lo=2500, hi=8000) * swell(n, 0.8, 1.5)
    out = mixn(n, (fw, 1.0), (wh, 0.18), (jet, 0.35), (thump(0.2, 170, 80, 0.02, 0.05, drive=2.0), 0.6))
    place(out, burst(r, 0.02, lo=3000, att=0.0002, tau=0.003), 0.0, 0.9)
    return out


@sfx("spark_snap", "fire", -22, 4, "Tiny electric snap: a sharp dry click with a fast falling zap and one or two "
     "after-ticks.", crest=16.0, fade_out=0.04, trim=-45)
def spark_snap(r):
    n = ns(0.2)
    out = mixn(n, (burst(r, 0.02, lo=2500, att=0.0001, tau=0.0006), 1.0),
               (burst(r, 0.05, lo=1500, hi=12000, att=0.0002, tau=0.004), 0.8),
               (dsine(3600, 0.012, g=-0.6), 0.5))
    place(out, burst(r, 0.02, lo=3000, att=0.0002, tau=0.002), 0.045, 0.4)
    place(out, burst(r, 0.02, lo=3500, att=0.0002, tau=0.002), 0.085, 0.25)
    place(out, dsine(2400, 0.02, g=-0.3), 0.045, 0.2)
    return reverb(r, out, 0.12, 0.2, 9000, 0.002)


@sfx("thunderclap", "fire", -11, 2, "Thunderclap: broadband rip then a heavy boom that rolls away in echoing rumbles. "
     "No zap or buzz tail, which separates it from lightning_strike.", crest=16.0, fade_out=0.25, max_dur=1.2)
def thunderclap(r):
    n = ns(1.4)
    t = tarr(n)
    rip = burst(r, 0.4, lo=300, hi=9000, att=0.0008, tau=0.05)
    boom = thump(0.9, 150, 55, 0.05, 0.28, drive=3.2)
    rum = fnoise(r, n, lo=120, hi=700) * ad(t, 0.03, 0.5) * (1 + 0.6 * fnoise(r, n, hi=9))
    out = mixn(n, (rip, 1.0), (boom, 0.9), (rum, 0.9), (fnoise(r, n, lo=3000) * ad(t, 0.001, 0.05), 0.2))
    for tt, g in ((0.25, 0.5), (0.5, 0.35), (0.8, 0.2)):
        place(out, filt(burst(r, 0.5, lo=100, hi=900, att=0.05, tau=0.12), "lp", 700, 2), tt, g)
    return filt(reverb(r, out, 0.7, 0.35, 3500), "hp", 110, 2)


@sfx("static_crackle_loop", "fire", -32, 1, "Static charge field: sparse dry sparks and snaps with a faint high "
     "hiss and no tonal hum (loop 2.0 s).", loop=True, crest=14.0)
def static_crackle_loop(r):
    L = 2.0
    n = ns(L)
    cr = np.zeros(n)
    crackle(r, cr, strat(r, 34, L), 2500, 10000, 0.0004, 0.0012, amp=1.0, power=1.6, wrap=True)
    sn = np.zeros(n)
    crackle(r, sn, strat(r, 8, L), 1500, 5000, 0.001, 0.004, amp=1.0, power=1.2, wrap=True)
    hs = snoise(r, n, m_bp(4000, 10000)) * loop_mod(r, n, 10, 0.8)
    return mixn(n, (cr, 0.8), (sn, 0.6), (hs, 0.06))


def _explosion(r, dur, f0, boom_tau, rumble_tau, debris, rt, wet, burst_tau, hp=130):
    n = ns(dur)
    t = tarr(n)
    deb = np.zeros(n)
    crackle(r, deb, poisson(r, dur * 0.8, debris, lambda x: np.exp(-x / (dur * 0.35))), 400, 5000, 0.002, 0.012, amp=0.8)
    out = mixn(n, (click(r, 0.003, lo=1500), 0.6),
               (burst(r, 0.3, lo=300, hi=9000, att=0.0006, tau=burst_tau), 1.0),
               (thump(dur, f0, f0 * 0.35, 0.04, boom_tau, drive=3.4), 1.1),
               (thump(0.3, f0 * 2.2, f0 * 1.2, 0.012, 0.05, drive=2.0), 0.7),
               (fnoise(r, n, lo=150, hi=1400) * ad(t, 0.01, rumble_tau), 0.8),
               (deb, 0.5))
    return filt(reverb(r, out, rt, wet, 5000), "hp", hp, 2)


@sfx("explosion_small", "fire", -15, 3, "Small explosion: short sharp bang, tight thump and a handful of fragments.",
     crest=16.0, fade_out=0.1, max_dur=0.8)
def explosion_small(r):
    return _explosion(r, 0.65, 230.0, 0.09, 0.15, 25, 0.25, 0.18, 0.035, hp=150)


@sfx("explosion_large", "fire", -9, 2, "Large explosion: a deep long boom, a long rolling rumble and a dense debris "
     "shower; scale (length, weight, level) grows with tier.", crest=16.0, fade_out=0.25, max_dur=1.2)
def explosion_large(r):
    x = _explosion(r, 1.4, 170.0, 0.25, 0.45, 70, 0.7, 0.3, 0.09, hp=120)
    place(x, filt(burst(r, 0.5, lo=100, hi=900, att=0.04, tau=0.14), "lp", 800, 2), 0.35, 0.35)
    return x


@sfx("fuse_tick", "fire", -26, 4, "Burning fuse: a woody tick over a short sizzle; repeat for a countdown.",
     crest=12.0, fade_out=0.05, max_dur=0.4)
def fuse_tick(r):
    n = ns(0.22)
    t = tarr(n)
    sz = fnoise(r, n, lo=3000, hi=9000) * ad(t, 0.004, 0.06)
    cr = np.zeros(n)
    crackle(r, cr, np.sort(r.uniform(0.0, 0.15, 7)), 1800, 6000, 0.0006, 0.002, amp=0.8)
    return mixn(n, (click(r, 0.002, lo=2000), 0.8), (dsine(1800, 0.01), 0.6), (sz, 0.35), (cr, 0.4))


# --- air ------------------------------------------------------------------------


@sfx("crescent_whoosh", "air", -19, 3, "Blade of air: a thin, fast, high band sweep (1.4-5 kHz) with a singing edge "
     "tone.", crest=14.0, fade_out=0.08, max_dur=0.7)
def crescent_whoosh(r):
    n = ns(0.55)
    w = whoosh(r, 0.5, [(0, 1400), (0.2, 5200), (0.5, 2600)], q=3.2, p=1.0, qq=2.0, air=0.3)
    ed = tone(2637, 0.3, 0.07, ((1, 1.0, 1.0),), att=0.02, bend=-0.25, bend_tau=0.1)
    return mixn(n, (w, 1.0), (ed, 0.1))


@sfx("tornado_loop", "air", -24, 1, "Tornado: two wandering howl formants rotating once per second over a low roar, "
     "debris ticks and a wide mid wash (loop 3.0 s).", loop=True, crest=12.0)
def tornado_loop(r):
    L = 3.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    h1 = tv_loop(snoise(r, n, m_bp(200, 3500)), "bp", 900 * 2 ** (0.7 * lfo(n, 3, ph)), 3.0)
    h2 = tv_loop(snoise(r, n, m_bp(300, 5000)), "bp", 1700 * 2 ** (0.6 * lfo(n, 3, ph + 2.1)), 3.5)
    rum = snoise(r, n, m_bp(80, 420)) * loop_mod(r, n, 2, 0.4)
    deb = np.zeros(n)
    crackle(r, deb, strat(r, 60, L), 600, 4000, 0.001, 0.004, amp=0.6, wrap=True)
    roar = snoise(r, n, m_bp(500, 3000)) * (0.6 + 0.4 * lfo(n, 3, ph + 1.0))
    return warm(mixn(n, (norm_std(h1), 0.8), (norm_std(h2), 0.5), (rum, 0.5), (deb, 0.3), (roar, 0.4)), 1.3)


@sfx("vacuum_implode", "air", -16, 2, "Air collapsing inward: a falling, ever louder suction sweep that ends in a "
     "sudden pop and a hollow breath.", crest=16.0, fade_out=0.1, max_dur=1.0)
def vacuum_implode(r):
    n = ns(0.95)
    w = whoosh(r, 0.58, [(0, 5500), (0.4, 1800), (0.58, 500)], q=1.0, p=3.0, qq=0.5, air=0.1)
    out = mixn(n, (w, 1.0))
    place(out, click(r, 0.003, lo=1200), 0.56, 1.0)
    place(out, thump(0.25, 250, 100, 0.012, 0.05, drive=2.2), 0.56, 0.8)
    place(out, burst(r, 0.3, lo=300, hi=1400, att=0.01, tau=0.09), 0.58, 0.45)
    return reverb(r, out, 0.3, 0.12, 5000)


@sfx("vacuum_loop", "air", -28, 1, "Vacuum pocket: a hollow, endlessly falling Shepard glide under a dull 200-1400 Hz "
     "breath; feels like absence (loop 2.0 s).", loop=True, crest=12.0)
def vacuum_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    fall = norm_std(shepard(L, 27.5, 10, 600.0, 0.9, harm=((1, 1.0), (2, 0.25))))[::-1]
    br = snoise(r, n, m_bp(200, 1400)) * loop_mod(r, n, 3, 0.3)
    ho = tv_loop(snoise(r, n, m_bp(300, 3000)), "bp", 800 * 2 ** (-0.5 * lfo(n, 1, ph)), 5.0)
    return mixn(n, (fall, 0.5), (br, 0.6), (norm_std(ho), 0.3))


@sfx("sonic_boom", "air", -10, 2, "Sonic boom: two crisp shock cracks 35 ms apart, a hard pressure thump and a "
     "falling airy wash; dry and short (thunderclap rolls, this does not).", crest=16.0, fade_out=0.15, max_dur=1.0)
def sonic_boom(r):
    n = ns(1.0)
    t = tarr(n)
    out = mixn(n, (thump(0.5, 220, 80, 0.02, 0.12, drive=3.0), 0.9),
               (norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 7000), (0.7, 1200)], log=True), 0.8)) * ad(t, 0.004, 0.18), 0.5))
    place(out, burst(r, 0.12, lo=800, hi=14000, att=0.0001, tau=0.006), 0.0, 1.0)
    place(out, burst(r, 0.12, lo=800, hi=14000, att=0.0001, tau=0.007), 0.035, 0.85)
    place(out, click(r, 0.002, lo=3000), 0.0, 0.6)
    place(out, click(r, 0.002, lo=3000), 0.035, 0.5)
    return filt(reverb(r, out, 0.5, 0.2, 6000), "hp", 140, 2)


@sfx("roar_wave", "air", -14, 2, "Roar wave: a rough falling 190-85 Hz voice-like buzz opened by sweeping vowel "
     "formants, with breath noise and a heavy front.", crest=14.0, fade_out=0.15, max_dur=1.2)
def roar_wave(r):
    n = ns(1.15)
    t = tarr(n)
    f = curve(n, [(0, 190), (0.25, 150), (1.15, 85)], log=True) * (1 + 0.04 * fnoise(r, n, hi=25))
    osc = harm_osc(f, 40, 1.0)
    f1 = norm_std(tv_biquad(osc, "bp", curve(n, [(0, 500), (0.3, 1100), (1.15, 420)], log=True), 2.0))
    f2 = norm_std(tv_biquad(osc, "bp", np.full(n, 2400.0), 3.0))
    env = ad(t, 0.06, 0.5)
    out = mixn(n, (f1 * env, 1.0), (f2 * env, 0.35), (fnoise(r, n, lo=600, hi=3500) * env, 0.3),
               (thump(0.4, 180, 70, 0.03, 0.1, drive=2.4), 0.4))
    return warm(out, 1.4)


@sfx("echo_ping", "air", -22, 2, "Sonar ping: a soft A5 sine ping repeated as three ever darker, quieter echoes "
     "(single plain ping, unlike the glassy deflect ring).", fade_out=0.12, max_dur=1.0)
def echo_ping(r):
    n = ns(0.95)
    h = ((1, 1.0, 1.0), (2, 0.15, 0.5))
    out = np.zeros(n)
    for i, (tt, g, lp) in enumerate(((0.0, 1.0, 9000), (0.2, 0.55, 3500), (0.4, 0.3, 2400), (0.6, 0.16, 1700))):
        place(out, filt(tone(A5 * (1 - 0.01 * i), 0.25, 0.04, h, att=0.002), "lp", lp, 2), tt, g)
    place(out, burst(r, 0.02, lo=3000, att=0.0002, tau=0.003), 0.0, 0.25)
    return reverb(r, out, 0.3, 0.14, 6000)


@sfx("flight_loop", "air", -26, 1, "Sustained flight: steady rushing wind with a wandering mid band, thin whistle and "
     "gentle flutter; fuller than glide_loop (loop 2.0 s).", loop=True, crest=12.0)
def flight_loop(r):
    L = 2.0
    n = ns(L)
    ph = r.uniform(0, TWO_PI)
    rush = tv_loop(snoise(r, n, m_bp(300, 4000)), "bp", 1300 * 2 ** (0.4 * lfo(n, 2, ph)), 1.5)
    air = snoise(r, n, m_bp(3000, 8500)) * loop_mod(r, n, 4, 0.4)
    low = snoise(r, n, m_bp(80, 420)) * loop_mod(r, n, 2, 0.35)
    wh = tv_loop(snoise(r, n, m_bp(1000, 4000)), "bp", 1900 * 2 ** (0.15 * lfo(n, 2, ph + 1.0)), 12.0)
    return mixn(n, (norm_std(rush), 0.8), (air, 0.35), (low, 0.4), (norm_std(wh), 0.18)) * (0.88 + 0.12 * lfo(n, 6, ph))


# --- system ---------------------------------------------------------------------
# Charge cues rise in size with the tier (0.3 / 0.45 / 0.7 s, -22 / -19 / -16 dB). The plain names are the
# defaults; AudioDirector.play(..., pitch) shifts them per element (earth 0.75, water 1.0, fire 1.26, air 1.5).


@sfx("charge_t1", "system", -22, 2, "Charge tier 1: one small rising D5 pluck with a faint airy sweep.",
     fade_out=0.06, max_dur=0.4)
def charge_t1(r):
    n = ns(0.35)
    h = ((1, 1.0, 1.0), (2, 0.25, 0.5), (3, 0.08, 0.4))
    out = mixn(n, (tone(D5, 0.25, 0.08, h, att=0.01, bend=-0.15, bend_tau=0.05), 1.0),
               (burst(r, 0.2, lo=2000, hi=5000, att=0.04, tau=0.05), 0.1))
    return reverb(r, out, 0.15, 0.1, 7000, 0.003)


@sfx("charge_t2", "system", -19, 2, "Charge tier 2: rising D5 to A5 pair over a held fifth, brighter and longer.",
     fade_out=0.08, max_dur=0.55)
def charge_t2(r):
    n = ns(0.5)
    h = ((1, 1.0, 1.0), (2, 0.3, 0.5), (3, 0.12, 0.4), (4, 0.05, 0.3))
    out = np.zeros(n)
    place(out, tone(D5, 0.3, 0.09, h, att=0.01, bend=-0.15, bend_tau=0.05), 0.0, 0.9)
    place(out, tone(A5, 0.35, 0.11, h, att=0.01, bend=-0.12, bend_tau=0.05), 0.09, 1.0)
    place(out, tone(D5 / 2, 0.4, 0.14, ((1, 1.0, 1.0), (2, 0.3, 0.6)), att=0.02), 0.0, 0.45)
    place(out, burst(r, 0.3, lo=2500, hi=7000, att=0.06, tau=0.07), 0.0, 0.1)
    return reverb(r, out, 0.25, 0.14, 8000, 0.003)


@sfx("charge_t3", "system", -16, 2, "Charge tier 3 (full): fast D5-A5-D6 arpeggio into a bell chord with sparkle and a "
     "low weight pulse.", crest=16.0, fade_out=0.12, max_dur=0.85)
def charge_t3(r):
    n = ns(0.8)
    h = ((1, 1.0, 1.0), (2, 0.3, 0.5), (3, 0.15, 0.4), (4, 0.06, 0.3))
    out = np.zeros(n)
    for i, f in enumerate((D5, A5, D6)):
        place(out, tone(f, 0.3, 0.09, h, att=0.008, bend=-0.1, bend_tau=0.04), 0.07 * i, 0.8 + 0.1 * i)
    for f, g in ((D6, 0.8), (FS5 * 2, 0.5), (A5, 0.6)):
        place(out, fm_bell(f, 0.5, 0.16, 1.0, 1.2, 0.06), 0.2, 0.4 * g)
    sp = np.zeros(n)
    crackle(r, sp, np.sort(r.uniform(0.1, 0.5, 14)), 5000, 10000, 0.01, 0.025, amp=0.7, power=1.5)
    out += 0.25 * sp
    place(out, thump(0.25, 220, 110, 0.02, 0.06, drive=2.0), 0.0, 0.4)
    place(out, burst(r, 0.4, lo=2500, hi=8000, att=0.08, tau=0.08), 0.0, 0.12)
    return reverb(r, out, 0.4, 0.18, 8500, 0.004)


@sfx("counter_success", "system", -13, 2, "Counter landed: a solid catch thump, then a warm rising D-major bell chord "
     "(D5-F#5-A5) over an upward sweep. A chord, so it can never be confused with the single deflect ping or the dull "
     "block thud.", crest=16.0, fade_out=0.15, max_dur=1.0)
def counter_success(r):
    n = ns(0.95)
    out = mixn(n, (thump(0.2, 260, 150, 0.015, 0.05, drive=2.0), 0.8), (click(r, 0.002, lo=2500), 0.4),
               (burst(r, 0.06, lo=500, hi=3500, att=0.0005, tau=0.012), 0.4))
    for tt, f, g in ((0.04, D5, 0.9), (0.09, FS5, 0.8), (0.14, A5, 1.0), (0.14, D6, 0.5)):
        place(out, fm_bell(f, 0.7, 0.2, 1.0, 1.3, 0.06), tt, 0.5 * g)
        place(out, tone(f, 0.7, 0.16, ((1, 1.0, 1.0), (2, 0.2, 0.5))), tt, 0.3 * g)
    m = ns(0.35)
    sw = norm_std(tv_biquad(white(r, m), "bp", curve(m, [(0, 1200), (0.3, 6000)], log=True), 2.5)) * swell(m, 1.4, 1.6)
    place(out, sw, 0.04, 0.1)
    return reverb(r, out, 0.45, 0.2, 8500, 0.005)


@sfx("counter_fail", "system", -16, 2, "Counter failed: a heavy muffled thump, a falling saturated buzz and a sinking "
     "noise wash. No ring, no bell; darker and harsher than challenge_fail.", crest=14.0, fade_out=0.1, max_dur=0.8)
def counter_fail(r):
    n = ns(0.7)
    t = tarr(n)
    f = curve(n, [(0, 420), (0.5, 130)], log=True)
    buz = norm_std(tv_biquad(harm_osc(f, 30, 0.8), "lp", curve(n, [(0, 1800), (0.5, 400)], log=True), 0.9)) * ad(t, 0.004, 0.18)
    wash = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 3000), (0.5, 300)], log=True), 0.8)) * ad(t, 0.004, 0.15)
    out = mixn(n, (thump(0.4, 180, 70, 0.04, 0.1, drive=2.8), 1.0), (warm(buz, 2.0), 0.8), (wash, 0.5),
               (burst(r, 0.08, lo=300, hi=1800, att=0.001, tau=0.02), 0.4))
    return filt(filt(out, "lp", 3200, 2), "hp", 130, 2)


@sfx("clash_solid", "system", -13, 3, "Two solid bodies meeting: a hard crack, a heavy thock, a short dead stone ring "
     "and a bounce echo; harder and cracklier than block.", crest=16.0, fade_out=0.1, max_dur=0.8)
def clash_solid(r):
    n = ns(0.7)
    out = mixn(n, (click(r, 0.003, lo=1800), 0.8), (burst(r, 0.15, lo=400, hi=7000, att=0.0004, tau=0.02), 0.95),
               (thump(0.3, 300, 170, 0.02, 0.06, drive=2.6), 0.9), (_stone_ring(520, 1.0, 0.35), 0.6),
               (_debris(r, n, 0.02, 0.35, 14, 400, 4000, 0.002, 0.008, 0.5, 1.2), 0.4))
    place(out, burst(r, 0.08, lo=600, hi=5000, att=0.0004, tau=0.015), 0.012, 0.5)
    return filt(reverb(r, out, 0.3, 0.16, 5500), "hp", 150, 2)


@sfx("clash_energy", "system", -13, 3, "Two energies colliding: a bright noise flash, a fast falling zap, crackle and a "
     "shimmering pressure thump; sizzly and unpitched compared with clash_solid.", crest=16.0, fade_out=0.12,
     max_dur=0.9)
def clash_energy(r):
    n = ns(0.8)
    t = tarr(n)
    fl = norm_std(tv_biquad(white(r, n), "lp", curve(n, [(0, 12000), (0.25, 2000)], log=True), 0.8)) * ad(t, 0.0006, 0.08)
    zp = np.sin(TWO_PI * np.cumsum(4000 * 0.1 ** (1 - np.exp(-t / 0.05))) / SR) * ad(t, 0.0005, 0.06)
    cr = np.zeros(n)
    crackle(r, cr, poisson(r, 0.6, 60, lambda x: np.exp(-x / 0.3)), 1500, 8000, 0.0006, 0.003, amp=0.9)
    sh = np.zeros(n)
    for f in (3000, 4200, 5600, 7000):
        place(sh, dsine(f * r.uniform(0.97, 1.03), 0.05), r.uniform(0.0, 0.1), 0.4)
    out = mixn(n, (fl, 1.0), (zp, 0.35), (cr, 0.5), (sh, 0.3), (thump(0.3, 140, 65, 0.025, 0.08, drive=2.6), 0.6))
    return reverb(r, out, 0.35, 0.16, 8000)



# ---------------------------------------------------------------------------
# finishing / output
# ---------------------------------------------------------------------------


def best_loop_start(x: np.ndarray) -> int:
    """Pick the rotation of a periodic loop whose seam is smoothest (small step, steady RMS across it)."""
    n = len(x)
    xx = np.concatenate([x, x, x])
    cs = np.concatenate([[0.0], np.cumsum(xx ** 2)])
    step_scale = float(np.sqrt(np.mean(np.diff(x) ** 2))) + 1e-12
    cost = {}
    for s0 in range(0, n, ns(0.005)):
        c = abs(xx[n + s0] - xx[n + s0 - 1]) / (4 * step_scale)
        for w, wt in ((ns(0.025), 1.0), (ns(0.1), 0.6)):
            b = np.sqrt((cs[n + s0] - cs[n + s0 - w]) / w) + 1e-9
            a = np.sqrt((cs[n + s0 + w] - cs[n + s0]) / w) + 1e-9
            c += wt * abs(20 * np.log10(a / b)) / 3.0
        cost[s0] = c
    return min(cost, key=cost.get)


def finish(x: np.ndarray, spec: Spec) -> np.ndarray:
    x = np.asarray(x, dtype=np.float64)
    if spec.crest:
        rms = float(np.sqrt(np.mean(x ** 2)))
        ceil = rms * 10 ** (spec.crest / 20)
        x = ceil * np.tanh(x / ceil)
    if spec.loop:
        x = circ_hp(x - x.mean(), 22.0)
        x = np.roll(x, -best_loop_start(x))
    else:
        x = x - x.mean()
        sos = signal.butter(2, 22.0, "highpass", fs=SR, output="sos")
        x = signal.sosfiltfilt(sos, x)
        pk = np.max(np.abs(x))
        w = ns(0.005)
        pad = (-len(x)) % w
        env = np.abs(np.concatenate([x, np.zeros(pad)])).reshape(-1, w).max(axis=1)
        idx = np.nonzero(env > pk * 10 ** (spec.trim / 20))[0]
        a = max(idx[0] * w - ns(0.001), 0)
        b = min((idx[-1] + 1) * w + ns(0.004), len(x))
        x = x[a:b].copy()
        capped = len(x) > ns(spec.max_dur)
        if capped:
            x = x[:ns(spec.max_dur)].copy()
        fo = ns(spec.fade_out if spec.fade_out else min(0.03, 0.12 * len(x) / SR))
        if capped:
            fo = max(fo, ns(0.15 if spec.max_dur > 0.5 else 0.05))
        fo = min(fo, len(x) // 2)
        fi = min(ns(spec.fade_in), len(x) // 2)
        if fi:
            x[:fi] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(fi) / fi)
        if fo:
            x[-fo:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(fo) / fo)
        x[-1] = 0.0
        win = np.sin(np.pi * (np.arange(len(x)) + 0.5) / len(x)) ** 2   # zero at both ends
        x = x - win * (np.mean(x) / np.mean(win))                      # remove residual DC, keep fades
    return x * (10 ** (spec.peak / 20) / np.max(np.abs(x)))


def wav_bytes(x: np.ndarray, loop: bool) -> bytes:
    q = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2")
    data = q.tobytes()
    fmt = struct.pack("<HHIIHH", 1, 1, SR, SR * 2, 2, 16)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    if loop:
        smpl = struct.pack("<9I", 0, 0, int(round(1e9 / SR)), 60, 0, 0, 0, 1, 0)
        smpl += struct.pack("<6I", 0, 0, 0, len(q) - 1, 0, 0)
        chunks += b"smpl" + struct.pack("<I", len(smpl)) + smpl
    chunks += b"data" + struct.pack("<I", len(data)) + data
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


def momentary_db(x: np.ndarray) -> float:
    """Loudest 100 ms RMS (50% hop) in dBFS."""
    w = ns(0.1)
    if len(x) <= w:
        return 20 * np.log10(max(float(np.sqrt(np.mean(x ** 2))), 1e-9))
    cs = np.concatenate([[0.0], np.cumsum(x ** 2)])
    starts = np.arange(0, len(x) - w + 1, w // 2)
    rms = np.sqrt((cs[starts + w] - cs[starts]) / w)
    return 20 * np.log10(max(float(rms.max()), 1e-9))


def suggested_volume(x: np.ndarray, level: float) -> float:
    """Volume (dB, <= 0) that brings this file's loudest 100 ms to its target playback level."""
    return float(np.clip(round((level - momentary_db(x)) * 2) / 2, -24.0, 0.0))


def render(spec: Spec) -> np.ndarray:
    global _CUR, _LOOP
    _CUR, _LOOP = spec.name, spec.loop
    raw = spec.fn(make_rng(spec.name))
    return finish(raw, spec)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    default_out = Path(__file__).resolve().parents[2] / "game" / "assets" / "audio"
    ap.add_argument("--out", type=Path, default=default_out, help="output directory (default: %(default)s)")
    ap.add_argument("--only", nargs="*", help="render only these names")
    ap.add_argument("--list", action="store_true", help="list sound names and exit")
    ap.add_argument("--audit", action="store_true", help="report layers that are cut off while still audible")
    args = ap.parse_args(argv)
    if args.list:
        for s in SOUNDS:
            print(f"{s.name:24s} {s.category:10s} {'loop' if s.loop else ''}")
        return 0
    names = [s.name for s in SOUNDS]
    if len(set(names)) != len(names):
        raise SystemExit("duplicate sound names")
    global AUDIT
    AUDIT = [] if args.audit else None
    todo = SOUNDS if not args.only else [s for s in SOUNDS if s.name in set(args.only)]
    if args.only and len(todo) != len(set(args.only)):
        raise SystemExit(f"unknown names: {sorted(set(args.only) - set(names))}")
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for spec in todo:
        x = render(spec)
        dur = len(x) / SR
        if spec.loop and not 1.0 <= dur <= 3.0:
            raise SystemExit(f"{spec.name}: loop length {dur:.3f}s outside 1-3 s")
        if not spec.loop and not 0.1 <= dur <= spec.max_dur + 1e-6:
            raise SystemExit(f"{spec.name}: one-shot length {dur:.3f}s outside 0.1-{spec.max_dur} s")
        (args.out / f"{spec.name}.wav").write_bytes(wav_bytes(x, spec.loop))
        manifest[spec.name] = {
            "category": spec.category,
            "loop": spec.loop,
            "duration_s": round(dur, 3),
            "suggested_volume_db": suggested_volume(x, spec.level),
            "max_voices": spec.voices,
            "notes": spec.notes,
        }
        print(f"{spec.name:24s} {dur:6.3f}s")
    if AUDIT:
        print("\nAUDIT: layers cut by their buffer while still audible (name, layer, level re layer peak dB, length s)")
        for row in AUDIT:
            print("  ", row)
    elif AUDIT is not None:
        print("\nAUDIT: no layer is truncated while audible")
    mpath = args.out / "manifest.json"
    if args.only and mpath.exists():
        merged = json.loads(mpath.read_text())      # --only updates just the named entries, keeps the rest as is
        merged.update(manifest)
        manifest = merged
    if not args.only or mpath.exists():
        mpath.write_text(json.dumps(manifest, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
