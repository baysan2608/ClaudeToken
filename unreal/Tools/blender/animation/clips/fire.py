"""Fire clips - Northern Shaolin long fist / Tan Tui (MARTIAL_ARTS.md §2.3, §2.4, §3.4): long lines, hip snap.

Base f_stance = springy long-fist ready: left foot forward (0.12, 0.27), right foot back (-0.15, -0.23) turned out 36
deg on its ball (heel up 9 deg), pelvis bladed 20 deg right, fists up.
Mechanics: chamber (fist palm-up at the hip / coil) -> the hip snaps -> the limb reaches FULL extension at contact (a
2-frame hold, fingers locked) -> snaps back as fast as it left; a 2-frame chest compression (breath burst) at contact.
Lightning (l_*) is the same body with sword fingers and Tai Chi sword gathering circles.
"""
import math

from ffa_dsl import BASES, HS, Clip, F, H, HW, apply_spec, copy_state

from . import clip

F_FOOT_L = dict(at=(0.12, 0.27), yaw=-4.0, pv=1.0)
F_FOOT_R = dict(at=(-0.15, -0.23), yaw=-36.0, pv=1.0, pitch=-9.0, kyaw=5.0)


def off(**kw):
    d = {"pel": 2.0, "spine": 1.0, "hand_l": -1.0, "hand_r": -1.0, "neck": -1.5}
    d.update(kw)
    return d


def _hw(p, f, m, e=None):
    return HW(p=p, f=f, m=m, e=e)


FIST_DOWN = dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, -1.0))           # horizontal fist, palm down


def _make_bases():
    st = BASES["f_stance"]
    # sitting (4-6) stance on the same footprints, hips dropped and turned side-on, fists chambered palm-up at the
    # hips: the charge / ready posture of the style (no foot travel from the ready stance)
    BASES["f_charge"] = apply_spec(st, dict(
        pel=(-0.01, -0.03, -0.215, 3.0, 0.0, -34.0), spine=(-2.0, 0.0, 6.0), neck=(-2.0, 0.0, 0.0),
        clav_l=(-2.0, -4.0), clav_r=(-2.0, -4.0),
        foot_r=F(**dict(F_FOOT_R, pitch=0.0)),
        hand_l=_hw((0.14, 0.02, 0.92), (0.15, 1.0, 0.0), (0.0, 0.0, 1.0), (0.4, -1.0, 0.0)),
        hand_r=_hw((-0.21, -0.12, 0.90), (0.15, 1.0, 0.0), (0.0, 0.0, 1.0), (-0.4, -1.0, 0.0)),
        fing="fist"), st)
    # rooted absorbing posture (heat draw): same footprints, sunk, both palms out toward the target
    BASES["f_draw"] = apply_spec(st, dict(
        pel=(0.0, -0.04, -0.17, 3.0, 0.0, -24.0), spine=(2.0, 0.0, 10.0), neck=(-3.0, 0.0, 0.0),
        hand_l=_hw((0.10, 0.52, 1.30), (0.0, 0.6, 0.8), (0.0, 1.0, -0.2), (0.6, -0.3, -1.0)),
        hand_r=_hw((-0.10, 0.48, 1.27), (0.0, 0.6, 0.8), (0.0, 1.0, -0.2), (-0.6, -0.3, -1.0)),
        fing="spread"), st)
    # magma grip: arms reaching toward the held mass, cupped hands facing each other
    BASES["f_grip"] = apply_spec(st, dict(
        pel=(0.0, -0.03, -0.16, 4.0, 0.0, -22.0), spine=(3.0, 0.0, 9.0), neck=(-4.0, 0.0, 0.0),
        clav_l=(2.0, 6.0), clav_r=(2.0, 6.0),
        hand_l=_hw((0.10, 0.56, 1.22), (-0.2, 0.9, 0.2), (-0.95, 0.1, 0.1), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.10, 0.54, 1.20), (0.2, 0.9, 0.2), (0.95, 0.1, 0.1), (-0.6, -0.2, -1.0)),
        fing="cup"), st)


_make_bases()


@clip("f_stance")
def f_stance():
    # springy ready: two bounces per loop on the balls of the feet, fists alive
    c = Clip("f_stance", 72, "f_stance", loop=True, priority="P0", technique="long-fist ready stance, springy",
             hands=("fist", "fist"))
    for k, f in enumerate((9, 27, 45, 63)):
        down = k % 2 == 0
        c.k(f, ease="sp", pel=dict(dz=-0.014 if down else 0.014),
            foot_r=F(pitch=-6.0 if down else -9.0),
            hand_l=H(dp=(0.0, 0.006, -0.008) if down else (0.0, -0.006, 0.008)),
            hand_r=H(dp=(0.0, -0.004, -0.006) if down else (0.0, 0.004, 0.006)))
    c.breathe(depth=0.6, cycles=2)
    return c


@clip("f_jab")
def f_jab():
    # chong quan, lead hand: a tiny coil, the hips snap and the rear foot drives, the lead fist shoots straight out at
    # shoulder height turning palm-down, full extension at contact, snaps back to the guard as fast as it left
    c = Clip("f_jab", 18, "f_stance", contact=6, priority="P0", hang=0.3, technique="chong quan lead straight punch",
             hands=("fist", "fist"), strike="hand_l", offsets=off(hand_l=0.0, hand_r=-1.5), antic=2, follow=10)
    c.k(2, ease="io", pel=dict(dy=-0.012, dz=-0.012, dyaw=5.0), spine=dict(dyaw=3.0),
        hand_l=H(dp=(0.0, -0.035, -0.01)))
    c.k(6, ease="in3", pel=dict(dy=0.05, dz=-0.02, dyaw=-14.0, dpitch=3.0), spine=dict(dyaw=-6.0, dpitch=2.0),
        clav_l=dict(prot=12.0, lift=-2.0), foot_r=F(pitch=-14.0),
        hand_l=HS((-0.06, 1.0, 0.04), ext=0.985, f=(0.0, 1.0, 0.0), m=(-0.1, 0.0, -1.0), e=(0.4, 0.0, -1.0)),
        hand_r=_hw((-0.03, 0.26, 1.31), (0.25, 0.5, 0.8), (0.85, 0.0, 0.25), (-0.3, -0.3, -1.0)))
    c.hold(8)
    c.k(12, ease="out", pel=dict(dy=-0.045, dz=0.022, dyaw=12.0, dpitch=-3.0), spine=dict(dyaw=5.0, dpitch=-2.0),
        clav_l=dict(prot=4.0, lift=0.0), foot_r=F(pitch=-9.0),
        hand_l=_hw((0.13, 0.46, 1.38), (-0.15, 0.75, 0.65), (-0.95, 0.0, 0.2), (0.25, -0.2, -1.0)))
    c.k(18, ease="io", base=True)
    return c


@clip("f_cross")
def f_cross():
    # rear straight punch in a bow stance: sink and coil, the rear heel turns out and the rear leg straightens, the
    # hips rotate square, the rear fist drives through to full extension palm-down while the lead fist pulls back to
    # the chest (counter-pull); snap back
    c = Clip("f_cross", 24, "f_stance", contact=8, priority="P0", hang=0.3, technique="rear straight punch, bow stance",
             hands=("fist", "fist"), strike="hand_r", offsets=off(hand_r=0.0, hand_l=-1.5), antic=3, follow=13)
    c.k(3, ease="io", pel=dict(dy=-0.03, dz=-0.03, dyaw=-8.0), spine=dict(dyaw=-6.0),
        hand_r=_hw((-0.12, 0.10, 1.12), (0.0, 1.0, 0.1), (0.0, 0.0, 1.0), (-0.3, -1.0, -0.2)))
    c.k(8, ease="in3", pel=dict(x=0.03, y=0.08, z=-0.15, pitch=6.0, yaw=4.0), spine=dict(pitch=3.0, yaw=8.0),
        clav_r=dict(prot=14.0, lift=-3.0), clav_l=dict(prot=-4.0), foot_r=F(yaw=-28.0, pitch=0.0),
        hand_r=HS((0.10, 1.0, 0.02), ext=0.99, f=(0.1, 1.0, 0.0), m=(0.1, 0.0, -1.0), e=(-0.4, 0.0, -1.0)),
        hand_l=_hw((0.16, 0.18, 1.12), (-0.2, 0.6, 0.6), (-0.9, 0.0, 0.3), (0.5, -0.6, -0.8)))
    c.hold(10)
    c.k(15, ease="out", pel=dict(x=0.0, y=-0.01, z=-0.10, pitch=4.0, yaw=-16.0), spine=dict(pitch=5.0, yaw=-5.0),
        clav_r=dict(prot=2.0, lift=0.0), clav_l=dict(prot=4.0), foot_r=F(yaw=-36.0, pitch=-7.0),
        hand_r=_hw((-0.04, 0.24, 1.29), (0.25, 0.5, 0.8), (0.85, 0.0, 0.25), (-0.3, -0.3, -1.0)),
        hand_l=_hw((0.12, 0.42, 1.37), (-0.15, 0.75, 0.65), (-0.95, 0.0, 0.2), (0.25, -0.2, -1.0)))
    c.k(24, ease="io", base=True)
    return c


@clip("f_charge")
def f_charge():
    # charge hold: sitting stance, fists chambered palm-up at the hips, breath gathering (chest rises / sinks), the
    # stance pulses lower on each exhale; a fine tremble on the arms (tier 2+ reads through it)
    c = Clip("f_charge", 48, "f_charge", loop=True, priority="P0", technique="sitting stance, fists at the hips, gathering",
             hands=("fist", "fist"), offsets={"neck": 0.0})
    c.k(12, ease="sp", pel=dict(dz=-0.012), hand_l=H(dp=(0.0, -0.006, -0.006)), hand_r=H(dp=(0.0, -0.006, -0.006)))
    c.k(24, ease="sp", pel=dict(dz=0.004), clav=dict(dlift=1.5))
    c.k(36, ease="sp", pel=dict(dz=-0.010), hand_l=H(dp=(0.0, -0.004, -0.004)), hand_r=H(dp=(0.0, -0.004, -0.004)))
    c.breathe(depth=1.4, cycles=1)
    c.tremble(amp=0.6)
    return c


@clip("f_palm_burst")
def f_palm_burst():
    # double palms out of the sitting stance: the hips snap square and the weight surges forward into a bow, both
    # palms thrust from the hips to chest height (fingers up, palm heels first) with a sharp exhale; snap back
    c = Clip("f_palm_burst", 24, "f_charge", base_end="f_stance", contact=6, priority="P0", hang=0.3,
             technique="double palms out of the horse (fa jin burst)", hands=("palm", "palm"), strike="hands",
             offsets=off(hand_l=0.0, hand_r=0.0, pel=1.0, spine=0.5), antic=2, follow=12)
    c.k(2, ease="io", pel=dict(dz=-0.012, dyaw=-4.0), hand_l=H(dp=(0.0, -0.02, 0.01)), hand_r=H(dp=(0.0, -0.02, 0.01)),
        fing="palm")
    c.k(6, ease="in3", pel=dict(x=0.02, y=0.07, z=-0.17, pitch=6.0, yaw=-6.0), spine=dict(pitch=3.0, yaw=2.0),
        clav=dict(lift=-3.0, prot=10.0), foot_r=F(yaw=-30.0, pitch=0.0),
        hand_l=HS((0.12, 1.0, 0.0), ext=0.95, f=(0.0, 0.2, 1.0), m=(0.0, 1.0, -0.15), e=(0.5, 0.0, -1.0)),
        hand_r=HS((-0.12, 1.0, 0.0), ext=0.95, f=(0.0, 0.2, 1.0), m=(0.0, 1.0, -0.15), e=(-0.5, 0.0, -1.0)))
    c.hold(8)
    c.k(14, ease="out", pel=dict(x=0.0, y=-0.02, z=-0.12, pitch=4.0, yaw=-18.0), spine=dict(pitch=4.0, yaw=-5.0),
        clav=dict(lift=0.0, prot=3.0), foot_r=F(**F_FOOT_R), fing="fist",
        hand_l=_hw((0.13, 0.42, 1.36), (-0.15, 0.75, 0.65), (-0.95, 0.0, 0.2), (0.25, -0.2, -1.0)),
        hand_r=_hw((-0.04, 0.22, 1.28), (0.25, 0.5, 0.8), (0.85, 0.0, 0.25), (-0.3, -0.3, -1.0)))
    c.k(24, ease="io", base=True)
    return c


@clip("f_snap_kick")
def f_snap_kick():
    # Tan Tui snap kick (tan tui) with the rear leg: weight onto the lead leg, the right knee chambers to hip height,
    # the shin snaps out so the instep reaches waist height in line with the shin at contact (the fireball leaves the
    # foot), the fists punch out to the sides for balance; re-chamber and set the foot down where it came from
    c = Clip("f_snap_kick", 24, "f_stance", contact=10, priority="P0", technique="tan tui snap kick",
             hands=("fist", "fist"), strike="foot_r", metric="fwd_ball_r",
             offsets=off(pel=1.0, spine=0.5, hand_l=-1.0, hand_r=-1.0),
             antic=6, follow=15, no_balance=True)
    c.k(3, ease="io", pel=dict(dx=0.05, dy=0.06, dz=-0.03, dyaw=8.0), spine=dict(dyaw=4.0),
        foot_r=F(pv=1.0, pitch=-24.0))
    c.k(6, ease="out", pel=dict(x=0.08, y=0.12, z=-0.07, pitch=-2.0, yaw=-6.0), spine=dict(pitch=-3.0, yaw=4.0),
        foot_r=F(at=(-0.06, 0.18), yaw=-6.0, pv=0.0, lift=0.42, pitch=-58.0, kup=0.6),
        hand_l=_hw((0.22, 0.40, 1.24), (0.1, 0.8, 0.4), (-0.9, 0.0, 0.2), (0.4, -0.3, -1.0)),
        hand_r=_hw((-0.16, 0.08, 1.06), (0.0, 1.0, 0.1), (0.0, 0.0, 1.0), (-0.3, -1.0, -0.2)))
    c.k(10, ease="in3", pel=dict(x=0.07, y=0.10, z=-0.06, pitch=-9.0, yaw=-10.0), spine=dict(pitch=-5.0, yaw=6.0),
        neck=dict(pitch=4.0), foot_l=F(yaw=-14.0),
        foot_r=F(at=(-0.03, 0.90), yaw=-4.0, pv=0.0, lift=0.80, pitch=-12.0, kup=0.1),
        hand_l=_hw((0.50, 0.30, 1.30), (1.0, 0.1, 0.0), (0.0, 0.0, -1.0), (0.2, -1.0, -0.2)),
        hand_r=_hw((-0.48, 0.02, 1.28), (-1.0, -0.1, 0.0), (0.0, 0.0, -1.0), (-0.2, -1.0, -0.2)))
    c.hold(12)
    c.k(15, ease="out", foot_r=F(at=(-0.06, 0.20), yaw=-6.0, pv=0.0, lift=0.40, pitch=-52.0, kup=0.6),
        pel=dict(x=0.07, y=0.10, z=-0.07, pitch=-2.0, yaw=-8.0), spine=dict(pitch=-2.0, yaw=3.0), neck=dict(pitch=0.0),
        hand_l=_hw((0.20, 0.38, 1.28), (0.1, 0.8, 0.4), (-0.9, 0.0, 0.2), (0.4, -0.3, -1.0)),
        hand_r=_hw((-0.10, 0.22, 1.22), (0.2, 0.6, 0.6), (0.9, 0.0, 0.3), (-0.3, -0.4, -1.0)))
    c.k(19, ease="in", foot_r=F(**dict(F_FOOT_R, pitch=-4.0)), foot_l=F(yaw=-4.0),
        pel=dict(x=0.0, y=-0.01, z=-0.11, pitch=4.0, yaw=-20.0), spine=dict(pitch=5.0, yaw=-6.0))
    c.k(24, ease="io", base=True)
    return c


def _loop_from_fn(c, fn, n, base_st):
    for f in range(n):
        st = apply_spec(base_st, fn(f / n), base_st)
        if f == 0:
            c.keys[0] = (0, st, "lin", None)
        else:
            c.keys.append((f, st, "lin", None))
    return c


@clip("f_heat_draw")
def f_heat_draw():
    # rooted absorbing (DRAW / Smelter): the palms pull the heat in toward the chest (sitting back a little, elbows
    # sinking) and push it out again, two slow cycles; the fingers stay spread
    b = BASES["f_draw"]

    def fn(ph):
        th = 2 * math.pi * ((ph * 2.0) % 1.0)
        pull = 0.5 - 0.5 * math.cos(th)                  # 0 out .. 1 in
        out = {"pel": (b["pel"][0], b["pel"][1] - 0.035 * pull, b["pel"][2] - 0.01 * pull, b["pel"][3],
                       b["pel"][4], b["pel"][5] - 3.0 * pull),
               "spine": (b["spine"][0] - 2.0 * pull, b["spine"][1], b["spine"][2])}
        for sd, sx in (("l", 1.0), ("r", -1.0)):
            x = 0.10 * sx + 0.03 * sx * pull
            y = (0.52 if sd == "l" else 0.48) - 0.24 * pull
            z = (1.30 if sd == "l" else 1.27) - 0.06 * pull + 0.02 * math.sin(th)
            m = (0.0 - 0.3 * sx * pull, 1.0 - 1.4 * pull, -0.2 + 0.1 * pull)
            out["hand_" + sd] = HW(p=(x, y, z), f=(0.0, 0.6 - 0.4 * pull, 0.8), m=m, e=(0.6 * sx, -0.3, -1.0))
        return out
    c = Clip("f_heat_draw", 96, "f_draw", loop=True, priority="P0", technique="rooted heat draw (pull in, push out)",
             hands=("spread", "spread"), offsets={"neck": 0.0})
    _loop_from_fn(c, fn, 96, b)
    c.breathe(depth=1.0, cycles=2)
    c.tremble(amp=0.35)
    return c


@clip("f_thermal_hold")
def f_thermal_hold():
    # magma grip (HEAT): cupped hands reach toward the held mass, the arms shake with heat and strain; small knee
    # pulses keep the stance alive
    c = Clip("f_thermal_hold", 72, "f_grip", loop=True, priority="P0", technique="magma grip (cupped hands, heat tremble)",
             hands=("cup", "cup"), offsets={"neck": 0.0})
    c.k(18, ease="sp", pel=dict(dz=-0.010), hand_l=H(dp=(0.004, -0.01, -0.006)), hand_r=H(dp=(-0.004, -0.01, -0.006)))
    c.k(36, ease="sp", pel=dict(dz=0.006), hand_l=H(dp=(-0.004, 0.008, 0.008)), hand_r=H(dp=(0.004, 0.008, 0.008)))
    c.k(54, ease="sp", pel=dict(dz=-0.008), hand_l=H(dp=(0.002, -0.006, -0.004)), hand_r=H(dp=(-0.002, -0.006, -0.004)))
    c.breathe(depth=0.8, cycles=1)
    c.tremble(amp=1.6, hz=14.0)
    return c


@clip("f_pour")
def f_pour():
    # pour: the cupped hands raise the molten mass to the face, turn over, press it down and sweep it forward along
    # the ground in a long low bow (the fire / magma runs along the floor)
    c = Clip("f_pour", 30, "f_stance", contact=16, priority="P0", technique="raise, press down, sweep along the ground",
             hands=("palm", "palm"), metric="low_hand_r", offsets=off(hand_l=-0.5, hand_r=0.0), antic=8, follow=22)
    c.k(6, ease="io", pel=dict(dy=-0.04, dz=0.02, yaw=-26.0, pitch=0.0), spine=dict(pitch=-6.0, yaw=0.0),
        clav=dict(lift=6.0),
        hand_l=_hw((0.12, 0.30, 1.42), (0.0, 1.0, 0.2), (0.0, 0.0, 1.0), (0.6, -0.3, -1.0)),
        hand_r=_hw((-0.10, 0.28, 1.42), (0.0, 1.0, 0.2), (0.0, 0.0, 1.0), (-0.6, -0.3, -1.0)), fing="cup")
    c.k(10, ease="io", hand_l=_hw((0.12, 0.40, 1.36), (0.0, 1.0, 0.0), (0.0, 0.0, -1.0), (0.6, -0.3, -1.0)),
        hand_r=_hw((-0.10, 0.38, 1.36), (0.0, 1.0, 0.0), (0.0, 0.0, -1.0), (-0.6, -0.3, -1.0)), fing="palm")
    c.k(16, ease="in3", pel=dict(x=0.03, y=0.12, z=-0.26, pitch=20.0, yaw=-12.0), spine=dict(pitch=18.0, yaw=6.0),
        neck=dict(pitch=-18.0), clav=dict(lift=-2.0, prot=8.0), foot_r=F(yaw=-30.0, pitch=0.0),
        hand_l=_hw((0.18, 0.86, 0.50), (0.0, 1.0, -0.2), (0.0, 0.1, -1.0), (0.6, -0.2, -0.8)),
        hand_r=_hw((-0.10, 0.86, 0.48), (0.0, 1.0, -0.2), (0.0, 0.1, -1.0), (-0.6, -0.2, -0.8)))
    c.hold(18)
    c.k(24, ease="out", pel=dict(dy=-0.08, dz=0.06, pitch=8.0), spine=dict(pitch=6.0), neck=dict(pitch=-6.0),
        clav=dict(lift=0.0, prot=2.0), foot_r=F(**F_FOOT_R), fing="fist",
        hand_l=_hw((0.13, 0.44, 1.30), (-0.15, 0.75, 0.65), (-0.95, 0.0, 0.2), (0.25, -0.2, -1.0)),
        hand_r=_hw((-0.05, 0.24, 1.24), (0.25, 0.5, 0.8), (0.85, 0.0, 0.25), (-0.3, -0.3, -1.0)))
    c.k(30, ease="io", base=True)
    return c


# ================================================================================================ lightning
def _lightning_fn(ph, lag=0.015):
    """Tai Chi sword gathering: the two sword-finger hands trace opposite vertical circles in front of the body, the
    hands separate wide at the sides and come back together, the weight sways left / right with each circle."""
    b = BASES["f_charge"]
    st = BASES["f_stance"]
    th = 2 * math.pi * ph
    out = {"pel": (0.025 * math.sin(th), -0.035, -0.15 + 0.01 * math.cos(2 * th), 3.0, 1.5 * math.sin(th),
                   -24.0 + 6.0 * math.sin(th)),
           "spine": (1.0, 0.0, 6.0 + 4.0 * math.sin(th)), "neck": (-3.0, 0.0, 0.0)}
    for sd, sx, ph0 in (("l", 1.0, 0.0), ("r", -1.0, math.pi)):
        a = th - 2 * math.pi * lag + ph0
        sep = 0.5 - 0.5 * math.cos(th)                   # the hands separate once per circle
        x = sx * (0.06 + 0.16 * sep) + 0.10 * math.sin(a) * sx
        z = 1.14 + 0.13 * math.cos(a)
        y = 0.36 + 0.05 * math.cos(a) - 0.06 * sep
        out["hand_" + sd] = HW(p=(x, y, z), f=(0.25 * sx * math.sin(a), 0.8, 0.5 * math.cos(a) + 0.2),
                               m=(-sx * 0.8, 0.1, -0.5), e=(0.6 * sx, -0.3, -1.0))
    return out


def _make_l_base():
    BASES["l_charge"] = apply_spec(BASES["f_stance"], dict(_lightning_fn(0.0), fing="sword",
                                                           foot_r=F(**dict(F_FOOT_R, pitch=0.0))), BASES["f_stance"])


_make_l_base()


@clip("l_charge")
def l_charge():
    c = Clip("l_charge", 96, "l_charge", loop=True, priority="P0",
             technique="circular gathering with sword fingers (Tai Chi sword energy)", hands=("sword", "sword"),
             offsets={"neck": 0.0})
    _loop_from_fn(c, lambda ph: dict(_lightning_fn((ph * 2.0) % 1.0), foot_r=F(**dict(F_FOOT_R, pitch=0.0))), 96,
                  BASES["l_charge"])
    c.breathe(depth=0.8, cycles=2)
    c.tremble(amp=0.4)
    return c


@clip("l_release")
def l_release():
    # two-finger release: step into a bow, the lead arm extends straight with the sword fingers pointing at the
    # target, the rear hand pulls back to the hip; 2-frame hold, fast snap back to the ready stance
    c = Clip("l_release", 20, "l_charge", base_end="f_stance", contact=6, priority="P0", hang=0.3,
             technique="sword-finger extension (two-finger release)", hands=("sword", "sword"), strike="hand_l",
             offsets=off(hand_l=0.0, hand_r=-1.0, pel=1.0, spine=0.5), antic=2, follow=11)
    c.k(2, ease="io", pel=dict(dy=-0.02, dz=-0.01, dyaw=6.0), foot_l=F(pv="heel", lift=0.02, pitch=8.0),
        hand_l=_hw((0.10, 0.26, 1.24), (0.0, 0.8, 0.6), (-0.9, 0.0, 0.2), (0.5, -0.4, -1.0)))
    c.k(4, ease="out", foot_l=F(at=(0.12, 0.40), yaw=-4.0, pv=1.0))
    c.k(6, ease="in3", pel=dict(x=0.02, y=0.13, z=-0.15, pitch=5.0, yaw=-30.0), spine=dict(pitch=2.0, yaw=-6.0),
        clav_l=dict(prot=12.0, lift=-2.0),
        hand_l=HS((-0.02, 1.0, 0.05), ext=0.995, f=(-0.02, 1.0, 0.05), m=(-1.0, 0.0, 0.0), e=(0.3, 0.0, -1.0)),
        hand_r=_hw((-0.20, -0.10, 0.92), (0.1, 1.0, 0.0), (0.0, 0.0, 1.0), (-0.4, -1.0, 0.0)))
    c.hold(8)
    c.k(12, ease="out", pel=dict(y=0.02, z=-0.12, pitch=3.0, yaw=-22.0), spine=dict(yaw=-4.0),
        clav_l=dict(prot=3.0, lift=0.0),
        hand_l=_hw((0.12, 0.44, 1.36), (-0.15, 0.75, 0.65), (-0.95, 0.0, 0.2), (0.25, -0.2, -1.0)),
        hand_r=_hw((-0.05, 0.22, 1.26), (0.25, 0.5, 0.8), (0.85, 0.0, 0.25), (-0.3, -0.3, -1.0)), fing="fist")
    c.k(14, ease="io", foot_l=F(pv="ball", lift=0.02, pitch=-6.0))
    c.k(16, ease="in", foot_l=F(**F_FOOT_L), foot_r=F(**F_FOOT_R))
    c.k(20, ease="io", base=True)
    return c


# ================================================================================================ P1
GUARD_FIST_L = ((0.12, 0.44, 1.39), (-0.15, 0.75, 0.65), (-0.95, 0.0, 0.2), (0.25, -0.2, -1.0))
GUARD_FIST_R = ((-0.035, 0.22, 1.30), (0.3, 0.5, 0.8), (0.9, 0.0, 0.2), (-0.3, -0.3, -1.0))


def _guard_hands(**kw):
    d = dict(hand_l=_hw(*GUARD_FIST_L), hand_r=_hw(*GUARD_FIST_R), fing="fist")
    d.update(kw)
    return d


@clip("f_column")
def f_column():
    # rising uppercut palm: sink and load the right palm low by the hip, then the legs drive and the palm rises
    # vertically past the face to above the head (palm up / forward) on a sharp exhale - the fire column goes up
    c = Clip("f_column", 30, "f_stance", contact=12, priority="P1", technique="rising palm (uppercut palm)",
             hands=("fist", "palm"), metric="high_hand_r", offsets=off(hand_r=0.0), antic=6, follow=18)
    c.k(6, ease="io", pel=dict(dz=-0.07, dpitch=6.0, dyaw=6.0), spine=dict(dpitch=6.0),
        hand_r=_hw((-0.14, 0.24, 0.86), (0.1, 0.9, -0.3), (0.0, -0.2, 1.0), (-0.4, -0.6, -1.0)), fing_r="palm")
    c.k(12, ease="in3", pel=dict(x=0.02, y=0.06, z=-0.03, pitch=-4.0, yaw=-8.0), spine=dict(pitch=-8.0, yaw=8.0),
        neck=dict(pitch=10.0), clav_r=dict(lift=18.0, prot=6.0), foot_r=F(pitch=-22.0),
        hand_r=_hw((-0.06, 0.42, 1.92), (0.0, 0.2, 1.0), (0.0, 0.9, 0.3), (-0.6, 0.0, -0.5)),
        hand_l=_hw((0.16, 0.26, 1.10), (-0.1, 0.7, 0.6), (-0.9, 0.0, 0.3), (0.5, -0.4, -1.0)))
    c.hold(14)
    c.k(21, ease="out", pel=dict(x=0.0, y=-0.02, z=-0.10, pitch=4.0, yaw=-20.0), spine=dict(pitch=5.0, yaw=-6.0),
        neck=dict(pitch=-5.0), clav_r=dict(lift=0.0, prot=2.0), foot_r=F(pitch=-9.0), **_guard_hands())
    c.k(30, ease="io", base=True)
    return c


@clip("f_inferno")
def f_inferno():
    # circle + double palm: the wrists cross before the face, the arms open overhead and wheel down the sides in one
    # big circle while the stance sinks into the sitting horse, the palms gather at the hips and burst forward
    ch = BASES["f_charge"]
    c = Clip("f_inferno", 48, "f_stance", contact=26, priority="P1", technique="great circle -> double palms (Inferno)",
             hands=("palm", "palm"), strike="hands", metric="fwd_hand_l", offsets=off(hand_l=0.0, hand_r=0.0),
             antic=18, follow=32)
    c.k(6, ease="io", pel=dict(dz=0.02, dyaw=6.0), spine=dict(dpitch=-4.0), fing="palm",
        hand_l=_hw((-0.04, 0.34, 1.52), (-0.5, 0.2, 0.8), (0.0, -1.0, 0.2), (0.6, -0.2, -1.0)),
        hand_r=_hw((0.04, 0.33, 1.50), (0.5, 0.2, 0.8), (0.0, -1.0, 0.2), (-0.6, -0.2, -1.0)))
    c.k(12, ease="io", pel=dict(dz=0.03), spine=dict(dpitch=-6.0), clav=dict(lift=16.0), neck=dict(pitch=6.0),
        hand_l=_hw((0.36, 0.16, 1.88), (0.4, 0.0, 1.0), (0.9, 0.0, 0.2), (0.6, 0.0, -0.4)),
        hand_r=_hw((-0.36, 0.14, 1.88), (-0.4, 0.0, 1.0), (-0.9, 0.0, 0.2), (-0.6, 0.0, -0.4)), path="arc")
    c.k(18, ease="io", pel=dict(z=-0.15, yaw=-28.0), clav=dict(lift=2.0), neck=dict(pitch=-2.0),
        hand_l=_hw((0.66, 0.10, 1.26), (1.0, 0.0, -0.1), (0.0, 0.0, -1.0), (0.2, -0.8, -0.5)),
        hand_r=_hw((-0.66, 0.06, 1.26), (-1.0, 0.0, -0.1), (0.0, 0.0, -1.0), (-0.2, -0.8, -0.5)), path="arc")
    c.k(22, ease="io", pel=ch["pel"], spine=ch["spine"], neck=ch["neck"], clav=dict(lift=-2.0, prot=-4.0),
        foot_r=F(**dict(F_FOOT_R, pitch=0.0)),
        hand_l=_hw((0.14, 0.04, 0.94), (0.15, 1.0, 0.0), (0.0, 0.0, 1.0), (0.4, -1.0, 0.0)),
        hand_r=_hw((-0.20, -0.08, 0.92), (0.15, 1.0, 0.0), (0.0, 0.0, 1.0), (-0.4, -1.0, 0.0)), path="arc")
    c.k(26, ease="in3", pel=dict(x=0.02, y=0.07, z=-0.17, pitch=6.0, yaw=-6.0), spine=dict(pitch=3.0, yaw=2.0),
        clav=dict(lift=-3.0, prot=10.0), foot_r=F(yaw=-30.0, pitch=0.0),
        hand_l=HS((0.12, 1.0, 0.0), ext=0.95, f=(0.0, 0.2, 1.0), m=(0.0, 1.0, -0.15), e=(0.5, 0.0, -1.0)),
        hand_r=HS((-0.12, 1.0, 0.0), ext=0.95, f=(0.0, 0.2, 1.0), m=(0.0, 1.0, -0.15), e=(-0.5, 0.0, -1.0)))
    c.hold(29)
    c.k(38, ease="out", pel=dict(x=0.0, y=-0.02, z=-0.11, pitch=4.0, yaw=-18.0), spine=dict(pitch=4.0, yaw=-5.0),
        clav=dict(lift=0.0, prot=3.0), foot_r=F(**F_FOOT_R), **_guard_hands())
    c.k(48, ease="io", base=True)
    return c


@clip("f_low_sweep")
def f_low_sweep():
    # sao tang tui: drop onto the bent lead leg with the left hand to the floor, the straight right leg sweeps flat
    # along the floor in a ~200 deg arc round the support foot from behind to the front-left (contact: the foot
    # crosses the front), the hips and chest turning with it; rise back to the ready stance
    c = Clip("f_low_sweep", 36, "f_stance", contact=17, priority="P1", technique="low spinning sweep (sao tang tui)",
             hands=("fist", "fist"), metric="fwd_ball_r", offsets=off(pel=1.0, spine=0.5), antic=8, follow=22,
             no_balance=True)
    c.k(5, ease="io", pel=dict(x=0.08, y=0.16, z=-0.36, pitch=22.0, yaw=-30.0), spine=dict(pitch=10.0),
        neck=dict(pitch=-14.0), foot_l=F(yaw=-20.0), foot_r=F(kup=0.6),
        hand_l=_hw((0.30, 0.40, 0.30), (0.0, 0.6, -0.8), (0.0, 0.0, -1.0), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.02, 0.38, 0.62), (0.2, 0.8, -0.4), (0.0, 0.0, -1.0), (-0.6, -0.2, -1.0)), fing="palm")
    cx, cy, r = 0.12, 0.25, 0.80
    arc = [(9, 228.0, -56.0), (11, 200.0, -40.0), (13, 172.0, -24.0), (15, 144.0, -8.0), (17, 112.0, 8.0),
           (19, 84.0, 20.0), (21, 58.0, 28.0)]
    for t, ang, pyaw in arc:
        x = cx + r * math.cos(math.radians(ang))
        y = cy + r * math.sin(math.radians(ang))
        fy = -36.0 + (228.0 - ang)        # the foot turns with the leg (no twist at the knee)
        spec = dict(pel=dict(x=0.10, y=0.22, z=-0.52, pitch=30.0, yaw=pyaw), spine=dict(pitch=12.0, yaw=0.3 * pyaw),
                    foot_l=F(yaw=-20.0 + 0.5 * (pyaw + 56.0)),
                    foot_r=F(at=(x, y), yaw=fy, pv=1.0, pitch=-10.0, kup=1.5))
        if t == 9:
            spec["hand_l"] = _hw((0.30, 0.46, 0.06), (0.0, 1.0, 0.0), (0.0, 0.0, -1.0), (0.6, -0.2, -1.0))
        c.k(t, ease="io" if t == 9 else "lin", **spec)
    c.k(23, ease="out", foot_r=F(at=(0.34, 0.80), yaw=110.0, pv=0.0, lift=0.08, pitch=0.0, kup=0.8),
        pel=dict(z=-0.46, yaw=24.0))
    c.k(26, ease="io", pel=dict(x=0.05, y=0.10, z=-0.30, pitch=14.0, yaw=0.0), spine=dict(pitch=6.0, yaw=0.0),
        neck=dict(pitch=-6.0), foot_l=F(yaw=-4.0),
        foot_r=F(at=(0.0, 0.45), yaw=50.0, pv=1.0, lift=0.07, pitch=-10.0, kup=0.3), **_guard_hands())
    c.k(31, ease="in", foot_r=F(**F_FOOT_R), pel=dict(x=0.0, y=-0.02, z=-0.12, pitch=4.0, yaw=-18.0),
        spine=dict(pitch=5.0, yaw=-6.0), neck=dict(pitch=-5.0))
    c.k(36, ease="io", base=True)
    return c


@clip("f_crescent_kick")
def f_crescent_kick():
    # outside crescent kick (bai lian): the weight moves onto the lead leg, the straight right leg swings up from the
    # left, sweeps across the front at its highest point (contact: the fan of fire leaves the foot) and falls away to
    # the right; the arms open to balance, then the foot sets back down behind
    c = Clip("f_crescent_kick", 30, "f_stance", contact=14, priority="P1", technique="outside crescent kick (bai lian)",
             hands=("palm", "palm"), metric="fwd_ball_r", offsets=off(pel=1.0, spine=0.5), antic=9, follow=18,
             no_balance=True)
    c.k(5, ease="io", pel=dict(x=0.07, y=0.10, z=-0.09, yaw=-2.0), spine=dict(yaw=2.0), foot_r=F(pv=1.0, pitch=-24.0),
        fing="palm",
        hand_l=_hw((0.38, 0.30, 1.36), (0.8, 0.3, 0.4), (0.0, 1.0, 0.0), (0.4, -0.6, -1.0)),
        hand_r=_hw((-0.38, 0.18, 1.34), (-0.8, 0.3, 0.4), (0.0, 1.0, 0.0), (-0.4, -0.6, -1.0)))
    c.k(10, ease="out", foot_r=F(at=(0.26, 0.52), yaw=20.0, pv=0.0, lift=0.78, pitch=10.0, kup=0.0),
        pel=dict(x=0.07, y=0.10, z=-0.06, pitch=-6.0, yaw=8.0), spine=dict(pitch=-3.0, yaw=-4.0), foot_l=F(yaw=-12.0))
    c.k(14, ease="lin", foot_r=F(at=(-0.06, 0.72), yaw=-10.0, pv=0.0, lift=1.00, pitch=14.0, kup=0.0),
        pel=dict(yaw=-6.0, pitch=-10.0), spine=dict(yaw=4.0, pitch=-4.0),
        hand_l=_hw((0.10, 0.62, 1.42), (0.3, 0.8, 0.4), (-0.6, 0.3, -0.6), (0.4, -0.3, -1.0)))
    c.k(18, ease="in", foot_r=F(at=(-0.50, 0.38), yaw=-50.0, pv=0.0, lift=0.62, pitch=6.0, kup=0.0),
        pel=dict(yaw=-18.0, pitch=-6.0), spine=dict(yaw=6.0, pitch=-2.0),
        hand_l=_hw((0.38, 0.30, 1.30), (0.8, 0.3, 0.4), (0.0, 1.0, 0.0), (0.4, -0.6, -1.0)))
    c.k(23, ease="in", foot_r=F(**F_FOOT_R), foot_l=F(yaw=-4.0),
        pel=dict(x=0.0, y=-0.02, z=-0.11, pitch=4.0, yaw=-20.0), spine=dict(pitch=5.0, yaw=-6.0), **_guard_hands())
    c.k(30, ease="io", base=True)
    return c


@clip("f_stomp")
def f_stomp():
    # stomp + palms down: the right knee rises to hip height as the palms lift to the chest, then the foot slams flat
    # and both palms press down beside the hips as the knees absorb (contact = impact); Ground Heat / Grounding
    c = Clip("f_stomp", 24, "f_stance", contact=9, priority="P1", technique="stomp with palms pressing down",
             hands=("palm", "palm"), offsets=off(), antic=5, follow=13, no_balance=True)
    c.k(5, ease="out", pel=dict(x=0.06, y=0.08, z=-0.05, yaw=-6.0), spine=dict(pitch=-3.0),
        foot_r=F(at=(-0.12, -0.10), yaw=-30.0, pv=0.0, lift=0.36, pitch=-6.0, kup=0.6), clav=dict(lift=5.0),
        hand_l=_hw((0.16, 0.30, 1.30), (-0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.12, 0.26, 1.28), (0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (-0.6, -0.2, -1.0)), fing="palm")
    c.k(9, ease="acc", foot_r=F(at=(-0.15, -0.16), yaw=-36.0, pv=0.0, lift=0.0, pitch=0.0),
        pel=dict(x=0.0, y=0.02, z=-0.17, pitch=8.0, yaw=-18.0), spine=dict(pitch=6.0), clav=dict(lift=-5.0),
        neck=dict(pitch=-8.0),
        hand_l=_hw((0.22, 0.20, 0.86), (-0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (0.7, -0.2, -0.8)),
        hand_r=_hw((-0.20, 0.10, 0.84), (0.2, 0.9, 0.0), (0.0, 0.0, -1.0), (-0.7, -0.2, -0.8)))
    c.hold(11)
    c.k(16, ease="out", pel=dict(z=-0.12, pitch=5.0, yaw=-20.0), spine=dict(pitch=5.0), clav=dict(lift=0.0),
        neck=dict(pitch=-5.0), **_guard_hands())
    c.k(19, ease="io", foot_r=F(pv=1.0, lift=0.03, pitch=-10.0))
    c.k(21, ease="in", foot_r=F(**F_FOOT_R))
    c.k(24, ease="io", base=True)
    return c


@clip("f_dash")
def f_dash():
    # lunge dash: a quick coil, then a long low lunge - the lead knee drives forward, the rear leg extends and leaves
    # the floor, the lead fist reaches out along the line (Flare Dash, Shimmer Step, Arc Step)
    c = Clip("f_dash", 24, "f_stance", contact=8, priority="P1", technique="long lunge dash",
             hands=("fist", "fist"), metric="fwd_hand_l", offsets=off(hand_l=0.0), antic=3, follow=13, no_balance=True)
    c.k(3, ease="io", pel=dict(dz=-0.05, dy=-0.03, dpitch=4.0), hand_l=H(dp=(0.0, -0.03, -0.02)))
    c.k(8, ease="in3", pel=dict(x=0.04, y=0.20, z=-0.22, pitch=24.0, yaw=-26.0), spine=dict(pitch=6.0, yaw=-4.0),
        neck=dict(pitch=-24.0), clav_l=dict(prot=10.0),
        foot_r=F(pv=1.0, lift=0.10, pitch=-40.0, move=(0.0, -0.06), kup=-0.2),
        hand_l=HS((0.0, 1.0, -0.05), ext=0.98, f=(0.0, 1.0, 0.0), m=(-0.1, 0.0, -1.0), e=(0.4, 0.0, -1.0)),
        hand_r=_hw((-0.26, -0.20, 1.02), (0.0, -0.4, -0.9), (0.9, 0.0, 0.0), (-0.4, 0.4, -1.0)))
    c.hold(11)
    c.k(17, ease="out", pel=dict(x=0.0, y=-0.02, z=-0.11, pitch=4.0, yaw=-20.0), spine=dict(pitch=5.0, yaw=-6.0),
        neck=dict(pitch=-5.0), clav_l=dict(prot=4.0), foot_r=F(**F_FOOT_R), **_guard_hands())
    c.k(24, ease="io", base=True)
    return c


@clip("f_hop")
def f_hop():
    # jump with palms down: crouch with the palms rising, then both palms thrust down by the hips (the blast pushes
    # the body up) as the feet leave the floor at contact; ends airborne
    st = BASES["air"]
    c = Clip("f_hop", 30, "f_stance", contact=8, priority="P1", technique="blast jump, palms thrust down",
             hands=("palm", "palm"), end_pose="air", base_end=None, metric="low_hand_r",
             offsets=off(hand_l=0.0, hand_r=0.0, pel=0.0, spine=0.0),
             antic=4, follow=14, no_balance=True)
    c.k(3, ease="io", pel=dict(z=-0.20, pitch=12.0, yaw=-8.0), spine=dict(pitch=6.0), neck=dict(pitch=-8.0),
        foot_r=F(pitch=0.0), fing="palm",
        hand_l=_hw((0.20, 0.24, 1.22), (0.0, 0.6, 0.8), (0.0, 0.0, -1.0), (0.6, -0.3, -1.0)),
        hand_r=_hw((-0.20, 0.20, 1.20), (0.0, 0.6, 0.8), (0.0, 0.0, -1.0), (-0.6, -0.3, -1.0)))
    c.k(7, ease="io", pel=dict(z=-0.01, pitch=0.0, yaw=-4.0), spine=dict(pitch=-2.0), neck=dict(pitch=-2.0),
        foot_l=F(pv=1.0, pitch=-30.0), foot_r=F(pv=1.0, pitch=-30.0))
    c.k(8, ease="lin", pel=dict(dz=0.01), foot_l=F(lift=0.015, pitch=-36.0), foot_r=F(lift=0.015, pitch=-36.0),
        hand_l=_hw((0.26, 0.04, 0.78), (0.1, 0.2, -1.0), (0.0, 0.2, -1.0), (0.6, -0.3, -0.6)),
        hand_r=_hw((-0.26, 0.02, 0.78), (-0.1, 0.2, -1.0), (0.0, 0.2, -1.0), (-0.6, -0.3, -0.6)))
    c.k(10, ease="lin", foot_l=F(lift=0.05, pitch=-34.0), foot_r=F(lift=0.05, pitch=-34.0))
    c.k(18, ease="out", pel=st["pel"], spine=st["spine"], neck=st["neck"],
        foot_l=F(at=(0.13, 0.08), yaw=0.0, pv=1.0, lift=0.26, pitch=-26.0, kup=0.3),
        foot_r=F(at=(-0.13, -0.10), yaw=-20.0, pv=1.0, lift=0.20, pitch=-30.0, kup=0.3))
    c.k(30, ease="io", pel=st["pel"], spine=st["spine"], neck=st["neck"],
        foot_l=F(at=(0.13, 0.05), yaw=6.0, pv=1.0, lift=0.20, pitch=-22.0, kup=0.3),
        foot_r=F(at=(-0.13, -0.06), yaw=-6.0, pv=1.0, lift=0.16, pitch=-26.0, kup=0.3),
        hand_l=HW(p=(0.36, 0.10, 1.22), f=(0.8, 0.2, -0.3), m=(0.0, 0.0, -1.0), e=(0.3, -1.0, -0.3)),
        hand_r=HW(p=(-0.36, 0.06, 1.20), f=(-0.8, 0.2, -0.3), m=(0.0, 0.0, -1.0), e=(-0.3, -1.0, -0.3)))
    return c


@clip("f_needle")
def f_needle():
    # sword-finger thrust (Blue): minimal wind-up, the lead sword fingers pierce straight out at eye level on a
    # narrow line, the rear hand chambered at the hip; a longer, still hold at contact (concentrated heat)
    c = Clip("f_needle", 20, "f_stance", contact=8, priority="P1", technique="sword-finger thrust",
             hands=("sword", "sword"), strike="hand_l", offsets=off(hand_l=0.0, pel=1.5, spine=0.75), antic=3, follow=14)
    c.k(3, ease="io", pel=dict(dy=-0.015, dz=-0.015, dyaw=4.0), fing="sword",
        hand_l=_hw((0.12, 0.34, 1.34), (-0.1, 0.9, 0.3), (-1.0, 0.0, 0.0), (0.4, -0.3, -1.0)),
        hand_r=_hw((-0.20, -0.06, 0.94), (0.1, 1.0, 0.0), (0.0, 0.0, 1.0), (-0.4, -1.0, 0.0)))
    c.k(8, ease="in3", pel=dict(dy=0.06, dz=-0.03, dyaw=-12.0, dpitch=3.0), spine=dict(dyaw=-4.0),
        clav_l=dict(prot=12.0, lift=-2.0), foot_r=F(pitch=-14.0),
        hand_l=HS((-0.04, 1.0, 0.10), ext=0.99, f=(-0.04, 1.0, 0.1), m=(-1.0, 0.0, 0.0), e=(0.3, 0.0, -1.0)))
    c.hold(12)
    c.k(16, ease="out", pel=dict(dy=-0.06, dz=0.03, dyaw=8.0, dpitch=-3.0), clav_l=dict(prot=4.0, lift=0.0),
        foot_r=F(pitch=-9.0), **_guard_hands())
    c.k(20, ease="io", base=True)
    return c


@clip("l_redirect")
def l_redirect():
    # Return Current: the lead sword fingers reach out and meet the bolt (catch), the arm draws it down in a half
    # circle across the lower belly (dan tian) as the weight sinks and the waist turns, and the rear arm extends and
    # sends it back out along the line (release); back to the guard
    c = Clip("l_redirect", 36, "guard", contacts=[10, 26], contact=10, priority="P1",
             technique="redirect through the lower belly (catch, release)", hands=("sword", "sword"),
             offsets=off(hand_l=0.0, hand_r=0.0), antic=6, follow=30)
    c.k(6, ease="io", pel=dict(dy=0.02, dyaw=-4.0), clav_l=dict(prot=8.0), fing="sword",
        hand_l=_hw((0.08, 0.62, 1.40), (0.0, 1.0, 0.05), (-1.0, 0.0, 0.0), (0.4, -0.1, -1.0)))
    c.k(10, ease="in", hand_l=_hw((0.06, 0.70, 1.40), (0.0, 1.0, 0.05), (-1.0, 0.0, 0.0), (0.4, -0.1, -1.0)))
    c.k(12, ease="lin")
    c.k(17, ease="io", pel=dict(dy=-0.04, dz=-0.07, yaw=-44.0), spine=dict(yaw=-8.0, pitch=4.0), clav_l=dict(prot=0.0),
        hand_l=_hw((-0.02, 0.26, 0.96), (-0.6, 0.4, -0.6), (0.0, -0.3, -0.9), (0.6, -0.2, -1.0)),
        hand_r=_hw((-0.20, 0.10, 1.22), (0.1, 0.8, 0.5), (0.9, 0.0, 0.3), (-0.4, -0.4, -1.0)), path="arc")
    c.k(21, ease="io", pel=dict(dz=0.0, yaw=-30.0), spine=dict(yaw=-2.0),
        hand_l=_hw((-0.18, 0.04, 0.92), (-0.4, -0.3, -0.8), (0.4, 0.4, -0.8), (0.6, 0.3, -1.0)),
        hand_r=_hw((-0.12, 0.30, 1.30), (0.1, 0.9, 0.4), (0.9, 0.0, 0.3), (-0.4, -0.4, -1.0)), path="arc")
    c.k(26, ease="in3", pel=dict(dy=0.08, dz=-0.02, yaw=-4.0, pitch=5.0), spine=dict(yaw=6.0, pitch=2.0),
        clav_r=dict(prot=12.0),
        hand_r=HS((0.05, 1.0, 0.08), ext=0.99, f=(0.05, 1.0, 0.08), m=(1.0, 0.0, 0.0), e=(-0.3, 0.0, -1.0)),
        hand_l=_hw((0.18, 0.00, 0.96), (0.1, 1.0, 0.0), (0.0, 0.0, 1.0), (0.4, -1.0, 0.0)))
    c.hold(28)
    c.k(36, ease="io", base=True)
    return c


@clip("l_skybreak")
def l_skybreak():
    # Skybreak: the lead sword fingers rise straight up to the sky, a held 6-frame pause at the top (the body
    # stretched, trembling), then the arm slashes down to point at the target as the knee drops deep
    c = Clip("l_skybreak", 36, "l_charge", base_end="f_stance", contact=24, priority="P1",
             technique="sky strike (arm to the sky, slash down)", hands=("sword", "sword"), metric="fwd_hand_l",
             offsets=off(hand_l=0.0), antic=16, follow=29, no_balance=True)
    c.k(8, ease="io", pel=dict(x=0.0, y=0.0, z=-0.04, pitch=-2.0, yaw=-22.0), spine=dict(pitch=-6.0, yaw=-2.0),
        neck=dict(pitch=10.0), clav_l=dict(lift=20.0), foot_r=F(pitch=-18.0),
        hand_l=_hw((0.16, 0.12, 2.00), (0.0, 0.05, 1.0), (-1.0, 0.0, 0.0), (0.6, 0.0, -0.3)),
        hand_r=_hw((-0.20, -0.08, 0.94), (0.1, 1.0, 0.0), (0.0, 0.0, 1.0), (-0.4, -1.0, 0.0)), fing="sword")
    c.k(18, ease="lin", pel=dict(dz=0.005), hand_l=H(dp=(0.0, 0.0, 0.005)))
    c.k(24, ease="acc", pel=dict(x=0.03, y=0.10, z=-0.26, pitch=12.0, yaw=-26.0), spine=dict(pitch=6.0, yaw=-4.0),
        neck=dict(pitch=-8.0), clav_l=dict(lift=-2.0, prot=10.0), foot_r=F(pitch=0.0),
        hand_l=HS((-0.02, 1.0, -0.12), ext=0.99, f=(-0.02, 1.0, -0.12), m=(-1.0, 0.0, 0.0), e=(0.3, 0.0, -1.0)),
        path="arc")
    c.hold(26)
    c.k(32, ease="out", pel=dict(x=0.0, y=-0.02, z=-0.11, pitch=4.0, yaw=-20.0), spine=dict(pitch=5.0, yaw=-6.0),
        neck=dict(pitch=-5.0), clav_l=dict(lift=0.0, prot=4.0), foot_r=F(**F_FOOT_R), **_guard_hands())
    c.k(36, ease="io", base=True)
    c.tremble(amp=0.8, f0=10, f1=19, ramp=3)
    return c


@clip("l_ground")
def l_ground():
    # palm to the ground (Ground Current): the right palm lifts high, the body folds down over the bent lead knee and
    # the palm slaps flat on the floor ahead of the lead foot (the current runs along the ground)
    c = Clip("l_ground", 30, "f_stance", contact=14, priority="P1", technique="palm strike to the ground",
             hands=("fist", "palm"), metric="low_hand_r", offsets=off(hand_r=0.0), antic=7, follow=20, no_balance=True)
    c.k(7, ease="io", pel=dict(dz=0.01, dyaw=-6.0, dpitch=-4.0), spine=dict(dpitch=-6.0), clav_r=dict(lift=14.0),
        hand_r=_hw((-0.24, -0.04, 1.62), (0.1, -0.1, 1.0), (0.0, 1.0, 0.2), (-0.6, 0.2, 0.2)), fing_r="palm")
    c.k(14, ease="acc", pel=dict(x=0.04, y=0.16, z=-0.36, pitch=40.0, yaw=-14.0), spine=dict(pitch=24.0, yaw=0.0),
        neck=dict(pitch=-30.0), clav_r=dict(lift=-4.0, prot=10.0), foot_r=F(pitch=-20.0),
        hand_r=_hw((0.0, 0.66, 0.22), (0.0, 1.0, -0.3), (0.0, 0.2, -1.0), (-0.6, -0.2, -0.6)),
        hand_l=_hw((0.22, 0.30, 0.84), (0.0, 0.6, 0.8), (-0.9, 0.0, 0.3), (0.6, -0.4, -1.0)), path="arc")
    c.hold(16)
    c.k(23, ease="out", pel=dict(x=0.0, y=-0.02, z=-0.11, pitch=4.0, yaw=-20.0), spine=dict(pitch=5.0, yaw=-6.0),
        neck=dict(pitch=-5.0), clav_r=dict(lift=0.0, prot=2.0), foot_r=F(**F_FOOT_R), **_guard_hands())
    c.k(30, ease="io", base=True)
    return c


@clip("c_point")
def c_point():
    # point -> fist snap (Combustion): the lead sword fingers point at the spot, a brief aim, then the hand snaps shut
    # into a fist with a sharp jolt of the whole arm (the detonation)
    c = Clip("c_point", 24, "f_stance", contact=10, priority="P1", technique="point, then fist snap (detonate)",
             hands=("fist", "fist"), offsets=off(hand_l=0.0), antic=6, follow=15)
    c.k(4, ease="out", pel=dict(dy=0.03, dyaw=-8.0), clav_l=dict(prot=8.0), fing_l="sword",
        hand_l=HS((-0.04, 1.0, 0.08), ext=0.95, f=(-0.04, 1.0, 0.08), m=(-1.0, 0.0, 0.0), e=(0.3, 0.0, -1.0)))
    c.k(7, ease="lin", hand_l=H(dp=(0.0, 0.005, 0.0)))
    c.k(10, ease="in4", pel=dict(dy=-0.015, dz=-0.02), spine=dict(dpitch=3.0), clav_l=dict(prot=3.0, lift=-3.0),
        fing_l="fist", hand_l=H(dp=(0.0, -0.06, -0.03), f=(0.0, 1.0, 0.0), m=(-0.2, 0.0, -1.0)))
    c.hold(12)
    c.k(18, ease="out", pel=dict(dy=-0.015, dyaw=8.0), clav_l=dict(prot=4.0, lift=0.0), **_guard_hands())
    c.k(24, ease="io", base=True)
    return c
