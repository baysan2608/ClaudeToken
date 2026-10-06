"""Air clips - Baguazhang (MARTIAL_ARTS.md §2.5, §3.5): circle walking, palm changes, coiling and uncoiling.

Base a_stance = dragon posture: feet on the circle (left (0.07, 0.24), right (-0.15, -0.17) turned out 45 deg), hips
turned 38 deg away while the chest is wound back toward the centre (spine +22), lead ox-tongue palm at eye level, rear
palm under the lead elbow.  Mechanics: wind (the waist turns away, the lead foot hooks in: kou bu) -> the palm change
unwinds the body like a spring -> the palm strikes at the end of the turn (the palm turns over as it extends) -> keep
turning.  Palms stay open (ox-tongue), the feet glide flat ("mud-wading"), breathing is smooth and continuous.
"""
import math

from ffa_dsl import BASES, HS, Clip, F, H, HW, apply_spec, copy_state

from . import clip

A_FOOT_L = dict(at=(0.07, 0.24), yaw=8.0, pv=1.0)
A_FOOT_R = dict(at=(-0.15, -0.17), yaw=-45.0, pv=1.0, kyaw=4.0)


def off(**kw):
    d = {"pel": 2.0, "spine": 1.0, "hand_l": -1.0, "hand_r": -1.0, "neck": -1.5}
    d.update(kw)
    return d


def _hw(p, f, m, e=None):
    return HW(p=p, f=f, m=m, e=e)


LEAD_HI = dict(p=(0.06, 0.52, 1.52), f=(-0.1, 0.35, 1.0), m=(-0.35, 1.0, 0.0), e=(0.2, -0.2, -1.0))
REAR_EL = dict(p=(0.0, 0.29, 1.20), f=(0.25, 0.4, 1.0), m=(-0.1, 1.0, 0.2), e=(-0.4, -0.3, -1.0))


@clip("a_stance")
def a_stance():
    # dragon posture: the torso wound toward the centre, slow coil / uncoil, the lead foot hooks in and out on the ball
    c = Clip("a_stance", 144, "a_stance", loop=True, priority="P0", technique="Bagua dragon posture, coiling",
             hands=("oxtongue", "oxtongue"))
    c.k(36, ease="sp", pel=dict(dyaw=-4, dz=-0.008), spine=dict(dyaw=5), foot_l=F(yaw=16.0),
        hand_l=H(dp=(-0.02, 0.01, -0.02)), hand_r=H(dp=(0.01, 0.01, 0.01)))
    c.k(72, ease="sp", pel=dict(dyaw=2, dz=0.004), spine=dict(dyaw=-2), foot_l=F(yaw=12.0),
        hand_l=H(dp=(0.01, 0.01, 0.015)), hand_r=H(dp=(-0.01, -0.01, -0.005)))
    c.k(108, ease="sp", pel=dict(dyaw=3, dz=0.002), spine=dict(dyaw=-4), foot_l=F(yaw=4.0),
        hand_l=H(dp=(0.015, -0.01, 0.01)), hand_r=H(dp=(-0.005, 0.0, -0.005)))
    c.breathe(depth=0.7, cycles=1)
    return c


@clip("a_palm")
def a_palm():
    # single pushing palm (tui zhang): the waist winds a little further away while the rear palm drops to the hip
    # palm-up, then the waist unwinds and the weight glides forward; the rear palm drives out at chest height turning
    # over (supinated -> palm forward) while the lead palm withdraws to the chest
    c = Clip("a_palm", 22, "a_stance", contact=8, priority="P0", technique="single pushing palm (tui zhang)",
             hands=("oxtongue", "oxtongue"), strike="hand_r", offsets=off(hand_r=0.0, hand_l=-1.5), antic=3, follow=12)
    c.k(3, ease="io", pel=dict(dyaw=-8.0, dz=-0.015, dy=-0.02), spine=dict(dyaw=4.0),
        hand_r=_hw((-0.16, 0.10, 1.02), (0.1, 1.0, 0.0), (0.0, 0.0, 1.0), (-0.4, -1.0, -0.2)),
        hand_l=_hw((0.04, 0.48, 1.46), (-0.1, 0.4, 1.0), (-0.4, 1.0, 0.0), (0.2, -0.2, -1.0)))
    c.k(8, ease="in3", pel=dict(x=0.0, y=0.06, z=-0.14, pitch=4.0, yaw=-16.0), spine=dict(yaw=8.0, pitch=2.0),
        clav_r=dict(prot=12.0, lift=-2.0), foot_r=F(pitch=-8.0),
        hand_r=HS((0.05, 1.0, 0.06), ext=0.96, f=(0.0, 0.25, 1.0), m=(0.0, 1.0, -0.2), e=(-0.5, 0.0, -1.0)),
        hand_l=_hw((0.04, 0.26, 1.26), (-0.3, 0.4, 0.9), (-0.8, 0.4, 0.0), (0.4, -0.3, -1.0)))
    c.hold(10)
    c.k(15, ease="out", pel=dict(y=-0.01, z=-0.125, pitch=2.0, yaw=-30.0), spine=dict(yaw=16.0, pitch=3.0),
        clav_r=dict(prot=3.0, lift=0.0), foot_r=F(pitch=0.0),
        hand_r=_hw((0.02, 0.33, 1.24), (0.2, 0.4, 1.0), (-0.1, 1.0, 0.2), (-0.4, -0.3, -1.0)),
        hand_l=_hw((0.06, 0.46, 1.46), (-0.1, 0.35, 1.0), (-0.35, 1.0, 0.0), (0.2, -0.2, -1.0)))
    c.k(22, ease="io", base=True)
    return c


@clip("a_double_palm")
def a_double_palm():
    # Double Palm Change (shuang huan zhang): the lead foot hooks in on its heel (kou bu) while the body winds away
    # and both palms gather low; the body uncoils, the toe swings back out and both palms drive forward (lead high,
    # rear low) at the end of the turn
    c = Clip("a_double_palm", 30, "a_stance", contact=12, priority="P0", technique="Double Palm Change (shuang huan zhang)",
             hands=("oxtongue", "oxtongue"), strike="hands", metric="fwd_hand_r",
             offsets=off(hand_l=0.0, hand_r=0.0, pel=2.0, spine=1.0),
             antic=7, follow=18)
    c.k(4, ease="io", foot_l=F(pv="heel", yaw=-34.0), pel=dict(dyaw=-18.0, dz=-0.02), spine=dict(dyaw=8.0),
        hand_l=_hw((0.12, 0.38, 1.30), (-0.3, 0.6, 0.7), (-0.4, 0.8, 0.0), (0.4, -0.2, -1.0)))
    c.k(7, ease="io", pel=dict(dyaw=-14.0, dz=-0.03, dy=-0.02), spine=dict(dyaw=6.0, dpitch=2.0),
        hand_l=_hw((0.10, 0.26, 1.02), (-0.5, 0.8, 0.0), (0.0, 0.0, 1.0), (0.5, -0.4, -1.0)),
        hand_r=_hw((-0.12, 0.20, 0.98), (0.5, 0.8, 0.0), (0.0, 0.0, 1.0), (-0.5, -0.4, -1.0)), path="arc")
    c.k(12, ease="in3", foot_l=F(pv="heel", yaw=8.0), pel=dict(x=0.0, y=0.07, z=-0.145, pitch=4.0, yaw=-14.0),
        spine=dict(yaw=10.0, pitch=2.0), clav=dict(prot=10.0, lift=-2.0),
        hand_l=HS((0.0, 1.0, 0.22), ext=0.95, f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.2), e=(0.5, 0.0, -1.0)),
        hand_r=HS((0.10, 1.0, -0.12), ext=0.93, f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.2), e=(-0.5, 0.0, -1.0)))
    c.hold(14)
    c.k(21, ease="out", foot_l=F(**A_FOOT_L), pel=dict(y=-0.02, z=-0.125, pitch=2.0, yaw=-32.0),
        spine=dict(yaw=18.0, pitch=3.0), clav=dict(prot=3.0, lift=0.0),
        hand_l=_hw((0.07, 0.48, 1.46), (-0.1, 0.35, 1.0), (-0.35, 1.0, 0.0), (0.2, -0.2, -1.0)),
        hand_r=_hw((0.02, 0.31, 1.20), (0.25, 0.4, 1.0), (-0.1, 1.0, 0.2), (-0.4, -0.3, -1.0)))
    c.k(30, ease="io", base=True)
    return c


@clip("a_updraft")
def a_updraft():
    # spring jump with rising palms: sink deep pressing both palms down by the hips (pushing the air down), then the
    # legs spring and the palms sweep up past the face; the feet leave the floor at contact, the clip ends airborne
    st = BASES["air"]
    c = Clip("a_updraft", 26, "a_stance", contact=10, priority="P0", technique="spring jump with rising palms",
             hands=("oxtongue", "oxtongue"), end_pose="air", base_end=None, metric="high_hand_l",
             offsets=off(hand_l=0.0, hand_r=0.0, pel=0.0, spine=0.0), antic=6, follow=16, no_balance=True)
    c.k(3, ease="io", pel=dict(dyaw=10.0, dz=-0.03), spine=dict(dyaw=-8.0),
        hand_l=_hw((0.22, 0.30, 1.10), (0.1, 1.0, 0.0), (0.0, 0.0, -1.0), (0.6, -0.3, -1.0)),
        hand_r=_hw((-0.20, 0.24, 1.08), (-0.1, 1.0, 0.0), (0.0, 0.0, -1.0), (-0.6, -0.3, -1.0)))
    c.k(6, ease="io", pel=dict(x=0.0, y=-0.03, z=-0.26, pitch=14.0, yaw=-24.0), spine=dict(pitch=6.0, yaw=12.0),
        neck=dict(pitch=-10.0), foot_r=F(yaw=-38.0),
        hand_l=_hw((0.26, 0.12, 0.82), (0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.26, 0.06, 0.82), (-0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (-0.6, -0.2, -1.0)))
    c.k(9, ease="io", pel=dict(z=-0.01, pitch=0.0, yaw=-14.0), spine=dict(pitch=-4.0, yaw=8.0), neck=dict(pitch=-8.0),
        foot_l=F(pv=1.0, pitch=-30.0), foot_r=F(pv=1.0, pitch=-30.0),
        hand_l=_hw((0.20, 0.30, 1.62), (0.0, 0.3, 1.0), (0.0, 0.4, 0.9), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.20, 0.26, 1.60), (0.0, 0.3, 1.0), (0.0, 0.4, 0.9), (-0.6, -0.2, -1.0)))
    c.k(10, ease="lin", pel=dict(dz=0.01), foot_l=F(lift=0.015, pitch=-36.0), foot_r=F(lift=0.015, pitch=-36.0),
        hand_l=_hw((0.18, 0.24, 1.86), (0.0, 0.1, 1.0), (-0.2, 0.3, 0.9), (0.6, 0.0, -1.0)),
        hand_r=_hw((-0.18, 0.20, 1.84), (0.0, 0.1, 1.0), (0.2, 0.3, 0.9), (-0.6, 0.0, -1.0)))
    c.k(17, ease="out", pel=st["pel"], spine=st["spine"], neck=st["neck"],
        foot_l=F(at=(0.10, 0.02), yaw=6.0, pv=1.0, lift=0.24, pitch=-30.0, kup=0.3),
        foot_r=F(at=(-0.12, -0.08), yaw=-10.0, pv=1.0, lift=0.18, pitch=-34.0, kup=0.3),
        hand_l=_hw((0.40, 0.18, 1.62), (0.7, 0.1, 0.6), (0.2, 0.0, -1.0), (0.4, -0.5, -1.0)),
        hand_r=_hw((-0.40, 0.14, 1.60), (-0.7, 0.1, 0.6), (-0.2, 0.0, -1.0), (-0.4, -0.5, -1.0)))
    c.k(26, ease="io", pel=st["pel"], spine=st["spine"], neck=st["neck"],
        foot_l=F(at=(0.13, 0.05), yaw=6.0, pv=1.0, lift=0.20, pitch=-22.0, kup=0.3),
        foot_r=F(at=(-0.13, -0.06), yaw=-6.0, pv=1.0, lift=0.16, pitch=-26.0, kup=0.3),
        hand_l=HW(p=(0.36, 0.10, 1.22), f=(0.8, 0.2, -0.3), m=(0.0, 0.0, -1.0), e=(0.3, -1.0, -0.3)),
        hand_r=HW(p=(-0.36, 0.06, 1.20), f=(-0.8, 0.2, -0.3), m=(0.0, 0.0, -1.0), e=(-0.3, -1.0, -0.3)))
    return c


@clip("a_dash")
def a_dash():
    # streamlined dash: a quick sink, the body pitches forward and the rear leg drives off and trails (light on the
    # ball), the palms sweep back along the body; the lead palm stays forward to lead the line; recover to the posture
    c = Clip("a_dash", 18, "a_stance", contact=8, priority="P0", technique="streamlined gliding dash",
             hands=("oxtongue", "oxtongue"), metric="fwd_head", offsets=off(pel=1.0, spine=0.5), antic=3, follow=12,
             no_balance=True)
    c.k(3, ease="io", pel=dict(dz=-0.05, dpitch=6.0, dyaw=8.0), spine=dict(dyaw=-6.0),
        hand_r=_hw((-0.14, 0.10, 1.06), (0.0, 1.0, -0.2), (0.6, 0.0, -0.8), (-0.4, -1.0, -0.2)))
    c.k(8, ease="in3", pel=dict(x=0.0, y=0.12, z=-0.20, pitch=34.0, yaw=-12.0), spine=dict(pitch=8.0, yaw=6.0),
        neck=dict(pitch=-32.0), foot_r=F(pv=1.0, pitch=-46.0, lift=0.07, kup=0.1), foot_l=F(yaw=0.0),
        hand_l=_hw((0.08, 0.70, 1.30), (0.0, 1.0, 0.2), (-0.6, 0.0, -0.8), (0.4, -0.2, -1.0)),
        hand_r=_hw((-0.24, -0.30, 1.00), (-0.1, -1.0, -0.2), (0.6, 0.0, -0.8), (-0.4, 0.3, -1.0)))
    c.hold(10)
    c.k(14, ease="out", pel=dict(y=0.0, z=-0.13, pitch=6.0, yaw=-34.0), spine=dict(pitch=3.0, yaw=18.0),
        neck=dict(pitch=-4.0), foot_r=F(**A_FOOT_R), foot_l=F(**A_FOOT_L),
        hand_l=_hw((0.07, 0.50, 1.48), (-0.1, 0.35, 1.0), (-0.35, 1.0, 0.0), (0.2, -0.2, -1.0)),
        hand_r=_hw((0.0, 0.27, 1.18), (0.25, 0.4, 1.0), (-0.1, 1.0, 0.2), (-0.4, -0.3, -1.0)))
    c.k(18, ease="io", base=True)
    return c


def _guard_fn(ph, lag=0.01):
    """Circling palms: both ox-tongue palms turn small circles in front (same direction, lead hand bigger), the waist
    turning with them like a Bagua palm guard."""
    b = BASES["a_stance"]
    th = 2 * math.pi * ph
    out = {"pel": (b["pel"][0], b["pel"][1], b["pel"][2] - 0.006 * math.cos(2 * th), b["pel"][3], b["pel"][4],
                   b["pel"][5] + 5.0 * math.sin(th)),
           "spine": (b["spine"][0], b["spine"][1], b["spine"][2] - 3.0 * math.sin(th))}
    a = th - 2 * math.pi * lag
    out["hand_l"] = HW(p=(0.06 + 0.07 * math.cos(a), 0.50 + 0.03 * math.sin(a), 1.44 + 0.06 * math.sin(a)),
                       f=(-0.1 - 0.3 * math.cos(a), 0.35, 1.0), m=(-0.35 + 0.3 * math.sin(a), 1.0, 0.0),
                       e=(0.2, -0.2, -1.0))
    out["hand_r"] = HW(p=(0.01 + 0.05 * math.cos(a + 0.6), 0.30 + 0.02 * math.sin(a + 0.6), 1.20 + 0.04 * math.sin(a + 0.6)),
                       f=(0.25, 0.4, 1.0), m=(-0.1, 1.0, 0.2), e=(-0.4, -0.3, -1.0))
    return out


def _make_guard_base():
    BASES["a_guard"] = apply_spec(BASES["a_stance"], _guard_fn(0.0), BASES["a_stance"])


_make_guard_base()


@clip("a_guard")
def a_guard():
    c = Clip("a_guard", 96, "a_guard", loop=True, priority="P0", technique="circling palms guard",
             hands=("oxtongue", "oxtongue"), offsets={"neck": 0.0})
    b = BASES["a_guard"]
    for f in range(96):
        st = apply_spec(b, _guard_fn((f / 96 * 2.0) % 1.0), b)
        if f == 0:
            c.keys[0] = (0, st, "lin", None)
        else:
            c.keys.append((f, st, "lin", None))
    c.breathe(depth=0.6, cycles=2)
    return c


# ================================================================================================ P1 helpers
def rot_xy(x, y, deg):
    """Rotate an A-frame floor point about the root by a body yaw (deg, + = toward the character's left)."""
    t = math.radians(deg)
    return (x * math.cos(t) + y * math.sin(t), -x * math.sin(t) + y * math.cos(t))


def foot_at(base_foot, turn, lift=None, **kw):
    """A base footprint carried round the root by `turn` degrees (continuous yaw, so 360 deg turns stay smooth)."""
    x, y = rot_xy(base_foot["at"][0], base_foot["at"][1], turn)
    d = dict(base_foot)
    d.update(at=(x, y), yaw=base_foot["yaw"] + turn)
    d.setdefault("kup", 0.0)
    d.setdefault("toe", 0.0)
    if lift is not None:
        d["lift"] = lift
    d.update(kw)
    return F(**d)


def base_turned(c, frame, turn, ease="io"):
    """Key the base stance carried round by `turn` degrees (yaws offset, so the turn completes instead of unwinding)."""
    b = BASES[c.base]
    pel = list(b["pel"])
    pel[5] += turn
    c.k(frame, ease=ease, pel=tuple(pel), spine=b["spine"], neck=b["neck"], clav_l=b["clav_l"], clav_r=b["clav_r"],
        foot_l=foot_at(A_FOOT_L, turn), foot_r=foot_at(A_FOOT_R, turn),
        hand_l=H(p=b["hand_l"][0:3], f=b["hand_l"][3:6], m=b["hand_l"][6:9], e=b["hand_l"][9:12]),
        hand_r=H(p=b["hand_r"][0:3], f=b["hand_r"][3:6], m=b["hand_r"][6:9], e=b["hand_r"][9:12]),
        fing_l=b["fing_l"], fing_r=b["fing_r"])


def stepping_turn(c, t0, dt, total=-360.0, pel_extra=None):
    """Bagua turning steps: five alternating hook / swing steps carry the stance round `total` degrees (L, R, L, R in
    quarter turns, then the left foot closes), the hips following the feet.  Keys from t0 every dt frames."""
    seq = [("l", 0.25), ("r", 0.5), ("l", 0.75), ("r", 1.0), ("l", 1.0)]
    ang = {"l": 0.0, "r": 0.0}
    t = t0
    base_yaw = BASES[c.base]["pel"][5]
    for side, frac in seq:
        prev = ang[side]
        ang[side] = total * frac
        mid = 0.5 * (prev + ang[side])
        bf = A_FOOT_L if side == "l" else A_FOOT_R
        c.k(t + dt // 2, ease="out", **{"foot_" + side: foot_at(bf, mid, lift=0.05, pv=0.0, kup=0.2)},
            pel=dict(yaw=base_yaw + 0.5 * (ang["l"] + ang["r"]) - 0.25 * (ang[side] - prev)))
        kw = {"foot_" + side: foot_at(bf, ang[side])}
        kw.update(pel_extra or {})
        c.k(t + dt, ease="in", pel=dict(yaw=base_yaw + 0.5 * (ang["l"] + ang["r"]), **(pel_extra or {}).get("pel", {})),
            **{k: v for k, v in kw.items() if k != "pel"})
        t += dt
    return t


# ================================================================================================ P1
@clip("a_hurricane")
def a_hurricane():
    # turning double palm (Hurricane Palm): the lead palm leads the body round a full turn on hook-in / swing-out
    # steps (kou bu / bai bu), the rear palm guarding the elbow; at the end of the turn the palms gather at the waist
    # and both drive forward as the body uncoils (the spring released by the whole turn)
    c = Clip("a_hurricane", 48, "a_stance", contact=28, priority="P1", technique="turning-body double palm drive",
             hands=("oxtongue", "oxtongue"), strike="hands", metric="fwd_hand_r",
             offsets=off(hand_l=0.0, hand_r=0.0, pel=1.0, spine=0.5), antic=20, follow=34, no_balance=True)
    c.k(3, ease="io", pel=dict(dyaw=10.0, dz=-0.03), spine=dict(dyaw=-4.0),
        hand_l=_hw((0.24, 0.42, 1.40), (0.6, 0.6, 0.5), (-0.6, 0.6, 0.0), (0.4, -0.2, -1.0)))
    t = stepping_turn(c, 3, 4, total=-360.0)
    c.k(t + 1, ease="io", pel=dict(dz=-0.04), spine=dict(dyaw=8.0),
        hand_l=_hw((0.12, 0.16, 1.02), (0.2, 1.0, 0.0), (0.0, 0.0, 1.0), (0.5, -0.8, -0.5)),
        hand_r=_hw((-0.14, 0.12, 1.00), (-0.2, 1.0, 0.0), (0.0, 0.0, 1.0), (-0.5, -0.8, -0.5)))
    c.k(28, ease="in3", pel=dict(dyaw=18.0, dy=0.07, dz=-0.02, dpitch=4.0), spine=dict(dyaw=-12.0),
        clav=dict(prot=10.0, lift=-2.0),
        hand_l=HS((0.06, 1.0, 0.14), ext=0.95, f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.2), e=(0.5, 0.0, -1.0)),
        hand_r=HS((0.10, 1.0, -0.06), ext=0.95, f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.2), e=(-0.5, 0.0, -1.0)))
    c.hold(31)
    c.k(40, ease="out", pel=dict(dyaw=-14.0, dy=-0.06, dz=0.04, dpitch=-4.0), spine=dict(dyaw=6.0),
        clav=dict(prot=3.0, lift=0.0),
        hand_l=_hw((0.07, 0.48, 1.46), (-0.1, 0.35, 1.0), (-0.35, 1.0, 0.0), (0.2, -0.2, -1.0)),
        hand_r=_hw((0.02, 0.30, 1.20), (0.25, 0.4, 1.0), (-0.1, 1.0, 0.2), (-0.4, -0.3, -1.0)))
    base_turned(c, 48, -360.0)
    return c


@clip("a_pierce")
def a_pierce():
    # piercing palm (chuan zhang): the rear palm rises under the lead elbow, then slides out over the lead forearm and
    # pierces forward at eye level (fingers first, palm up) as the waist unwinds; the old lead hand drops under the
    # new lead elbow - the hands change roles in one smooth spiral
    c = Clip("a_pierce", 24, "a_stance", contact=10, priority="P1", technique="piercing palm (chuan zhang)",
             hands=("oxtongue", "oxtongue"), strike="hand_r", metric="fwd_hand_r", offsets=off(hand_r=0.0, hand_l=-1.5),
             antic=4, follow=15)
    c.k(4, ease="io", pel=dict(dyaw=-6.0, dz=-0.015), spine=dict(dyaw=4.0),
        hand_r=_hw((0.04, 0.36, 1.32), (0.2, 0.8, 0.5), (0.0, 0.2, 1.0), (-0.4, -0.3, -1.0)),
        hand_l=_hw((0.06, 0.46, 1.42), (-0.1, 0.4, 1.0), (-0.4, 1.0, 0.0), (0.2, -0.2, -1.0)))
    c.k(10, ease="in3", pel=dict(x=0.0, y=0.06, z=-0.14, pitch=4.0, yaw=-18.0), spine=dict(yaw=8.0, pitch=2.0),
        clav_r=dict(prot=12.0), foot_r=F(pitch=-8.0),
        hand_r=HS((0.10, 1.0, 0.20), ext=0.98, f=(0.08, 1.0, 0.18), m=(0.0, -0.2, 1.0), e=(-0.4, 0.0, -1.0)),
        hand_l=_hw((0.00, 0.30, 1.20), (0.25, 0.4, 1.0), (-0.1, 1.0, 0.2), (0.4, -0.3, -1.0)))
    c.hold(12)
    c.k(18, ease="out", pel=dict(y=-0.02, z=-0.125, pitch=2.0, yaw=-32.0), spine=dict(yaw=18.0, pitch=3.0),
        clav_r=dict(prot=3.0), foot_r=F(pitch=0.0),
        hand_r=_hw((0.02, 0.32, 1.22), (0.25, 0.4, 1.0), (-0.1, 1.0, 0.2), (-0.4, -0.3, -1.0)),
        hand_l=_hw((0.06, 0.48, 1.46), (-0.1, 0.35, 1.0), (-0.35, 1.0, 0.0), (0.2, -0.2, -1.0)))
    c.k(24, ease="io", base=True)
    return c


@clip("a_low_palm")
def a_low_palm():
    # swallow skims the water: the body drops low over the bent rear leg (drop stance) and the lead palm skims forward
    # along the floor, then the body rises back into the posture
    c = Clip("a_low_palm", 30, "a_stance", contact=14, priority="P1", technique="swallow skims the water (low palm)",
             hands=("oxtongue", "oxtongue"), metric="fwd_hand_l", offsets=off(hand_l=0.0), antic=7, follow=19,
             no_balance=True)
    c.k(7, ease="io", pel=dict(x=-0.04, y=-0.16, z=-0.34, pitch=16.0, yaw=-30.0), spine=dict(yaw=14.0, pitch=10.0),
        neck=dict(pitch=-12.0), foot_r=F(yaw=-60.0),
        hand_l=_hw((0.14, 0.24, 0.40), (0.0, 0.8, -0.6), (0.0, 0.0, -1.0), (0.6, 0.0, -1.0)),
        hand_r=_hw((-0.30, -0.20, 1.20), (-0.2, -0.3, 0.9), (0.0, -1.0, 0.0), (-0.4, 0.3, -1.0)))
    c.k(14, ease="in", pel=dict(x=-0.02, y=-0.01, z=-0.40, pitch=28.0, yaw=-26.0), spine=dict(yaw=10.0, pitch=16.0),
        neck=dict(pitch=-22.0),
        hand_l=_hw((0.12, 0.82, 0.14), (0.0, 1.0, -0.1), (0.0, 0.0, -1.0), (0.6, 0.2, -1.0)))
    c.hold(16)
    c.k(19, ease="out", pel=dict(y=-0.04, z=-0.32, pitch=20.0), spine=dict(pitch=12.0), neck=dict(pitch=-16.0),
        hand_l=_hw((0.12, 0.50, 0.50), (0.0, 0.8, 0.5), (0.0, 0.3, -0.9), (0.6, 0.0, -1.0)))
    c.k(24, ease="io", pel=dict(x=0.0, y=-0.03, z=-0.14, pitch=3.0, yaw=-36.0), spine=dict(yaw=20.0, pitch=3.0),
        neck=dict(pitch=-3.0), foot_r=F(yaw=-45.0),
        hand_l=_hw((0.07, 0.48, 1.44), (-0.1, 0.35, 1.0), (-0.35, 1.0, 0.0), (0.2, -0.2, -1.0)),
        hand_r=_hw((0.0, 0.28, 1.18), (0.25, 0.4, 1.0), (-0.1, 1.0, 0.2), (-0.4, -0.3, -1.0)))
    c.k(30, ease="io", base=True)
    return c


@clip("a_turn_palm")
def a_turn_palm():
    # turning-body palm: the lead foot hooks in (kou bu) and the body pivots right, the lead palm sweeping flat round
    # at shoulder height (contact as it crosses the front); the foot swings back out (bai bu) to the posture
    c = Clip("a_turn_palm", 36, "a_stance", contact=12, priority="P1", technique="turning-body palm (kou bu / bai bu)",
             hands=("oxtongue", "oxtongue"), metric="fwd_hand_l", offsets=off(hand_l=0.0), antic=5, follow=18)
    c.k(5, ease="io", pel=dict(dyaw=14.0, dz=-0.02), spine=dict(dyaw=-4.0),
        hand_l=_hw((0.42, 0.34, 1.36), (0.8, 0.4, 0.3), (-0.4, 0.8, 0.0), (0.4, -0.3, -1.0)),
        foot_l=foot_at(A_FOOT_L, 0.0, lift=0.03, pv=0.0))
    c.k(8, ease="in", foot_l=foot_at(A_FOOT_L, -50.0, pv=1.0), pel=dict(dyaw=-20.0))
    c.k(12, ease="in3", pel=dict(yaw=-80.0, dz=-0.02), spine=dict(yaw=24.0), foot_r=F(yaw=-80.0), clav_l=dict(prot=10.0),
        hand_l=HS((0.0, 1.0, 0.05), ext=0.96, f=(-0.4, 0.8, 0.2), m=(-0.6, 0.6, -0.4), e=(0.5, 0.0, -1.0)), path="arc")
    c.k(17, ease="out", pel=dict(yaw=-100.0), spine=dict(yaw=28.0),
        hand_l=_hw((-0.40, 0.36, 1.32), (-0.8, 0.3, 0.2), (-0.3, -0.5, -0.7), (0.4, -0.2, -1.0)), path="arc")
    c.k(23, ease="io", foot_l=foot_at(A_FOOT_L, -50.0, lift=0.03, pv=0.0), pel=dict(yaw=-60.0), spine=dict(yaw=22.0),
        foot_r=F(yaw=-60.0), clav_l=dict(prot=4.0))
    c.k(27, ease="in", foot_l=F(**A_FOOT_L), foot_r=F(**A_FOOT_R), pel=dict(yaw=-38.0),
        hand_l=_hw((0.07, 0.48, 1.46), (-0.1, 0.35, 1.0), (-0.35, 1.0, 0.0), (0.2, -0.2, -1.0)))
    c.k(36, ease="io", base=True)
    return c


@clip("a_wall_push")
def a_wall_push():
    # big two-palm push with a step: the palms draw in to the chest as the lead foot steps in, then the whole body
    # drives both palms out wide and high (a wall of wind)
    c = Clip("a_wall_push", 30, "a_stance", contact=10, priority="P1", technique="big double-palm push with a step",
             hands=("palm", "palm"), strike="hands", metric="fwd_hand_l", offsets=off(hand_l=0.0, hand_r=0.0),
             antic=4, follow=16)
    c.k(4, ease="io", pel=dict(dyaw=8.0, dz=-0.02, dy=-0.02), foot_l=F(pv=0.0, lift=0.03),
        hand_l=_hw((0.14, 0.22, 1.24), (0.0, 0.4, 0.9), (0.0, 0.8, -0.5), (0.6, -0.3, -1.0)),
        hand_r=_hw((-0.12, 0.20, 1.22), (0.0, 0.4, 0.9), (0.0, 0.8, -0.5), (-0.6, -0.3, -1.0)), fing="palm")
    c.k(7, ease="in", foot_l=F(at=(0.10, 0.40), yaw=0.0, pv=1.0), pel=dict(dy=0.05))
    c.k(10, ease="in3", pel=dict(x=0.02, y=0.12, z=-0.16, pitch=6.0, yaw=-14.0), spine=dict(yaw=6.0, pitch=3.0),
        clav=dict(prot=10.0, lift=-1.0), foot_r=F(yaw=-38.0, pitch=-6.0),
        hand_l=HS((0.25, 1.0, 0.10), ext=0.96, f=(0.0, 0.25, 1.0), m=(0.0, 1.0, -0.2), e=(0.5, 0.0, -1.0)),
        hand_r=HS((-0.10, 1.0, 0.10), ext=0.96, f=(0.0, 0.25, 1.0), m=(0.0, 1.0, -0.2), e=(-0.5, 0.0, -1.0)))
    c.hold(12)
    c.k(18, ease="out", pel=dict(y=0.02, z=-0.13, pitch=2.0, yaw=-30.0), spine=dict(yaw=16.0, pitch=3.0),
        clav=dict(prot=3.0, lift=0.0), foot_r=F(yaw=-45.0, pitch=0.0), fing="oxtongue",
        hand_l=_hw((0.07, 0.50, 1.46), (-0.1, 0.35, 1.0), (-0.35, 1.0, 0.0), (0.2, -0.2, -1.0)),
        hand_r=_hw((0.0, 0.30, 1.20), (0.25, 0.4, 1.0), (-0.1, 1.0, 0.2), (-0.4, -0.3, -1.0)))
    c.k(21, ease="io", foot_l=F(pv=0.0, lift=0.025))
    c.k(24, ease="in", foot_l=F(**A_FOOT_L), pel=dict(y=-0.04))
    c.k(30, ease="io", base=True)
    return c


@clip("a_downdraft")
def a_downdraft():
    # palms press down from above: both palms rise overhead on the inhale, then press straight down to the hips as
    # the stance sinks hard (Downdraft, Funnel Down, Anchor)
    c = Clip("a_downdraft", 22, "a_stance", contact=7, priority="P1", technique="palms press down from above",
             hands=("palm", "palm"), metric="low_hand_r", offsets=off(hand_l=0.0, hand_r=0.0), antic=3, follow=12)
    c.k(3, ease="out", pel=dict(dz=0.03, dyaw=8.0), spine=dict(dyaw=-8.0, dpitch=-4.0), clav=dict(lift=14.0), fing="palm",
        hand_l=_hw((0.18, 0.30, 1.86), (-0.2, 0.6, 0.0), (0.0, 0.0, -1.0), (0.6, 0.0, -0.4)),
        hand_r=_hw((-0.16, 0.28, 1.84), (0.2, 0.6, 0.0), (0.0, 0.0, -1.0), (-0.6, 0.0, -0.4)))
    c.k(7, ease="acc", pel=dict(dz=-0.15, dpitch=6.0), spine=dict(dpitch=8.0), clav=dict(lift=-4.0), neck=dict(pitch=-8.0),
        hand_l=_hw((0.22, 0.30, 0.78), (-0.3, 0.9, 0.0), (0.0, 0.0, -1.0), (0.7, -0.2, -0.6)),
        hand_r=_hw((-0.20, 0.26, 0.76), (0.3, 0.9, 0.0), (0.0, 0.0, -1.0), (-0.7, -0.2, -0.6)))
    c.hold(9)
    c.k(16, ease="out", pel=dict(dz=0.11, dpitch=-6.0), spine=dict(dpitch=-8.0), clav=dict(lift=0.0), fing="oxtongue",
        neck=dict(pitch=-3.0),
        hand_l=_hw((0.07, 0.48, 1.44), (-0.1, 0.35, 1.0), (-0.35, 1.0, 0.0), (0.2, -0.2, -1.0)),
        hand_r=_hw((0.0, 0.28, 1.18), (0.25, 0.4, 1.0), (-0.1, 1.0, 0.2), (-0.4, -0.3, -1.0)))
    c.k(22, ease="io", base=True)
    return c


@clip("a_spin")
def a_spin():
    # 360 deg turn on one foot: wind left, then the body spins right on the ball of the left foot with the right knee
    # drawn up and both palms flung out (contact at three-quarters of the turn), the right foot sets down to close
    c = Clip("a_spin", 36, "a_stance", contact=12, priority="P1", technique="spin on one foot (360 deg)",
             hands=("oxtongue", "oxtongue"), offsets=off(pel=0.0, spine=0.0), antic=3, follow=18, no_balance=True)
    c.k(3, ease="io", pel=dict(dyaw=24.0, dz=-0.04, x=0.04, y=0.10), spine=dict(dyaw=-8.0),
        hand_l=_hw((0.36, 0.26, 1.30), (0.8, 0.2, 0.3), (0.0, 1.0, 0.0), (0.4, -0.4, -1.0)))
    yaw0 = BASES["a_stance"]["pel"][5] + 24.0
    for i, (t, turn) in enumerate(((6, -90.0), (9, -180.0), (12, -270.0), (15, -360.0))):
        tt = turn - 24.0 * (1 - (i + 1) / 4.0)
        rx, ry = rot_xy(-0.10, 0.10, tt)
        c.k(t, ease="lin", pel=dict(yaw=yaw0 + turn - 24.0 * ((i + 1) / 4.0), x=0.05, y=0.16, z=-0.10),
            foot_l=F(**dict(A_FOOT_L, yaw=8.0 + turn)),
            foot_r=F(at=(0.05 + rx, 0.16 + ry), yaw=-20.0 + turn, pv=0.0, lift=0.26, pitch=-30.0, kup=0.4),
            hand_l=HS(rot_xy(1.0, 0.25, tt) + (0.1,), ext=0.9, f=(0.0, 0.0, 1.0), m=(0.0, 0.0, -1.0)),
            hand_r=HS(rot_xy(-1.0, 0.25, tt) + (0.1,), ext=0.9, f=(0.0, 0.0, 1.0), m=(0.0, 0.0, -1.0)))
    c.k(19, ease="in", foot_r=foot_at(A_FOOT_R, -360.0), pel=dict(x=0.0, y=-0.02, z=-0.14, yaw=-38.0 - 360.0 + 6.0))
    base_turned(c, 36, -360.0)
    return c


def _circle_arms(ph):
    b = BASES["a_stance"]
    g = 0.012 * math.sin(4 * math.pi * ph)
    return {"hand_l": H(p=(b["hand_l"][0], b["hand_l"][1], b["hand_l"][2] + g), f=b["hand_l"][3:6], m=b["hand_l"][6:9],
                        e=b["hand_l"][9:12]),
            "hand_r": H(p=b["hand_r"][0:3], f=b["hand_r"][3:6], m=b["hand_r"][6:9], e=b["hand_r"][9:12]),
            "spine": (2.0, 0.0, 26.0 + 3.0 * math.cos(2 * math.pi * ph)), "neck": (-2.0, 0.0, 6.0)}


@clip("a_circle_walk")
def a_circle_walk():
    # circle walking (zou quan) with the mud-wading step (tang ni bu): knees bent, feet gliding flat just above the
    # floor and landing flat, the torso wound toward the centre (left) with the dragon-posture palms fixed on it
    from .shared import gait
    return gait("a_circle_walk", 96, 2.0, (0.0, 1.0), 0.62, base="a_stance", z=-0.13, lift=0.035, hs=0.0, to=-8.0,
                lane=0.09, lean=2.0, bob=0.006, sway=0.006, yaw_amp=3.0, spec_fn=_circle_arms, cycles=3,
                technique="circle walking, mud-wading step (2.0 m/s)", priority="P1", hands=("oxtongue", "oxtongue"),
                foot_yaw=4.0, offsets={"neck": 0.0})


@clip("a_gather")
def a_gather():
    # wide gathering circle (Eye of the Storm / Vacuum Well): the arms sweep out low and up the sides into a wide
    # circle overhead, then come down the centre gathering the air into a big ball in front of the chest (contact);
    # the ball is held and stirred, then released back to the posture
    c = Clip("a_gather", 40, "a_stance", contact=16, priority="P1", technique="wide gathering circle overhead",
             hands=("oxtongue", "oxtongue"), offsets=off(hand_l=0.0, hand_r=0.0), antic=10, follow=24)
    c.k(5, ease="io", pel=dict(dyaw=12.0, dz=-0.02), spine=dict(dyaw=-8.0),
        hand_l=_hw((0.44, 0.20, 1.00), (0.6, 0.2, -0.6), (0.0, 0.3, 1.0), (0.4, -0.4, -1.0)),
        hand_r=_hw((-0.44, 0.16, 1.00), (-0.6, 0.2, -0.6), (0.0, 0.3, 1.0), (-0.4, -0.4, -1.0)), path="arc")
    c.k(10, ease="io", pel=dict(dz=0.04), spine=dict(dpitch=-4.0), clav=dict(lift=14.0), neck=dict(pitch=6.0),
        hand_l=_hw((0.36, 0.20, 1.86), (0.3, 0.1, 1.0), (-0.6, 0.0, 0.3), (0.6, 0.0, -0.4)),
        hand_r=_hw((-0.36, 0.16, 1.86), (-0.3, 0.1, 1.0), (0.6, 0.0, 0.3), (-0.6, 0.0, -0.4)), path="arc")
    c.k(16, ease="in", pel=dict(dz=-0.07, dyaw=-6.0, dpitch=3.0), spine=dict(dpitch=2.0), clav=dict(lift=0.0),
        neck=dict(pitch=-4.0),
        hand_l=_hw((0.16, 0.44, 1.22), (-0.5, 0.6, 0.4), (-0.9, 0.0, 0.0), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.16, 0.42, 1.20), (0.5, 0.6, 0.4), (0.9, 0.0, 0.0), (-0.6, -0.2, -1.0)), path="arc")
    c.k(22, ease="sp", hand_l=H(dp=(-0.02, 0.02, 0.03)), hand_r=H(dp=(-0.02, -0.02, -0.03)), pel=dict(dyaw=3.0))
    c.k(28, ease="sp", hand_l=H(dp=(0.03, -0.02, -0.02)), hand_r=H(dp=(0.03, 0.02, 0.02)), pel=dict(dyaw=-3.0))
    c.k(40, ease="io", base=True)
    return c


@clip("a_pluck")
def a_pluck():
    # pluck (cai): both hands shoot out and seize, then rip back to the hips as the body sits back and down - the
    # air is torn toward the body (Suction Line)
    c = Clip("a_pluck", 24, "a_stance", contact=10, priority="P1", technique="pluck (cai), pulling to the body",
             hands=("spread", "spread"), offsets=off(hand_l=0.0, hand_r=0.0), antic=5, follow=15)
    c.k(5, ease="out", pel=dict(dy=0.04, dyaw=6.0), clav=dict(prot=10.0), fing="spread",
        hand_l=HS((0.10, 1.0, 0.12), ext=0.96, f=(0.0, 1.0, 0.1), m=(0.0, 0.0, -1.0), e=(0.5, 0.0, -1.0)),
        hand_r=HS((-0.04, 1.0, 0.0), ext=0.96, f=(0.0, 1.0, 0.1), m=(0.0, 0.0, -1.0), e=(-0.5, 0.0, -1.0)))
    c.k(10, ease="in3", pel=dict(dy=-0.11, dz=-0.05, dyaw=-6.0, dpitch=-2.0), spine=dict(dpitch=-3.0),
        clav=dict(prot=-4.0), fing="fist",
        hand_l=_hw((0.18, 0.12, 1.00), (0.0, 1.0, -0.2), (0.0, 0.0, -1.0), (0.5, -1.0, -0.4)),
        hand_r=_hw((-0.20, 0.06, 0.98), (0.0, 1.0, -0.2), (0.0, 0.0, -1.0), (-0.5, -1.0, -0.4)))
    c.hold(12)
    c.k(18, ease="out", pel=dict(dy=0.06, dz=0.04, dpitch=2.0), spine=dict(dpitch=3.0), clav=dict(prot=3.0),
        fing="oxtongue",
        hand_l=_hw((0.07, 0.48, 1.44), (-0.1, 0.35, 1.0), (-0.35, 1.0, 0.0), (0.2, -0.2, -1.0)),
        hand_r=_hw((0.0, 0.28, 1.18), (0.25, 0.4, 1.0), (-0.1, 1.0, 0.2), (-0.4, -0.3, -1.0)))
    c.k(24, ease="io", base=True)
    return c


@clip("a_clap")
def a_clap():
    # sharp clap: the palms fly open wide, then slam together in front of the chest with a small forward jolt
    c = Clip("a_clap", 18, "a_stance", contact=6, priority="P1", technique="sharp clap", hands=("palm", "palm"),
             offsets=off(hand_l=0.0, hand_r=0.0), antic=3, follow=10)
    c.k(3, ease="out", pel=dict(dyaw=10.0, dz=0.01), spine=dict(dyaw=-6.0, dpitch=-3.0), clav=dict(lift=4.0), fing="palm",
        hand_l=_hw((0.50, 0.22, 1.32), (0.6, 0.6, 0.3), (-0.8, 0.4, 0.0), (0.4, -0.4, -1.0)),
        hand_r=_hw((-0.50, 0.20, 1.30), (-0.6, 0.6, 0.3), (0.8, 0.4, 0.0), (-0.4, -0.4, -1.0)))
    c.k(6, ease="in4", pel=dict(dy=0.03, dz=-0.02, dyaw=-4.0), spine=dict(dpitch=6.0), clav=dict(lift=-2.0, prot=8.0),
        hand_l=_hw((0.015, 0.46, 1.30), (0.0, 0.6, 0.8), (-1.0, 0.0, 0.0), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.015, 0.46, 1.30), (0.0, 0.6, 0.8), (1.0, 0.0, 0.0), (-0.6, -0.2, -1.0)))
    c.hold(8)
    c.k(18, ease="io", base=True)
    return c


@clip("a_roar")
def a_roar():
    # chest-expanding shout: a deep breath draws the arms in to the chest, then the arms fling open and back, the
    # chest drives forward and the head pushes out - the shout is held a moment (Shout / Roar / Resonance)
    c = Clip("a_roar", 36, "a_stance", contact=14, priority="P1", technique="chest-expanding shout",
             hands=("spread", "spread"), offsets=off(hand_l=0.0, hand_r=0.0), antic=8, follow=22)
    c.k(8, ease="io", pel=dict(dyaw=14.0, dy=-0.03, dz=-0.02), spine=dict(dyaw=-10.0, dpitch=-6.0), neck=dict(pitch=4.0),
        clav=dict(lift=6.0, prot=6.0),
        hand_l=_hw((0.10, 0.18, 1.30), (-0.4, 0.4, 0.8), (-0.6, -0.6, 0.0), (0.6, -0.4, -1.0)),
        hand_r=_hw((-0.10, 0.16, 1.30), (0.4, 0.4, 0.8), (0.6, -0.6, 0.0), (-0.6, -0.4, -1.0)), fing="willow")
    c.k(14, ease="in3", pel=dict(dyaw=-6.0, dy=0.08, dz=-0.04, dpitch=6.0), spine=dict(dyaw=-2.0, dpitch=-8.0),
        neck=dict(pitch=-12.0), clav=dict(lift=-2.0, prot=-10.0), fing="spread",
        hand_l=_hw((0.66, -0.04, 1.30), (1.0, -0.2, 0.2), (0.0, 1.0, 0.0), (0.2, -1.0, -0.4)),
        hand_r=_hw((-0.66, -0.06, 1.28), (-1.0, -0.2, 0.2), (0.0, 1.0, 0.0), (-0.2, -1.0, -0.4)))
    c.k(20, ease="lin", hand_l=H(dp=(0.0, -0.01, 0.01)), hand_r=H(dp=(0.0, -0.01, 0.01)))
    c.tremble(amp=1.0, f0=14, f1=21, ramp=2)
    c.k(36, ease="io", base=True)
    return c
