"""Shared action clips (MARTIAL_ARTS.md §3.1): evades, jump / fall / land, glide, hit reactions, knockdown / getup,
stagger, block impact, deflect.  Element-neutral body mechanics; every clip is in place (the sim moves the fighter)."""
from ffa_dsl import BASES, Clip, F, H, HW, apply_spec, copy_state

from . import clip

IDLE_L = dict(at=(0.145, -0.025), yaw=8.0, pv=1.0)
IDLE_R = dict(at=(-0.145, -0.025), yaw=-8.0, pv=1.0)
GUARD_L = dict(p=(0.12, 0.33, 1.30), f=(-0.25, 0.55, 0.8), m=(-0.85, 0.0, 0.3), e=(0.3, -0.2, -1.0))
GUARD_R = dict(p=(-0.06, 0.22, 1.26), f=(0.25, 0.45, 0.85), m=(0.85, 0.0, 0.25), e=(-0.3, -0.3, -1.0))


def _hw(d, dz=0.0, dx=0.0, dy=0.0):
    p = d["p"]
    return HW(p=(p[0] + dx, p[1] + dy, p[2] + dz), f=d["f"], m=d["m"], e=d["e"])


def _make_air_base():
    """Airborne pose shared by jump (end) / fall / land (start): knees drawn up a little, toes pointing down, arms
    out for balance.  Feet are relative to the root (the sim lifts the root)."""
    st = apply_spec(BASES["idle"], dict(
        pel=(0.0, 0.0, 0.0, 4.0, 0.0, 0.0), spine=(3.0, 0.0, 0.0), neck=(-4.0, 0.0, 0.0),
        foot_l=F(at=(0.13, 0.05), yaw=6.0, pv=1.0, lift=0.20, pitch=-22.0, kup=0.3),
        foot_r=F(at=(-0.13, -0.06), yaw=-6.0, pv=1.0, lift=0.16, pitch=-26.0, kup=0.3),
        hand_l=HW(p=(0.36, 0.10, 1.22), f=(0.8, 0.2, -0.3), m=(0.0, 0.0, -1.0), e=(0.3, -1.0, -0.3)),
        hand_r=HW(p=(-0.36, 0.06, 1.20), f=(-0.8, 0.2, -0.3), m=(0.0, 0.0, -1.0), e=(-0.3, -1.0, -0.3)),
        fing="relaxed"), BASES["idle"])
    BASES["air"] = st
    return st


def _make_lying_base():
    """Lying on the back after a knockdown: pelvis on the floor, legs along the floor, toes up, arms out."""
    st = apply_spec(BASES["idle"], dict(
        pel=(0.0, -0.62, -0.835, -86.0, 0.0, 0.0), spine=(4.0, 0.0, 0.0), neck=(16.0, 0.0, 6.0), gaze=(0.0, 0.0, 0.0),
        foot_l=F(at=(0.20, 0.30), yaw=18.0, pv=-1.0, pitch=62.0), foot_r=F(at=(-0.16, 0.26), yaw=-14.0, pv=-1.0, pitch=58.0),
        hand_l=HW(p=(0.50, -0.88, 0.07), f=(0.6, -0.4, 0.0), m=(0.0, 0.0, 1.0), e=(0.0, 0.0, 1.0)),
        hand_r=HW(p=(-0.48, -0.70, 0.07), f=(-0.6, 0.2, 0.0), m=(0.0, 0.0, 1.0), e=(0.0, 0.0, 1.0)),
        fing="relaxed"), BASES["idle"])
    BASES["lying"] = st
    return st


_make_air_base()
_make_lying_base()


# ================================================================================================ evades
def _evade(name, d):
    """Low evasive hop: load the far leg, push off, both feet briefly off the floor at the apex (contact = centre of
    the i-frames), land lead foot first (ball), absorb, rise.  d = travel direction (A-frame x, y)."""
    dx, dy = d
    c = Clip(name, 28, "idle", contact=12, priority="P0", technique="low evasive hop (step out of the line)",
             hands=("relaxed", "relaxed"), metric="apex", offsets={"pel": 1.0, "spine": 0.5, "hand_l": -1.0,
                                                                     "hand_r": -1.0}, antic=5, follow=18, no_balance=True)
    lead = "foot_l" if dx > 0.5 else ("foot_r" if dx < -0.5 else None)
    # anticipation: sink and load the far side, guard comes up
    c.k(5, ease="io", pel=dict(dx=-0.045 * dx, dy=-0.04 * dy, dz=-0.10, dpitch=5.0 + 8.0 * dy, dside=-6.0 * dx),
        spine=dict(dpitch=4.0 * dy + 3.0, dside=-4.0 * dx), hand_l=_hw(GUARD_L, -0.08), hand_r=_hw(GUARD_R, -0.08),
        fing="fist")
    # push-off: the near foot lifts first and reaches into the travel, the far leg extends
    near = {}
    if lead:
        near[lead] = F(pv=1.0, lift=0.07, pitch=-14.0, move=(0.16 * dx, 0.0), kup=0.2)
    else:
        near["foot_l"] = F(pv=1.0, lift=0.0, pitch=-22.0)
        near["foot_r"] = F(pv=1.0, lift=0.0, pitch=-22.0)
    c.k(8, ease="out", pel=dict(dx=0.09 * dx, dy=0.07 * dy, dz=0.08, dpitch=8.0 * dy, dside=16.0 * dx),
        spine=dict(dside=5.0 * dx, dpitch=3.0 * dy), **near)
    # apex: both feet off the floor, knees tucked, the body leaning into the travel
    feet = dict(foot_l=F(pv=1.0, lift=0.11 if lead != "foot_r" else 0.07, pitch=-18.0, kup=0.3),
                foot_r=F(pv=1.0, lift=0.11 if lead != "foot_l" else 0.07, pitch=-18.0, kup=0.3))
    c.k(12, ease="out", pel=dict(dx=0.0, dz=0.035, dside=3.0 * dx, dpitch=2.0 * dy), spine=dict(dside=3.0 * dx),
        hand_l=_hw(GUARD_L, -0.03), hand_r=_hw(GUARD_R, -0.03), **feet)
    # touch down: lead foot first on the ball, then the other, deep absorb
    land1 = dict(IDLE_L) if lead != "foot_r" else dict(IDLE_R)
    k1 = "foot_l" if lead != "foot_r" else "foot_r"
    c.k(15, ease="in", **{k1: F(**dict(land1, pitch=-10.0))}, pel=dict(dz=-0.05, dside=-3.0 * dx))
    other = "foot_r" if k1 == "foot_l" else "foot_l"
    land2 = dict(IDLE_R) if other == "foot_r" else dict(IDLE_L)
    c.k(17, ease="in", **{other: F(**land2), k1: F(**land1)}, pel=dict(dz=-0.06))
    c.k(20, ease="out", pel=dict(x=-0.02 * dx, y=-0.02 * dy, z=-0.115, pitch=6.0, side=-3.0 * dx, yaw=0.0),
        spine=dict(pitch=4.0, side=1.0 * dx), hand_l=_hw(GUARD_L, -0.13), hand_r=_hw(GUARD_R, -0.13))
    c.k(28, ease="io", base=True)
    return c


@clip("evade_l")
def evade_l():
    return _evade("evade_l", (1.0, 0.0))


@clip("evade_r")
def evade_r():
    return _evade("evade_r", (-1.0, 0.0))


@clip("evade_fwd")
def evade_fwd():
    return _evade("evade_fwd", (0.0, 1.0))


@clip("evade_back")
def evade_back():
    return _evade("evade_back", (0.0, -1.0))


# ================================================================================================ air
@clip("jump")
def jump():
    # crouch -> arms swing back -> triple extension (hip, knee, ankle) -> feet leave the floor at contact
    c = Clip("jump", 18, "idle", contact=10, priority="P0", technique="two-foot take-off", base_end=None,
             end_pose="air", hands=("relaxed", "relaxed"), offsets={"pel": 0.5, "hand_l": -1.0, "hand_r": -1.0},
             antic=5, no_balance=True)
    c.k(5, ease="io", pel=dict(z=-0.17, pitch=16.0), spine=dict(pitch=10.0), neck=dict(pitch=-10.0),
        hand_l=HW(p=(0.22, -0.18, 0.92), f=(0.2, -0.6, -0.8), m=(-0.9, 0.0, 0.3), e=(0.5, 0.3, -1.0)),
        hand_r=HW(p=(-0.22, -0.18, 0.92), f=(-0.2, -0.6, -0.8), m=(0.9, 0.0, 0.3), e=(-0.5, 0.3, -1.0)))
    c.k(9, ease="in", pel=dict(z=0.0, pitch=2.0), spine=dict(pitch=-2.0), neck=dict(pitch=-2.0),
        feet=F(pv=1.0, pitch=-30.0),
        hand_l=HW(p=(0.24, 0.24, 1.55), f=(0.1, 0.6, 0.8), m=(-0.8, 0.0, 0.0), e=(0.5, -0.3, -1.0)),
        hand_r=HW(p=(-0.24, 0.24, 1.55), f=(-0.1, 0.6, 0.8), m=(0.8, 0.0, 0.0), e=(-0.5, -0.3, -1.0)))
    c.k(10, ease="lin", pel=dict(dz=0.008), foot_l=F(lift=0.012, pitch=-34.0), foot_r=F(lift=0.012, pitch=-34.0))
    st = BASES["air"]
    c.k(18, ease="out", pel=st["pel"], spine=st["spine"], neck=st["neck"], foot_l=F(**_air_foot("l")),
        foot_r=F(**_air_foot("r")), hand_l=HW(p=(0.34, 0.12, 1.30), f=(0.8, 0.2, 0.0), m=(0.0, 0.0, -1.0), e=(0.3, -1.0, -0.3)),
        hand_r=HW(p=(-0.34, 0.10, 1.28), f=(-0.8, 0.2, 0.0), m=(0.0, 0.0, -1.0), e=(-0.3, -1.0, -0.3)))
    return c


def _air_foot(side):
    if side == "l":
        return dict(at=(0.13, 0.05), yaw=6.0, pv=1.0, lift=0.20, pitch=-22.0, kup=0.3)
    return dict(at=(-0.13, -0.06), yaw=-6.0, pv=1.0, lift=0.16, pitch=-26.0, kup=0.3)


@clip("fall")
def fall():
    # airborne loop: legs cycle slowly (one knee rises as the other drops), arms balance in slow opposite circles
    c = Clip("fall", 48, "air", loop=True, priority="P0", technique="airborne, arms balancing",
             hands=("relaxed", "relaxed"), no_balance=True, offsets={"neck": -2.0, "hand_l": -2.0, "hand_r": -2.0},
             base_check=False)
    c.k(12, ease="sp", foot_l=F(lift=0.24, pitch=-18.0), foot_r=F(lift=0.13, pitch=-30.0),
        pel=dict(dside=1.5), hand_l=H(dp=(0.02, 0.03, 0.05)), hand_r=H(dp=(-0.01, -0.03, -0.03)))
    c.k(24, ease="sp", foot_l=F(lift=0.17, pitch=-26.0), foot_r=F(lift=0.21, pitch=-20.0),
        pel=dict(dside=-1.0), hand_l=H(dp=(0.0, 0.0, 0.01)), hand_r=H(dp=(0.0, 0.01, 0.02)))
    c.k(36, ease="sp", foot_l=F(lift=0.14, pitch=-28.0), foot_r=F(lift=0.22, pitch=-18.0),
        pel=dict(dside=-1.5), hand_l=H(dp=(-0.01, -0.03, -0.03)), hand_r=H(dp=(0.02, 0.03, 0.05)))
    return c


@clip("land")
def land():
    # balls then heels touch, knees and hips absorb (contact = impact), arms drop forward, rise to idle
    c = Clip("land", 18, "air", base_end="idle", contact=4, priority="P0", technique="landing absorption",
             hands=("relaxed", "relaxed"), start_pose_free=True,
             offsets={"pel": 0.0, "hand_l": -1.0, "hand_r": -1.0, "neck": -1.5}, antic=2, follow=8, no_balance=True)
    c.k(2, ease="in", foot_l=F(**dict(IDLE_L, pitch=-16.0)), foot_r=F(**dict(IDLE_R, pitch=-16.0)),
        pel=dict(z=-0.02, pitch=4.0))
    c.k(4, ease="lin", foot_l=F(**IDLE_L), foot_r=F(**IDLE_R), pel=dict(z=-0.10, pitch=10.0), spine=dict(pitch=6.0),
        neck=dict(pitch=-6.0), hand_l=HW(p=(0.26, 0.20, 1.02), f=(0.2, 0.7, -0.5), m=(-0.4, 0.0, -0.9), e=(0.5, -0.5, -1.0)),
        hand_r=HW(p=(-0.26, 0.20, 1.02), f=(-0.2, 0.7, -0.5), m=(0.4, 0.0, -0.9), e=(-0.5, -0.5, -1.0)))
    c.k(7, ease="out", pel=dict(z=-0.155, pitch=14.0), spine=dict(pitch=8.0), neck=dict(pitch=-8.0),
        hand_l=HW(p=(0.25, 0.26, 0.96), f=(0.1, 0.8, -0.4), m=(-0.4, 0.0, -0.9), e=(0.5, -0.5, -1.0)),
        hand_r=HW(p=(-0.25, 0.26, 0.96), f=(-0.1, 0.8, -0.4), m=(0.4, 0.0, -0.9), e=(-0.5, -0.5, -1.0)))
    c.k(12, ease="io", pel=dict(z=-0.06, pitch=4.0), spine=dict(pitch=2.0), neck=dict(pitch=-2.0))
    c.k(18, ease="io", base=True)
    return c


@clip("glide")
def glide():
    # arms spread like wings, body pitched forward, legs trailing together; slow banking undulation
    st = apply_spec(BASES["idle"], dict(
        pel=(0.0, 0.0, 0.05, 58.0, 0.0, 0.0), spine=(-14.0, 0.0, 0.0), neck=(-30.0, 0.0, 0.0),
        foot_l=F(at=(0.10, -0.62), yaw=4.0, pv=1.0, lift=0.62, pitch=-48.0, kup=-0.2),
        foot_r=F(at=(-0.10, -0.66), yaw=-4.0, pv=1.0, lift=0.58, pitch=-50.0, kup=-0.2),
        hand_l=HW(p=(0.68, 0.18, 1.30), f=(1.0, 0.0, 0.05), m=(0.0, 0.2, -1.0), e=(0.0, -0.3, 1.0)),
        hand_r=HW(p=(-0.68, 0.18, 1.30), f=(-1.0, 0.0, 0.05), m=(0.0, 0.2, -1.0), e=(0.0, -0.3, 1.0)),
        fing="palm"), BASES["idle"])
    BASES["glide"] = st
    c = Clip("glide", 96, "glide", loop=True, priority="P0", technique="gliding, arms spread, legs trailing",
             hands=("palm", "palm"), no_balance=True, offsets={"neck": -3.0, "hand_l": -3.0, "hand_r": -3.0},
             base_check=False)
    c.k(24, ease="sp", pel=dict(dside=-6.0, dz=0.012), spine=dict(dside=-2.0), hand_l=H(dp=(0.0, 0.0, 0.05)),
        hand_r=H(dp=(0.0, 0.0, -0.04)), foot_l=F(lift=0.60), foot_r=F(lift=0.61))
    c.k(48, ease="sp", pel=dict(dside=0.0, dz=-0.008), hand_l=H(dp=(0.0, 0.0, -0.02)), hand_r=H(dp=(0.0, 0.0, -0.02)))
    c.k(72, ease="sp", pel=dict(dside=6.0, dz=0.012), spine=dict(dside=2.0), hand_l=H(dp=(0.0, 0.0, -0.04)),
        hand_r=H(dp=(0.0, 0.0, 0.05)), foot_l=F(lift=0.63), foot_r=F(lift=0.58))
    return c


# ================================================================================================ hit reactions
@clip("hit_light_front")
def hit_light_front():
    # a light hit to the front-left: the chest and head snap back AND twist away from the blow (the left shoulder is
    # driven back, the head turns with it), the struck-side arm flies up more than the other, the weight rocks back
    # onto the heels, then a rebound past centre and a damped settle (the settle spring adds the last ring)
    c = Clip("hit_light_front", 22, "idle", contact=3, priority="P0", technique="light hit from the front",
             hands=("relaxed", "relaxed"), offsets={"neck": -1.0, "hand_l": -1.0, "hand_r": -2.0}, antic=1, follow=8)
    c.k(3, ease="out3", pel=dict(dy=-0.025, dz=-0.012, dpitch=-3.0, dyaw=-5.0, dside=1.5),
        spine=dict(pitch=-8.0, yaw=-7.0, side=2.5), neck=dict(pitch=-12.0, yaw=-8.0, side=4.0),
        clav_l=dict(lift=7.0, prot=-4.0), clav_r=dict(lift=3.0),
        hand_l=H(dp=(0.05, 0.07, 0.12)), hand_r=H(dp=(-0.015, 0.04, 0.05)), fing="spread")
    c.k(8, ease="io", pel=dict(dy=0.015, dz=0.004, dpitch=4.0, dyaw=7.0, dside=-2.0),
        spine=dict(pitch=2.5, yaw=2.5, side=-1.0), neck=dict(pitch=1.0, yaw=3.0, side=-1.0),
        clav_l=dict(lift=-1.0, prot=0.0), clav_r=dict(lift=-0.5),
        hand_l=H(dp=(-0.055, -0.08, -0.13)), hand_r=H(dp=(0.02, -0.05, -0.06)), fing="relaxed")
    c.k(14, ease="io", pel=dict(dy=-0.006, dpitch=-1.5, dyaw=-2.0, dside=0.5), spine=dict(pitch=-0.5, yaw=-0.5, side=0.0),
        neck=dict(pitch=-2.0, yaw=-1.0, side=0.0), clav_l=dict(lift=0.0), clav_r=dict(lift=0.0),
        hand_l=H(dp=(0.004, -0.008, -0.006)), hand_r=H(dp=(-0.006, -0.012, -0.004)))
    c.k(22, ease="io", base=True)
    return c


@clip("hit_heavy")
def hit_heavy():
    # thrown back: chest caves, the left foot leaves the floor, a stumble step back, then the foot steps home
    c = Clip("hit_heavy", 36, "idle", contact=6, priority="P0", technique="heavy hit: thrown back, stumble, recover",
             hands=("relaxed", "relaxed"), offsets={"neck": -1.5, "hand_l": -2.0, "hand_r": -2.0}, antic=2, follow=12,
             no_balance=True)
    c.k(3, ease="out3", pel=dict(dy=-0.06, dz=-0.01, dpitch=-6.0), spine=dict(pitch=-12.0), neck=dict(pitch=-16.0),
        clav=dict(lift=8.0), hand_l=H(dp=(0.06, 0.12, 0.16)), hand_r=H(dp=(-0.06, 0.12, 0.16)), fing="spread")
    c.k(6, ease="out", pel=dict(y=-0.13, z=-0.02, pitch=-8.0, side=3.0), spine=dict(pitch=-16.0, yaw=6.0),
        neck=dict(pitch=-12.0), foot_l=F(pv=1.0, lift=0.07, pitch=-10.0, move=(0.0, -0.04), kup=0.3),
        hand_l=H(dp=(0.03, 0.04, 0.05)), hand_r=H(dp=(-0.02, 0.05, 0.04)))
    c.k(11, ease="in", foot_l=F(at=(0.17, -0.30), yaw=12.0, pv=1.0), pel=dict(y=-0.20, z=-0.07, pitch=-2.0, side=0.0),
        spine=dict(pitch=-6.0, yaw=2.0), neck=dict(pitch=-4.0))
    c.k(16, ease="out", pel=dict(y=-0.17, z=-0.10, pitch=6.0), spine=dict(pitch=4.0, yaw=0.0), neck=dict(pitch=-2.0),
        clav=dict(lift=0.0), hand_l=HW(p=(0.24, 0.10, 1.00), f=(0.1, 0.6, -0.8), m=(-0.8, 0.0, -0.4), e=(0.6, -0.4, -1.0)),
        hand_r=HW(p=(-0.24, 0.08, 1.00), f=(-0.1, 0.6, -0.8), m=(0.8, 0.0, -0.4), e=(-0.6, -0.4, -1.0)), fing="relaxed")
    c.k(21, ease="io", pel=dict(y=-0.09, z=-0.04, pitch=2.0))
    c.k(24, ease="io", foot_l=F(pv=1.0, lift=0.05, pitch=-12.0), pel=dict(y=-0.05, z=-0.03, side=-2.0))
    c.k(28, ease="in", foot_l=F(**IDLE_L), pel=dict(y=-0.01, z=-0.035, side=0.0))
    c.k(36, ease="io", base=True)
    return c


@clip("stagger")
def stagger():
    # off balance: the body tips over to the right, the right foot hops out to catch it, wobble, step home
    c = Clip("stagger", 30, "idle", contact=6, priority="P0", technique="off-balance wobble, one foot replants",
             hands=("spread", "spread"), offsets={"neck": -2.0, "hand_l": -2.0, "hand_r": -2.0}, antic=3, follow=12,
             no_balance=True)
    c.k(3, ease="out", pel=dict(dx=-0.025, dside=-5.0, dy=-0.02, dz=-0.012), spine=dict(side=-6.0, pitch=-4.0),
        neck=dict(side=4.0), foot_l=F(pv=1.0, pitch=-6.0),
        hand_l=H(dp=(0.10, 0.06, 0.20)), hand_r=H(dp=(-0.12, 0.04, 0.12)), fing="spread")
    c.k(6, ease="out", pel=dict(dx=-0.035, dside=-4.0, dz=-0.012), spine=dict(side=-9.0, pitch=-3.0),
        foot_l=F(pv=1.0, pitch=-12.0),
        foot_r=F(pv=1.0, lift=0.06, pitch=-10.0, kup=0.2), hand_l=H(dp=(0.03, 0.0, 0.05)), hand_r=H(dp=(-0.04, 0.0, 0.0)))
    c.k(10, ease="in", foot_r=F(at=(-0.32, 0.03), yaw=-20.0, pv=1.0), foot_l=F(pv=1.0, pitch=0.0),
        pel=dict(x=-0.11, z=-0.08, side=4.0, pitch=4.0),
        spine=dict(side=5.0, pitch=2.0), neck=dict(side=-3.0))
    c.k(15, ease="io", pel=dict(x=-0.06, z=-0.06, side=-2.0), spine=dict(side=-2.0), neck=dict(side=1.0),
        hand_l=H(dp=(-0.08, -0.03, -0.18)), hand_r=H(dp=(0.10, -0.02, -0.10)), fing="relaxed")
    c.k(19, ease="io", foot_r=F(pv=1.0, lift=0.05, pitch=-10.0), pel=dict(x=-0.01, z=-0.03, side=0.0),
        spine=dict(side=0.0))
    c.k(23, ease="in", foot_r=F(**IDLE_R), pel=dict(x=0.0, z=-0.035))
    c.k(30, ease="io", base=True)
    return c


@clip("block_impact")
def block_impact():
    # additive-friendly shudder on the guard: arms driven back 3 cm, chest gives, a small rebound and settle
    c = Clip("block_impact", 16, "guard", contact=2, priority="P0", technique="block shudder (additive on guards)",
             hands=("fist", "fist"), offsets={"neck": -1.0, "hand_l": -0.5, "hand_r": -1.0}, antic=1, follow=6)
    c.k(2, ease="out3", pel=dict(dy=-0.018, dz=-0.008), spine=dict(dpitch=-3.0), neck=dict(dpitch=-3.0),
        clav=dict(dlift=3.0), hand_l=H(dp=(0.0, -0.035, 0.012)), hand_r=H(dp=(0.0, -0.03, 0.010)))
    c.k(5, ease="io", pel=dict(dy=0.026, dz=0.006), spine=dict(dpitch=4.0), neck=dict(dpitch=3.5),
        clav=dict(dlift=-4.0), hand_l=H(dp=(0.0, 0.045, -0.016)), hand_r=H(dp=(0.0, 0.04, -0.013)))
    c.k(9, ease="io", pel=dict(dy=-0.010, dz=0.001), spine=dict(dpitch=-1.5), neck=dict(dpitch=-1.0),
        clav=dict(dlift=1.5), hand_l=H(dp=(0.0, -0.014, 0.005)), hand_r=H(dp=(0.0, -0.012, 0.004)))
    c.k(16, ease="io", base=True)
    return c


@clip("deflect")
def deflect():
    # perfect parry: the lead hand opens outward while the weight eases back (the hips open a little), then the waist
    # turns hard right and the lead forearm sweeps across the centre line outside -> inside on a circle while the
    # stance sits back into the rear leg (absorbing, not blocking); the rear fist checks low.  The lead hand then
    # rolls on round the circle (down and out) and rises back to the guard as the weight returns forward.
    c = Clip("deflect", 22, "guard", contact=5, priority="P0", technique="cross-body sweep parry with a hip turn",
             hands=("palm", "fist"), strike="hand_l", metric="fwd_hand_l", offsets={"pel": 1.0, "spine": 0.5, "hand_r": -1.0},
             antic=2, follow=11)
    c.k(2, ease="io", pel=dict(dyaw=8.0, dz=-0.012, dy=-0.012), spine=dict(dyaw=6.0),
        hand_l=HW(p=(0.32, 0.32, 1.40), f=(0.1, 0.4, 1.0), m=(0.5, 1.0, 0.0), e=(0.5, -0.3, -1.0)),
        hand_r=HW(p=(-0.05, 0.20, 1.29), f=(0.25, 0.45, 0.85), m=(0.85, 0.0, 0.25), e=(-0.3, -0.3, -1.0)), fing_l="palm")
    c.k(5, ease="in3", pel=dict(dyaw=-24.0, dz=-0.016, dy=-0.022, dside=2.0), spine=dict(dyaw=-13.0, dpitch=2.0),
        neck=dict(dyaw=4.0),
        hand_l=HW(p=(-0.08, 0.43, 1.33), f=(-0.35, 0.3, 1.0), m=(-0.25, 1.0, 0.1), e=(0.6, -0.1, -1.0)),
        hand_r=HW(p=(-0.12, 0.25, 1.11), f=(0.4, 0.5, 0.6), m=(0.8, 0.0, 0.3), e=(-0.4, -0.3, -1.0)))
    c.hold(7)
    c.k(11, ease="out", pel=dict(dyaw=10.0, dz=0.012, dy=0.016, dside=-1.5), spine=dict(dyaw=7.0, dpitch=-1.0),
        neck=dict(dyaw=-3.0),
        hand_l=HW(p=(0.04, 0.38, 1.24), f=(-0.3, 0.7, 0.6), m=(-0.6, 0.5, -0.4), e=(0.5, -0.2, -1.0)))
    c.k(16, ease="io", pel=dict(dyaw=4.0, dz=0.004, dy=0.008, dside=-0.5), spine=dict(dyaw=3.0),
        hand_l=HW(p=(0.13, 0.38, 1.34), f=(-0.2, 0.55, 0.8), m=(-0.85, 0.1, 0.25), e=(0.35, -0.2, -1.0)),
        hand_r=HW(p=(-0.035, 0.21, 1.31), f=(0.25, 0.45, 0.85), m=(0.85, 0.0, 0.25), e=(-0.3, -0.3, -1.0)))
    c.k(22, ease="io", base=True)
    return c


# ================================================================================================ down and up
@clip("knockdown")
def knockdown():
    # struck hard: the torso snaps back, knees buckle, sit-fall backwards, back hits the floor (slam ~f34), lying
    c = Clip("knockdown", 42, "idle", contact=4, priority="P0", technique="knocked down onto the back",
             hands=("spread", "spread"), end_pose="lying", base_end=None, no_balance=True,
             offsets={"neck": -2.0, "hand_l": -2.0, "hand_r": -2.0}, antic=2, follow=20)
    c.k(4, ease="out3", pel=dict(dy=-0.07, dz=-0.02, dpitch=-8.0), spine=dict(pitch=-16.0), neck=dict(pitch=-18.0),
        clav=dict(lift=8.0), hand_l=H(dp=(0.08, 0.14, 0.18)), hand_r=H(dp=(-0.08, 0.14, 0.18)), fing="spread")
    c.k(11, ease="io", pel=dict(y=-0.16, z=-0.20, pitch=-6.0), spine=dict(pitch=-8.0), neck=dict(pitch=-4.0),
        foot_l=F(pv=1.0, lift=0.05, pitch=-8.0, move=(0.03, 0.06)), clav=dict(lift=4.0))
    c.k(15, ease="io", foot_l=F(at=(0.19, 0.06), yaw=14.0, pv=-1.0, pitch=8.0))
    c.k(20, ease="in", pel=dict(y=-0.36, z=-0.52, pitch=-28.0), spine=dict(pitch=-4.0), neck=dict(pitch=10.0),
        foot_l=F(pitch=24.0), foot_r=F(**dict(IDLE_R, pv=-1.0, pitch=10.0)),
        hand_l=HW(p=(0.32, -0.45, 0.30), f=(0.2, -0.5, -0.8), m=(0.0, 0.3, -1.0), e=(0.4, 0.4, -1.0)),
        hand_r=HW(p=(-0.32, -0.45, 0.30), f=(-0.2, -0.5, -0.8), m=(0.0, 0.3, -1.0), e=(-0.4, 0.4, -1.0)))
    c.k(26, ease="in", pel=dict(y=-0.50, z=-0.74, pitch=-58.0), spine=dict(pitch=6.0), neck=dict(pitch=22.0),
        foot_l=F(at=(0.20, 0.22), yaw=16.0, pv=-1.0, pitch=40.0), foot_r=F(at=(-0.16, 0.18), yaw=-12.0, pv=-1.0, pitch=36.0),
        hand_l=HW(p=(0.44, -0.70, 0.10), f=(0.5, -0.5, -0.2), m=(0.0, 0.0, -1.0), e=(0.5, 0.0, 1.0)),
        hand_r=HW(p=(-0.42, -0.62, 0.10), f=(-0.5, -0.3, -0.2), m=(0.0, 0.0, -1.0), e=(-0.5, 0.0, 1.0)))
    lie = BASES["lying"]
    c.k(34, ease="in", pel=lie["pel"], spine=(10.0, 0.0, 0.0), neck=(26.0, 0.0, 6.0),
        foot_l=F(at=(0.20, 0.30), yaw=18.0, pv=-1.0, pitch=62.0), foot_r=F(at=(-0.16, 0.26), yaw=-14.0, pv=-1.0, pitch=58.0),
        hand_l=HW(p=(0.50, -0.88, 0.07), f=(0.6, -0.4, 0.0), m=(0.0, 0.0, 1.0), e=(0.0, 0.0, 1.0)),
        hand_r=HW(p=(-0.48, -0.70, 0.07), f=(-0.6, 0.2, 0.0), m=(0.0, 0.0, 1.0), e=(0.0, 0.0, 1.0)), fing="relaxed")
    c.k(37, ease="out", pel=dict(z=lie["pel"][2] + 0.02, pitch=-82.0), spine=(1.0, 0.0, 0.0), neck=(10.0, 0.0, 6.0))
    c.k(42, ease="io", pel=lie["pel"], spine=lie["spine"], neck=lie["neck"])
    return c


@clip("getup")
def getup():
    # lying -> sit up with the knees drawn in, hands push the floor -> rock forward over the feet into a deep squat
    # -> stand up through the legs to the ready stance
    lie = BASES["lying"]
    c = Clip("getup", 48, "idle", start=lie, priority="P0", technique="sit up, rock forward onto the feet, stand",
             hands=("relaxed", "relaxed"), start_pose_free=True, no_balance=True,
             offsets={"neck": -2.0, "hand_l": -1.5, "hand_r": -1.5}, antic=10, follow=30)
    c.k(9, ease="io", pel=dict(y=-0.50, z=-0.80, pitch=-45.0), spine=dict(pitch=22.0), neck=dict(pitch=14.0),
        foot_l=F(at=(0.17, 0.10), yaw=12.0, pv=-1.0, pitch=30.0), foot_r=F(at=(-0.17, 0.08), yaw=-12.0, pv=-1.0, pitch=28.0),
        hand_l=HW(p=(0.36, -0.75, 0.06), f=(0.2, -0.6, 0.0), m=(0.0, 0.0, -1.0), e=(0.5, 0.5, 0.5)),
        hand_r=HW(p=(-0.36, -0.75, 0.06), f=(-0.2, -0.6, 0.0), m=(0.0, 0.0, -1.0), e=(-0.5, 0.5, 0.5)))
    c.k(17, ease="io", pel=dict(y=-0.32, z=-0.70, pitch=-6.0), spine=dict(pitch=30.0), neck=dict(pitch=-6.0),
        foot_l=F(**IDLE_L), foot_r=F(**IDLE_R),
        hand_l=HW(p=(0.30, 0.25, 0.55), f=(0.1, 0.9, 0.2), m=(-0.3, 0.0, -1.0), e=(0.6, -0.4, -1.0)),
        hand_r=HW(p=(-0.30, 0.25, 0.55), f=(-0.1, 0.9, 0.2), m=(0.3, 0.0, -1.0), e=(-0.6, -0.4, -1.0)))
    c.k(26, ease="io", pel=dict(y=-0.10, z=-0.52, pitch=26.0), spine=dict(pitch=18.0), neck=dict(pitch=-18.0),
        hand_l=HW(p=(0.28, 0.42, 0.75), f=(0.0, 1.0, 0.1), m=(-0.4, 0.0, -1.0), e=(0.6, -0.4, -1.0)),
        hand_r=HW(p=(-0.28, 0.42, 0.75), f=(0.0, 1.0, 0.1), m=(0.4, 0.0, -1.0), e=(-0.6, -0.4, -1.0)))
    c.k(36, ease="io", pel=dict(y=-0.02, z=-0.18, pitch=12.0), spine=dict(pitch=6.0), neck=dict(pitch=-6.0),
        hand_l=HW(p=(0.24, 0.16, 0.95), f=(0.1, 0.5, -0.8), m=(-0.8, 0.0, -0.4), e=(0.6, -0.4, -1.0)),
        hand_r=HW(p=(-0.24, 0.16, 0.95), f=(-0.1, 0.5, -0.8), m=(0.8, 0.0, -0.4), e=(-0.6, -0.4, -1.0)))
    c.k(48, ease="io", base=True)
    return c


# ================================================================================================ P1
@clip("hit_light_back")
def hit_light_back():
    # a light hit from behind: the hips are shoved forward, the back arches and the head whips back, arms jerk back
    c = Clip("hit_light_back", 22, "idle", contact=3, priority="P1", technique="light hit from behind",
             hands=("relaxed", "relaxed"), offsets={"neck": -1.5, "hand_l": -1.0, "hand_r": -2.0}, antic=1, follow=8)
    c.k(3, ease="out3", pel=dict(dy=0.035, dz=-0.012, dpitch=4.0), spine=dict(pitch=-9.0), neck=dict(pitch=-10.0),
        clav=dict(lift=4.0, prot=-6.0), hand_l=H(dp=(0.04, -0.07, 0.06)), hand_r=H(dp=(-0.04, -0.06, 0.05)), fing="spread")
    c.k(8, ease="io", pel=dict(dy=-0.012, dz=0.004, dpitch=-2.0), spine=dict(pitch=3.0), neck=dict(pitch=2.0),
        clav=dict(lift=-1.0, prot=1.0), hand_l=H(dp=(-0.045, 0.08, -0.07)), hand_r=H(dp=(0.045, 0.07, -0.06)),
        fing="relaxed")
    c.k(14, ease="io", pel=dict(dy=0.005, dpitch=1.0), spine=dict(pitch=-0.8), neck=dict(pitch=-1.0),
        clav=dict(lift=0.0, prot=0.0), hand_l=H(dp=(0.006, -0.01, 0.006)), hand_r=H(dp=(-0.006, -0.01, 0.004)))
    c.k(22, ease="io", base=True)
    return c


GUARD_FOOT_L = dict(at=(0.12, 0.21), yaw=-4.0, pv=1.0, kyaw=4.0)
GUARD_FOOT_R = dict(at=(-0.15, -0.20), yaw=-38.0, pv=1.0, kyaw=6.0)


@clip("guard_break")
def guard_break():
    # the guard is smashed open: both arms fly wide and high, the chest is exposed, the lead foot is forced back a
    # step, the body sags; then the stance is rebuilt and the guard comes back up
    c = Clip("guard_break", 36, "guard", contact=6, priority="P1", technique="guard broken open, backstep",
             hands=("spread", "spread"), offsets={"neck": -1.5, "hand_l": -1.0, "hand_r": -1.5}, antic=3, follow=14,
             no_balance=True)
    c.k(3, ease="out3", pel=dict(dy=-0.04, dz=-0.01, dpitch=-4.0), spine=dict(dpitch=-8.0), neck=dict(dpitch=-10.0),
        clav=dict(lift=10.0), fing="spread",
        hand_l=HW(p=(0.40, 0.18, 1.62), f=(0.5, 0.2, 0.8), m=(0.3, 1.0, 0.0), e=(0.6, 0.0, -0.6)),
        hand_r=HW(p=(-0.42, 0.04, 1.58), f=(-0.5, 0.2, 0.8), m=(-0.3, 1.0, 0.0), e=(-0.6, 0.0, -0.6)))
    c.k(6, ease="out", pel=dict(dy=-0.08, dz=-0.02, dpitch=-7.0), spine=dict(dpitch=-12.0), neck=dict(dpitch=-12.0),
        foot_l=F(pv=1.0, lift=0.05, pitch=-10.0, kup=0.2),
        hand_l=HW(p=(0.58, 0.02, 1.56), f=(0.8, -0.2, 0.5), m=(0.0, 1.0, 0.0), e=(0.4, 0.4, -0.6)),
        hand_r=HW(p=(-0.60, -0.10, 1.50), f=(-0.8, -0.2, 0.5), m=(0.0, 1.0, 0.0), e=(-0.4, 0.4, -0.6)))
    c.k(11, ease="in", foot_l=F(at=(0.13, 0.02), yaw=-8.0, pv=1.0), pel=dict(dy=-0.06, dz=-0.05, dpitch=8.0),
        spine=dict(dpitch=10.0), neck=dict(dpitch=8.0), clav=dict(lift=0.0),
        hand_l=HW(p=(0.30, 0.10, 1.02), f=(0.2, 0.5, -0.8), m=(-0.8, 0.0, -0.4), e=(0.6, -0.4, -1.0)),
        hand_r=HW(p=(-0.30, 0.02, 1.00), f=(-0.2, 0.5, -0.8), m=(0.8, 0.0, -0.4), e=(-0.6, -0.4, -1.0)), fing="relaxed")
    c.k(19, ease="io", pel=dict(dy=0.03, dz=0.02), spine=dict(dpitch=-3.0))
    c.k(23, ease="io", foot_l=F(pv=1.0, lift=0.04, pitch=-10.0))
    c.k(27, ease="in", foot_l=F(**GUARD_FOOT_L), fing="fist")
    c.k(36, ease="io", base=True)
    return c


@clip("salute")
def salute():
    # fist-and-palm salute (bao quan li): the hands come up to the chest, the left palm covering the right fist,
    # a slight bow with the eyes kept up, then the hands lower back to the ready stance
    c = Clip("salute", 60, "idle", priority="P1", technique="fist-and-palm salute (bao quan li)",
             hands=("palm", "fist"), offsets={"neck": -2.0, "hand_l": -1.0, "hand_r": 0.0})
    c.k(12, ease="io", fing_r="fist", fing_l="palm",
        hand_r=HW(p=(-0.01, 0.30, 1.24), f=(0.6, 0.6, 0.4), m=(0.9, -0.3, 0.0), e=(-0.6, -0.3, -1.0)),
        hand_l=HW(p=(0.04, 0.34, 1.27), f=(-0.2, 0.3, 1.0), m=(-0.9, -0.3, 0.0), e=(0.6, -0.3, -1.0)))
    c.k(22, ease="io", pel=dict(dpitch=4.0, dy=-0.01), spine=dict(pitch=12.0), neck=dict(pitch=-4.0),
        gaze=(0.6, 0.0, 6.0))
    c.k(38, ease="io")
    c.k(46, ease="io", pel=dict(dpitch=-4.0, dy=0.01), spine=dict(pitch=0.5), neck=dict(pitch=-1.0),
        gaze=BASES["idle"]["gaze"])
    c.k(60, ease="io", base=True)
    return c


def _air_loop(name, base_spec, keys, technique, hands, priority="P1"):
    st = apply_spec(BASES["air"], base_spec, BASES["air"])
    BASES[name] = st
    c = Clip(name, 72, name, loop=True, priority=priority, technique=technique, hands=hands, no_balance=True,
             offsets={"neck": 0.0})
    for f, spec in keys:
        c.k(f, ease="sp", **spec)
    return c


@clip("flight")
def flight():
    # upright flight (Sound flight): legs hanging together, toes pointed, the palms press down at the sides on each
    # beat (the body rises a little against them)
    return _air_loop("flight", dict(
        pel=(0.0, 0.0, 0.02, 4.0, 0.0, 0.0), spine=(-2.0, 0.0, 0.0), neck=(-2.0, 0.0, 0.0),
        foot_l=F(at=(0.07, -0.02), yaw=4.0, pv=1.0, lift=0.24, pitch=-42.0, kup=0.1),
        foot_r=F(at=(-0.07, -0.06), yaw=-4.0, pv=1.0, lift=0.20, pitch=-44.0, kup=0.1),
        hand_l=HW(p=(0.42, 0.12, 1.02), f=(0.4, 0.8, 0.0), m=(0.0, 0.0, -1.0), e=(0.4, -1.0, -0.2)),
        hand_r=HW(p=(-0.42, 0.12, 1.02), f=(-0.4, 0.8, 0.0), m=(0.0, 0.0, -1.0), e=(-0.4, -1.0, -0.2)),
        fing="palm"),
        [(18, dict(pel=dict(dz=-0.02), hand_l=H(dp=(0.0, 0.0, 0.07)), hand_r=H(dp=(0.0, 0.0, 0.07)), clav=dict(dlift=3.0))),
         (36, dict(pel=dict(dz=0.0), hand_l=H(dp=(0.0, 0.0, -0.07)), hand_r=H(dp=(0.0, 0.0, -0.07)), clav=dict(dlift=-3.0))),
         (54, dict(pel=dict(dz=-0.02), hand_l=H(dp=(0.0, 0.0, 0.07)), hand_r=H(dp=(0.0, 0.0, 0.07)), clav=dict(dlift=3.0)))],
        "upright flight, palms pressing down", ("palm", "palm"))


@clip("hover")
def hover():
    # lotus-like hover: knees open, the soles drawn together under the body, palms floating slowly at waist height
    return _air_loop("hover", dict(
        pel=(0.0, 0.0, 0.06, 2.0, 0.0, 0.0), spine=(0.0, 0.0, 0.0), neck=(-2.0, 0.0, 0.0),
        foot_l=F(at=(-0.09, 0.17), yaw=-70.0, pv=0.0, lift=0.47, pitch=-10.0, kyaw=125.0, kup=0.5),
        foot_r=F(at=(0.09, 0.13), yaw=70.0, pv=0.0, lift=0.44, pitch=-10.0, kyaw=-125.0, kup=0.5),
        hand_l=HW(p=(0.30, 0.22, 1.10), f=(0.3, 0.8, 0.0), m=(0.0, 0.1, -1.0), e=(0.5, -0.6, -0.6)),
        hand_r=HW(p=(-0.30, 0.22, 1.10), f=(-0.3, 0.8, 0.0), m=(0.0, 0.1, -1.0), e=(-0.5, -0.6, -0.6)),
        fing="relaxed"),
        [(18, dict(pel=dict(dz=0.02), hand_l=H(dp=(0.03, 0.02, 0.05)), hand_r=H(dp=(-0.02, -0.01, 0.02)))),
         (36, dict(pel=dict(dz=0.0), hand_l=H(dp=(0.0, 0.0, 0.0)), hand_r=H(dp=(0.0, 0.0, 0.0)))),
         (54, dict(pel=dict(dz=0.02), hand_l=H(dp=(-0.02, -0.01, 0.02)), hand_r=H(dp=(0.03, 0.02, 0.05))))],
        "lotus-like hover, floating arms", ("relaxed", "relaxed"))


@clip("skate")
def skate():
    # ice skating push-glide: low and pitched forward, each leg in turn pushes out diagonally behind while the other
    # glides under the body, the arms swing across; the feet glide (no planted ranges: the runtime must not lock them)
    base = apply_spec(BASES["idle"], dict(
        pel=(0.06, 0.0, -0.14, 22.0, -4.0, 8.0), spine=(6.0, 2.0, -6.0), neck=(-24.0, 0.0, 4.0),
        foot_l=F(at=(0.10, 0.06), yaw=10.0, pv=0.0), foot_r=F(at=(-0.36, -0.30), yaw=-30.0, pv=0.0, lift=0.04, pitch=-10.0),
        hand_l=HW(p=(-0.12, 0.36, 1.02), f=(-0.35, 0.4, -0.85), m=(-0.9, 0.2, 0.0), e=(0.6, -0.3, -1.0)),
        hand_r=HW(p=(-0.36, -0.30, 1.06), f=(-0.1, -0.5, -0.85), m=(0.9, 0.0, 0.2), e=(-0.6, 0.3, -1.0)),
        fing="relaxed"), BASES["idle"])
    BASES["skate"] = base
    c = Clip("skate", 48, "skate", loop=True, priority="P1", technique="ice skating push-glide", hands=("relaxed", "relaxed"),
             no_balance=True, offsets={"neck": -2.0, "hand_l": -2.0, "hand_r": -2.0}, plants={"l": [], "r": []},
             base_check=False)
    mid = dict(pel=(0.0, 0.0, -0.12, 22.0, 0.0, 0.0), spine=(6.0, 0.0, 0.0),
               foot_l=F(at=(0.12, -0.02), yaw=8.0, pv=0.0, lift=0.02), foot_r=F(at=(-0.12, -0.02), yaw=-8.0, pv=0.0, lift=0.02),
               hand_l=HW(p=(0.24, 0.06, 0.98), f=(0.05, 0.0, -1.0), m=(-0.9, 0.1, 0.0), e=(0.6, -0.3, -1.0)),
               hand_r=HW(p=(-0.24, 0.06, 0.98), f=(-0.05, 0.0, -1.0), m=(0.9, 0.1, 0.0), e=(-0.6, -0.3, -1.0)))
    c.k(12, ease="sp", **mid)
    c.k(24, ease="sp", pel=(-0.06, 0.0, -0.14, 22.0, 4.0, -8.0), spine=(6.0, -2.0, 6.0), neck=(-24.0, 0.0, -4.0),
        foot_r=F(at=(-0.10, 0.06), yaw=-10.0, pv=0.0, lift=0.0), foot_l=F(at=(0.36, -0.30), yaw=30.0, pv=0.0, lift=0.04, pitch=-10.0),
        hand_r=HW(p=(0.12, 0.36, 1.02), f=(0.35, 0.4, -0.85), m=(0.9, 0.2, 0.0), e=(-0.6, -0.3, -1.0)),
        hand_l=HW(p=(0.36, -0.30, 1.06), f=(0.1, -0.5, -0.85), m=(-0.9, 0.0, 0.2), e=(0.6, 0.3, -1.0)))
    c.k(36, ease="sp", **mid)
    return c


@clip("surf")
def surf():
    # riding a wave / dune: side-on surfer stance (left foot forward), knees bent, arms out for balance, the body
    # rising and sinking with the swell and leaning into the carve
    base = apply_spec(BASES["idle"], dict(
        pel=(0.0, 0.0, -0.16, 6.0, 0.0, -62.0), spine=(4.0, 0.0, 30.0), neck=(-4.0, 0.0, 20.0),
        foot_l=F(at=(0.05, 0.30), yaw=-70.0, pv=0.0, kyaw=10.0), foot_r=F(at=(-0.05, -0.30), yaw=-80.0, pv=0.0, kyaw=10.0),
        hand_l=HW(p=(0.30, 0.46, 1.20), f=(0.5, 0.7, -0.2), m=(0.0, 0.0, -1.0), e=(0.5, -0.3, -1.0)),
        hand_r=HW(p=(-0.40, -0.30, 1.20), f=(-0.6, -0.6, -0.2), m=(0.0, 0.0, -1.0), e=(-0.5, 0.3, -1.0)),
        fing="relaxed"), BASES["idle"])
    BASES["surf"] = base
    c = Clip("surf", 48, "surf", loop=True, priority="P1", technique="riding the wave, knees bent, arms balancing",
             hands=("relaxed", "relaxed"), no_balance=True, offsets={"neck": -2.0, "hand_l": -2.0, "hand_r": -2.0},
             base_check=False)
    c.k(12, ease="sp", pel=dict(dz=-0.04, dside=-4.0), spine=dict(dside=3.0), hand_l=H(dp=(0.0, 0.0, -0.05)),
        hand_r=H(dp=(0.0, 0.0, 0.06)))
    c.k(24, ease="sp", pel=dict(dz=0.01), hand_l=H(dp=(0.0, 0.0, 0.0)))
    c.k(36, ease="sp", pel=dict(dz=-0.03, dside=4.0), spine=dict(dside=-3.0), hand_l=H(dp=(0.0, 0.0, 0.06)),
        hand_r=H(dp=(0.0, 0.0, -0.05)))
    return c
