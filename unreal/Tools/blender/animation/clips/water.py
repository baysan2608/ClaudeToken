"""Water clips - Tai Chi, Yang style (MARTIAL_ARTS.md §2.2, §3.3): waist-led circles, empty / full weight shifts.

Base w_stance = left Ward-Off (peng): left foot forward (0.11, 0.25), right foot back (-0.13, -0.20) turned out 42 deg,
weight ~60 % rear, left forearm rounded across the chest, right palm pressing down by the hip.
Mechanics used everywhere: the waist leads (pelvis offset +2 frames, spine +1, hands lag 1), the weight goes BACK
first (sit on the rear leg, front foot empties: toes lift on the heel) and then forward with the release (bow stance,
rear heel turns out), arms stay rounded (no locked elbows), the contact has a soft 2-frame hold and a long exhale.
"""
import math

from ffa_dsl import BASES, Clip, F, H, HW, apply_spec, copy_state

from . import clip

W_FOOT_L = dict(at=(0.11, 0.25), yaw=0.0, pv=1.0)
W_FOOT_R = dict(at=(-0.13, -0.20), yaw=-42.0, pv=1.0, kyaw=8.0)
OFF = {"pel": 2.0, "spine": 1.0, "hand_l": -1.0, "hand_r": -1.5, "neck": -1.5}


def off(**kw):
    """Channel offsets for a clip: the waist leads; the striking hand(s) get 0 so the contact frame is the true
    extremity; the free hand settles late."""
    d = dict(OFF)
    d.update(kw)
    return d


def _hw(p, f, m, e=None):
    return HW(p=p, f=f, m=m, e=e)


# hold-the-ball shapes (world, A-frame) used by several clips
def ball_right(z=1.18, y=0.30):
    """Ball held on the right side: right palm on top (down), left palm below (up)."""
    return dict(hand_r=_hw((-0.12, y, z), (0.75, 0.6, -0.05), (0.0, 0.1, -1.0), (-0.6, -0.2, -1.0)),
                hand_l=_hw((-0.06, y + 0.02, z - 0.22), (-0.7, 0.65, 0.0), (0.0, 0.0, 1.0), (0.6, -0.4, -1.0)))


def _make_bases():
    st = BASES["w_stance"]
    # hold the ball in front of the chest (centre): right hand on top, left below - the Draw & Shape hold
    BASES["w_hold"] = apply_spec(st, dict(
        pel=(0.005, -0.03, -0.115, 1.0, 0.0, -10.0), spine=(1.0, 0.0, 2.0),
        hand_r=_hw((-0.04, 0.38, 1.25), (0.8, 0.55, -0.05), (0.0, 0.1, -1.0), (-0.6, -0.2, -1.0)),
        hand_l=_hw((0.03, 0.39, 1.01), (-0.75, 0.6, 0.0), (0.0, 0.05, 1.0), (0.6, -0.3, -1.0)),
        fing="cup"), st)


_make_bases()


@clip("w_stance")
def w_stance():
    # ward-off ready: weight drifts back (hands sink and draw in) then forward (hands rise and ward off), one cycle
    c = Clip("w_stance", 144, "w_stance", loop=True, priority="P0", technique="ward-off (peng) ready, weight shifting",
             hands=("willow", "willow"))
    c.k(36, ease="sp", pel=dict(dy=-0.05, dz=-0.008, dyaw=-4), spine=dict(dyaw=-2),
        hand_l=H(dp=(-0.01, -0.05, -0.05)), hand_r=H(dp=(0.01, -0.02, -0.01)))
    c.k(72, ease="sp", pel=dict(dy=0.03, dz=0.004, dyaw=3), spine=dict(dyaw=1),
        hand_l=H(dp=(0.0, 0.02, 0.02)), hand_r=H(dp=(0.02, 0.04, 0.03)))
    c.k(108, ease="sp", pel=dict(dy=0.035, dz=0.004, dyaw=2), spine=dict(dyaw=1),
        hand_l=H(dp=(0.01, 0.04, 0.035)), hand_r=H(dp=(-0.02, 0.0, -0.01)))
    c.breathe(depth=0.9, cycles=1, phase=0.0)
    return c


@clip("w_lash")
def w_lash():
    # Part the Wild Horse's Mane: sit back and gather the ball on the right, the left foot steps out heel first,
    # the weight flows forward into a left bow while the waist turns left ~50 deg and the left arm splits diagonally
    # up and forward (palm up-in), the right palm presses down to the hip.  The water lash leaves the lead hand.
    c = Clip("w_lash", 30, "w_stance", contact=14, priority="P0", technique="Part the Wild Horse's Mane",
             hands=("willow", "willow"), strike="hand_l", offsets=off(hand_l=0.0, hand_r=-2.0), antic=6, follow=20)
    c.k(6, ease="io", pel=dict(dy=-0.075, dz=-0.02, yaw=-32.0, pitch=0.0), spine=dict(yaw=-8.0),
        foot_l=F(pv="heel", pitch=12.0), **ball_right(z=1.17, y=0.26))
    c.k(8, ease="out", foot_l=F(pv="heel", lift=0.025, pitch=16.0, move=(0.015, 0.10)), pel=dict(dy=0.0))
    c.k(10, ease="in", foot_l=F(pv="heel", lift=0.0, pitch=12.0), pel=dict(dy=0.03, yaw=-18.0))
    c.k(14, ease="in", pel=dict(x=0.025, y=0.10, z=-0.115, pitch=3.0, yaw=14.0), spine=dict(yaw=12.0, pitch=2.0),
        clav_l=dict(lift=2.0, prot=6.0), foot_l=F(pv=1.0, pitch=0.0), foot_r=F(yaw=-52.0),
        hand_l=_hw((0.30, 0.66, 1.44), (0.35, 0.8, 0.45), (-0.45, -0.15, 0.88), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.24, 0.10, 0.90), (0.1, 1.0, -0.1), (0.0, 0.05, -1.0), (-0.5, -0.6, -0.5)), path="arc")
    c.hold(16)
    c.k(20, ease="out", pel=dict(dy=-0.05, dz=0.005, yaw=4.0), spine=dict(yaw=4.0), clav_l=dict(lift=0.0, prot=2.0),
        foot_l=F(pv="heel", pitch=8.0),
        hand_l=_hw((0.18, 0.55, 1.34), (-0.2, 0.9, 0.3), (-0.6, -0.4, 0.6), (0.6, -0.2, -1.0)))
    c.k(23, ease="io", foot_l=F(pv="heel", lift=0.02, pitch=10.0, move=(-0.015, -0.10)))
    c.k(25, ease="in", foot_l=F(**W_FOOT_L), foot_r=F(yaw=-42.0))
    c.k(30, ease="io", base=True)
    return c


@clip("w_freeze")
def w_freeze():
    # Hands Play the Pipa -> clench: sit fully back (front heel down, toes up: empty stance), both hands rise and
    # reach forward open as if around a lute, then snap shut into fists with a small downward jolt; 3-frame freeze.
    c = Clip("w_freeze", 24, "w_stance", contact=14, priority="P0", technique="Hands Play the Pipa -> clench (Ice)",
             hands=("fist", "fist"), strike="hands", offsets=off(hand_l=0.0, hand_r=0.0), antic=10, follow=18)
    c.k(5, ease="io", pel=dict(dy=-0.085, dz=-0.025, yaw=-20.0), spine=dict(yaw=-2.0, pitch=-1.0),
        foot_l=F(pv="heel", pitch=16.0),
        hand_l=_hw((0.10, 0.44, 1.30), (0.0, 0.7, 0.7), (-0.9, 0.0, 0.2), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.06, 0.32, 1.12), (0.3, 0.8, 0.4), (0.9, 0.0, 0.3), (-0.6, -0.2, -1.0)), fing="willow")
    c.k(10, ease="io", pel=dict(dy=-0.07, yaw=-14.0), spine=dict(yaw=2.0, pitch=-2.0), clav=dict(lift=3.0),
        hand_l=_hw((0.10, 0.53, 1.38), (-0.1, 0.8, 0.6), (-1.0, 0.1, 0.0), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.02, 0.45, 1.19), (0.2, 0.8, 0.5), (1.0, 0.1, 0.0), (-0.6, -0.2, -1.0)), fing="spread")
    c.k(14, ease="in4", pel=dict(dz=-0.012, dy=0.005), spine=dict(pitch=3.0), clav=dict(lift=-3.0),
        hand_l=_hw((0.09, 0.51, 1.35), (-0.15, 0.85, 0.5), (-1.0, 0.0, -0.1), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.01, 0.43, 1.15), (0.15, 0.85, 0.45), (1.0, 0.0, -0.1), (-0.6, -0.2, -1.0)), fing="fist")
    c.hold(17)
    c.k(21, ease="out", pel=dict(dy=-0.02, dz=0.01, yaw=-14.0), spine=dict(yaw=-6.0, pitch=1.0), clav=dict(lift=0.0),
        foot_l=F(pv=1.0, pitch=0.0), fing="willow",
        hand_l=_hw((0.06, 0.45, 1.28), (-0.8, 0.4, 0.1), (0.0, -1.0, 0.1), (0.5, -0.1, -1.0)),
        hand_r=_hw((-0.17, 0.20, 1.00), (0.15, 1.0, -0.05), (0.0, 0.0, -1.0), (-0.4, -1.0, -0.5)))
    c.k(24, ease="io", base=True)
    return c


@clip("w_push")
def w_push():
    # Push (an): sit back, the palms draw in and sink to the belly (the circle goes down), then the whole body flows
    # forward into a bow stance and both palms push up-forward at chest height, fingers up, elbows sunk; exhale.
    c = Clip("w_push", 30, "w_stance", contact=12, priority="P0", technique="Push (an)", hands=("palm", "palm"),
             strike="hands", offsets=off(hand_l=0.0, hand_r=0.0), antic=7, follow=18)
    c.k(5, ease="io", pel=dict(dy=-0.075, dz=-0.02, yaw=-10.0), spine=dict(yaw=0.0, pitch=-1.0),
        foot_l=F(pv="heel", pitch=8.0),
        hand_l=_hw((0.12, 0.27, 1.16), (0.0, 0.6, 0.8), (0.0, 0.45, -0.9), (0.6, -0.1, -1.0)),
        hand_r=_hw((-0.12, 0.25, 1.14), (0.0, 0.6, 0.8), (0.0, 0.45, -0.9), (-0.6, -0.1, -1.0)), fing="willow")
    c.k(8, ease="io", pel=dict(dy=-0.06, dz=-0.025, yaw=-6.0),
        hand_l=_hw((0.12, 0.30, 1.04), (0.0, 0.35, 0.95), (0.0, 1.0, -0.3), (0.6, -0.1, -1.0)),
        hand_r=_hw((-0.12, 0.28, 1.03), (0.0, 0.35, 0.95), (0.0, 1.0, -0.3), (-0.6, -0.1, -1.0)), fing="palm")
    c.k(12, ease="in", pel=dict(x=0.02, y=0.11, z=-0.11, pitch=5.0, yaw=-4.0), spine=dict(pitch=3.0, yaw=2.0),
        clav=dict(lift=-3.0, prot=7.0), foot_l=F(pv=1.0, pitch=0.0), foot_r=F(yaw=-50.0),
        hand_l=_hw((0.13, 0.69, 1.30), (0.0, 0.25, 1.0), (0.0, 1.0, -0.25), (0.6, -0.1, -1.0)),
        hand_r=_hw((-0.11, 0.67, 1.29), (0.0, 0.25, 1.0), (0.0, 1.0, -0.25), (-0.6, -0.1, -1.0)))
    c.hold(14)
    c.k(20, ease="out", pel=dict(dy=-0.06, dz=0.01), spine=dict(pitch=1.0), clav=dict(lift=0.0, prot=2.0),
        foot_r=F(yaw=-42.0), fing="willow",
        hand_l=_hw((0.09, 0.52, 1.28), (-0.5, 0.6, 0.5), (-0.3, -0.6, 0.5), (0.6, -0.1, -1.0)),
        hand_r=_hw((-0.14, 0.40, 1.10), (0.1, 0.9, 0.3), (0.2, 0.3, -0.9), (-0.6, -0.4, -1.0)))
    c.k(30, ease="io", base=True)
    return c


# ------------------------------------------------------------------------------------------------ procedural loops
def _loop_from_fn(c, fn, n, base_st):
    for f in range(n):
        spec = fn(f / n)
        st = apply_spec(base_st, spec, base_st)
        if f == 0:
            c.keys[0] = (0, st, "lin", None)
        else:
            c.keys.append((f, st, "lin", None))
    return c


def _cloud(ph, lag=0.012):
    """Cloud Hands: each hand travels a vertical circle in front of the body, half a cycle apart; the waist follows
    the upper hand (turns +-24 deg) and the weight shifts toward the side the upper hand moves to."""
    out = {}
    th = 2 * math.pi * ph
    yaw = 24.0 * math.sin(th)
    out["pel"] = (0.035 * math.sin(th), -0.02, -0.12 + 0.008 * math.cos(2 * th), 1.0, -2.0 * math.sin(th), yaw - 8.0)
    out["spine"] = (1.0, 0.0, 0.25 * yaw)
    for side, ph0 in (("l", 0.0), ("r", math.pi)):
        a = th + ph0 - 2 * math.pi * lag          # the hands trail the waist slightly
        # circle centre a little to the hand's own side, radius 0.21 x 0.17
        x = 0.21 * math.sin(a) + (0.03 if side == "l" else -0.03)
        z = 1.18 + 0.17 * math.cos(a)
        y = 0.33 + 0.04 * math.cos(a)
        up = math.cos(a)                        # 1 at the top, -1 at the bottom
        # palm faces the body at the top (looking at the hand), turns down / out at the bottom
        top, bot = 0.5 + 0.5 * up, 0.5 - 0.5 * up
        m = (-0.25 * math.sin(a), -0.85 * top - 0.12, -0.85 * bot - 0.12)     # never vanishes (smooth roll)
        fdir = (0.6 if side == "r" else -0.6, 0.6, 0.35 * up)
        out["hand_" + side] = HW(p=(x, y, z), f=fdir, m=m, e=(0.6 if side == "l" else -0.6, -0.2, -1.0))
    return out


@clip("w_shield")
def w_shield():
    # Cloud Hands as the Water guard: continuous double circles, the waist turning side to side, weight following
    base = apply_spec(BASES["w_stance"], _cloud(0.0), BASES["w_stance"])
    base["fing_l"] = base["fing_r"] = BASES["w_stance"]["fing_l"]
    BASES["w_shield"] = base
    c = Clip("w_shield", 96, "w_shield", loop=True, priority="P0", technique="Cloud Hands (yun shou) guard",
             hands=("willow", "willow"), offsets={"neck": 0.0})
    _loop_from_fn(c, lambda ph: _cloud((ph * 2.0) % 1.0), 96, base)
    c.breathe(depth=0.6, cycles=2)
    return c


_HOLD0 = None


def _ball(ph):
    """Hold the Ball: the two hands roll a sphere between them (opposite phase on a small vertical circle), the waist
    sways +-6 deg, the knees pulse."""
    th = 2 * math.pi * ph
    b = _HOLD0
    out = {"pel": (b["pel"][0] + 0.015 * math.sin(th), b["pel"][1] + 0.012 * math.cos(th), b["pel"][2] - 0.006 * math.cos(th),
                   b["pel"][3], b["pel"][4], b["pel"][5] + 6.0 * math.sin(th)),
           "spine": (b["spine"][0], b["spine"][1], b["spine"][2] + 2.0 * math.sin(th))}
    r = 0.045
    out["hand_r"] = HW(p=(-0.04 + r * math.sin(th), 0.38 + 0.02 * math.cos(th), 1.25 + 0.02 * math.cos(th)),
                       f=(0.8, 0.55, -0.05), m=(0.0, 0.1 + 0.2 * math.sin(th), -1.0), e=(-0.6, -0.2, -1.0))
    out["hand_l"] = HW(p=(0.03 - r * math.sin(th), 0.39 - 0.02 * math.cos(th), 1.01 - 0.02 * math.cos(th)),
                       f=(-0.75, 0.6, 0.0), m=(0.0, 0.05 - 0.2 * math.sin(th), 1.0), e=(0.6, -0.3, -1.0))
    return out


_HOLD0 = copy_state(BASES["w_hold"])
BASES["w_hold"] = apply_spec(BASES["w_hold"], _ball(0.0), BASES["w_hold"])


@clip("w_hold")
def w_hold():
    c = Clip("w_hold", 96, "w_hold", loop=True, priority="P0", technique="Hold the Ball (bao qiu), rolling the sphere",
             hands=("cup", "cup"), offsets={"neck": 0.0})
    _loop_from_fn(c, lambda ph: _ball((ph * 2.0) % 1.0), 96, BASES["w_hold"])
    c.breathe(depth=0.7, cycles=2)
    return c


@clip("w_draw")
def w_draw():
    # Roll Back (lu) into a gather: sit back and turn the waist right, both hands sweep down and back past the right
    # hip (scooping), circle under and rise to the front, ending in Hold the Ball at contact (the water is drawn)
    hb = BASES["w_hold"]
    c = Clip("w_draw", 24, "w_stance", contact=14, priority="P0", technique="Roll Back (lu) -> gather",
             hands=("cup", "cup"), metric="high_hand_r", offsets=off(hand_l=-1.0, hand_r=0.0), antic=6, follow=19)
    c.k(6, ease="io", pel=dict(dy=-0.08, dz=-0.03, yaw=-38.0, pitch=4.0), spine=dict(yaw=-10.0, pitch=4.0),
        foot_l=F(pv="heel", pitch=8.0),
        hand_l=_hw((-0.10, 0.12, 0.92), (-0.5, -0.3, -0.8), (-0.3, -0.6, -0.5), (0.4, 0.2, -1.0)),
        hand_r=_hw((-0.33, -0.05, 0.86), (-0.3, -0.4, -0.8), (0.3, -0.5, -0.6), (-0.6, 0.3, -1.0)), fing="willow",
        path="arc")
    c.k(10, ease="io", pel=dict(dy=-0.06, dz=-0.03, yaw=-18.0, pitch=2.0), spine=dict(yaw=-2.0, pitch=2.0),
        hand_l=_hw((0.02, 0.30, 0.86), (-0.4, 0.8, -0.3), (0.0, 0.0, 1.0), (0.6, -0.3, -1.0)),
        hand_r=_hw((-0.16, 0.24, 0.88), (0.4, 0.8, -0.3), (0.0, 0.0, 1.0), (-0.6, -0.3, -1.0)), fing="cup",
        path="arc")
    c.k(14, ease="in", pel=hb["pel"], spine=hb["spine"], foot_l=F(pv=1.0, pitch=0.0),
        hand_l=H(p=hb["hand_l"][0:3], f=hb["hand_l"][3:6], m=hb["hand_l"][6:9], e=hb["hand_l"][9:12]),
        hand_r=H(p=hb["hand_r"][0:3], f=hb["hand_r"][3:6], m=hb["hand_r"][6:9], e=hb["hand_r"][9:12]), path="arc")
    c.hold(16)
    c.k(24, ease="io", base=True)
    return c


@clip("w_release")
def w_release():
    # Push release from Hold the Ball: the ball is drawn in to the chest and both palms send it out as a stream
    c = Clip("w_release", 24, "w_hold", base_end="w_stance", contact=8, priority="P0",
             technique="push release of the gathered water", hands=("palm", "palm"), strike="hands",
             offsets=off(hand_l=0.0, hand_r=0.0),
             antic=4, follow=14)
    c.k(4, ease="io", pel=dict(dy=-0.03, dz=-0.012, yaw=-6.0), spine=dict(pitch=-1.0),
        hand_l=_hw((0.10, 0.26, 1.12), (0.0, 0.5, 0.85), (0.0, 0.7, -0.6), (0.6, -0.1, -1.0)),
        hand_r=_hw((-0.10, 0.25, 1.14), (0.0, 0.5, 0.85), (0.0, 0.7, -0.6), (-0.6, -0.1, -1.0)), fing="palm")
    c.k(8, ease="in3", pel=dict(x=0.02, y=0.10, z=-0.11, pitch=5.0, yaw=-4.0), spine=dict(pitch=3.0, yaw=2.0),
        clav=dict(lift=-3.0, prot=8.0), foot_r=F(yaw=-50.0),
        hand_l=_hw((0.12, 0.70, 1.28), (0.0, 0.25, 1.0), (0.0, 1.0, -0.25), (0.6, -0.1, -1.0)),
        hand_r=_hw((-0.10, 0.68, 1.27), (0.0, 0.25, 1.0), (0.0, 1.0, -0.25), (-0.6, -0.1, -1.0)))
    c.hold(10)
    c.k(17, ease="out", pel=dict(dy=-0.07, dz=0.005), clav=dict(lift=0.0, prot=2.0), foot_r=F(yaw=-42.0),
        fing="willow",
        hand_l=_hw((0.08, 0.50, 1.27), (-0.6, 0.6, 0.4), (-0.3, -0.7, 0.4), (0.6, -0.1, -1.0)),
        hand_r=_hw((-0.15, 0.35, 1.06), (0.1, 0.9, 0.2), (0.2, 0.2, -0.9), (-0.6, -0.4, -1.0)))
    c.k(24, ease="io", base=True)
    return c


# ================================================================================================ P1
WARD_L = dict(f=(-1.0, 0.25, 0.1), m=(0.0, -1.0, 0.1))           # peng arm: forearm across, palm in
PRESS_R = dict(f=(0.3, 0.6, 0.75), m=(0.6, 0.8, 0.0))             # right palm on the inside of the left wrist


@clip("w_press")
def w_press():
    # Press (ji): sit back as the right palm joins the inside of the rounded left wrist, then the weight flows
    # forward and both arms press out together at chest height (the left forearm leading, right palm behind it)
    c = Clip("w_press", 24, "w_stance", contact=10, priority="P1", technique="Press (ji)", hands=("willow", "palm"),
             metric="fwd_hand_l", offsets=off(hand_l=0.0, hand_r=0.0), antic=5, follow=15)
    c.k(5, ease="io", pel=dict(dy=-0.06, dz=-0.015, yaw=-20.0), spine=dict(yaw=-2.0), foot_l=F(pv="heel", pitch=6.0),
        hand_l=_hw((0.04, 0.32, 1.22), WARD_L["f"], WARD_L["m"], (0.6, -0.1, -1.0)),
        hand_r=_hw((-0.04, 0.26, 1.18), PRESS_R["f"], PRESS_R["m"], (-0.6, -0.3, -1.0)), fing_r="palm")
    c.k(10, ease="in", pel=dict(x=0.02, y=0.11, z=-0.11, pitch=5.0, yaw=-8.0), spine=dict(pitch=3.0, yaw=0.0),
        clav=dict(prot=9.0, lift=-2.0), foot_l=F(pv=1.0, pitch=0.0), foot_r=F(yaw=-50.0),
        hand_l=_hw((0.02, 0.66, 1.26), (-1.0, 0.2, 0.1), (0.0, -1.0, 0.1), (0.6, -0.1, -1.0)),
        hand_r=_hw((-0.04, 0.58, 1.22), PRESS_R["f"], PRESS_R["m"], (-0.6, -0.3, -1.0)))
    c.hold(12)
    c.k(18, ease="out", pel=dict(dy=-0.06), clav=dict(prot=2.0, lift=0.0), foot_r=F(yaw=-42.0), fing_r="willow",
        hand_l=_hw((0.04, 0.46, 1.25), WARD_L["f"], WARD_L["m"], (0.5, -0.1, -1.0)),
        hand_r=_hw((-0.16, 0.24, 1.02), (0.15, 1.0, -0.05), (0.0, 0.0, -1.0), (-0.4, -1.0, -0.5)))
    c.k(24, ease="io", base=True)
    return c


@clip("w_ground")
def w_ground():
    # Needle at Sea Bottom -> rising push: sit back (front foot empty), the right hand lifts by the ear, the body
    # folds and the hand plunges like a needle to the floor in front of the lead foot (contact: the wave leaves the
    # ground there), then rises and pushes forward as the weight returns
    c = Clip("w_ground", 36, "w_stance", contact=18, priority="P1", technique="Needle at Sea Bottom -> rising push",
             hands=("willow", "palm"), metric="low_hand_r", offsets=off(hand_r=0.0), antic=8, follow=26)
    c.k(6, ease="io", pel=dict(dy=-0.09, dz=-0.02, yaw=-24.0), spine=dict(yaw=-6.0), foot_l=F(pv=1.0, pitch=-10.0),
        hand_r=_hw((-0.20, 0.06, 1.48), (0.1, 0.9, 0.3), (0.9, 0.0, 0.0), (-0.6, -0.3, -0.8)),
        hand_l=_hw((0.16, 0.28, 0.92), (0.1, 0.8, -0.4), (0.0, 0.0, -1.0), (0.6, -0.2, -1.0)))
    c.k(12, ease="io", pel=dict(dy=-0.08, z=-0.20, pitch=22.0, yaw=-12.0), spine=dict(pitch=14.0, yaw=-2.0),
        neck=dict(pitch=-14.0),
        hand_r=_hw((-0.08, 0.40, 1.04), (0.0, 0.6, -0.8), (0.9, 0.0, 0.0), (-0.6, 0.0, -0.6)))
    c.k(18, ease="in3", pel=dict(y=-0.08, z=-0.30, pitch=38.0, yaw=-8.0), spine=dict(pitch=22.0), neck=dict(pitch=-24.0),
        hand_r=_hw((0.02, 0.58, 0.20), (0.0, 0.4, -1.0), (0.9, 0.0, 0.0), (-0.6, 0.0, -0.6)),
        hand_l=_hw((0.20, 0.10, 0.84), (0.1, 0.8, -0.4), (0.0, 0.0, -1.0), (0.6, -0.2, -1.0)))
    c.hold(20)
    c.k(27, ease="out", pel=dict(y=0.08, z=-0.12, pitch=5.0, yaw=-6.0), spine=dict(pitch=3.0, yaw=0.0),
        neck=dict(pitch=-2.0), foot_l=F(pv=1.0, pitch=0.0), foot_r=F(yaw=-50.0), fing_r="palm",
        hand_r=_hw((-0.06, 0.66, 1.24), (0.0, 0.25, 1.0), (0.0, 1.0, -0.25), (-0.6, -0.1, -1.0)),
        hand_l=_hw((0.06, 0.40, 1.22), WARD_L["f"], WARD_L["m"], (0.6, -0.1, -1.0)))
    c.k(36, ease="io", base=True)
    return c


@clip("w_single_whip")
def w_single_whip():
    # Single Whip (dan bian): the hands gather on the right as the waist turns right and the right hand forms the hook
    # (crane beak); the waist turns back left, the hook extends behind at shoulder height and the left palm sweeps
    # out wide to the left-front, turning over to face forward at the end (the spray fans off it)
    c = Clip("w_single_whip", 30, "w_stance", contact=14, priority="P1", technique="Single Whip (dan bian)",
             hands=("willow", "crane"), strike="hand_l", offsets=off(hand_l=0.0, hand_r=-1.0), antic=6, follow=19)
    c.k(6, ease="io", pel=dict(dy=-0.05, dz=-0.015, yaw=-34.0), spine=dict(yaw=-10.0),
        hand_r=_hw((-0.20, 0.38, 1.26), (0.0, 0.4, -0.9), (0.6, 0.6, 0.0), (-0.6, -0.2, -1.0)), fing_r="crane",
        hand_l=_hw((-0.06, 0.30, 1.04), (-0.7, 0.6, 0.0), (0.0, 0.0, 1.0), (0.6, -0.4, -1.0)))
    c.k(14, ease="in", pel=dict(dy=0.05, z=-0.11, yaw=6.0), spine=dict(yaw=10.0, pitch=2.0), clav_l=dict(prot=6.0),
        hand_r=_hw((-0.56, -0.12, 1.32), (0.0, 0.1, -1.0), (0.3, 0.9, 0.0), (-0.3, 0.0, -1.0)),
        hand_l=_hw((0.42, 0.56, 1.32), (0.3, 0.4, 0.9), (0.2, 1.0, -0.1), (0.6, -0.2, -1.0)), path="arc")
    c.hold(16)
    c.k(22, ease="out", pel=dict(dy=-0.02, yaw=-10.0), spine=dict(yaw=-4.0), clav_l=dict(prot=2.0), fing_r="willow",
        hand_r=_hw((-0.24, 0.12, 1.00), (0.15, 1.0, -0.05), (0.0, 0.0, -1.0), (-0.4, -1.0, -0.5)),
        hand_l=_hw((0.10, 0.44, 1.26), WARD_L["f"], WARD_L["m"], (0.5, -0.1, -1.0)))
    c.k(30, ease="io", base=True)
    return c


@clip("w_snake")
def w_snake():
    # Snake Creeps Down (xia shi): the weight sinks all the way back onto the bent rear leg (drop stance), the front
    # leg extends, the lead hand traces down the inside of the front leg to the floor while the rear hand holds the
    # hook behind (the slick spreads from the lead hand)
    c = Clip("w_snake", 26, "w_stance", contact=8, priority="P1", technique="Snake Creeps Down (xia shi)",
             hands=("willow", "crane"), metric="low_hand_l", offsets=off(hand_l=0.0, pel=1.0, spine=0.5), antic=3,
             follow=13, no_balance=True)
    c.k(3, ease="io", pel=dict(dy=-0.06, dz=-0.04, yaw=-24.0), spine=dict(yaw=-4.0),
        hand_r=_hw((-0.40, -0.08, 1.20), (0.0, 0.1, -1.0), (0.3, 0.9, 0.0), (-0.3, 0.0, -1.0)), fing_r="crane")
    c.k(8, ease="in", pel=dict(x=-0.06, y=-0.30, z=-0.44, pitch=18.0, yaw=-22.0, side=-4.0),
        spine=dict(pitch=16.0, yaw=8.0), neck=dict(pitch=-14.0), foot_r=F(yaw=-62.0),
        hand_l=_hw((0.16, 0.24, 0.20), (0.05, 1.0, -0.3), (-0.9, 0.0, 0.2), (0.6, 0.2, -1.0)),
        hand_r=_hw((-0.46, -0.30, 0.82), (0.0, 0.1, -1.0), (0.3, 0.9, 0.0), (-0.3, 0.0, -1.0)))
    c.hold(10)
    c.k(18, ease="out", pel=dict(x=0.0, y=-0.03, z=-0.13, pitch=2.0, yaw=-16.0, side=0.0), spine=dict(pitch=1.0, yaw=-4.0),
        neck=dict(pitch=-2.0), foot_r=F(yaw=-42.0), fing_r="willow",
        hand_l=_hw((0.06, 0.40, 1.18), WARD_L["f"], WARD_L["m"], (0.5, -0.1, -1.0)),
        hand_r=_hw((-0.20, 0.14, 0.98), (0.15, 1.0, -0.05), (0.0, 0.0, -1.0), (-0.4, -1.0, -0.5)))
    c.k(26, ease="io", base=True)
    return c


@clip("w_clench")
def w_clench():
    # T+A freeze while holding the ball: the hands open wide around the sphere, then clench sharply into fists as the
    # body sinks; a 3-frame freeze, then back to the hold
    hb = BASES["w_hold"]
    c = Clip("w_clench", 18, "w_hold", contact=10, priority="P1", technique="sharp double clench (freeze the shape)",
             hands=("fist", "fist"), offsets=off(hand_l=0.0, hand_r=0.0, pel=1.0, spine=0.5), antic=5,
             follow=14)
    c.k(5, ease="io", pel=dict(dz=0.01), clav=dict(lift=3.0), hand_l=H(dp=(0.0, 0.03, -0.04)), hand_r=H(dp=(0.0, 0.03, 0.04)),
        fing="spread")
    c.k(10, ease="in4", pel=dict(dz=-0.025), spine=dict(dpitch=3.0), clav=dict(lift=-3.0),
        hand_l=H(dp=(0.0, -0.05, 0.05)), hand_r=H(dp=(0.0, -0.05, -0.05)), fing="fist")
    c.hold(13)
    c.k(18, ease="io", base=True)
    return c


@clip("w_heel_kick")
def w_heel_kick():
    # Separate Foot / Kick with the Heel (deng jiao): the weight moves onto the front leg and the wrists cross at the
    # chest, the rear knee rises, then the hands separate wide (palms out) as the heel drives forward at waist height
    c = Clip("w_heel_kick", 30, "w_stance", contact=14, priority="P1", technique="Separate Foot heel kick (deng jiao)",
             hands=("palm", "palm"), strike="foot_r", metric="fwd_foot_r", offsets=off(hand_l=-1.0, hand_r=-1.0, pel=1.0,
                                                                                         spine=0.5),
             antic=9, follow=19, no_balance=True)
    c.k(5, ease="io", pel=dict(x=0.06, y=0.12, z=-0.08, yaw=-2.0), spine=dict(yaw=0.0), foot_r=F(pv=1.0, pitch=-20.0),
        hand_l=_hw((-0.04, 0.32, 1.26), (-0.6, 0.4, 0.7), (0.0, -1.0, 0.1), (0.6, -0.3, -1.0)),
        hand_r=_hw((0.04, 0.33, 1.24), (0.6, 0.4, 0.7), (0.0, -1.0, 0.1), (-0.6, -0.3, -1.0)), fing="palm")
    c.k(9, ease="out", pel=dict(x=0.08, y=0.14, z=-0.07, pitch=-2.0), foot_l=F(yaw=-10.0),
        foot_r=F(at=(-0.04, 0.26), yaw=-10.0, pv=0.0, lift=0.42, pitch=10.0, kup=0.6))
    c.k(14, ease="in3", pel=dict(x=0.08, y=0.12, z=-0.06, pitch=-8.0, yaw=-6.0), spine=dict(pitch=-4.0, yaw=4.0),
        foot_r=F(at=(-0.03, 0.82), yaw=-6.0, pv=0.0, lift=0.76, pitch=24.0, kup=0.2),
        hand_l=_hw((0.56, 0.34, 1.34), (0.3, 0.2, 1.0), (1.0, 0.2, 0.0), (0.2, -1.0, -0.3)),
        hand_r=_hw((-0.56, 0.30, 1.32), (-0.3, 0.2, 1.0), (-1.0, 0.2, 0.0), (-0.2, -1.0, -0.3)))
    c.hold(16)
    c.k(20, ease="out", foot_r=F(at=(-0.06, 0.20), yaw=-14.0, pv=0.0, lift=0.36, pitch=4.0, kup=0.6),
        pel=dict(pitch=-2.0, yaw=-4.0), spine=dict(pitch=0.0, yaw=0.0),
        hand_l=_hw((0.16, 0.40, 1.24), WARD_L["f"], WARD_L["m"], (0.5, -0.1, -1.0)),
        hand_r=_hw((-0.20, 0.20, 1.04), (0.15, 1.0, -0.05), (0.0, 0.0, -1.0), (-0.4, -1.0, -0.5)))
    c.k(24, ease="in", foot_r=F(**W_FOOT_R), foot_l=F(yaw=0.0), pel=dict(x=0.01, y=-0.01, z=-0.11, pitch=1.0, yaw=-14.0),
        spine=dict(pitch=1.0, yaw=-6.0))
    c.k(30, ease="io", base=True)
    return c


@clip("w_repulse")
def w_repulse():
    # Repulse the Monkey (dao juan gong): sink back as the front foot lightly re-plants, the right hand comes up past
    # the ear and pushes forward (palm out) while the left hand draws back to the hip palm-up - retreating while striking
    c = Clip("w_repulse", 28, "w_stance", contact=12, priority="P1", technique="Repulse the Monkey (dao juan gong)",
             hands=("palm", "willow"), strike="hand_r", metric="fwd_hand_r", offsets=off(hand_r=0.0), antic=6, follow=18)
    c.k(6, ease="io", pel=dict(dy=-0.09, dz=-0.02, yaw=-26.0), spine=dict(yaw=-8.0),
        foot_l=F(pv=1.0, lift=0.03, pitch=-8.0, kup=0.2),
        hand_r=_hw((-0.22, -0.02, 1.40), (0.1, 0.6, 0.8), (0.6, 0.6, 0.0), (-0.6, 0.0, -0.8)),
        hand_l=_hw((0.04, 0.50, 1.20), (-0.2, 0.9, 0.1), (0.0, 0.0, 1.0), (0.6, -0.2, -1.0)))
    c.k(9, ease="in", foot_l=F(**W_FOOT_L))
    c.k(12, ease="in", pel=dict(dy=0.02, z=-0.13, yaw=-4.0, pitch=2.0), spine=dict(yaw=4.0, pitch=2.0),
        clav_r=dict(prot=8.0),
        hand_r=_hw((-0.06, 0.60, 1.30), (0.0, 0.25, 1.0), (0.0, 1.0, -0.2), (-0.6, -0.1, -1.0)), fing_r="palm",
        hand_l=_hw((0.18, 0.04, 0.96), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (0.4, -1.0, -0.2)))
    c.hold(14)
    c.k(21, ease="out", pel=dict(dy=-0.02, yaw=-12.0), spine=dict(yaw=-4.0), clav_r=dict(prot=0.0), fing_r="willow",
        hand_r=_hw((-0.18, 0.18, 1.00), (0.15, 1.0, -0.05), (0.0, 0.0, -1.0), (-0.4, -1.0, -0.5)),
        hand_l=_hw((0.06, 0.40, 1.22), WARD_L["f"], WARD_L["m"], (0.5, -0.1, -1.0)))
    c.k(28, ease="io", base=True)
    return c
