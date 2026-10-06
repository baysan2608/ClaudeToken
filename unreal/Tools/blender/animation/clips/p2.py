"""P2 clips (MARTIAL_ARTS.md §3, priority P2): turns in place, Earth spatter / shadowless kick, Water turning whip /
White Crane / Shuttles, Fire tornado kick / corona spin, Lightning fan, Combustion toss / fuse / chained stomps, Air
rising guard.  Same toolkit and conventions as the element modules (whose stance landmarks they reuse)."""
import math

from ffa_dsl import BASES, HS, Clip, F, H, HW, apply_spec, copy_state

from . import clip
from .air import rot_xy
from .earth import E_FOOT_L, E_FOOT_R
from .fire import F_FOOT_L, F_FOOT_R, _guard_hands
from .reactions import IDLE_L, IDLE_R
from .water import W_FOOT_L, W_FOOT_R


def _off(**kw):
    d = {"pel": 2.0, "spine": 1.0, "hand_l": -1.0, "hand_r": -1.0, "neck": -1.5}
    d.update(kw)
    return d


def _hw(p, f, m, e=None):
    return HW(p=p, f=f, m=m, e=e)


def _foot_at(bf, turn, lift=None, **kw):
    x, y = rot_xy(bf["at"][0], bf["at"][1], turn)
    d = dict(bf)
    d.update(at=(x, y), yaw=bf["yaw"] + turn)
    d.setdefault("kup", 0.0)
    d.setdefault("toe", 0.0)
    if lift is not None:
        d["lift"] = lift
    d.update(kw)
    return F(**d)


def _base_hands(b):
    return dict(hand_l=H(p=b["hand_l"][0:3], f=b["hand_l"][3:6], m=b["hand_l"][6:9], e=b["hand_l"][9:12]),
                hand_r=H(p=b["hand_r"][0:3], f=b["hand_r"][3:6], m=b["hand_r"][6:9], e=b["hand_r"][9:12]),
                fing_l=b["fing_l"], fing_r=b["fing_r"])


def _key_base_turned(c, frame, turn, feet, ease="io"):
    b = BASES[c.base]
    pel = list(b["pel"])
    pel[5] += turn
    c.k(frame, ease=ease, pel=tuple(pel), spine=b["spine"], neck=b["neck"], clav_l=b["clav_l"], clav_r=b["clav_r"],
        foot_l=_foot_at(feet[0], turn), foot_r=_foot_at(feet[1], turn), **_base_hands(b))


def _turn_steps(c, t0, dt, total, feet, start=0.0):
    """Alternating Bagua-style steps carrying the stance from `start` to `start + total` degrees (quarter turns for
    the left foot, then the right, ...; a final closing step)."""
    n = max(1, int(round(abs(total) / 90.0)))
    seq = []
    for i in range(1, n + 1):
        seq.append(("l" if i % 2 else "r", start + total * i / n))
    seq.append(("r" if n % 2 else "l", start + total))
    ang = {"l": start, "r": start}
    yaw0 = BASES[c.base]["pel"][5]
    t = t0
    for side, a in seq:
        prev = ang[side]
        ang[side] = a
        bf = feet[0] if side == "l" else feet[1]
        c.k(t + dt // 2, ease="out", **{"foot_" + side: _foot_at(bf, 0.5 * (prev + a), lift=0.05, pv=0.0, kup=0.2)},
            pel=dict(yaw=yaw0 + 0.5 * (ang["l"] + ang["r"]) - 0.25 * (a - prev)))
        c.k(t + dt, ease="in", **{"foot_" + side: _foot_at(bf, a)}, pel=dict(yaw=yaw0 + 0.5 * (ang["l"] + ang["r"])))
        t += dt
    return t


# ================================================================================================ shared
def _turn(name, total):
    # a turn in place (the runtime snaps the root to the new facing): the body starts turned back by the angle and
    # catches up with two pivot steps, contact = the closing foot plant
    c = Clip(name, 30, "idle", contact=18, priority="P2", technique=f"in-place turn {abs(total):.0f} deg (two steps)",
             hands=("relaxed", "relaxed"), offsets={"neck": -1.0, "pel": 1.0}, start_pose_free=True, no_balance=True)
    b = BASES["idle"]
    pel = list(b["pel"])
    pel[5] -= total
    st = apply_spec(b, dict(pel=tuple(pel), foot_l=_foot_at(IDLE_L, -total), foot_r=_foot_at(IDLE_R, -total)), b)
    c.keys[0] = (0, st, "lin", None)
    first, second = ("foot_l", "foot_r") if total > 0 else ("foot_r", "foot_l")
    bf = {"foot_l": IDLE_L, "foot_r": IDLE_R}
    c.k(4, ease="io", pel=dict(dz=-0.02, yaw=pel[5] + total * 0.25))
    c.k(8, ease="out", **{first: _foot_at(bf[first], -total * 0.4, lift=0.05, pv=0.0)}, pel=dict(yaw=pel[5] + total * 0.6))
    c.k(11, ease="in", **{first: _foot_at(bf[first], 0.0)}, pel=dict(yaw=b["pel"][5] - total * 0.15))
    c.k(14, ease="out", **{second: _foot_at(bf[second], -total * 0.5, lift=0.05, pv=0.0)})
    c.k(18, ease="in", **{second: _foot_at(bf[second], 0.0)}, pel=dict(yaw=b["pel"][5], dz=0.0))
    c.k(30, ease="io", base=True)
    return c


@clip("turn_l90")
def turn_l90():
    return _turn("turn_l90", 90.0)


@clip("turn_r90")
def turn_r90():
    return _turn("turn_r90", -90.0)


# ================================================================================================ earth
@clip("e_spatter")
def e_spatter():
    # low crescent arm spray: sink, the right arm swings low from behind the left knee across to the front-right in
    # a crescent, fingers flung open at contact (the molten droplets spray off them)
    c = Clip("e_spatter", 30, "e_stance", contact=12, priority="P2", technique="low crescent arm spray",
             hands=("tiger", "spread"), metric="fwd_hand_r", offsets=_off(hand_r=0.0), antic=6, follow=17)
    c.k(6, ease="io", pel=dict(dz=-0.05, yaw=24.0, pitch=10.0), spine=dict(yaw=10.0, pitch=8.0), neck=dict(pitch=-8.0),
        hand_r=_hw((0.22, 0.10, 0.62), (0.5, -0.2, -0.8), (0.4, 0.4, 0.4), (-0.4, -0.6, -1.0)), fing_r="cup")
    c.k(12, ease="in3", pel=dict(dz=-0.06, yaw=-16.0, pitch=10.0), spine=dict(yaw=-6.0, pitch=6.0), clav_r=dict(prot=10.0),
        hand_r=HS((-0.30, 1.0, -0.45), ext=0.95, f=(-0.3, 1.0, -0.2), m=(0.0, 0.3, 1.0), e=(-0.4, 0.0, -1.0)),
        fing_r="spread", path="arc")
    c.k(16, ease="out", pel=dict(yaw=-24.0), spine=dict(yaw=-8.0),
        hand_r=_hw((-0.46, 0.30, 0.86), (-0.8, 0.4, 0.0), (0.0, 0.3, 1.0), (-0.3, -0.3, -1.0)), path="arc")
    c.k(23, ease="io", pel=dict(dz=0.0, yaw=0.0, pitch=4.0), spine=dict(yaw=0.0, pitch=-1.0), neck=dict(pitch=-4.0),
        clav_r=dict(prot=0.0), hand_r=_hw((-0.19, 0.12, 0.98), (0.05, 0.45, 1.0), (0.15, 1.0, -0.3)), fing_r="tiger")
    c.k(30, ease="io", base=True)
    return c


@clip("e_shadowless_kick")
def e_shadowless_kick():
    # shadowless kick (mo ying geuk): the hands stay up in front and hide it; the weight slips onto the rear leg and
    # the lead foot snaps out low at shin height, toes up (sole / heel first), and is set straight back down
    c = Clip("e_shadowless_kick", 30, "e_stance", contact=12, priority="P2", technique="shadowless low snap kick",
             hands=("tiger", "tiger"), strike="foot_l", metric="fwd_ball_l", offsets=_off(pel=1.0, spine=0.5),
             antic=6, follow=17, no_balance=True)
    c.k(6, ease="io", pel=dict(dx=-0.12, dz=0.02, yaw=-10.0), spine=dict(yaw=4.0), foot_l=F(pv=1.0, pitch=-10.0),
        hand_l=H(dp=(0.0, 0.03, 0.02)), hand_r=_hw((-0.06, 0.34, 1.10), (0.1, 0.4, 1.0), (0.2, 1.0, -0.2), (-0.5, -0.3, -1.0)))
    c.k(9, ease="out", foot_l=F(at=(0.24, 0.22), yaw=-10.0, pv=0.0, lift=0.16, pitch=10.0, kup=0.4))
    c.k(12, ease="in3", foot_l=F(at=(0.18, 0.62), yaw=-14.0, pv=0.0, lift=0.20, pitch=24.0, kup=0.2),
        pel=dict(dx=0.0, dy=0.02, dpitch=-4.0))
    c.hold(13)
    c.k(17, ease="out", foot_l=F(at=(0.28, 0.20), yaw=0.0, pv=0.0, lift=0.10, pitch=6.0, kup=0.4),
        pel=dict(dy=-0.02, dpitch=4.0))
    c.k(21, ease="in", foot_l=F(**E_FOOT_L), pel=dict(x=0.0, z=-0.30, yaw=0.0), spine=dict(yaw=0.0))
    c.k(30, ease="io", base=True)
    return c


# ================================================================================================ water
@clip("w_maelstrom")
def w_maelstrom():
    # turning whip: the body turns a full circle on hook / swing steps with the lead arm trailing loose behind like
    # a whip, then the waist stops and the arm cracks round to the front at contact (Maelstrom Lash, Briar Storm)
    c = Clip("w_maelstrom", 48, "w_stance", contact=26, priority="P2", technique="turning whip (full turn)",
             hands=("willow", "willow"), metric="fwd_hand_l", offsets=_off(hand_l=0.0, pel=0.0, spine=0.0),
             antic=18, follow=32, no_balance=True)
    c.k(2, ease="io", pel=dict(dyaw=12.0, dz=-0.02), spine=dict(dyaw=6.0),
        hand_l=_hw((0.42, 0.20, 1.22), (0.9, -0.2, 0.1), (0.0, 0.0, -1.0), (0.3, -0.6, -1.0)))
    t = _turn_steps(c, 2, 4, -360.0, (W_FOOT_L, W_FOOT_R))
    c.k(23, ease="io", hand_l=_hw((0.52, -0.16, 1.24), (0.6, -0.8, 0.1), (0.0, 0.0, -1.0), (0.2, -0.3, -1.0)))
    yaw_end = BASES["w_stance"]["pel"][5] - 360.0
    c.k(26, ease="in3", pel=dict(yaw=yaw_end + 14.0, dy=0.06), spine=dict(yaw=10.0),
        hand_l=HS((0.20, 1.0, 0.15), ext=0.97, f=(0.2, 1.0, 0.1), m=(-1.0, 0.0, -0.1), e=(0.5, 0.0, -1.0)), path="arc")
    c.hold(28)
    c.k(37, ease="out", pel=dict(yaw=yaw_end + 4.0, dy=-0.04), spine=dict(yaw=-4.0),
        hand_l=_hw((0.10, 0.44, 1.24), (-1.0, 0.25, 0.1), (0.0, -1.0, 0.1), (0.5, -0.1, -1.0)))
    _key_base_turned(c, 48, -360.0, (W_FOOT_L, W_FOOT_R))
    return c


@clip("w_crane")
def w_crane():
    # White Crane Spreads Its Wings: the weight sits back on the rear leg, the front foot becomes empty (toes just
    # touching), the right hand rises to the right temple (palm turning out) while the left hand presses down past
    # the left hip - the wings open (Veil, Creeping Fog)
    c = Clip("w_crane", 30, "w_stance", contact=15, priority="P2", technique="White Crane Spreads Its Wings",
             hands=("willow", "willow"), offsets=_off(hand_r=0.0), antic=7, follow=20)
    c.k(7, ease="io", pel=dict(dy=-0.06, dz=-0.02, yaw=-24.0), spine=dict(yaw=-4.0),
        **{"hand_r": _hw((-0.04, 0.32, 1.04), (0.6, 0.6, 0.2), (0.0, 0.0, 1.0), (-0.6, -0.3, -1.0)),
           "hand_l": _hw((0.00, 0.36, 1.26), (-0.6, 0.6, 0.2), (0.0, 0.0, -1.0), (0.6, -0.3, -1.0))})
    c.k(15, ease="in", pel=dict(dy=-0.10, dz=-0.03, yaw=-10.0), spine=dict(yaw=2.0, pitch=-2.0),
        foot_l=F(at=(0.10, 0.20), yaw=0.0, pv=2.0, pitch=-30.0),
        hand_r=_hw((-0.30, 0.30, 1.62), (0.1, 0.4, 1.0), (0.4, 1.0, 0.0), (-0.6, 0.0, -0.6)),
        hand_l=_hw((0.30, 0.20, 0.86), (0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (0.6, -0.3, -1.0)), path="arc")
    c.hold(18)
    c.k(24, ease="io", foot_l=F(**W_FOOT_L), pel=dict(dy=0.08, dz=0.02, yaw=-14.0),
        hand_r=_hw((-0.19, 0.16, 0.97), (0.15, 1.0, -0.05), (0.0, 0.0, -1.0), (-0.4, -1.0, -0.5)),
        hand_l=_hw((0.03, 0.40, 1.24), (-1.0, 0.25, 0.1), (0.0, -1.0, 0.1), (0.5, -0.1, -1.0)))
    c.k(30, ease="io", base=True)
    return c


@clip("w_shuttle")
def w_shuttle():
    # Fair Lady Works the Shuttles: the lead forearm rolls up to ward above the forehead (palm out), the rear palm
    # pushes out at chest height as the weight flows forward into a bow (Lattice Roll, Vinegrip release)
    c = Clip("w_shuttle", 30, "w_stance", contact=14, priority="P2", technique="Fair Lady Works the Shuttles",
             hands=("willow", "palm"), strike="hand_r", metric="fwd_hand_r", offsets=_off(hand_r=0.0), antic=7,
             follow=19)
    c.k(7, ease="io", pel=dict(dy=-0.07, dz=-0.02, yaw=-24.0), spine=dict(yaw=-6.0), foot_l=F(pv="heel", pitch=8.0),
        hand_l=_hw((0.02, 0.34, 1.06), (-0.7, 0.6, 0.0), (0.0, 0.0, 1.0), (0.6, -0.4, -1.0)),
        hand_r=_hw((-0.10, 0.28, 1.22), (0.6, 0.6, 0.2), (0.0, 0.2, -1.0), (-0.6, -0.3, -1.0)))
    c.k(14, ease="in", pel=dict(x=0.02, y=0.10, z=-0.11, pitch=4.0, yaw=-8.0), spine=dict(pitch=2.0, yaw=0.0),
        clav=dict(prot=8.0), foot_l=F(pv=1.0, pitch=0.0), foot_r=F(yaw=-50.0),
        hand_l=_hw((0.10, 0.36, 1.64), (-0.9, 0.2, 0.3), (0.0, 0.6, 0.8), (0.6, 0.0, -0.6)),
        hand_r=HS((0.10, 1.0, 0.0), ext=0.93, f=(0.0, 0.25, 1.0), m=(0.0, 1.0, -0.2), e=(-0.5, 0.0, -1.0)))
    c.hold(16)
    c.k(23, ease="out", pel=dict(dy=-0.08, dz=0.01, yaw=-14.0), spine=dict(yaw=-6.0), clav=dict(prot=0.0),
        foot_r=F(yaw=-42.0), fing_r="willow",
        hand_l=_hw((0.04, 0.42, 1.26), (-1.0, 0.25, 0.1), (0.0, -1.0, 0.1), (0.5, -0.1, -1.0)),
        hand_r=_hw((-0.18, 0.18, 1.00), (0.15, 1.0, -0.05), (0.0, 0.0, -1.0), (-0.4, -1.0, -0.5)))
    c.k(30, ease="io", base=True)
    return c


# ================================================================================================ fire
@clip("f_tornado_kick")
def f_tornado_kick():
    # tornado kick (xuan feng jiao): crouch and wind, jump and spin a full turn in the air; at the top of the jump
    # the right leg whips across in an inside crescent (contact), land on the left foot then the right
    c = Clip("f_tornado_kick", 48, "f_stance", contact=28, priority="P2", technique="tornado kick (xuan feng jiao)",
             hands=("palm", "palm"), metric="high_ball_r", offsets=_off(pel=0.0, spine=0.0), antic=12, follow=34,
             no_balance=True)
    yaw0 = BASES["f_stance"]["pel"][5]
    c.k(8, ease="io", pel=dict(z=-0.22, pitch=10.0, yaw=yaw0 + 40.0), spine=dict(yaw=10.0, pitch=4.0),
        foot_l=F(pv=1.0, yaw=26.0), foot_r=F(pv=1.0, yaw=-6.0, pitch=0.0), fing="palm",
        hand_l=_hw((0.40, -0.10, 1.10), (0.6, -0.6, 0.0), (0.0, 0.0, -1.0), (0.4, 0.2, -1.0)),
        hand_r=_hw((-0.10, 0.10, 1.02), (0.4, -0.6, 0.0), (0.0, 0.0, -1.0), (-0.4, 0.2, -1.0)))
    c.k(14, ease="in", pel=dict(z=0.12, pitch=0.0, yaw=yaw0 - 60.0), spine=dict(yaw=-6.0, pitch=-2.0),
        foot_l=_foot_at(F_FOOT_L, -80.0, lift=0.24, pv=1.0, pitch=-30.0, kup=0.4),
        foot_r=_foot_at(F_FOOT_R, -60.0, lift=0.30, pv=1.0, pitch=-30.0, kup=0.4),
        hand_l=_hw((0.40, 0.30, 1.66), (0.3, 0.3, 0.9), (0.0, 1.0, 0.0), (0.5, -0.3, -0.6)),
        hand_r=_hw((-0.40, 0.26, 1.66), (-0.3, 0.3, 0.9), (0.0, 1.0, 0.0), (-0.5, -0.3, -0.6)))
    c.k(22, ease="lin", pel=dict(z=0.20, yaw=yaw0 - 200.0), spine=dict(yaw=-10.0),
        foot_l=_foot_at(F_FOOT_L, -200.0, lift=0.36, pv=1.0, pitch=-30.0, kup=0.5),
        foot_r=_foot_at(F_FOOT_R, -170.0, lift=0.42, pv=0.0, pitch=-20.0, kup=0.3))
    c.k(28, ease="in", pel=dict(z=0.18, yaw=yaw0 - 300.0, pitch=-6.0), spine=dict(yaw=-14.0),
        foot_l=_foot_at(F_FOOT_L, -300.0, lift=0.30, pv=1.0, pitch=-30.0, kup=0.5),
        foot_r=F(**dict(at=rot_xy(0.20, 0.55, -10.0), yaw=-300.0, pv=0.0, lift=1.15, pitch=10.0, kup=0.0)),
        hand_l=_hw((0.24, 0.62, 1.40), (0.0, 0.8, 0.5), (-0.6, 0.0, -0.8), (0.5, -0.3, -1.0)))
    c.k(34, ease="out", pel=dict(z=-0.14, pitch=8.0, yaw=yaw0 - 350.0), spine=dict(yaw=-6.0, pitch=4.0),
        foot_l=_foot_at(F_FOOT_L, -360.0), foot_r=_foot_at(F_FOOT_R, -360.0, lift=0.10, kup=0.3),
        **_guard_hands())
    c.k(38, ease="in", foot_r=_foot_at(F_FOOT_R, -360.0), pel=dict(z=-0.16))
    b = BASES["f_stance"]
    c.k(48, ease="io", pel=(b["pel"][0], b["pel"][1], b["pel"][2], b["pel"][3], b["pel"][4], b["pel"][5] - 360.0),
        spine=b["spine"], neck=b["neck"], foot_l=_foot_at(F_FOOT_L, -360.0), foot_r=_foot_at(F_FOOT_R, -360.0),
        clav_l=b["clav_l"], clav_r=b["clav_r"], **_base_hands(b))
    return c


@clip("f_corona")
def f_corona():
    # corona: a full spin on the ball of the lead foot with both sword-finger arms extended to the sides, the rear
    # leg sweeping round low; the ring of fire leaves the fingertips (contact at three-quarters of the turn)
    c = Clip("f_corona", 36, "f_stance", contact=14, priority="P2", technique="arms-extended spin (corona ring)",
             hands=("sword", "sword"), offsets=_off(pel=0.0, spine=0.0), antic=3, follow=20, no_balance=True)
    yaw0 = BASES["f_stance"]["pel"][5]
    c.k(3, ease="io", pel=dict(dyaw=20.0, dz=-0.06, x=0.06, y=0.16), spine=dict(dyaw=-6.0), fing="sword")
    for i, (t, turn) in enumerate(((6, -90.0), (9, -180.0), (12, -270.0), (15, -360.0))):
        rx, ry_ = rot_xy(-0.30, -0.10, turn)
        c.k(t, ease="lin", pel=dict(yaw=yaw0 + 20.0 + turn - 20.0 * ((i + 1) / 4.0), x=0.08, y=0.20, z=-0.14),
            foot_l=F(**dict(F_FOOT_L, yaw=-4.0 + turn)),
            foot_r=F(at=(0.10 + rx, 0.24 + ry_), yaw=-30.0 + turn, pv=1.0, lift=0.10, pitch=-20.0, kup=0.3),
            hand_l=HS(rot_xy(1.0, 0.1, turn) + (0.12,), ext=0.97, f=rot_xy(1.0, 0.1, turn) + (0.12,), m=(0.0, 0.0, -1.0)),
            hand_r=HS(rot_xy(-1.0, 0.1, turn) + (0.12,), ext=0.97, f=rot_xy(-1.0, 0.1, turn) + (0.12,), m=(0.0, 0.0, -1.0)))
    c.k(19, ease="in", foot_r=_foot_at(F_FOOT_R, -360.0), pel=dict(x=0.0, y=-0.02, z=-0.12, yaw=yaw0 - 356.0),
        **_guard_hands())
    b = BASES["f_stance"]
    c.k(36, ease="io", pel=(b["pel"][0], b["pel"][1], b["pel"][2], b["pel"][3], b["pel"][4], b["pel"][5] - 360.0),
        spine=b["spine"], neck=b["neck"], foot_l=_foot_at(F_FOOT_L, -360.0), foot_r=_foot_at(F_FOOT_R, -360.0),
        clav_l=b["clav_l"], clav_r=b["clav_r"], **_base_hands(b))
    return c


@clip("l_fan")
def l_fan():
    # arc fan: the waist winds right, then the lead hand - fingers spread - sweeps flat across the front from right to
    # left at shoulder height as the waist turns (the arc of sparks fans out from the fingertips)
    c = Clip("l_fan", 30, "f_stance", contact=12, priority="P2", technique="fingers-spread arc (lightning fan)",
             hands=("spread", "sword"), metric="fwd_hand_l", offsets=_off(hand_l=0.0), antic=6, follow=17)
    c.k(6, ease="io", pel=dict(dyaw=-18.0, dz=-0.03), spine=dict(dyaw=-6.0), fing_l="spread",
        hand_l=_hw((-0.30, 0.40, 1.30), (-0.8, 0.5, 0.0), (0.0, 0.3, -1.0), (0.4, -0.3, -1.0)))
    c.k(12, ease="in3", pel=dict(dyaw=26.0, dy=0.04), spine=dict(dyaw=10.0), clav_l=dict(prot=10.0),
        hand_l=HS((0.0, 1.0, 0.05), ext=0.97, f=(0.0, 1.0, 0.05), m=(0.0, 0.0, -1.0), e=(0.5, 0.0, -1.0)), path="arc")
    c.k(16, ease="out", pel=dict(dyaw=8.0), hand_l=_hw((0.52, 0.40, 1.30), (0.9, 0.3, 0.0), (0.0, 0.0, -1.0), (0.4, -0.3, -1.0)),
        path="arc")
    c.k(24, ease="io", pel=dict(dyaw=-16.0, dy=-0.04), spine=dict(dyaw=-4.0), clav_l=dict(prot=4.0), **_guard_hands())
    c.k(30, ease="io", base=True)
    return c


@clip("c_toss")
def c_toss():
    # ember flick: the right hand loads by the hip (pinched like a crane beak) and flicks forward-down, fingers
    # snapping open to scatter the embers in front (Spark Mine, Scatter Charges)
    c = Clip("c_toss", 20, "f_stance", contact=8, priority="P2", technique="ember flick (underhand)",
             hands=("fist", "spread"), metric="fwd_hand_r", offsets=_off(hand_r=0.0, pel=1.0, spine=0.5), antic=4,
             follow=12)
    c.k(4, ease="io", pel=dict(dyaw=-8.0, dz=-0.02), fing_r="crane",
        hand_r=_hw((-0.24, -0.04, 0.96), (0.0, -0.4, -0.9), (0.6, 0.0, 0.3), (-0.4, 0.2, -1.0)))
    c.k(8, ease="in3", pel=dict(dyaw=10.0, dy=0.04), clav_r=dict(prot=8.0), fing_r="spread",
        hand_r=HS((0.05, 1.0, -0.35), ext=0.95, f=(0.0, 1.0, -0.3), m=(0.0, 0.2, 1.0), e=(-0.4, 0.0, -1.0)), path="arc")
    c.hold(10)
    c.k(20, ease="io", base=True)
    return c


def _make_fuse_base():
    st = BASES["f_stance"]
    BASES["c_fuse"] = apply_spec(st, dict(
        pel=(0.0, -0.01, -0.13, 4.0, 0.0, -28.0), spine=(3.0, 0.0, -4.0), neck=(-4.0, 0.0, 0.0),
        hand_l=HW(p=(0.10, 0.66, 1.36), f=(-0.05, 1.0, 0.05), m=(-1.0, 0.0, 0.0), e=(0.3, 0.0, -1.0)),
        hand_r=HW(p=(0.06, 0.36, 1.22), f=(0.2, 0.8, 0.4), m=(-0.3, 0.3, 0.9), e=(-0.4, -0.3, -1.0)),
        fing_l="sword", fing_r="sword"), st)


_make_fuse_base()


@clip("c_fuse_loop")
def c_fuse_loop():
    # fuse channel: the lead arm points with sword fingers at the charged spot, the rear sword fingers support the lead
    # forearm; held very still with a fine tremble and slow breathing
    c = Clip("c_fuse_loop", 72, "c_fuse", loop=True, priority="P2", technique="focus with sword fingers (fuse)",
             hands=("sword", "sword"), offsets={"neck": 0.0})
    c.k(24, ease="sp", pel=dict(dz=-0.006), hand_l=H(dp=(0.0, 0.004, -0.003)))
    c.k(48, ease="sp", pel=dict(dz=0.003), hand_l=H(dp=(0.0, -0.003, 0.003)))
    c.breathe(depth=0.7, cycles=1)
    c.tremble(amp=0.7)
    return c


@clip("c_chain_stomp")
def c_chain_stomp():
    # chained stomps: three heavy stomps marching in place - right (contact 12), left (24), right (36) - each with the
    # palms pressing down and the knees absorbing, then back to the ready stance (Chain Blasts)
    c = Clip("c_chain_stomp", 60, "f_stance", contacts=[12, 24, 36], contact=12, priority="P2",
             technique="stepping stomps (three)", hands=("palm", "palm"), offsets=_off(), antic=7, follow=44,
             no_balance=True)
    feet = {"r": F_FOOT_R, "l": F_FOOT_L}
    for t, side in ((12, "r"), (24, "l"), (36, "r")):
        other = "l" if side == "r" else "r"
        bf = feet[side]
        c.k(t - 5, ease="out", **{"foot_" + side: F(**dict(bf, pv=0.0, lift=0.30, pitch=-4.0, kup=0.6))},
            pel=dict(x=0.06 if side == "r" else -0.06, z=-0.06, yaw=BASES["f_stance"]["pel"][5]), clav=dict(lift=4.0),
            hand_l=_hw((0.18, 0.30, 1.26), (-0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (0.6, -0.2, -1.0)),
            hand_r=_hw((-0.12, 0.26, 1.24), (0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (-0.6, -0.2, -1.0)), fing="palm")
        c.k(t, ease="acc", **{"foot_" + side: F(**dict(bf, pitch=0.0 if side == "r" else 0.0))},
            pel=dict(x=0.0, z=-0.17, pitch=8.0), clav=dict(lift=-4.0),
            hand_l=_hw((0.22, 0.20, 0.88), (-0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (0.7, -0.2, -0.8)),
            hand_r=_hw((-0.20, 0.10, 0.86), (0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (-0.7, -0.2, -0.8)))
        c.k(t + 2, ease="lin")
    c.k(46, ease="out", pel=dict(z=-0.12, pitch=5.0), clav=dict(lift=0.0), foot_r=F(**F_FOOT_R), **_guard_hands())
    c.k(60, ease="io", base=True)
    return c


# ================================================================================================ air
@clip("a_rising_guard")
def a_rising_guard():
    # rising crossed forearms (Vortex Wall raise): the forearms cross low in front, then rise crossed above the head
    # with the palms turning out as the legs straighten a little, then open into the circling-palm guard
    c = Clip("a_rising_guard", 24, "a_stance", contact=10, priority="P2", technique="rising crossed forearms",
             hands=("oxtongue", "oxtongue"), metric="high_hand_l", offsets=_off(hand_l=0.0, hand_r=0.0), antic=4,
             follow=15)
    c.k(4, ease="io", pel=dict(dz=-0.04, dyaw=10.0), spine=dict(dyaw=-6.0),
        hand_l=_hw((-0.06, 0.36, 1.00), (-0.6, 0.4, 0.6), (0.0, -0.6, 0.6), (0.6, -0.3, -1.0)),
        hand_r=_hw((0.06, 0.34, 1.00), (0.6, 0.4, 0.6), (0.0, -0.6, 0.6), (-0.6, -0.3, -1.0)))
    c.k(10, ease="in", pel=dict(dz=0.06, dyaw=-10.0), spine=dict(dyaw=6.0, dpitch=-4.0), clav=dict(lift=10.0),
        hand_l=_hw((-0.06, 0.30, 1.76), (-0.5, 0.2, 0.8), (0.0, 1.0, 0.3), (0.6, 0.0, -0.6)),
        hand_r=_hw((0.06, 0.28, 1.76), (0.5, 0.2, 0.8), (0.0, 1.0, 0.3), (-0.6, 0.0, -0.6)))
    c.hold(12)
    c.k(24, ease="io", base=True)
    return c
