"""Shared clips (MARTIAL_ARTS.md §3.1): idle, guard, locomotion, evades, air, reactions, modes."""
from ffa_dsl import BASES, Clip, F, H, HW, both, copy_state

from . import clip


@clip("idle")
def idle():
    # wu ji: feet shoulder width, knees soft, hands relaxed; one slow breath and a barely visible weight drift
    c = Clip("idle", 120, "idle", loop=True, priority="P0", technique="wu ji ready stance (breathing)",
             hands=("relaxed", "relaxed"))
    c.k(30, ease="sp", pel=dict(dx=0.006, dside=0.6), spine=dict(dside=-0.5), neck=dict(dside=0.3))
    c.k(60, ease="sp", pel=dict(dx=-0.004, dside=-0.5), spine=dict(dside=0.5), neck=dict(dside=-0.3),
        hand_l=H(dp=(0.004, 0.006, 0.003)), hand_r=H(dp=(-0.003, 0.005, 0.004)))
    c.k(90, ease="sp", pel=dict(dx=-0.007, dside=-0.5), spine=dict(dside=0.4), neck=dict(dside=-0.2),
        hand_l=H(dp=(-0.004, -0.006, -0.003)), hand_r=H(dp=(0.003, -0.005, -0.004)))
    c.breathe(depth=1.0, cycles=1)
    return c


@clip("guard")
def guard_loop():
    # generic (Fire) guard: fists up, weight 55 % rear, small rhythmic bob and hand drift
    c = Clip("guard", 96, "guard", loop=True, priority="P0", technique="long-fist guard (fists up, bladed)",
             hands=("fist", "fist"))
    c.k(24, ease="sp", pel=dict(dz=-0.010, dy=-0.004), hand_l=H(dp=(-0.006, -0.008, -0.010)), hand_r=H(dp=(0.004, 0.006, -0.008)))
    c.k(48, ease="sp", pel=dict(dz=0.008, dy=0.002), hand_l=H(dp=(0.004, 0.010, 0.012)), hand_r=H(dp=(-0.004, -0.008, 0.010)))
    c.k(72, ease="sp", pel=dict(dz=-0.009, dy=-0.004), hand_l=H(dp=(0.002, -0.006, -0.008)), hand_r=H(dp=(0.002, 0.008, -0.006)))
    c.breathe(depth=0.8, cycles=2)
    return c


# ================================================================================================ gaits
import math  # noqa: E402

import ffa_solver as _solver  # noqa: E402
from ffa_dsl import PIVOTS, apply_spec, neutral_state  # noqa: E402


def _ss(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def _swing_o(u, a, s):
    """Swing-foot travel (body frame) from -a to +a: a cubic Hermite whose end velocities equal the stance velocity, so
    the foot leaves and meets the ground with zero ground speed (no velocity pop at toe-off / heel strike); the small
    overshoot at the end is the natural swing-leg retraction."""
    m = -2.0 * a * (1.0 - s) / s
    h00 = 2 * u ** 3 - 3 * u ** 2 + 1
    h10 = u ** 3 - 2 * u ** 2 + u
    h01 = -2 * u ** 3 + 3 * u ** 2
    h11 = u ** 3 - u ** 2
    return -a * h00 + m * h10 + a * h01 + m * h11


def _swing_lift(u, lift, peak=0.38):
    """Swing-foot clearance: quick rise after toe-off, low glide forward, soft touchdown (zero vertical speed)."""
    if u < peak:
        w = u / peak
        return lift * (0.35 * w + 0.65 * _ss(w))
    w = (u - peak) / (1.0 - peak)
    return lift * (1.0 - _ss(w))


def gait(name, frames, v, d, s, *, base, z=-0.03, lift=0.09, hs=14.0, to=-28.0, lane=0.09, lean=3.0, bob=0.012,
         sway=0.012, yaw_amp=6.0, arms="swing", foot_yaw=6.0, phase_r=0.5, arm_amp=0.13, toe_first=False,
         technique="", priority="P0", hands=("relaxed", "relaxed"), spec_fn=None, lane_y=(0.0, 0.0), knee_out=0.0,
         cycles=1, offsets=None, bias=0.0):
    """Procedural in-place gait loop.  frames per cycle, v ground speed (m/s), d travel direction (A-frame x, y),
    s stance fraction.  Stance footprints move backwards at exactly v (no foot sliding at the design speed); the
    pelvis is lowered automatically where a planted leg would overstretch."""
    N = frames
    a = v * (N / 60.0 / cycles) * s / 2.0
    c = Clip(name, N, base, loop=True, priority=priority, technique=technique, hands=hands, speed=v,
             treadmill=(-d[0] * v, -d[1] * v), auto_hips=True,
             offsets=offsets or {"neck": -1.5, "hand_l": -1.0, "hand_r": -1.0}, no_balance=True, base_check=False)
    perp = (d[1], -d[0])
    base_st = c.keys[0][1]
    keys = []
    for f in range(N):
        ph = (f / N * cycles) % 1.0
        spec = {}
        for side in "lr":
            phi = ph if side == "l" else (ph + phase_r) % 1.0
            sx = 1.0 if side == "l" else -1.0
            yaw = foot_yaw * sx
            if phi < s:                                  # stance: the footprint travels back at exactly v
                u = phi / s
                o = a * (1 - 2 * u)
                zl = 0.0
                if toe_first:
                    pitch = to * 0.45 * (1 - _ss(u / 0.18)) if u < 0.18 else (hs * 0.0 if u < 0.6 else 0.0)
                    pv = PIVOTS["ball"]
                else:
                    if u < 0.14:
                        pitch, pv = hs * (1 - _ss(u / 0.14)), PIVOTS["heel"]
                    elif u < 0.5:
                        pitch, pv = 0.0, PIVOTS["heel"]
                    else:
                        pitch, pv = to * _ss((u - 0.5) / 0.5), PIVOTS["ball"]
            else:                                        # swing: back to the front of the stride, foot lifted
                u = (phi - s) / (1 - s)
                o = _swing_o(u, a, s)
                zl = _swing_lift(u, lift)
                if toe_first:
                    pitch = to * 0.45 * _ss(u / 0.4) if u < 0.4 else to * 0.45
                    pv = PIVOTS["ball"]
                else:
                    pitch = to * (1 - _ss(u / 0.45)) + hs * _ss((u - 0.45) / 0.55)
                    pv = PIVOTS["ball"] if u < 0.5 else PIVOTS["heel"]
            lx = sx * lane + perp[0] * 0.0
            ly = lane_y[0] if side == "l" else lane_y[1]
            fx, fy = lx + d[0] * (o - bias), ly + d[1] * (o - bias)   # bias: stance window behind the hips
            st = _solver.footprint(side, fx, fy, yaw=yaw, pv=pv, lift=zl, pitch=pitch, kyaw=knee_out * sx)
            # footprint() puts the pivot at pv along a FLAT foot; re-place it so a pitched foot rolls about it
            spec["foot_" + side] = st
        cph = math.cos(2 * math.pi * ph)
        cs = math.cos(2 * math.pi * (ph - s / 2.0))
        bz = z + bob * math.cos(4 * math.pi * (ph - s / 2.0))
        spec["pel"] = (sway * cs * perp[0], sway * cs * perp[1], bz, lean * 0.4, -2.0 * cs, -yaw_amp * cph)
        spec["spine"] = (lean * 0.6, 1.5 * cs, yaw_amp * 1.6 * cph)
        spec["neck"] = (-lean * 0.8, 0.0, -yaw_amp * 0.6 * cph)
        if spec_fn:
            spec.update(spec_fn(ph))
        keys.append((f, spec))
    st = base_st
    for f, spec in keys:
        st = dict(st)
        for k, vv in spec.items():
            if k.startswith("foot_"):
                st[k] = tuple(vv)
            elif k in ("pel", "spine", "neck"):
                st[k] = tuple(vv)
        st = apply_spec(st, {k: vv for k, vv in spec.items() if k.startswith("hand_") or k.startswith("fing")}, st)
        if f == 0:
            c.keys[0] = (0, st, "lin", None)
        else:
            c.keys.append((f, st, "lin", None))
    return c


def _swing_arms(amp=0.13, height=-0.50, out=0.07):
    def fn(ph):
        sl = math.cos(2 * math.pi * (ph - 0.5))
        sr = math.cos(2 * math.pi * ph)
        return {"hand_l": H(p=(out, 0.03 + amp * sl, height + 0.03 * abs(sl)), f=(0.1, 0.35 * sl, -1.0),
                            m=(-1.0, 0.2, 0.0), e=(0.3, -1.0, -0.2)),
                "hand_r": H(p=(-out, 0.03 + amp * sr, height + 0.03 * abs(sr)), f=(-0.1, 0.35 * sr, -1.0),
                            m=(1.0, 0.2, 0.0), e=(-0.3, -1.0, -0.2))}
    return fn


def _run_arms(amp=0.24):
    def fn(ph):
        def one(sgn, phase):
            th = 2 * math.pi * (ph - phase)
            return H(p=(-0.06 * sgn, 0.08 + amp * math.cos(th), -0.33 + 0.10 * math.cos(th)),
                     f=(-0.2 * sgn, 1.0, 0.4), m=(-sgn, 0.0, 0.3), e=(0.3 * sgn, -1.0, -0.2))
        return {"hand_l": one(1.0, 0.5), "hand_r": one(-1.0, 0.0)}
    return fn


def _guard_arms(ph):
    g = 0.010 * math.sin(4 * math.pi * ph)
    gl = BASES["guard"]["hand_l"]
    gr = BASES["guard"]["hand_r"]
    return {"hand_l": H(p=(gl[0], gl[1] + g, gl[2] - g)), "hand_r": H(p=(gr[0], gr[1] - g, gr[2] + g))}


@clip("walk")
def walk():
    return gait("walk", 54, 1.4, (0.0, 1.0), 0.62, base="idle", z=-0.012, lift=0.075, hs=16.0, to=-26.0, lane=0.095,
                lean=3.0, bob=0.014, sway=0.014, yaw_amp=6.0, spec_fn=_swing_arms(), technique="natural walk 1.4 m/s",
                foot_yaw=6.0)


@clip("run")
def run():
    return gait("run", 36, 5.5, (0.0, 1.0), 0.30, base="idle", z=-0.05, lift=0.20, hs=8.0, to=-46.0, lane=0.07,
                lean=12.0, bob=0.03, sway=0.01, yaw_amp=9.0, spec_fn=_run_arms(), technique="run 5.5 m/s, 12 deg lean",
                foot_yaw=4.0, hands=("fist", "fist"), bias=0.16)


def _ground_footprint(side, f):
    """(x, y, yaw) of the point under the ankle (A-frame) for a foot tuple."""
    _, ankle, _, _ = _solver.foot_pose(side, f)
    return ankle[0], -ankle[1], f[3]


def shuffle(name, frames, v, d, *, base, cycles=2, s=0.64, lead="l", lift=0.045, bob=0.010, lean=3.0,
            toe_first=False, technique="", priority="P0", hands=("fist", "fist"), arm_bounce=0.010, heel_up=-9.0):
    """Fighting-stance stepping loop (strafe / back-pedal): the stance footprints of the base keep their stagger and
    yaw, the lead foot (the one on the side of travel) steps first and the trail foot follows half a cycle later, feet
    flat (or landing toe-first), never crossing.  Stance feet travel backwards at exactly v (stride-matched)."""
    N = frames
    T = N / 60.0 / cycles
    a = v * T * s / 2.0
    w = 1.0 - s
    c = Clip(name, N, base, loop=True, priority=priority, technique=technique, hands=hands, speed=v,
             treadmill=(-d[0] * v, -d[1] * v), auto_hips=True, offsets={"neck": -1.5, "hand_l": -1.0, "hand_r": -1.0},
             no_balance=True, base_check=False)
    base_st = BASES[base]
    fp = {sd: _ground_footprint(sd, base_st["foot_" + sd]) for sd in "lr"}
    start = {lead: 0.0, ("r" if lead == "l" else "l"): 0.5}
    for f in range(N):
        ph = (f / N * cycles) % 1.0
        st = copy_state(base_st)
        for sd in "lr":
            phi = (ph - start[sd]) % 1.0
            x0, y0, yaw0 = fp[sd]
            if phi < w:                                   # swing
                u = phi / w
                o = _swing_o(u, a, s)
                zl = _swing_lift(u, lift, peak=0.45)
                if toe_first:
                    pitch = heel_up * (1 - _ss(u / 0.3)) - 14.0 * _ss((u - 0.35) / 0.65)
                    pv = PIVOTS["ball"]
                else:
                    pitch = heel_up * (1 - _ss(u / 0.5))
                    pv = PIVOTS["ball"] if u < 0.5 else PIVOTS["mid"]
            else:                                         # stance: flat (toe-first lands on the ball, heel lowers)
                u = (phi - w) / s
                o = a * (1 - 2 * u)
                zl = 0.0
                if toe_first and u < 0.25:
                    pitch, pv = -14.0 * (1 - _ss(u / 0.25)), PIVOTS["ball"]
                elif u > 0.78:
                    pitch, pv = heel_up * _ss((u - 0.78) / 0.22), PIVOTS["ball"]
                else:
                    pitch, pv = 0.0, PIVOTS["ball"] if toe_first else PIVOTS["mid"]
            st["foot_" + sd] = tuple(_solver.footprint(sd, x0 + d[0] * o, y0 + d[1] * o, yaw=yaw0, pv=pv, lift=zl,
                                                       pitch=pitch))
        pel = list(st["pel"])
        pel[2] += -bob * 0.5 + bob * 0.5 * math.cos(4 * math.pi * ph)
        pel[4] += lean * (d[0])           # lean the trunk a little toward the travel (side bend)
        pel[3] += lean * 0.5 * (-d[1])
        st["pel"] = tuple(pel)
        g = arm_bounce * math.sin(4 * math.pi * ph + 0.6)
        for sd, sg in (("l", 1.0), ("r", -1.0)):
            h = list(st["hand_" + sd])
            h[1] += g * 0.6 * sg
            h[2] -= g
            st["hand_" + sd] = tuple(h)
        if f == 0:
            c.keys[0] = (0, st, "lin", None)
        else:
            c.keys.append((f, st, "lin", None))
    return c


@clip("strafe_l")
def strafe_l():
    return shuffle("strafe_l", 54, 1.1, (1.0, 0.0), base="guard", lead="l",
                   technique="guard-up side shuffle to the left, feet never cross")


@clip("strafe_r")
def strafe_r():
    return shuffle("strafe_r", 54, 1.1, (-1.0, 0.0), base="guard", lead="r",
                   technique="guard-up side shuffle to the right, feet never cross")


@clip("walk_back")
def walk_back():
    return shuffle("walk_back", 54, 1.0, (0.0, -1.0), base="guard", lead="r", toe_first=True, lean=2.0,
                   technique="guard-up back-pedal, toe first")
