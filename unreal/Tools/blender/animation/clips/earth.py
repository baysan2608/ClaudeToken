"""Earth clips - Hung Gar (MARTIAL_ARTS.md §2.1, §3.2): rooted horse stance, short heavy bridge-arm strikes, stomps."""
from ffa_dsl import BASES, HS, Clip, F, H, HW, apply_spec, both, copy_state

from . import clip


@clip("e_stance")
def e_stance():
    # sei ping ma with double tiger claws: heavy 2 s breath, iron-wire tension on the exhale, tiny knee pulses
    c = Clip("e_stance", 120, "e_stance", loop=True, priority="P0", technique="sei ping ma, double tiger claws",
             hands=("tiger", "tiger"))
    c.k(30, ease="sp", pel=dict(dz=-0.006), hand_l=H(dp=(0.0, 0.006, -0.004)), hand_r=H(dp=(0.0, 0.004, 0.002)))
    c.k(60, ease="sp", pel=dict(dz=0.004), hand_l=H(dp=(0.0, -0.004, 0.006)), hand_r=H(dp=(0.0, -0.002, -0.002)))
    c.k(90, ease="sp", pel=dict(dz=-0.006), hand_l=H(dp=(0.0, 0.005, -0.004)), hand_r=H(dp=(0.0, 0.003, 0.002)))
    c.breathe(depth=1.5, cycles=1)
    return c

# rest-of-stance landmarks (e_stance): feet at x +-0.37, y 0.02; pelvis z drop 0.30 -> shoulders ~1.14, chest ~1.10
E_FOOT_L = dict(at=(0.37, 0.02), yaw=8.0, pv=1.0)
E_FOOT_R = dict(at=(-0.37, 0.02), yaw=-8.0, pv=1.0)
FIST_V_R = dict(f=(0.0, 1.0, 0.0), m=(1.0, 0.0, 0.0))      # vertical fist (right): palm side faces in (+x)
PALM_FWD = dict(f=(0.0, 0.25, 1.0), m=(0.0, 1.0, -0.2))     # palm forward, fingers up


def _hw(p, o=None, e=None):
    o = o or {}
    return HW(p=p, f=o.get("f"), m=o.get("m"), e=e)


@clip("e_lift")
def e_lift():
    # stomp with the right foot (knee rises, foot slams flat, knees absorb), hands scoop from the knees and the palms
    # turn up and rise to the chest: the stone pops at contact
    c = Clip("e_lift", 24, "e_stance", contact=12, priority="P0", technique="stomp + rising scoop (lifting the bridge)",
             hands=("tiger", "tiger"), metric="high_hand_r", offsets={"pel": 1.5, "spine": 0.75, "hand_l": -1.0},
             antic=6)
    c.k(3, ease="out", pel=dict(dx=0.055, dz=0.015, dside=-3.0), spine=dict(dside=3.0),
        foot_r=F(pv=0.0, lift=0.14, pitch=-4.0, kup=0.4),
        hand_l=_hw((0.24, 0.30, 0.92), dict(f=(0.0, 1.0, -0.3), m=(0.0, 0.0, -1.0))),
        hand_r=_hw((-0.24, 0.26, 0.92), dict(f=(0.0, 1.0, -0.3), m=(0.0, 0.0, -1.0))), fing="palm")
    c.k(5, ease="acc", pel=dict(dx=-0.055, dz=-0.05, dside=3.0, pitch=8.0), spine=dict(dside=-3.0, pitch=6.0),
        foot_r=F(**E_FOOT_R), clav=dict(lift=-6.0),
        hand_l=_hw((0.20, 0.36, 0.74), dict(f=(0.0, 1.0, -0.5), m=(0.0, 0.0, -1.0))),
        hand_r=_hw((-0.20, 0.34, 0.74), dict(f=(0.0, 1.0, -0.5), m=(0.0, 0.0, -1.0))))
    c.k(7, ease="out", pel=dict(dz=-0.012), clav=dict(lift=-2.0),
        hand_l=_hw((0.18, 0.37, 0.70), dict(f=(0.0, 1.0, -0.2), m=(0.0, 0.2, 1.0))),
        hand_r=_hw((-0.18, 0.35, 0.70), dict(f=(0.0, 1.0, -0.2), m=(0.0, 0.2, 1.0))), fing="tiger")
    c.k(12, ease="in", pel=dict(z=-0.27, pitch=0.0), spine=dict(pitch=-4.0), clav=dict(lift=4.0),
        hand_l=_hw((0.15, 0.36, 1.21), dict(f=(0.0, 1.0, 0.25), m=(0.0, -0.15, 1.0)), e=(0.6, -0.4, -1.0)),
        hand_r=_hw((-0.15, 0.34, 1.21), dict(f=(0.0, 1.0, 0.25), m=(0.0, -0.15, 1.0)), e=(-0.6, -0.4, -1.0)))
    c.hold(14)
    c.k(19, ease="io", pel=dict(z=-0.295, pitch=3.0), spine=dict(pitch=-1.0), clav=dict(lift=-2.0),
        hand_l=_hw((0.12, 0.38, 1.17), dict(f=(-0.1, 0.4, 1.0), m=(-0.1, 1.0, -0.2))),
        hand_r=_hw((-0.19, 0.14, 1.02), dict(f=(0.05, 0.5, 1.0), m=(0.1, 1.0, -0.3))))
    c.k(24, ease="io", base=True)
    return c


@clip("e_strike")
def e_strike():
    # iron-bridge drive (tit kiu), short power from the root: sink and coil (hips turn right, the chest winds a little
    # further, the right fist chambers palm-up at the hip, the lead claw reaches out as the guide), the lead foot slides
    # forward and PLANTS before the strike, then the hips drive square-left and the weight surges onto the front leg
    # while the right forearm drives a vertical fist at chest height (elbow not locked) and the lead claw pulls back to
    # the chest (counter-pull).  After the contact the stance sinks a last centimetre on the exhale (rooting), then the
    # front foot gathers back into the horse in two unhurried stages.
    c = Clip("e_strike", 34, "e_stance", contact=11, priority="P0", technique="iron-bridge drive (tit kiu)",
             hands=("tiger", "fist"), strike="hand_r", offsets={"pel": 2.0, "spine": 1.0, "hand_l": -1.5}, antic=5,
             follow=17)
    c.k(5, ease="io", pel=dict(dz=-0.025, dy=-0.02, dyaw=-12.0, dside=2.0), spine=dict(dyaw=-8.0, dpitch=2.0),
        clav_r=dict(prot=-4.0),
        hand_l=_hw((0.14, 0.47, 1.18), dict(f=(-0.1, 0.4, 1.0), m=(-0.1, 1.0, -0.2)), e=(0.45, -0.1, -1.0)),
        hand_r=_hw((-0.22, -0.02, 0.97), dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, 1.0)), e=(-0.3, -1.0, -0.3)),
        fing_r="fist")
    c.k(7, ease="out", foot_l=F(pv=0.0, lift=0.02, move=(0.0, 0.07)), pel=dict(dy=0.03, dyaw=6.0))
    c.k(9, ease="in", foot_l=F(at=(0.36, 0.19), yaw=2.0, pv=1.0), pel=dict(dy=0.03, dz=-0.005, dyaw=10.0),
        spine=dict(dyaw=4.0))
    c.k(11, ease="in3", pel=dict(x=0.05, y=0.10, z=-0.255, yaw=16.0, pitch=6.0, side=-2.0),
        spine=dict(pitch=4.0, yaw=6.0, side=-1.0), clav_r=dict(prot=10.0, lift=-2.0), clav_l=dict(prot=-3.0),
        foot_r=F(yaw=-30.0),
        hand_l=_hw((0.15, 0.22, 1.12), dict(f=(-0.2, 0.3, 1.0), m=(-0.2, 1.0, -0.1))),
        hand_r=HS((0.06, 1.0, 0.0), ext=0.93, f=(0.0, 1.0, 0.0), m=(1.0, 0.0, 0.0), e=(-0.4, -0.2, -1.0)))
    c.hold(13)
    c.k(17, ease="out", pel=dict(dz=-0.012, dy=-0.012, dpitch=1.0), spine=dict(dpitch=1.0), clav_r=dict(prot=5.0),
        hand_r=_hw((-0.06, 0.55, 1.12), FIST_V_R, e=(-0.4, -0.3, -1.0)))
    c.k(23, ease="io", foot_l=F(pv=0.0, lift=0.02, move=(0.0, -0.09)),
        pel=dict(x=0.02, y=-0.03, z=-0.28, yaw=5.0, pitch=4.0, side=0.0), spine=dict(pitch=0.0, yaw=2.0, side=0.0),
        clav_r=dict(prot=0.0, lift=0.0), clav_l=dict(prot=0.0),
        hand_r=_hw((-0.16, 0.28, 1.03), FIST_V_R, e=(-0.4, -0.6, -1.0)),
        hand_l=_hw((0.11, 0.34, 1.15), dict(f=(-0.1, 0.3, 1.0), m=(-0.1, 1.0, -0.2))))
    c.k(27, ease="in", foot_l=F(**E_FOOT_L), foot_r=F(**E_FOOT_R), pel=dict(x=0.0, y=-0.07, z=-0.297, yaw=0.0, pitch=4.0),
        spine=dict(pitch=-0.5, yaw=0.0))
    c.k(34, ease="io", base=True)
    return c


@clip("e_heave")
def e_heave():
    # both hands under the mass at the knees, heave it overhead (back arches), then drive forward-down into a deep bow
    c = Clip("e_heave", 36, "e_stance", contact=18, priority="P0", technique="heave overhead, drive down into bow",
             hands=("tiger", "tiger"), metric="low_hand_r", offsets={"pel": 2.0, "spine": 1.0}, antic=12)
    c.k(6, ease="io", pel=dict(dz=-0.06, pitch=14.0, dy=-0.02), spine=dict(pitch=16.0), neck=dict(pitch=-14.0),
        hand_l=_hw((0.17, 0.40, 0.66), dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, 1.0)), e=(0.5, -0.3, -1.0)),
        hand_r=_hw((-0.17, 0.40, 0.66), dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, 1.0)), e=(-0.5, -0.3, -1.0)))
    c.k(12, ease="io", pel=dict(z=-0.20, pitch=-4.0, y=-0.11), spine=dict(pitch=-10.0), neck=dict(pitch=4.0),
        clav=dict(lift=28.0, prot=0.0),
        hand_l=_hw((0.15, 0.02, 1.80), dict(f=(-0.2, 0.3, 1.0), m=(0.0, 0.1, 1.0)), e=(0.8, 0.0, -0.4)),
        hand_r=_hw((-0.15, 0.02, 1.80), dict(f=(0.2, 0.3, 1.0), m=(0.0, 0.1, 1.0)), e=(-0.8, 0.0, -0.4)))
    c.k(15, ease="io", foot_l=F(at=(0.33, 0.27), yaw=2.0, pv=1.0))
    c.k(18, ease="in3", pel=dict(x=0.03, y=0.12, z=-0.33, pitch=16.0, yaw=4.0), spine=dict(pitch=16.0),
        neck=dict(pitch=-14.0), clav=dict(lift=4.0, prot=8.0), foot_r=F(yaw=-34.0),
        hand_l=_hw((0.15, 0.78, 0.96), dict(f=(0.0, 0.45, 1.0), m=(0.0, 1.0, -0.5)), e=(0.5, -0.2, -1.0)),
        hand_r=_hw((-0.15, 0.78, 0.96), dict(f=(0.0, 0.45, 1.0), m=(0.0, 1.0, -0.5)), e=(-0.5, -0.2, -1.0)))
    c.hold(20)
    c.k(27, ease="out", pel=dict(dy=-0.07, dz=0.02, pitch=8.0), spine=dict(pitch=6.0), neck=dict(pitch=-6.0),
        clav=dict(lift=0.0, prot=2.0), foot_l=F(pv=0.0, lift=0.02),
        hand_l=_hw((0.14, 0.45, 1.05), dict(f=(-0.1, 0.3, 1.0), m=(-0.1, 1.0, -0.2))),
        hand_r=_hw((-0.18, 0.25, 1.0), dict(f=(0.05, 0.5, 1.0), m=(0.1, 1.0, -0.3))))
    c.k(31, ease="io", foot_l=F(**E_FOOT_L), foot_r=F(**E_FOOT_R), pel=dict(x=0.0, y=-0.08, z=-0.30, yaw=0.0, pitch=4.0),
        spine=dict(pitch=-1.0))
    c.k(36, ease="io", base=True)
    return c


@clip("e_wall")
def e_wall():
    # Bulwark: right stomp at f4, both palms rise to face height at contact, elbows down
    c = Clip("e_wall", 24, "e_stance", contact=10, priority="P0", technique="stomp + double palms up (raise the wall)",
             hands=("palm", "palm"), metric="high_hand_l", offsets={"pel": 1.0, "spine": 0.5}, antic=4)
    c.k(2, ease="out", pel=dict(dx=0.05, dz=0.015, dside=-3.0), spine=dict(dside=3.0),
        foot_r=F(pv=0.0, lift=0.11, pitch=-3.0, kup=0.4),
        hand_l=_hw((0.22, 0.30, 0.98), dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, -1.0))),
        hand_r=_hw((-0.22, 0.28, 0.98), dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, -1.0))), fing="palm")
    c.k(4, ease="acc", pel=dict(dx=-0.05, dz=-0.045, dside=3.0), spine=dict(dside=-3.0), foot_r=F(**E_FOOT_R),
        clav=dict(lift=-6.0),
        hand_l=_hw((0.21, 0.34, 0.86), dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, -1.0))),
        hand_r=_hw((-0.21, 0.32, 0.86), dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, -1.0))))
    c.k(6, ease="out", pel=dict(dz=0.01), clav=dict(lift=-1.0))
    c.k(10, ease="in", pel=dict(z=-0.27, pitch=1.0), spine=dict(pitch=-4.0), clav=dict(lift=3.0),
        hand_l=_hw((0.14, 0.34, 1.37), PALM_FWD, e=(0.3, -0.2, -1.0)),
        hand_r=_hw((-0.14, 0.32, 1.37), PALM_FWD, e=(-0.3, -0.2, -1.0)))
    c.hold(12)
    c.k(18, ease="io", pel=dict(z=-0.295, pitch=3.0), spine=dict(pitch=-1.0), clav=dict(lift=-2.0),
        hand_l=_hw((0.11, 0.39, 1.19), dict(f=(-0.1, 0.25, 1.0), m=(-0.15, 1.0, -0.1))),
        hand_r=_hw((-0.19, 0.12, 1.02), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3))), fing="tiger")
    c.k(24, ease="io", base=True)
    return c


@clip("e_guard")
def e_guard():
    # bridge-arm guard held with iron-wire tension: two slow press-and-release breaths, a faint tremble while pressing
    c = Clip("e_guard", 96, "e_guard", loop=True, priority="P0", technique="bridge-arm guard, iron-wire tension",
             hands=("palm", "palm"))
    c.k(24, ease="sp", pel=dict(dz=-0.012), spine=dict(dpitch=2.0), clav=dict(dprot=2.0, dlift=-2.0),
        hand_l=H(dp=(0.012, 0.012, -0.004)), hand_r=H(dp=(-0.012, 0.012, -0.004)))
    c.k(48, ease="sp", pel=dict(dz=0.012), spine=dict(dpitch=-2.0), clav=dict(dprot=-2.0, dlift=2.0),
        hand_l=H(dp=(-0.012, -0.012, 0.004)), hand_r=H(dp=(0.012, -0.012, 0.004)))
    c.k(72, ease="sp", pel=dict(dz=-0.009), spine=dict(dpitch=1.5), clav=dict(dprot=1.5, dlift=-1.5),
        hand_l=H(dp=(0.009, 0.01, -0.003)), hand_r=H(dp=(-0.009, 0.01, -0.003)))
    c.breathe(depth=1.2, cycles=2)
    c.tremble(amp=0.5)
    return c


@clip("e_seize_loop")
def e_seize_loop():
    # embrace the mountain: a heavy mass held at the chest, knees pulsing under the load, strained tremble
    c = Clip("e_seize_loop", 72, "e_seize", loop=True, priority="P0", technique="embrace the mountain (holding a mass)",
             hands=("spread", "spread"))
    c.k(18, ease="sp", pel=dict(dz=-0.012, dpitch=1.0), hand_l=H(dp=(0.0, 0.004, -0.012)), hand_r=H(dp=(0.0, 0.004, -0.012)))
    c.k(36, ease="sp", pel=dict(dz=0.014, dpitch=-1.5), hand_l=H(dp=(0.0, -0.002, 0.016)), hand_r=H(dp=(0.0, -0.002, 0.016)))
    c.k(54, ease="sp", pel=dict(dz=-0.010, dpitch=1.0), hand_l=H(dp=(0.0, 0.002, -0.010)), hand_r=H(dp=(0.0, 0.002, -0.010)))
    c.breathe(depth=1.0, cycles=1)
    c.tremble(amp=1.4)
    return c


@clip("e_throw")
def e_throw():
    # from the hold: draw the mass in toward the belly (sit, the lead foot unweights), then a short slide into a bow and
    # both palms drive it away - butterfly palms, the lead (left) palm high, the rear palm low - as the hips turn in
    # behind the rear palm; the stance roots with a sinking exhale, then gathers back to the horse
    c = Clip("e_throw", 30, "e_seize", base_end="e_stance", contact=8, priority="P0",
             technique="double palm push / release of the held mass", hands=("palm", "palm"), metric="fwd_hand_l",
             offsets={"pel": 1.5, "spine": 0.75}, antic=4, follow=14)
    c.k(4, ease="io", pel=dict(dz=-0.025, dy=-0.04, pitch=2.0, dyaw=-6.0), spine=dict(pitch=1.0, dyaw=-4.0),
        foot_l=F(pv=0.0, lift=0.02),
        hand_l=_hw((0.15, 0.25, 1.14), dict(f=(-0.3, 0.4, 0.9), m=(-0.5, 0.8, 0.0)), e=(0.7, -0.1, -0.7)),
        hand_r=_hw((-0.15, 0.24, 1.08), dict(f=(0.3, 0.4, 0.9), m=(0.5, 0.8, 0.0)), e=(-0.7, -0.1, -0.7)),
        fing="palm")
    c.k(8, ease="in3", pel=dict(x=0.035, y=0.065, z=-0.255, pitch=7.0, yaw=8.0, side=-2.0),
        spine=dict(pitch=5.0, yaw=3.0), clav=dict(lift=2.0, prot=9.0), foot_l=F(at=(0.37, 0.13), yaw=4.0, pv=1.0),
        foot_r=F(yaw=-26.0),
        hand_l=_hw((0.12, 0.77, 1.20), PALM_FWD, e=(0.45, -0.2, -1.0)),
        hand_r=_hw((-0.10, 0.73, 1.00), dict(f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.3)), e=(-0.45, -0.2, -1.0)))
    c.hold(10)
    c.k(14, ease="out", pel=dict(dy=-0.02, dz=-0.012), spine=dict(dpitch=1.0), clav=dict(prot=5.0),
        hand_l=_hw((0.13, 0.66, 1.17), PALM_FWD, e=(0.45, -0.2, -1.0)),
        hand_r=_hw((-0.12, 0.60, 1.00), dict(f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.3)), e=(-0.45, -0.2, -1.0)))
    c.k(20, ease="io", pel=dict(dy=-0.05, dz=-0.008, yaw=2.0, side=0.0), clav=dict(prot=2.0, lift=0.0),
        foot_l=F(pv=0.0, lift=0.02),
        hand_l=_hw((0.13, 0.46, 1.13), dict(f=(-0.1, 0.3, 1.0), m=(-0.1, 1.0, -0.2))),
        hand_r=_hw((-0.17, 0.28, 1.02), dict(f=(0.05, 0.45, 1.0), m=(0.1, 1.0, -0.3))))
    c.k(24, ease="in", foot_l=F(**E_FOOT_L), foot_r=F(**E_FOOT_R), pel=dict(x=0.0, y=-0.075, z=-0.30, pitch=4.0, yaw=0.0),
        spine=dict(pitch=-1.0, yaw=0.0), fing="tiger")
    c.k(30, ease="io", base=True)
    return c


@clip("e_pour")
def e_pour():
    # press the mountain down: raise the molten mass, turn the palms over, press it down and sweep it forward along
    # the ground in a deep forward bow (Magma Surge, Slag Wave)
    c = Clip("e_pour", 30, "e_stance", contact=16, priority="P0", technique="raise, press down and sweep forward",
             hands=("palm", "palm"), metric="low_hand_r", offsets={"pel": 1.5, "spine": 0.75}, antic=10)
    c.k(6, ease="io", pel=dict(dy=-0.04, dz=0.03, pitch=-2.0), spine=dict(pitch=-5.0), clav=dict(lift=6.0),
        hand_l=_hw((0.16, 0.30, 1.30), dict(f=(0.0, 1.0, 0.1), m=(0.0, 0.0, 1.0)), e=(0.6, -0.3, -1.0)),
        hand_r=_hw((-0.16, 0.30, 1.30), dict(f=(0.0, 1.0, 0.1), m=(0.0, 0.0, 1.0)), e=(-0.6, -0.3, -1.0)),
        fing="cup")
    c.k(10, ease="io", hand_l=_hw((0.16, 0.40, 1.27), dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, -1.0))),
        hand_r=_hw((-0.16, 0.40, 1.27), dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, -1.0))), fing="palm",
        foot_l=F(pv=0.0, lift=0.02))
    c.k(16, ease="in3", pel=dict(x=0.03, y=0.10, z=-0.34, pitch=18.0), spine=dict(pitch=18.0), neck=dict(pitch=-16.0),
        clav=dict(lift=-2.0, prot=8.0), foot_l=F(at=(0.36, 0.20), yaw=2.0, pv=1.0), foot_r=F(yaw=-30.0),
        hand_l=_hw((0.19, 0.80, 0.60), dict(f=(0.0, 1.0, -0.15), m=(0.0, 0.1, -1.0)), e=(0.6, -0.2, -0.8)),
        hand_r=_hw((-0.19, 0.80, 0.60), dict(f=(0.0, 1.0, -0.15), m=(0.0, 0.1, -1.0)), e=(-0.6, -0.2, -0.8)))
    c.hold(18)
    c.k(24, ease="out", pel=dict(dy=-0.07, dz=0.03, pitch=8.0), spine=dict(pitch=5.0), neck=dict(pitch=-5.0),
        clav=dict(lift=0.0, prot=2.0), foot_l=F(pv=0.0, lift=0.02),
        hand_l=_hw((0.14, 0.45, 1.02), dict(f=(-0.1, 0.3, 1.0), m=(-0.1, 1.0, -0.2))),
        hand_r=_hw((-0.18, 0.25, 0.98), dict(f=(0.05, 0.5, 1.0), m=(0.1, 1.0, -0.3))), fing="tiger")
    c.k(27, ease="io", foot_l=F(**E_FOOT_L), foot_r=F(**E_FOOT_R), pel=dict(x=0.0, y=-0.075, z=-0.30, pitch=4.0),
        spine=dict(pitch=-1.0), neck=dict(pitch=-4.0))
    c.k(30, ease="io", base=True)
    return c


# ================================================================================================ P1
def _eoff(**kw):
    d = {"pel": 2.0, "spine": 1.0, "hand_l": -1.0, "hand_r": -1.0, "neck": -1.5}
    d.update(kw)
    return d


GUARD_CLAW_L = dict(f=(-0.1, 0.25, 1.0), m=(-0.15, 1.0, -0.1))      # lead tiger claw, palm heel forward
CHAMBER_R = dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, 1.0))             # fist / claw chambered palm-up at the hip


def _step_bow(c, t_lift, t_land, y=0.17):
    c.k(t_lift, ease="io", foot_l=F(pv=0.0, lift=0.03, kup=0.2))
    c.k(t_land, ease="in", foot_l=F(at=(0.37, y), yaw=2.0, pv=1.0))


def _step_home(c, t_lift, t_land):
    c.k(t_lift, ease="io", foot_l=F(pv=0.0, lift=0.025))
    c.k(t_land, ease="in", foot_l=F(**E_FOOT_L), foot_r=F(**E_FOOT_R))


@clip("e_thrust")
def e_thrust():
    # tiger claw thrust (fu jow): the rear hand chambers palm-up at the hip while the waist coils, the lead foot
    # slides into a bow, the hips square and the claw drives forward palm-heel first, opening into the claw at contact
    c = Clip("e_thrust", 24, "e_stance", contact=12, priority="P1", technique="tiger claw thrust (fu jow)",
             hands=("tiger", "tiger"), strike="hand_r", offsets=_eoff(hand_r=0.0, hand_l=-1.5), antic=5, follow=16)
    c.k(5, ease="io", pel=dict(dz=-0.02, dy=-0.02, yaw=-12.0), spine=dict(yaw=-6.0),
        hand_r=_hw((-0.21, 0.00, 0.96), CHAMBER_R, e=(-0.3, -1.0, -0.2)), fing_r="fist",
        hand_l=_hw((0.10, 0.46, 1.14), GUARD_CLAW_L, e=(0.45, -0.1, -1.0)))
    _step_bow(c, 7, 9)
    c.k(12, ease="in3", pel=dict(x=0.04, y=0.09, z=-0.24, pitch=6.0, yaw=8.0), spine=dict(pitch=3.0, yaw=6.0),
        clav_r=dict(prot=10.0, lift=-2.0), foot_r=F(yaw=-34.0), fing_r="tiger",
        hand_r=HS((0.12, 1.0, 0.05), ext=0.95, f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.2), e=(-0.4, 0.0, -1.0)),
        hand_l=_hw((0.14, 0.22, 1.12), dict(f=(-0.2, 0.3, 1.0), m=(-0.2, 1.0, -0.1)), e=(0.5, -0.4, -1.0)))
    c.hold(14)
    c.k(18, ease="out", pel=dict(dy=-0.05, dz=-0.02, yaw=0.0), spine=dict(yaw=0.0), clav_r=dict(prot=0.0, lift=0.0),
        hand_r=_hw((-0.15, 0.30, 1.04), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3)), e=(-0.3, -1.0, -0.4)))
    _step_home(c, 19, 21)
    c.k(24, ease="io", base=True)
    return c


@clip("e_ground_rise")
def e_ground_rise():
    # lifting the bridge: drop deep into the horse, the palms reach down by the knees, turn up and lift sharply to
    # chest height as the legs drive (the spikes rise from the ground)
    c = Clip("e_ground_rise", 30, "e_stance", contact=15, priority="P1", technique="lifting the bridge (tok kiu)",
             hands=("palm", "palm"), metric="high_hand_r", offsets=_eoff(hand_l=0.0, hand_r=0.0), antic=8, follow=20)
    c.k(5, ease="io", pel=dict(z=-0.36, pitch=18.0), spine=dict(pitch=14.0), neck=dict(pitch=-16.0),
        clav=dict(lift=-4.0),
        hand_l=_hw((0.20, 0.42, 0.62), dict(f=(0.0, 1.0, -0.3), m=(0.0, 0.0, -1.0)), e=(0.6, -0.3, -1.0)),
        hand_r=_hw((-0.20, 0.42, 0.62), dict(f=(0.0, 1.0, -0.3), m=(0.0, 0.0, -1.0)), e=(-0.6, -0.3, -1.0)), fing="palm")
    c.k(9, ease="io", pel=dict(z=-0.37, pitch=16.0),
        hand_l=_hw((0.19, 0.44, 0.58), dict(f=(0.0, 1.0, -0.1), m=(0.0, 0.1, 1.0)), e=(0.6, -0.3, -1.0)),
        hand_r=_hw((-0.19, 0.44, 0.58), dict(f=(0.0, 1.0, -0.1), m=(0.0, 0.1, 1.0)), e=(-0.6, -0.3, -1.0)))
    c.k(15, ease="in", pel=dict(z=-0.27, pitch=0.0), spine=dict(pitch=-4.0), neck=dict(pitch=-2.0), clav=dict(lift=5.0),
        hand_l=_hw((0.17, 0.38, 1.16), dict(f=(0.0, 1.0, 0.2), m=(0.0, -0.1, 1.0)), e=(0.6, -0.5, -1.0)),
        hand_r=_hw((-0.17, 0.38, 1.16), dict(f=(0.0, 1.0, 0.2), m=(0.0, -0.1, 1.0)), e=(-0.6, -0.5, -1.0)))
    c.hold(17)
    c.k(24, ease="io", pel=dict(z=-0.295, pitch=3.0), spine=dict(pitch=-1.0), clav=dict(lift=-2.0),
        hand_l=_hw((0.11, 0.39, 1.16), dict(f=(-0.1, 0.25, 1.0), m=(-0.15, 1.0, -0.1))),
        hand_r=_hw((-0.19, 0.12, 1.00), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3))), fing="tiger")
    c.k(30, ease="io", base=True)
    return c


@clip("e_ground_slap")
def e_ground_slap():
    # hammer palm to the ground: the right palm winds up high behind the head while the weight loads the right leg,
    # then the body folds forward and down into a low lead-side drop and the palm slaps the floor ahead of the lead foot
    c = Clip("e_ground_slap", 30, "e_stance", contact=14, priority="P1", technique="hammer palm to the ground",
             hands=("tiger", "palm"), metric="low_hand_r", offsets=_eoff(hand_r=0.0), antic=6, follow=19)
    c.k(6, ease="io", pel=dict(dx=-0.06, dz=0.01, pitch=-4.0, yaw=-14.0, side=-4.0), spine=dict(pitch=-8.0, yaw=-8.0),
        clav_r=dict(lift=14.0),
        hand_r=_hw((-0.24, -0.06, 1.62), dict(f=(0.1, -0.2, 1.0), m=(0.0, 1.0, 0.2)), e=(-0.6, 0.2, 0.2)), fing_r="palm",
        hand_l=_hw((0.14, 0.48, 1.08), GUARD_CLAW_L, e=(0.45, -0.1, -1.0)))
    c.k(10, ease="io", pel=dict(dx=0.0, z=-0.34, pitch=20.0, yaw=4.0, side=0.0), spine=dict(pitch=12.0, yaw=4.0),
        hand_r=_hw((-0.12, 0.40, 1.30), dict(f=(0.1, 0.8, 0.5), m=(0.0, 0.5, -0.8)), e=(-0.6, 0.0, -0.4)),
        path="arc")
    c.k(14, ease="acc", pel=dict(x=0.04, y=0.0, z=-0.45, pitch=38.0, yaw=10.0), spine=dict(pitch=30.0, yaw=6.0),
        neck=dict(pitch=-30.0), clav_r=dict(lift=-4.0, prot=10.0),
        hand_r=_hw((-0.02, 0.62, 0.20), dict(f=(0.0, 1.0, -0.3), m=(0.0, 0.2, -1.0)), e=(-0.6, -0.2, -0.6)),
        hand_l=_hw((0.22, 0.20, 0.80), dict(f=(0.0, 0.6, 0.8), m=(-0.9, 0.0, 0.3)), e=(0.6, -0.4, -1.0)), path="arc")
    c.hold(16)
    c.k(22, ease="out", pel=dict(x=0.0, z=-0.32, pitch=10.0, yaw=0.0), spine=dict(pitch=3.0, yaw=0.0),
        neck=dict(pitch=-6.0), clav_r=dict(lift=0.0, prot=0.0),
        hand_r=_hw((-0.19, 0.14, 1.00), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3))),
        hand_l=_hw((0.11, 0.39, 1.16), dict(f=(-0.1, 0.25, 1.0), m=(-0.15, 1.0, -0.1))), fing_r="tiger")
    c.k(30, ease="io", base=True)
    return c


@clip("e_sweep")
def e_sweep():
    # crane wing sweep (hok yik): the waist winds right and the right arm opens back like a wing, then the waist
    # pivots ~70 deg left and the arm sweeps flat across the front at chest height, finishing in a crane beak
    c = Clip("e_sweep", 30, "e_stance", contact=13, priority="P1", technique="crane wing sweep (hok yik)",
             hands=("tiger", "crane"), metric="fwd_hand_r", offsets=_eoff(hand_r=0.0), antic=6, follow=17)
    c.k(6, ease="io", pel=dict(dx=-0.04, dz=-0.01, yaw=-34.0), spine=dict(yaw=-14.0),
        hand_r=_hw((-0.58, -0.12, 1.14), dict(f=(-1.0, -0.2, 0.0), m=(0.0, 0.0, -1.0)), e=(0.0, -0.3, -1.0)),
        hand_l=_hw((0.16, 0.36, 1.12), dict(f=(-0.2, 0.3, 1.0), m=(-0.2, 1.0, -0.1)), e=(0.5, -0.4, -1.0)),
        fing_r="crane")
    c.k(13, ease="in3", pel=dict(dx=0.0, x=0.03, dz=-0.02, yaw=16.0), spine=dict(yaw=10.0, pitch=2.0),
        clav_r=dict(prot=12.0), foot_r=F(yaw=-2.0),
        hand_r=_hw((-0.02, 0.62, 1.12), dict(f=(0.6, 0.6, -0.4), m=(0.2, -0.3, -1.0)), e=(-0.3, 0.0, -1.0)),
        hand_l=_hw((0.18, 0.18, 1.08), dict(f=(-0.2, 0.3, 1.0), m=(-0.2, 1.0, -0.1)), e=(0.5, -0.4, -1.0)), path="arc")
    c.k(17, ease="out", pel=dict(yaw=30.0), spine=dict(yaw=14.0),
        hand_r=_hw((0.40, 0.36, 1.10), dict(f=(0.6, -0.3, -0.6), m=(0.5, -0.5, -0.6)), e=(-0.2, 0.3, -1.0)), path="arc")
    c.k(24, ease="io", pel=dict(x=0.0, yaw=4.0, z=-0.30), spine=dict(yaw=0.0, pitch=-1.0), clav_r=dict(prot=0.0),
        foot_r=F(**E_FOOT_R),
        hand_r=_hw((-0.17, 0.16, 1.00), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3))),
        hand_l=_hw((0.10, 0.39, 1.16), GUARD_CLAW_L), fing_r="tiger")
    c.k(30, ease="io", base=True)
    return c


@clip("e_push")
def e_push():
    # double tiger palms push (seung fu jow): sink and draw both palms to the chest, lunge the lead foot into a bow,
    # both palms drive forward at shoulder height (palm heels first, fingers clawed) with the whole body behind them
    c = Clip("e_push", 30, "e_stance", contact=10, priority="P1", technique="double tiger palms push",
             hands=("tiger", "tiger"), strike="hands", metric="fwd_hand_l", offsets=_eoff(hand_l=0.0, hand_r=0.0),
             antic=4, follow=16)
    c.k(4, ease="io", pel=dict(dz=-0.025, dy=-0.03, pitch=2.0), foot_l=F(pv=0.0, lift=0.03),
        hand_l=_hw((0.15, 0.20, 1.12), dict(f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.2)), e=(0.6, -0.4, -1.0)),
        hand_r=_hw((-0.15, 0.20, 1.12), dict(f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.2)), e=(-0.6, -0.4, -1.0)))
    c.k(7, ease="in", foot_l=F(at=(0.37, 0.20), yaw=2.0, pv=1.0), pel=dict(dy=0.05))
    c.k(10, ease="in3", pel=dict(x=0.03, y=0.10, z=-0.24, pitch=8.0, yaw=0.0), spine=dict(pitch=5.0),
        clav=dict(lift=1.0, prot=10.0), foot_r=F(yaw=-30.0),
        hand_l=HS((0.14, 1.0, 0.06), ext=0.94, f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.2), e=(0.5, 0.0, -1.0)),
        hand_r=HS((-0.14, 1.0, 0.06), ext=0.94, f=(0.0, 0.3, 1.0), m=(0.0, 1.0, -0.2), e=(-0.5, 0.0, -1.0)))
    c.hold(12)
    c.k(18, ease="out", pel=dict(dy=-0.06, dz=-0.02), clav=dict(prot=2.0, lift=0.0),
        hand_l=_hw((0.13, 0.46, 1.12), GUARD_CLAW_L), hand_r=_hw((-0.17, 0.28, 1.04), dict(f=(0.05, 0.45, 1.0), m=(0.1, 1.0, -0.3))))
    _step_home(c, 20, 23)
    c.k(30, ease="io", base=True)
    return c


@clip("e_sink")
def e_sink():
    # pressing the earth: palms rise a little with the inhale, then press straight down to knee height with a heavy
    # exhale as the horse drops 6 cm (Swallow / Quicksand / Melt Pit)
    c = Clip("e_sink", 24, "e_stance", contact=8, priority="P1", technique="pressing the earth (palms down)",
             hands=("palm", "palm"), metric="low_hand_r", offsets=_eoff(hand_l=0.0, hand_r=0.0), antic=4, follow=13)
    c.k(4, ease="io", pel=dict(dz=0.015), spine=dict(pitch=-3.0), clav=dict(lift=4.0),
        hand_l=_hw((0.18, 0.34, 1.16), dict(f=(-0.3, 0.9, 0.0), m=(0.0, 0.0, -1.0)), e=(0.7, -0.3, -0.6)),
        hand_r=_hw((-0.18, 0.34, 1.16), dict(f=(0.3, 0.9, 0.0), m=(0.0, 0.0, -1.0)), e=(-0.7, -0.3, -0.6)), fing="palm")
    c.k(8, ease="in3", pel=dict(z=-0.36, pitch=8.0), spine=dict(pitch=6.0), neck=dict(pitch=-8.0), clav=dict(lift=-5.0),
        hand_l=_hw((0.20, 0.38, 0.66), dict(f=(-0.35, 0.9, 0.0), m=(0.0, 0.0, -1.0)), e=(0.8, -0.2, -0.5)),
        hand_r=_hw((-0.20, 0.38, 0.66), dict(f=(0.35, 0.9, 0.0), m=(0.0, 0.0, -1.0)), e=(-0.8, -0.2, -0.5)))
    c.hold(10)
    c.k(17, ease="out", pel=dict(z=-0.31, pitch=5.0), spine=dict(pitch=0.0), neck=dict(pitch=-4.0), clav=dict(lift=-2.0),
        hand_l=_hw((0.12, 0.40, 1.08), GUARD_CLAW_L), hand_r=_hw((-0.19, 0.14, 0.96), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3))),
        fing="tiger")
    c.k(24, ease="io", base=True)
    return c


@clip("e_overhead_slam")
def e_overhead_slam():
    # double hammer down: rise onto the toes with both fists high overhead (back arched), then crash down into a deep
    # horse, both fists hammering to waist height in front (Rod Plant)
    c = Clip("e_overhead_slam", 30, "e_stance", contact=12, priority="P1", technique="double hammer fists down",
             hands=("fist", "fist"), metric="low_hand_r", offsets=_eoff(hand_l=0.0, hand_r=0.0), antic=6, follow=17,
             no_balance=True)
    c.k(6, ease="out", pel=dict(z=-0.10, pitch=-6.0), spine=dict(pitch=-10.0), neck=dict(pitch=6.0),
        clav=dict(lift=24.0), feet=F(pv=1.0, pitch=-22.0),
        hand_l=_hw((0.10, 0.02, 1.94), dict(f=(-0.3, 0.2, 1.0), m=(-0.9, 0.0, 0.2)), e=(0.8, 0.0, -0.3)),
        hand_r=_hw((-0.10, 0.02, 1.94), dict(f=(0.3, 0.2, 1.0), m=(0.9, 0.0, 0.2)), e=(-0.8, 0.0, -0.3)), fing="fist")
    c.k(12, ease="acc", pel=dict(z=-0.40, pitch=14.0), spine=dict(pitch=10.0), neck=dict(pitch=-14.0),
        clav=dict(lift=-4.0, prot=8.0), feet=F(pv=1.0, pitch=0.0),
        hand_l=_hw((0.08, 0.50, 0.74), dict(f=(0.0, 0.6, -0.8), m=(-1.0, 0.0, 0.0)), e=(0.7, 0.0, -0.6)),
        hand_r=_hw((-0.08, 0.50, 0.74), dict(f=(0.0, 0.6, -0.8), m=(1.0, 0.0, 0.0)), e=(-0.7, 0.0, -0.6)), path="arc")
    c.hold(14)
    c.k(22, ease="out", pel=dict(z=-0.31, pitch=5.0), spine=dict(pitch=0.0), neck=dict(pitch=-4.0), clav=dict(lift=-2.0),
        hand_l=_hw((0.11, 0.39, 1.14), GUARD_CLAW_L), hand_r=_hw((-0.19, 0.12, 0.98), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3))),
        fing="tiger")
    c.k(30, ease="io", base=True)
    return c


@clip("e_disc_flick")
def e_disc_flick():
    # crane-beak flick: the right hand loads across the body to the left shoulder as the waist coils left, then the
    # waist snaps back and the hand flicks out backhand to the front-right, the wrist cracking into a crane beak
    c = Clip("e_disc_flick", 20, "e_stance", contact=8, priority="P1", technique="crane-beak backhand flick",
             hands=("tiger", "crane"), strike="hand_r", offsets=_eoff(hand_r=0.0, pel=1.5, spine=0.75), antic=4, follow=12)
    c.k(4, ease="io", pel=dict(yaw=18.0, dz=-0.01), spine=dict(yaw=10.0),
        hand_r=_hw((0.16, 0.22, 1.18), dict(f=(0.6, -0.2, 0.6), m=(0.0, -1.0, 0.0)), e=(-0.2, -0.6, -1.0)),
        hand_l=_hw((0.18, 0.30, 1.04), dict(f=(-0.2, 0.3, 1.0), m=(-0.2, 1.0, -0.1)), e=(0.5, -0.4, -1.0)))
    c.k(8, ease="in3", pel=dict(yaw=-16.0, dz=-0.02), spine=dict(yaw=-10.0), clav_r=dict(prot=10.0),
        hand_r=HS((-0.55, 1.0, 0.05), ext=0.96, f=(-0.4, 0.6, -0.6), m=(0.0, -0.3, -1.0), e=(-0.3, 0.0, -1.0)),
        fing_r="crane")
    c.hold(10)
    c.k(15, ease="out", pel=dict(yaw=-4.0), spine=dict(yaw=-2.0), clav_r=dict(prot=0.0),
        hand_r=_hw((-0.19, 0.18, 1.00), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3))),
        hand_l=_hw((0.10, 0.40, 1.16), GUARD_CLAW_L), fing_r="tiger")
    c.k(20, ease="io", base=True)
    return c


@clip("e_chain_whirl")
def e_chain_whirl():
    # hanging back-fist (gwa choi): the right fist loads low across the body, swings up over the left side past the
    # head and hangs down onto the target in a 120 deg vertical arc, the waist turning with it; follow through low
    c = Clip("e_chain_whirl", 36, "e_stance", contact=14, priority="P1", technique="hanging back-fist arc (gwa choi)",
             hands=("tiger", "fist"), metric="fwd_hand_r", offsets=_eoff(hand_r=0.0), antic=8, follow=19)
    c.k(5, ease="io", pel=dict(yaw=14.0, dz=-0.02), spine=dict(yaw=8.0, pitch=4.0),
        hand_r=_hw((0.10, 0.16, 0.92), dict(f=(0.4, 0.3, -0.8), m=(0.8, 0.0, 0.2)), e=(-0.4, -0.4, -1.0)), fing_r="fist")
    c.k(10, ease="io", pel=dict(yaw=10.0, dz=0.0, pitch=-2.0), spine=dict(yaw=4.0, pitch=-6.0), clav_r=dict(lift=14.0),
        hand_r=_hw((0.04, 0.10, 1.72), dict(f=(0.0, 0.2, 1.0), m=(0.5, 0.0, 0.0)), e=(-0.6, 0.0, -0.3)), path="arc")
    c.k(14, ease="in3", pel=dict(yaw=-14.0, dz=-0.03, pitch=6.0), spine=dict(yaw=-6.0, pitch=6.0),
        clav_r=dict(lift=2.0, prot=10.0),
        hand_r=HS((-0.10, 1.0, -0.05), ext=0.96, f=(0.0, 0.8, -0.6), m=(0.0, -0.6, -0.8), e=(-0.3, 0.0, -1.0)), path="arc")
    c.k(18, ease="out", pel=dict(yaw=-20.0), spine=dict(yaw=-8.0), clav_r=dict(lift=0.0, prot=4.0),
        hand_r=_hw((-0.18, 0.40, 0.86), dict(f=(0.0, 0.5, -0.9), m=(0.6, -0.4, -0.5)), e=(-0.5, -0.2, -1.0)), path="arc")
    c.k(28, ease="io", pel=dict(yaw=0.0), spine=dict(yaw=0.0, pitch=-1.0),
        hand_r=_hw((-0.19, 0.12, 0.98), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3))), fing_r="tiger")
    c.k(36, ease="io", base=True)
    return c


@clip("e_lob")
def e_lob():
    # underhand scoop toss: sink and scoop the glob from low behind the right knee, then swing it forward-up and
    # release at chest height (palm up) as the legs rise
    c = Clip("e_lob", 24, "e_stance", contact=12, priority="P1", technique="underhand scoop toss",
             hands=("tiger", "cup"), metric="fwd_hand_r", offsets=_eoff(hand_r=0.0), antic=6, follow=16)
    c.k(6, ease="io", pel=dict(z=-0.36, pitch=14.0, yaw=-18.0), spine=dict(pitch=10.0, yaw=-8.0), neck=dict(pitch=-10.0),
        hand_r=_hw((-0.30, -0.06, 0.62), dict(f=(0.0, -0.6, -0.8), m=(0.0, 0.6, -0.6)), e=(-0.5, 0.3, -1.0)), fing_r="cup")
    c.k(12, ease="in", pel=dict(z=-0.27, pitch=0.0, yaw=6.0), spine=dict(pitch=-2.0, yaw=4.0), neck=dict(pitch=-4.0),
        clav_r=dict(lift=6.0, prot=8.0),
        hand_r=HS((0.0, 1.0, 0.18), ext=0.93, f=(0.0, 1.0, 0.3), m=(0.0, -0.2, 1.0), e=(-0.4, 0.0, -1.0)), path="arc")
    c.hold(13)
    c.k(18, ease="out", pel=dict(z=-0.29, yaw=0.0), spine=dict(pitch=-1.0, yaw=0.0), clav_r=dict(lift=0.0, prot=0.0),
        hand_r=_hw((-0.18, 0.18, 1.02), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3))), fing_r="tiger")
    c.k(24, ease="io", base=True)
    return c


def _make_p1_bases():
    st = BASES["e_stance"]
    BASES["e_magma"] = apply_spec(st, dict(
        pel=dict(dz=-0.02, pitch=3.0), spine=dict(pitch=4.0), clav_l=(2.0, 6.0), clav_r=(2.0, 6.0),
        hand_l=HW(p=(0.12, 0.50, 1.08), f=(-0.2, 0.9, 0.2), m=(-0.95, 0.1, 0.1), e=(0.6, -0.2, -1.0)),
        hand_r=HW(p=(-0.12, 0.50, 1.06), f=(0.2, 0.9, 0.2), m=(0.95, 0.1, 0.1), e=(-0.6, -0.2, -1.0)),
        fing_l="cup", fing_r="cup"), st)
    # iron wire: rooted horse, double bridge arms (forearms forward, fists, elbows sunk), tension
    BASES["e_skin"] = apply_spec(st, dict(
        pel=dict(dz=-0.01), spine=dict(pitch=0.0), clav_l=(-4.0, 2.0), clav_r=(-4.0, 2.0),
        hand_l=HW(p=(0.17, 0.36, 1.02), f=(-0.1, 0.9, 0.4), m=(-0.9, 0.0, 0.3), e=(0.3, -0.3, -1.0)),
        hand_r=HW(p=(-0.17, 0.36, 1.02), f=(0.1, 0.9, 0.4), m=(0.9, 0.0, 0.3), e=(-0.3, -0.3, -1.0)),
        fing_l="fist", fing_r="fist"), st)


_make_p1_bases()


@clip("e_magma_hold")
def e_magma_hold():
    c = Clip("e_magma_hold", 72, "e_magma", loop=True, priority="P1", technique="magma hold (cupped hands, heat tremble)",
             hands=("cup", "cup"), offsets={"neck": 0.0})
    c.k(18, ease="sp", pel=dict(dz=-0.012), hand_l=H(dp=(0.004, -0.008, -0.008)), hand_r=H(dp=(-0.004, -0.008, -0.008)))
    c.k(36, ease="sp", pel=dict(dz=0.006), hand_l=H(dp=(-0.004, 0.006, 0.008)), hand_r=H(dp=(0.004, 0.006, 0.008)))
    c.k(54, ease="sp", pel=dict(dz=-0.008))
    c.breathe(depth=1.0, cycles=1)
    c.tremble(amp=1.6, hz=13.0)
    return c


@clip("e_burrow")
def e_burrow():
    # sink and shoot: drop low behind the arms, dive forward flat (the body goes into the ground at contact = centre of
    # the burrow), come up through the legs
    c = Clip("e_burrow", 28, "e_stance", contact=14, priority="P1", technique="sink and shoot (burrow dive)",
             hands=("relaxed", "relaxed"), metric="fwd_head", offsets=_eoff(), antic=6, follow=20, no_balance=True)
    c.k(6, ease="io", pel=dict(z=-0.44, pitch=24.0), spine=dict(pitch=14.0), neck=dict(pitch=-18.0),
        hand_l=_hw((0.12, 0.40, 0.95), dict(f=(-0.2, 0.6, 0.8), m=(-0.8, 0.3, 0.0)), e=(0.6, -0.3, -1.0)),
        hand_r=_hw((-0.12, 0.40, 0.95), dict(f=(0.2, 0.6, 0.8), m=(0.8, 0.3, 0.0)), e=(-0.6, -0.3, -1.0)), fing="palm")
    c.k(14, ease="in", pel=dict(y=0.10, z=-0.46, pitch=46.0), spine=dict(pitch=14.0), neck=dict(pitch=-34.0),
        feet=F(pv=1.0, pitch=-18.0),
        hand_l=_hw((0.08, 0.98, 0.78), dict(f=(0.0, 1.0, 0.0), m=(-0.4, 0.0, -0.9)), e=(0.4, 0.0, 1.0)),
        hand_r=_hw((-0.08, 0.98, 0.78), dict(f=(0.0, 1.0, 0.0), m=(0.4, 0.0, -0.9)), e=(-0.4, 0.0, 1.0)))
    c.hold(16)
    c.k(22, ease="out", pel=dict(y=-0.04, z=-0.33, pitch=8.0), spine=dict(pitch=2.0), neck=dict(pitch=-6.0),
        feet=F(pv=1.0, pitch=0.0),
        hand_l=_hw((0.11, 0.39, 1.12), GUARD_CLAW_L), hand_r=_hw((-0.19, 0.12, 0.98), dict(f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3))),
        fing="tiger")
    c.k(28, ease="io", base=True)
    return c


@clip("e_stone_skin")
def e_stone_skin():
    # iron-wire tension (tit sin): rooted horse, double bridge arms; the arms press slowly forward under dynamic
    # tension with a long hissing exhale (trembling), then draw back on the inhale
    c = Clip("e_stone_skin", 96, "e_skin", loop=True, priority="P1", technique="iron-wire tension (tit sin)",
             hands=("fist", "fist"), offsets={"neck": 0.0})
    c.k(48, ease="sp", pel=dict(dz=-0.014), spine=dict(dpitch=2.0), clav=dict(dprot=5.0, dlift=-2.0),
        hand_l=H(dp=(-0.02, 0.10, 0.02)), hand_r=H(dp=(0.02, 0.10, 0.02)))
    c.breathe(depth=-1.4, cycles=1)          # chest sinks (hissing exhale) while the arms press out
    c.tremble(amp=0.9)
    return c
