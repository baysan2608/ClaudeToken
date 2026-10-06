"""Fourfold animation toolkit - hand shapes (MARTIAL_ARTS.md §1.4) as finger-curl presets.

A hand shape is a flat vector of 26 numbers, so shapes interpolate linearly:
    for each finger in (index, middle, ring, pinky): meta (palm-arch curl of the metacarpal), mcp, pip, dip, spread
    thumb: tip target (tx, ty, tz) in HAND coordinates (metres from the wrist; x toward the pinky, y along the
           fingers, z out of the palm), t_roll (deg, turns the pad toward the fingers), t_mcp, t_ip (deg curls)
Curl is a positive rotation about the bone's local +X (rig convention: toward the palm); spread rotates about the palm
normal (local Z), positive = away from the middle finger.  The thumb is solved: thumb_02 / 03 take the curls,
thumb_01 aims the chain tip at the target (after the roll), so shapes blend through natural thumb paths.
"""
import numpy as np

import ffa_rig as rig
from ffa_math import rot_between, rx, ry, rz

FINGERS = ("index", "middle", "ring", "pinky")
F_KEYS = ("meta", "mcp", "pip", "dip", "spread")
T_KEYS = ("tx", "ty", "tz", "t_roll", "t_mcp", "t_ip")
N = len(FINGERS) * len(F_KEYS) + len(T_KEYS)
# spread direction per finger: index fans toward the thumb side, pinky toward the outside
SPREAD_DIR = {"index": 1.0, "middle": 0.2, "ring": -0.55, "pinky": -1.0}


def shape(fingers, thumb, per=None):
    """fingers = (meta, mcp, pip, dip, spread) applied to all four, `per` overrides per finger (tuples or dicts),
    thumb = (tx, ty, tz, t_roll, t_mcp, t_ip)."""
    v = np.zeros(N)
    for i, f in enumerate(FINGERS):
        vals = list(fingers)
        if per and f in per:
            o = per[f]
            if isinstance(o, dict):
                for k, x in o.items():
                    vals[F_KEYS.index(k)] = x
            else:
                vals = list(o)
        v[i * 5:(i + 1) * 5] = vals
    v[20:26] = thumb
    return v


# ------------------------------------------------------------------------------------------------ presets
# Tuned against close-up renders (SourceArt/Animation/previews/hands.jpg).
PRESETS = {
    # natural half-open: fingers progressively more curled toward the pinky, thumb resting beside the index
    "relaxed": shape((0, 18, 24, 12, 3), (-0.052, 0.098, 0.034, 10, 14, 12),
                     per={"index": (0, 12, 18, 10, 4), "ring": (3, 24, 30, 14, 3), "pinky": (6, 30, 34, 16, 5)}),
    # standard fist, thumb wrapped over the index / middle middle phalanges
    "fist": shape((0, 88, 100, 55, 0), (0.004, 0.076, 0.050, 40, 22, 28),
                  per={"index": (0, 84, 100, 52, 0), "ring": (6, 90, 102, 56, 0), "pinky": (12, 92, 104, 58, 0)}),
    # flat palm: fingers straight and together, thumb along the index (the wrist cock is the move's job)
    "palm": shape((0, 2, 2, 0, -3), (-0.044, 0.104, 0.010, 0, 6, 4),
                  per={"index": (0, 0, 2, 0, -4), "pinky": (2, 4, 4, 2, -3)}),
    # fair lady's hand: soft, slightly separated and curved
    "willow": shape((0, 12, 16, 8, 5), (-0.056, 0.100, 0.026, 8, 10, 10),
                    per={"index": (0, 8, 12, 6, 6), "ring": (2, 15, 19, 9, 4), "pinky": (4, 18, 22, 10, 6)}),
    # tiger claw: fingers spread, every joint curled hard, palm heel forward
    "tiger": shape((0, 22, 78, 62, 11), (-0.058, 0.086, 0.052, 20, 30, 40),
                   per={"index": (0, 18, 74, 60, 12), "ring": (4, 24, 80, 64, 10), "pinky": (8, 26, 82, 64, 13)}),
    # crane beak: fingertips gathered to a point with the thumb
    "crane": shape((0, 50, 34, 18, -7), (-0.006, 0.142, 0.052, 40, 12, 14),
                   per={"index": (0, 46, 30, 16, -9), "middle": (0, 52, 34, 18, -2), "ring": (5, 54, 36, 18, 4),
                        "pinky": (9, 56, 38, 18, 9)}),
    # sword fingers: index + middle straight together, ring + pinky curled under the thumb
    "sword": shape((0, 88, 102, 56, 0), (0.018, 0.074, 0.050, 40, 22, 28),
                   per={"index": (0, 0, 2, 0, -5), "middle": (0, 0, 2, 0, -2), "ring": (8, 92, 104, 58, 0),
                        "pinky": (12, 94, 106, 60, 0)}),
    # ox-tongue palm: fingers together, thumb tucked, slight cup
    "oxtongue": shape((0, 10, 12, 6, -3), (-0.030, 0.092, 0.024, 12, 10, 8),
                      per={"ring": (4, 12, 13, 6, -3), "pinky": (8, 14, 14, 7, -2)}),
    # cupped round as if holding a ball
    "cup": shape((0, 30, 34, 20, 7), (-0.036, 0.112, 0.062, 30, 16, 16),
                 per={"index": (0, 26, 30, 18, 8), "ring": (5, 32, 36, 20, 6), "pinky": (9, 34, 38, 22, 8)}),
    # fingers wide and tense
    "spread": shape((0, -8, 4, 2, 18), (-0.094, 0.070, 0.006, 0, 0, 0),
                    per={"index": (0, -10, 2, 0, 18), "ring": (-2, -8, 4, 2, 18), "pinky": (-4, -6, 6, 2, 24)}),
}
SHAPE_NAMES = ["fist", "palm", "willow", "tiger", "crane", "sword", "oxtongue", "relaxed", "cup", "spread"]

_T = {}
for _s in rig.SIDES:
    _Hh = rig.REST["hand_" + _s]
    _T[_s] = dict(R1=_Hh.T @ rig.REST["thumb_01_" + _s],
                  head=_Hh.T @ (rig.HEAD["thumb_01_" + _s] - rig.HEAD["hand_" + _s]),
                  L=(rig.LENGTH["thumb_01_" + _s], rig.LENGTH["thumb_02_" + _s], rig.LENGTH["thumb_03_" + _s]))


def get(name_or_vec):
    if isinstance(name_or_vec, str):
        return PRESETS[name_or_vec].copy()
    return np.asarray(name_or_vec, float).copy()


def mix(a, b, u):
    return get(a) * (1.0 - u) + get(b) * u


def tense(v, amount):
    """Tighten (amount > 0) or soften a shape: scales the curls of every joint around its value."""
    v = get(v)
    out = v.copy()
    for i in range(4):
        for k in (1, 2, 3):
            out[i * 5 + k] = v[i * 5 + k] + amount * (8.0 if v[i * 5 + k] >= 0 else -4.0)
    return out


def finger_locals(side, v):
    """{bone: local 3x3} for the 15 finger bones of one hand."""
    sgn = 1.0 if side == "l" else -1.0
    out = {}
    for i, f in enumerate(FINGERS):
        meta, mcp, pip, dip, spread = v[i * 5:(i + 1) * 5]
        sd = SPREAD_DIR[f] * spread * sgn
        out[f"{f}_metacarpal_{side}"] = rx(meta)
        out[f"{f}_01_{side}"] = rz(sd) @ rx(mcp)
        out[f"{f}_02_{side}"] = rx(pip)
        out[f"{f}_03_{side}"] = rx(dip)
    tx, ty, tz, t_roll, t_mcp, t_ip = v[20:26]
    d = _T[side]
    L1, L2, L3 = d["L"]
    Q2, Q3 = rx(t_mcp), rx(t_ip)
    tip0 = np.array([0.0, L1, 0.0]) + Q2 @ (np.array([0.0, L2, 0.0]) + Q3 @ np.array([0.0, L3, 0.0]))
    Rr = ry(t_roll * sgn)
    goal = d["R1"].T @ (np.array([tx * sgn, ty, tz]) - d["head"])
    Q1 = rot_between(Rr @ tip0, goal) @ Rr
    out[f"thumb_01_{side}"] = Q1
    out[f"thumb_02_{side}"] = Q2
    out[f"thumb_03_{side}"] = Q3
    return out
