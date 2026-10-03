"""Authoring DSL on top of fighter_pose: partial key specs -> full states -> per-frame solved poses."""
import math

from mathutils import Vector

import fighter_pose as fp
from fighter_pose import (ALL_CHANNELS, ROT_CHANNELS, FPS, HAND_FIELDS, FOOT_FIELDS, merge_hand, merge_foot,
                          mirror_state, Timeline, blA)

# neutral hanging pose (rest) -------------------------------------------------------------------
REST_HAND_L = (0.16, 0.088, -0.524, 0.18, 0.20, -0.96, -1.0, 0.0, 0.0, 0.4196, -0.9074, -0.024, 0.0)
REST_FOOT_L = (0.09, -0.005, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)


def rest_state():
    st = {
        "hp": (0.0, 0.0, 0.0),
        "hips": (0.0, 0.0, 0.0), "spine": (0.0, 0.0, 0.0), "chest": (0.0, 0.0, 0.0),
        "neck": (0.0, 0.0, 0.0), "head": (0.0, 0.0, 0.0),
        "sl": (0.0, 0.0), "sr": (0.0, 0.0),
        "hl": REST_HAND_L, "fl": REST_FOOT_L,
    }
    st["hr"] = fp.mirror_state_value("hl", st["hl"])
    st["fr"] = fp.mirror_state_value("fl", st["fl"])
    return st


# spec constructors -----------------------------------------------------------------------------
def Hd(p=None, f=None, m=None, e=None, ow=None, sp="sh"):
    d = {"sp": sp}
    if p is not None:
        d["p"] = p
    if f is not None:
        d["f"] = f
        d.setdefault("ow", 1.0)
    if m is not None:
        d["m"] = m
        d.setdefault("ow", 1.0)
    if e is not None:
        d["e"] = e
    if ow is not None:
        d["ow"] = ow
    return d


def Fd(**kw):
    return kw


def mirror_hand_dict(d):
    out = dict(d)
    for k in ("p", "f", "m", "e"):
        if k in out:
            v = out[k]
            out[k] = (-v[0], v[1], v[2])
    return out


def both(dl):
    """Same hand spec on both sides (right is the mirror image)."""
    return {"hl": dl, "hr": mirror_hand_dict(dl)}


def mirror_foot_dict(d):
    out = dict(d)
    if "x" in out:
        out["x"] = -out["x"]
    for k in ("yaw", "roll", "kyaw"):
        if k in out:
            out[k] = -out[k]
    return out


def both_feet(dl):
    return {"fl": dl, "fr": mirror_foot_dict(dl)}


def merged(*dicts):
    out = {}
    for d in dicts:
        out.update(d)
    return out


# spec application ------------------------------------------------------------------------------
def _convert_hand_space(rig, st, side, upd):
    """upd given in root space ('rt') -> chest-aligned shoulder space, using the body channels of st."""
    Rp, Hp, _ = rig.body_fk(st)
    Dc = rig.chest_delta(Rp)
    Dci = Dc.inverted()
    sname = f"shoulder.{side}"
    S0 = Hp["chest"] + Rp["chest"] @ (rig.Rinv["chest"] @ (rig.tail[sname] - rig.head["chest"]))
    out = dict(upd)
    if "p" in upd:
        v = Dci @ (blA(upd["p"]) - S0)
        out["p"] = (v.x, -v.y, v.z)
    for k in ("f", "m", "e"):
        if k in upd:
            v = Dci @ blA(upd[k])
            out[k] = (v.x, -v.y, v.z)
    return out


def apply_spec(rig, prev, base, spec):
    st = dict(base if spec.get("_base") else prev)
    for ch in ("hp",) + ROT_CHANNELS + ("sl", "sr"):
        if ch in spec:
            st[ch] = tuple(float(x) for x in spec[ch])
    for ch in ("fl", "fr"):
        if ch in spec:
            st[ch] = merge_foot(st[ch], spec[ch])
    for ch, side in (("hl", "L"), ("hr", "R")):
        if ch in spec:
            upd = spec[ch]
            sp = upd.get("sp", "sh")
            if sp == "rt":
                upd = _convert_hand_space(rig, st, side, upd)
            st[ch] = merge_hand(st[ch], upd)
    return st


class ClipDef:
    def __init__(self, name, dur, loop, base, keys, contact=None, notes="", lag=None, mirror_of=None,
                 extra=None):
        self.name = name
        self.dur = dur
        self.loop = loop
        self.base = base          # spec dict (merged onto rest)
        self.keys = keys          # list of (t_seconds, spec, ease)
        self.contact = contact
        self.notes = notes
        self.lag = {"neck": 1.0, "head": 2.0} if lag is None else lag
        self.mirror_of = mirror_of
        self.extra = extra or {}


def n_frames(dur):
    return max(1, int(math.floor(dur * FPS + 0.5)))


def build_clip(rig, cd, rest=None):
    """Returns dict(frames=[(ql, hips_loc)], diag=[...], n=N, loop=bool, states=[...])."""
    rest = rest or rest_state()
    base = apply_spec(rig, rest, rest, cd.base)
    N = n_frames(cd.dur)
    keys = []
    prev = base
    for (t, spec, ease) in cd.keys:
        f = int(math.floor(t * FPS + 0.5))
        st = apply_spec(rig, prev, base, spec)
        keys.append((f, st, ease))
        prev = st
    if cd.mirror_of:
        raise RuntimeError("mirror_of is resolved by the caller")
    tl = Timeline(keys, N, cd.loop)
    lag_for = lambda ch: cd.lag.get(ch, 0.0)
    states = []
    for f in range(0, N + 1):
        if cd.loop and f == N:
            states.append(states[0])
            continue
        st = tl.eval(float(f))
        for ch, lag in cd.lag.items():
            if lag:
                st[ch] = tl.eval(float(f) - lag)[ch]
        states.append(st)
    # optional: lower the hips where a planted leg would be out of reach (gaits)
    if cd.extra.get("auto_hips"):
        drops = []
        for st in states:
            cur = dict(st)
            drop = 0.0
            for _ in range(10):
                _, _, diag = rig.solve(cur)
                miss = max(diag["reach_L_leg"], diag["reach_R_leg"])
                if miss < 0.0015:
                    break
                step = miss * 1.15 + 0.0005
                drop -= step
                hp = st["hp"]
                cur["hp"] = (hp[0], hp[1], hp[2] + drop)
            drops.append(drop)
        n = len(drops) - (1 if cd.loop else 0)
        sm = []
        for i in range(n):
            if cd.loop:
                w = [drops[(i + k) % n] for k in (-2, -1, 0, 1, 2)]
            else:
                w = [drops[min(max(i + k, 0), n - 1)] for k in (-2, -1, 0, 1, 2)]
            sm.append(sum(w) / 5.0)
        for i in range(len(states)):
            d = min(drops[i], sm[i % n] if cd.loop else sm[min(i, n - 1)])
            hp = states[i]["hp"]
            states[i] = dict(states[i])
            states[i]["hp"] = (hp[0], hp[1], hp[2] + d)
    frames, diags = [], []
    ctx = {}
    for f, st in enumerate(states):
        ql, hips_loc, diag = rig.solve(st, ctx)
        frames.append((ql, hips_loc))
        diags.append(diag)
    return dict(frames=frames, diags=diags, states=states, n=N, loop=cd.loop)


def mirror_clipdef(cd, new_name):
    """Mirror every spec (left/right swap + x flip) of a ClipDef."""
    def mspec(spec):
        out = {}
        for ch, v in spec.items():
            if ch == "_base":
                out[ch] = v
            elif ch in ("hp",) + ROT_CHANNELS:
                out[ch] = fp.mirror_state_value(ch, v)
            elif ch in ("sl", "sr"):
                out[fp.SWAP[ch]] = v
            elif ch in ("hl", "hr"):
                out[fp.SWAP[ch]] = mirror_hand_dict(v)
            elif ch in ("fl", "fr"):
                out[fp.SWAP[ch]] = mirror_foot_dict(v)
            else:
                raise KeyError(ch)
        return out
    nc = ClipDef(new_name, cd.dur, cd.loop, mspec(cd.base), [(t, mspec(s), e) for t, s, e in cd.keys],
                 cd.contact, cd.notes, dict(cd.lag), None, dict(cd.extra))
    return nc
