"""Fourfold animation toolkit - authoring DSL: partial key specs -> full pose states -> per-frame states -> poses.

    c = Clip("e_strike", 24, base="e_stance", contact=8, priority="P0", technique="iron-bridge drive",
             hands=("tiger", "fist"), strike="hand_r")
    c.k(4, ease="io", pel=dict(z=-0.31, yaw=10), hand_r=HW(p=(-0.12, 0.05, 1.0), f=FWD, m=UP))
    c.k(8, ease="in3", ...)          # contact (eases INTO the key: fastest at contact)
    c.hold(10)                        # 2-frame hold of the contact pose
    c.k(24, ease="io", base=True)     # back to the base stance pose
    result = c.build()

Interpolation: per segment ease (the ease of the key that ENDS the segment); "sp" = Catmull-Rom spline through the
neighbouring keys (loops wrap).  Hands interpolate in CHEST SPACE (they ride the torso between keys); path="arc"
swings the wrist on a sphere around the shoulder instead of a straight line.  Feet interpolate about the pivot of the
segment's end key (the start key is re-pivoted exactly first), so planted heels / balls never slide.
Overlap: per-channel time offsets in frames (+ = leads, - = lags; neck lags 1.5 by default), faded in / out at the
clip ends so the first / last frame stay exactly on the base pose.  Layers (breath, tremble, custom) add on top.
"""
import math

import numpy as np

import ffa_hands as hands
import ffa_rig as rig
import ffa_solver as solver
from ffa_math import A, norm, smoothstep

FWD, BACK, UP, DOWN, LEFT, RIGHT = (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1), (1, 0, 0), (-1, 0, 0)
PIVOTS = {"heel": -1.0, "ankle": 0.0, "mid": 0.5, "ball": 1.0, "toe": 2.0}
FPS = 60

# ------------------------------------------------------------------------------------------------ eases
def ease(kind, u):
    u = min(max(u, 0.0), 1.0)
    if kind in ("lin", "sp"):
        return u
    if kind == "io":
        return u * u * (3 - 2 * u)
    if kind == "io3":
        return u * u * u * (u * (u * 6 - 15) + 10)
    if kind == "in":
        return u * u
    if kind == "in3":
        return u * u * u
    if kind == "in4":
        return u ** 4
    if kind == "out":
        return 1 - (1 - u) ** 2
    if kind == "out3":
        return 1 - (1 - u) ** 3
    if kind == "out4":
        return 1 - (1 - u) ** 4
    if kind == "ovs":            # ease-out that overshoots ~10 % then settles
        c = 1.6
        return 1 + (c + 1) * (u - 1) ** 3 + c * (u - 1) ** 2
    if kind == "ovs2":           # softer overshoot ~5 %
        c = 1.1
        return 1 + (c + 1) * (u - 1) ** 3 + c * (u - 1) ** 2
    if kind == "hold":
        return 0.0 if u < 1.0 else 1.0
    if kind == "snap":           # whip: almost all of the travel in the first 40 %
        return 1 - (1 - u) ** 5
    if kind == "acc":            # accelerate (gravity-like) then a hard stop
        return u ** 2.5
    raise ValueError(kind)


# ------------------------------------------------------------------------------------------------ orientation blend
def _frame(fv, mv):
    """Hand frame columns (finger dir, palm normal re-orthogonalised, side) from a finger / palm pair."""
    f = norm(np.asarray(fv, float))
    if np.linalg.norm(f) < 1e-9:
        f = np.array([0.0, 1.0, 0.0])
    m = np.asarray(mv, float) - f * float(np.dot(mv, f))
    if np.linalg.norm(m) < 1e-6:
        m = np.cross(f, np.array([0.0, 0.0, 1.0]) if abs(f[2]) < 0.9 else np.array([1.0, 0.0, 0.0]))
    m = norm(m)
    return np.stack([f, m, np.cross(f, m)], axis=1)


def hand_orient_lerp(oa, ob, u):
    """Blend the (finger, palm, elbow-pole) part of two hand states: the hand ROTATION is slerped (so a palm turning
    from down to up rolls through the side instead of collapsing through a zero vector) and the elbow pole is
    slerped as a direction."""
    from ffa_math import mat_slerp
    Ra, Rb = _frame(oa[0:3], oa[3:6]), _frame(ob[0:3], ob[3:6])
    R = mat_slerp(Ra, Rb, u)
    ea, eb = np.asarray(oa[6:9], float), np.asarray(ob[6:9], float)
    na, nb = np.linalg.norm(ea), np.linalg.norm(eb)
    if na > 1e-9 and nb > 1e-9:
        da, db = ea / na, eb / nb
        c = float(np.dot(da, db))
        if c < -0.999:                       # opposite poles: swing through the downward side
            mid = norm(np.cross(da, np.array([1.0, 0.0, 0.0])) if abs(da[0]) < 0.9 else np.cross(da, np.array([0.0, 1.0, 0.0])))
            e = norm(da * (1 - u) + mid * math.sin(math.pi * u) + db * u)
        else:
            om = math.acos(max(-1.0, min(1.0, c)))
            e = da if om < 1e-6 else (math.sin((1 - u) * om) * da + math.sin(u * om) * db) / math.sin(om)
        e = e * (na + (nb - na) * u)
    else:
        e = ea + (eb - ea) * u
    return tuple(R[:, 0]) + tuple(R[:, 1]) + tuple(e)


# ------------------------------------------------------------------------------------------------ spec helpers
def H(p=None, f=None, m=None, e=None, dp=None):
    """Hand target in CHEST SPACE (A-frame axes carried by spine_05, origin at that side's shoulder).
    dp = offset added to the current position (chest space)."""
    d = {"space": "c"}
    for k, v in (("p", p), ("f", f), ("m", m), ("e", e), ("dp", dp)):
        if v is not None:
            d[k] = tuple(float(x) for x in v)
    return d


def HW(p=None, f=None, m=None, e=None, dp=None):
    """Hand target in WORLD (A-frame, origin on the floor between the rest feet), converted to chest space at the key.
    dp = world offset added to where the hand currently is (with this key's body)."""
    d = H(p, f, m, e)
    if dp is not None:
        d["dpw"] = tuple(float(x) for x in dp)
    d["space"] = "w"
    return d


ARM_LEN = 0.545


def HS(d, ext=0.97, f=None, m=None, e=None):
    """Hand target relative to that side's SHOULDER: wrist = shoulder + unit(d) * ext * arm length, with d, f, m, e in
    WORLD axes (A-frame) - the way to aim an extended strike regardless of how the body moved."""
    dd = {"space": "s", "dir": tuple(float(x) for x in d), "ext": float(ext)}
    for k, v in (("f", f), ("m", m), ("e", e)):
        if v is not None:
            dd[k] = tuple(float(x) for x in v)
    return dd


def F(at=None, **kw):
    """Foot update.  at=(x, y): flat footprint (point under the ankle).  pivot / pv: 'heel' 'ankle' 'mid' 'ball' 'toe'
    or a number; yaw / pitch / roll / lift / toe / kyaw / kup; move=(dx, dy) shifts the pivot."""
    d = dict(kw)
    if at is not None:
        d["at"] = tuple(float(x) for x in at)
    if "pivot" in d:
        d["pv"] = d.pop("pivot")
    if isinstance(d.get("pv"), str):
        d["pv"] = PIVOTS[d["pv"]]
    return d


def mirror_hand_spec(d):
    out = dict(d)
    for k in ("p", "f", "m", "e", "dp", "dpw", "dir"):
        if k in out:
            v = out[k]
            out[k] = (-v[0], v[1], v[2])
    return out


def mirror_foot_spec(d):
    out = dict(d)
    if "at" in out:
        out["at"] = (-out["at"][0], out["at"][1])
    if "move" in out:
        out["move"] = (-out["move"][0], out["move"][1])
    for k in ("yaw", "roll", "kyaw"):
        if k in out:
            out[k] = -out[k]
    return out


def both(h):
    """The same hand spec on both sides (right = mirror image of the left spec)."""
    return {"hand_l": h, "hand_r": mirror_hand_spec(h)}


def both_feet(f):
    return {"foot_l": f, "foot_r": mirror_foot_spec(f)}


# ------------------------------------------------------------------------------------------------ states
_TUP = {"pel": ("x", "y", "z", "pitch", "side", "yaw"), "spine": ("pitch", "side", "yaw"),
        "neck": ("pitch", "side", "yaw"), "gaze": ("w", "yaw", "pitch"), "clav_l": ("lift", "prot"),
        "clav_r": ("lift", "prot")}
FOOT_IDX = {"px": 0, "py": 1, "lift": 2, "yaw": 3, "pitch": 4, "roll": 5, "pv": 6, "toe": 7, "kyaw": 8, "kup": 9}


def rest_hand(side):
    sh = rig.HEAD["upperarm_" + side]
    wr = rig.HEAD["hand_" + side]
    p = wr - sh
    arm = norm(rig.TAIL["hand_" + side] - wr)
    palm = rig.REST["hand_" + side][:, 2]
    sx = 1.0 if side == "l" else -1.0
    return (p[0], -p[1], p[2], arm[0], -arm[1], arm[2], palm[0], -palm[1], palm[2], 0.25 * sx, -1.0, -0.2)


def neutral_state():
    st = {"pel": (0.0,) * 6, "spine": (0.0,) * 3, "neck": (0.0,) * 3, "gaze": (0.0, 0.0, 0.0),
          "clav_l": (0.0, 0.0), "clav_r": (0.0, 0.0)}
    for s in rig.SIDES:
        st["hand_" + s] = rest_hand(s)
        x = rig.side_point(rig.ANKLE_L, s)
        st["foot_" + s] = solver.footprint(s, x[0], -x[1])
        st["fing_" + s] = tuple(hands.get("relaxed"))
    return st


def copy_state(st):
    return {k: tuple(v) for k, v in st.items()}


def _upd_tuple(cur, val, names):
    """dict values set fields; a field name prefixed with 'd' (dz, dyaw ...) ADDS to the current value."""
    if isinstance(val, dict):
        t = list(cur)
        for k, v in val.items():
            if k not in names and k.startswith("d") and k[1:] in names:
                t[names.index(k[1:])] += float(v)
            else:
                t[names.index(k)] = float(v)
        return tuple(t)
    if isinstance(val, (int, float)):
        t = list(cur)
        t[0] = float(val)
        return tuple(t)
    return tuple(float(x) for x in val)


def _apply_foot(side, cur, d):
    f = list(cur)
    if "at" in d:
        yaw = d.get("yaw", f[3])
        pv = d.get("pv", f[6])
        new = solver.footprint(side, d["at"][0], d["at"][1], yaw=yaw, pv=pv, lift=d.get("lift", 0.0),
                               pitch=d.get("pitch", 0.0), roll=d.get("roll", 0.0), toe=d.get("toe", f[7]),
                               kyaw=d.get("kyaw", f[8]), kup=d.get("kup", f[9]))
        return tuple(new)
    if "pv" in d and abs(d["pv"] - f[6]) > 1e-9:
        f = list(solver.repivot(side, f, float(d["pv"])))
    if "move" in d:
        f[0] += d["move"][0]
        f[1] += d["move"][1]
    for k in ("lift", "yaw", "pitch", "roll", "toe", "kyaw", "kup"):
        if k in d:
            f[FOOT_IDX[k]] = float(d[k])
    return tuple(f)


def _apply_hand(st, side, d):
    cur = list(st["hand_" + side])
    if d.get("space") == "s":
        dv = np.array(d["dir"], float)
        dv = dv / max(np.linalg.norm(dv), 1e-9) * d["ext"] * ARM_LEN
        (p,) = solver.world_to_chest(st, side, None, [tuple(dv)])
        cur[0:3] = p
        names = [k for k in ("f", "m", "e") if k in d]
        conv = solver.world_to_chest(st, side, None, [d[k] for k in names])
        for k, v in zip(names, conv):
            a = {"f": 3, "m": 6, "e": 9}[k]
            cur[a:a + 3] = v
        return tuple(cur)
    if "dpw" in d and "p" not in d:
        w = solver.chest_to_world(st, side, cur[0:3])
        d = dict(d)
        d["p"] = tuple(a + b for a, b in zip(w, d["dpw"]))
    if "dp" in d and d.get("space", "c") == "c":
        cur[0:3] = [a + b for a, b in zip(cur[0:3], d["dp"])]
    if d.get("space", "c") == "w":
        names = [k for k in ("f", "m", "e") if k in d]
        conv = solver.world_to_chest(st, side, d.get("p"), [d[k] for k in names])
        i = 0
        if "p" in d:
            cur[0:3] = conv[0]
            i = 1
        for k in names:
            a = {"f": 3, "m": 6, "e": 9}[k]
            cur[a:a + 3] = conv[i]
            i += 1
    else:
        for k, a in (("p", 0), ("f", 3), ("m", 6), ("e", 9)):
            if k in d:
                cur[a:a + 3] = d[k]
    return tuple(cur)


def apply_spec(prev, spec, base):
    st = copy_state(base if spec.get("base") else prev)
    for ch in ("pel", "spine", "neck", "gaze", "clav_l", "clav_r"):
        if ch in spec:
            st[ch] = _upd_tuple(st[ch], spec[ch], _TUP[ch])
    if "clav" in spec:
        v = spec["clav"]
        st["clav_l"] = _upd_tuple(st["clav_l"], v, _TUP["clav_l"])
        st["clav_r"] = _upd_tuple(st["clav_r"], v, _TUP["clav_r"])
    if "fing" in spec:
        st["fing_l"] = tuple(hands.get(spec["fing"]))
        st["fing_r"] = tuple(hands.get(spec["fing"]))
    for s in rig.SIDES:
        if "fing_" + s in spec:
            st["fing_" + s] = tuple(hands.get(spec["fing_" + s]))
    if "feet" in spec:
        spec = dict(spec)
        spec.update(both_feet(spec["feet"]))
    for s in rig.SIDES:
        if "foot_" + s in spec:
            st["foot_" + s] = _apply_foot(s, st["foot_" + s], spec["foot_" + s])
    if "hands" in spec:
        spec = dict(spec)
        spec.update(both(spec["hands"]))
    # hands last: world-space targets are converted with this key's body
    for s in rig.SIDES:
        if "hand_" + s in spec:
            st["hand_" + s] = _apply_hand(st, s, spec["hand_" + s])
    return st


def mirror_state(st):
    out = {}
    px, py, pz, pp, ps, pyw = st["pel"]
    out["pel"] = (-px, py, pz, pp, -ps, -pyw)
    for ch in ("spine", "neck"):
        a, b, c = st[ch]
        out[ch] = (a, -b, -c)
    w, gy, gp = st["gaze"]
    out["gaze"] = (w, -gy, gp)
    for s, o in (("l", "r"), ("r", "l")):
        out["clav_" + o] = st["clav_" + s]
        h = list(st["hand_" + s])
        for i in (0, 3, 6, 9):
            h[i] = -h[i]
        out["hand_" + o] = tuple(h)
        f = list(st["foot_" + s])
        f[0], f[3], f[5], f[8] = -f[0], -f[3], -f[5], -f[8]
        out["foot_" + o] = tuple(f)
        out["fing_" + o] = st["fing_" + s]
        if "tw_" + s in st:
            out["tw_" + o] = (-st["tw_" + s][0],)
    return out


# ------------------------------------------------------------------------------------------------ the clip
BASES = {}          # name -> state (filled by clips/bases.py)
DEFAULT_OFFSETS = {"neck": -1.5}
OFFSET_CHANNELS = ("pel", "spine", "neck", "gaze", "clav_l", "clav_r", "hand_l", "hand_r", "fing_l", "fing_r")


class ClipResult:
    pass


class Clip:
    def __init__(self, name, frames, base, *, loop=False, contact=None, contacts=None, priority="P1", technique="",
                 style="", hands=("relaxed", "relaxed"), strike=None, speed=0.0, notes="", start=None,
                 offsets=None, mirror=False, base_end=None, plants=None, auto_hips=False, treadmill=None,
                 metric=None, end_pose=None, start_pose_free=False, no_balance=False, antic=None, follow=None,
                 base_check=True):
        self.name = name
        self.frames = int(frames)
        self.base = base                      # name of the base stance clip (None for free clips)
        self.base_end = base_end if base_end is not None else base
        self.loop = loop
        self.contact = contact
        self.contacts = list(contacts) if contacts else ([contact] if contact is not None else [])
        self.priority = priority
        self.technique = technique
        self.style = style
        self.hands = hands
        self.strike = strike                  # 'hand_l' 'hand_r' 'hands' 'foot_l' 'foot_r' or None
        self.metric = metric                  # contact metric override (see ffa_validate)
        self.speed = speed
        self.notes = notes
        self.offsets = dict(DEFAULT_OFFSETS)
        if offsets:
            self.offsets.update(offsets)
        self.mirror = mirror
        self.plants_override = plants
        self.auto_hips = auto_hips
        self.treadmill = treadmill            # (vx, vy) A-frame ground velocity of planted feet (m/s) for gaits
        self.end_pose = end_pose              # name of the pose the clip ends in when not the base (knockdown)
        self.start_pose_free = start_pose_free  # the first frame is not a base pose (getup starts lying)
        self.no_balance = no_balance          # skip the centre-of-mass warning (jumps, falls, spins)
        self.base_check = base_check          # False for gait loops (they blend from the base, not start on it)
        self.max_hand_turn = 30.0             # deg / frame: hand orientations roll no faster (forearm roll speed)
        self.max_joint_turn = 34.0            # deg / frame: arm joints turn no faster (key frames stay exact)
        self.antic = antic                    # anticipation-peak frame for the contact sheet
        self.follow = follow                  # follow-through frame for the contact sheet
        start_state = start if start is not None else (BASES[base] if base else neutral_state())
        self.base_state = copy_state(BASES[base]) if base in BASES else copy_state(start_state)
        self.keys = [(0, copy_state(start_state), "lin", None)]
        self.layers = []

    # ---------------------------------------------------------------- authoring
    def k(self, frame, ease="io", path=None, **spec):
        frame = int(frame)
        assert frame >= self.keys[-1][0], f"{self.name}: keys must be added in frame order ({frame})"
        base = self.base_state
        if spec.get("base") and self.base_end and self.base_end in BASES:
            base = BASES[self.base_end]
        st = apply_spec(self.keys[-1][1], spec, base)
        if frame == self.keys[-1][0]:
            self.keys[-1] = (frame, st, ease, path)
        else:
            self.keys.append((frame, st, ease, path))
        return self

    def hold(self, frame, ease="lin"):
        """Holds the previous key's pose until `frame`."""
        return self.k(frame, ease=ease)

    def state_at_key(self, i=-1):
        return self.keys[i][1]

    def layer(self, fn):
        self.layers.append(fn)
        return self

    def breathe(self, depth=1.0, cycles=None, period=2.0, phase=0.0):
        self.layers.append(("breath", depth, cycles, period, phase))
        return self

    def tremble(self, amp=1.0, f0=0, f1=None, hz=12.0, ramp=6):
        self.layers.append(("tremble", amp, f0, f1, hz, ramp))
        return self

    # ---------------------------------------------------------------- evaluation
    def _segment(self, t):
        keys = self.keys
        n = self.frames
        if self.loop:
            t = t % n
        else:
            t = min(max(t, 0.0), float(keys[-1][0]))
        i = 0
        while i < len(keys) - 2 and keys[i + 1][0] <= t:
            i += 1
        return i, t

    def _tangent(self, i, ch):
        keys = self.keys
        m = len(keys)
        if self.loop:
            # keys[-1] is the copy of keys[0] at frame n
            if i == 0 or i == m - 1:
                a, ta = keys[m - 2][1][ch], keys[m - 2][0] - self.frames
                b, tb = keys[1][1][ch], keys[1][0]
            else:
                a, ta = keys[i - 1][1][ch], keys[i - 1][0]
                b, tb = keys[i + 1][1][ch], keys[i + 1][0]
        else:
            if i == 0 or i == m - 1:
                return None
            a, ta = keys[i - 1][1][ch], keys[i - 1][0]
            b, tb = keys[i + 1][1][ch], keys[i + 1][0]
        span = max(tb - ta, 1e-6)
        return tuple((y - x) / span for x, y in zip(a, b))

    def _eval_channel(self, ch, t):
        i, t = self._segment(t)
        k0, k1 = self.keys[i], self.keys[i + 1]
        t0, t1 = k0[0], k1[0]
        a, b = k0[1][ch], k1[1][ch]
        eas = k1[2]
        if isinstance(eas, dict):
            eas = eas.get(ch, eas.get("*", "io"))
        u = 0.0 if t1 <= t0 else (t - t0) / (t1 - t0)
        if ch.startswith("foot_"):
            side = ch[-1]
            a = solver.repivot(side, a, b[6])
            e = ease("io" if eas == "sp" else eas, u)
            out = [x + (y - x) * e for x, y in zip(a, b)]
            # a planted foot that turns about its ball (heel) lifts the heel (toes) a little while it turns, like a
            # real pivot, so the free end never scrubs the floor
            dyaw = b[3] - a[3]
            if abs(dyaw) > 2.0 and a[2] < 0.002 and b[2] < 0.002 and abs(a[0] - b[0]) < 1e-3 and abs(a[1] - b[1]) < 1e-3:
                amp = min(14.0, 0.45 * abs(dyaw) + 4.0) * math.sin(math.pi * u) ** 2
                if b[6] > -0.5:
                    out[4] -= amp           # heel up on the ball ...
                    out[7] += 0.6 * amp     # ... and the toes off the floor
                else:
                    out[4] += amp           # toes up on the heel
            return tuple(out)
        if ch in ("tw_l", "tw_r"):
            e = ease("io" if eas == "sp" else eas, u)
            return (a[0] + (b[0] - a[0]) * e,)
        if eas == "sp" and not ch.startswith("fing_"):
            ma = self._tangent(i, ch)
            mb = self._tangent(i + 1, ch)
            dt = t1 - t0
            ma = ma or (0.0,) * len(a)
            mb = mb or (0.0,) * len(a)
            h00 = 2 * u ** 3 - 3 * u ** 2 + 1
            h10 = u ** 3 - 2 * u ** 2 + u
            h01 = -2 * u ** 3 + 3 * u ** 2
            h11 = u ** 3 - u ** 2
            out = tuple(h00 * x + h10 * dt * p + h01 * y + h11 * dt * q for x, y, p, q in zip(a, b, ma, mb))
            if ch.startswith("hand_"):
                out = out[0:3] + hand_orient_lerp(a[3:12], b[3:12], smoothstep(u))
            return out
        e = ease("io" if eas == "sp" else eas, u)
        if ch.startswith("hand_"):
            if k1[3] == "arc":
                pa, pb = np.array(a[0:3]), np.array(b[0:3])
                ra, rb = np.linalg.norm(pa), np.linalg.norm(pb)
                da, db = pa / max(ra, 1e-9), pb / max(rb, 1e-9)
                om = math.acos(max(-1.0, min(1.0, float(np.dot(da, db)))))
                if om > 1e-4:
                    d = (math.sin((1 - e) * om) * da + math.sin(e * om) * db) / math.sin(om)
                else:
                    d = da + (db - da) * e
                p = tuple(d * (ra + (rb - ra) * e))
            else:
                p = tuple(x + (y - x) * e for x, y in zip(a[0:3], b[0:3]))
            return p + hand_orient_lerp(a[3:12], b[3:12], e)
        return tuple(x + (y - x) * e for x, y in zip(a, b))

    def _envelope(self, f):
        if self.loop:
            return 1.0
        n = self.frames
        ramp = 5.0
        return smoothstep(f / ramp) * smoothstep((n - f) / ramp)

    def states(self):
        keys = self.keys
        if self.loop:
            if keys[-1][0] != self.frames:
                wrap = keys[-1][2] if keys[-1][2] in ("sp", "lin") else "io"
                keys.append((self.frames, copy_state(keys[0][1]), wrap, None))
        elif keys[-1][0] < self.frames:
            keys.append((self.frames, copy_state(keys[-1][1]), "lin", None))
        # anatomical forearm roll: every key's twist is solved fresh (the roll the author's pose needs, in +-180)
        # and interpolated as a plain number between keys, so the forearm never takes the "short way" through the
        # impossible half of the roll circle when the hand orientation is slerped
        if "tw_l" not in keys[0][1]:
            for i, (kf, kst, ke, kp) in enumerate(keys):
                _, info = solver.solve(kst, None)
                kst["tw_l"] = (info["twist_raw_l"],)
                kst["tw_r"] = (info["twist_raw_r"],)
        out = []
        for f in range(self.frames + 1):
            env = self._envelope(f)
            st = {}
            for ch in keys[0][1].keys():
                src = {"tw_l": "hand_l", "tw_r": "hand_r"}.get(ch, ch)
                off = self.offsets.get(src, 0.0) if src in OFFSET_CHANNELS else 0.0
                st[ch] = self._eval_channel(ch, f + off * env)
            out.append(st)
        for lay in self.layers:
            out = self._apply_layer(lay, out)
        if not self.loop and self.max_hand_turn:
            # orientation / roll may lag a little behind the keys (an invisible forearm-roll delay) rather than pop:
            # only the clip ends are anchored
            ends = [0, self.frames]
            out = _limit_hand_turn(out, self.max_hand_turn, ends)
            out = _limit_scalar(out, ("tw_l", "tw_r"), self.max_hand_turn, ends)
        if self.loop:
            out[-1] = copy_state(out[0])
        if self.mirror:
            out = [mirror_state(s) for s in out]
        return out

    def _apply_layer(self, lay, sts):
        n = self.frames
        if callable(lay):
            return [lay(f, s) or s for f, s in enumerate(sts)]
        kind = lay[0]
        res = []
        if kind == "breath":
            _, depth, cycles, period, phase = lay
            if cycles is None:
                cycles = max(1, round(n / (period * FPS))) if self.loop else n / (period * FPS)
            for f, s in enumerate(sts):
                b = 0.5 - 0.5 * math.cos(2 * math.pi * (f / n * cycles + phase))
                if not self.loop:
                    b *= self._envelope(f)
                s = dict(s)
                pel = list(s["pel"])
                pel[2] += 0.004 * depth * b          # the whole body rises a little on the inhale
                s["pel"] = tuple(pel)
                sp = list(s["spine"])
                sp[0] -= 2.2 * depth * b             # chest lifts / opens
                s["spine"] = tuple(sp)
                nk = list(s["neck"])
                nk[0] += 1.4 * depth * b
                s["neck"] = tuple(nk)
                for c in ("clav_l", "clav_r"):
                    v = list(s[c])
                    v[0] += 2.5 * depth * b
                    s[c] = tuple(v)
                res.append(s)
            return res
        if kind == "tremble":
            _, amp, f0, f1, hz, ramp = lay
            f1 = n if f1 is None else f1
            cyc = hz * n / FPS
            if self.loop:
                cyc = max(1, round(cyc))
            for f, s in enumerate(sts):
                w = smoothstep((f - f0) / max(ramp, 1)) * (smoothstep((f1 - f) / max(ramp, 1)) if not self.loop else 1.0)
                if self.loop:
                    w = 1.0
                if w <= 0:
                    res.append(s)
                    continue
                s = dict(s)
                ph = 2 * math.pi * cyc * f / n
                for side, k in (("l", 0.0), ("r", 1.3)):
                    h = list(s["hand_" + side])
                    j1 = math.sin(ph) + 0.5 * math.sin(2.0 * ph + k)
                    j2 = math.sin(1.0 * ph + 1.0 + k) - math.sin(1.0 + k) + 0.4 * math.sin(3.0 * ph)
                    h[0] += 0.0018 * amp * w * j1
                    h[2] += 0.0022 * amp * w * j2
                    s["hand_" + side] = tuple(h)
                    c = list(s["clav_" + side])
                    c[0] += 0.35 * amp * w * math.sin(ph)
                    s["clav_" + side] = tuple(c)
                sp = list(s["spine"])
                sp[2] += 0.25 * amp * w * math.sin(ph)
                s["spine"] = tuple(sp)
                res.append(s)
            return res
        raise ValueError(kind)

    # ---------------------------------------------------------------- build
    def build(self):
        sts = self.states()
        if self.auto_hips:
            sts = _auto_hips(sts, self.loop)
        poses, infos = [], []
        ctx = {}
        for st in sts:
            p, info = solver.solve(st, ctx)
            poses.append(p)
            infos.append(info)
        if not self.loop and self.max_joint_turn:
            # anchored at the clip ends and the contact frames only (the strike pose is exact); other keys may be
            # reached a frame early / late when the authored ease is faster than a real arm
            anchors = sorted({0, self.frames} | {int(x) for x in self.contacts if 0 < x < self.frames})
            _limit_joint_speed(poses, ARM_CHAIN, anchors, self.max_joint_turn)
            # a leg is limited only while its foot is off the floor (kicks, hops): planted legs keep exact IK
            for sd in rig.SIDES:
                free = [st["foot_" + sd][2] > 0.012 for st in sts]
                bones = [f"{b}_{sd}" for b in LEG_BONES]
                before = [{b: p.q[b].copy() for b in bones} for p in poses]
                _limit_joint_speed(poses, bones, anchors, self.max_joint_turn, free)
                # a slowed leg must never push its foot through the floor: keep the IK pose on those frames
                for f, p in enumerate(poses):
                    if not free[f]:
                        continue
                    W, Hd, _ = rig.fk(p)
                    low = min(float(Hd["ball_" + sd][2]) - 0.025, float(Hd["foot_" + sd][2]) - 0.085)
                    if low < -0.004:
                        for b in bones:
                            p.q[b] = before[f][b]
        r = ClipResult()
        r.clip = self
        r.name = self.name
        r.frames = self.frames
        r.loop = self.loop
        r.states = sts
        r.poses = poses
        r.infos = infos
        r.plants = self.plants_override or detect_plants(sts, self.loop, self.treadmill)
        return r


def _limit_hand_turn(sts, max_deg, anchors):
    """Key-anchored rate limit of each hand's orientation target (finger / palm frame), max_deg per frame (a physical
    forearm-roll speed): over-fast authored rolls start earlier (backward pass from the later key) and finish later
    (forward pass); the keyed orientations stay exact."""
    from ffa_math import angle_of, mat_slerp
    out = [dict(s) for s in sts]
    for side in rig.SIDES:
        ch = "hand_" + side
        R = [_frame(s[ch][3:6], s[ch][6:9]) for s in sts]
        for a0, a1 in _segments(anchors):
            seg = R[a0:a1 + 1]
            for i in range(len(seg) - 2, 0, -1):
                a = angle_of(seg[i + 1].T @ seg[i])
                if a > max_deg:
                    seg[i] = mat_slerp(seg[i + 1], seg[i], max_deg / a)
            for i in range(1, len(seg) - 1):
                a = angle_of(seg[i - 1].T @ seg[i])
                if a > max_deg:
                    seg[i] = mat_slerp(seg[i - 1], seg[i], max_deg / a)
            R[a0:a1 + 1] = seg
        for f in range(len(R)):
            h = list(out[f][ch])
            h[3:6] = R[f][:, 0]
            h[6:9] = R[f][:, 1]
            out[f][ch] = tuple(float(x) for x in h)
    return out


ARM_CHAIN = [f"{b}_{s}" for s in ("l", "r") for b in ("clavicle", "upperarm", "lowerarm", "hand", "upperarm_twist_01",
                                                    "upperarm_twist_02", "lowerarm_twist_01", "lowerarm_twist_02")]


LEG_BONES = ("thigh", "calf", "foot", "ball", "thigh_twist_01", "thigh_twist_02", "calf_twist_01", "calf_twist_02")


def _limit_joint_speed(poses, bones, anchors, max_deg, allowed=None):
    """Speed limit on local joint rotations, anchored at the key frames: between two keys a bone may turn at most
    max_deg per frame; when the authored ease asks for more (a 90 deg elbow snap squeezed into its last frame), the
    motion is started earlier (backward pass from the later key) and, if still too fast, finished later (forward
    pass) - the keyed poses themselves never move.  Only arm chains are limited (they carry no floor contacts)."""
    from ffa_math import angle_of, mat_slerp
    for b in bones:
        Q = [p.q[b] for p in poses]
        for a0, a1 in zip(anchors, anchors[1:]):
            if a1 - a0 < 2:
                continue
            seg = [Q[f].copy() for f in range(a0, a1 + 1)]
            ok = [True] * len(seg) if allowed is None else [bool(allowed[f]) for f in range(a0, a1 + 1)]
            # backward from the later key: never arrive faster than max_deg / frame
            for i in range(len(seg) - 2, 0, -1):
                ang = angle_of(seg[i + 1].T @ seg[i])
                if ang > max_deg and ok[i]:
                    seg[i] = mat_slerp(seg[i + 1], seg[i], max_deg / ang)
            # forward from the earlier key: never leave faster than max_deg / frame
            for i in range(1, len(seg) - 1):
                ang = angle_of(seg[i - 1].T @ seg[i])
                if ang > max_deg and ok[i]:
                    seg[i] = mat_slerp(seg[i - 1], seg[i], max_deg / ang)
            for i in range(1, len(seg) - 1):
                poses[a0 + i].q[b] = seg[i]


def _segments(anchors):
    return [(a0, a1) for a0, a1 in zip(anchors, anchors[1:]) if a1 - a0 >= 2]


def _limit_scalar(sts, chans, max_step, anchors):
    """Key-anchored rate limit of scalar channels: inside each key-to-key segment the value may change at most
    max_step per frame - the change starts earlier (backward pass) / ends later (forward pass); keys stay exact."""
    out = [dict(s) for s in sts]
    for ch in chans:
        v = [s[ch][0] for s in sts]
        for a0, a1 in _segments(anchors):
            seg = v[a0:a1 + 1]
            for i in range(len(seg) - 2, 0, -1):
                seg[i] = seg[i + 1] + max(-max_step, min(max_step, seg[i] - seg[i + 1]))
            for i in range(1, len(seg) - 1):
                seg[i] = seg[i - 1] + max(-max_step, min(max_step, seg[i] - seg[i - 1]))
            v[a0:a1 + 1] = seg
        for f in range(len(v)):
            out[f][ch] = (v[f],)
    return out


def _auto_hips(sts, loop):
    """Lower the pelvis wherever a planted leg would be out of reach (smoothed), so feet never get pulled."""
    drops = []
    for st in sts:
        cur = copy_state(st)
        drop = 0.0
        for _ in range(12):
            _, info = solver.solve(cur)
            miss = max(info["leg_miss_l"] if cur["foot_l"][2] < 0.01 else 0.0,
                       info["leg_miss_r"] if cur["foot_r"][2] < 0.01 else 0.0)
            if miss < 0.0008:
                break
            drop -= miss * 1.1 + 0.0003
            p = list(st["pel"])
            p[2] += drop
            cur["pel"] = tuple(p)
        drops.append(drop)
    n = len(drops) - (1 if loop else 0)
    k = 4

    def at(arr, j):
        return arr[j % n] if loop else arr[min(max(j, 0), len(arr) - 1)]
    # erosion (min over +-k) then a +-k box average: smooth, and never higher than what any frame needs
    ero = [min(at(drops, j + o) for o in range(-k, k + 1)) for j in range(len(drops))]
    sm = [sum(at(ero, j + o) for o in range(-k, k + 1)) / (2 * k + 1) for j in range(len(drops))]
    out = []
    for i, st in enumerate(sts):
        s = dict(st)
        p = list(s["pel"])
        p[2] += sm[i % n if loop else i]
        s["pel"] = tuple(p)
        out.append(s)
    if loop:
        out[-1] = copy_state(out[0])
    return out


def sole_landmarks(side, f):
    """World positions of heel / ball-contact / toe-tip of a foot state (Blender coords)."""
    heel = rig.side_point(rig.HEEL_L, side)
    ball = rig.side_point(rig.BALL_L, side)
    ball = np.array([ball[0], ball[1], 0.0])
    toe = rig.side_point(rig.TOE_L, side)
    toe = np.array([toe[0], toe[1], 0.0])
    out = []
    D, _, P, prest = solver.foot_pose(side, f)
    for p in (heel, ball, toe):
        out.append(P + D @ (p - prest))
    # the ball contact rides on the toe bone when the heel is up: use the toe-bone floor point instead
    return out


def detect_plants(sts, loop, treadmill=None, tol=0.0006, ground=0.004):
    """Planted frame ranges per foot [start, end) from the foot states: a foot is planted while it touches the floor
    and its floor contact does not move (in the treadmill frame for gait clips)."""
    n = len(sts) - 1
    vel = np.zeros(3) if treadmill is None else A(treadmill[0], treadmill[1], 0.0) / FPS
    out = {}
    for s in rig.SIDES:
        lm = [sole_landmarks(s, st["foot_" + s]) for st in sts]
        planted = []
        for f in range(n + 1):
            ok = False
            for j in range(3):
                if lm[f][j][2] > ground:
                    continue
                stat = True
                for g in (f - 1, f + 1):
                    if g < 0 or g > n:
                        continue
                    if lm[g][j][2] > ground:
                        stat = False
                        break
                    d = lm[g][j] - lm[f][j] - vel * (g - f)
                    if math.hypot(d[0], d[1]) > tol:
                        stat = False
                        break
                if stat:
                    ok = True
                    break
            planted.append(ok)
        ranges = []
        f = 0
        while f <= n:
            if planted[f]:
                g = f
                while g + 1 <= n and planted[g + 1]:
                    g += 1
                a, b = f, min(g + 1, n)
                if b > a:                       # [start, end) inside the clip; a lone planted last frame is dropped
                    ranges.append([a, b])
                f = g + 1
            else:
                f += 1
        out[s] = ranges
    return out
