"""Earth clips - Hung Gar (MARTIAL_ARTS.md §2.1, §3.2): rooted horse stance, short heavy bridge-arm strikes, stomps."""
from ffa_dsl import BASES, Clip, F, H, HW, both, copy_state

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
    # iron-bridge drive: coil, the lead foot slides forward into a bow stance, hips square at contact, the rear
    # forearm / vertical fist drives straight at chest height while the lead claw pulls back to the chest
    c = Clip("e_strike", 24, "e_stance", contact=8, priority="P0", technique="iron-bridge drive (tit kiu)",
             hands=("tiger", "fist"), strike="hand_r", offsets={"pel": 2.0, "spine": 1.0, "hand_l": -1.5}, antic=3)
    c.k(3, ease="io", pel=dict(dz=-0.02, dy=-0.015, dyaw=-10.0), spine=dict(dyaw=-6.0),
        foot_l=F(pv=0.0, lift=0.025),
        hand_l=_hw((0.12, 0.44, 1.14), dict(f=(-0.1, 0.35, 1.0), m=(-0.1, 1.0, -0.2))),
        hand_r=_hw((-0.21, 0.02, 0.96), dict(f=(0.0, 1.0, 0.0), m=(0.0, 0.0, 1.0)), e=(-0.3, -1.0, -0.3)),
        fing_r="fist")
    c.k(6, ease="out", foot_l=F(at=(0.37, 0.17), yaw=2.0, pv=1.0), pel=dict(dy=0.06, dyaw=6.0))
    c.k(8, ease="in3", pel=dict(x=0.04, y=0.085, z=-0.235, yaw=6.0, pitch=6.0), spine=dict(pitch=4.0, yaw=4.0),
        clav_r=dict(prot=8.0), foot_r=F(yaw=-38.0),
        hand_l=_hw((0.15, 0.22, 1.12), dict(f=(-0.2, 0.3, 1.0), m=(-0.2, 1.0, -0.1))),
        hand_r=_hw((-0.02, 0.66, 1.17), FIST_V_R, e=(-0.4, -0.2, -1.0)))
    c.hold(10)
    c.k(15, ease="out", pel=dict(dy=-0.05, dz=-0.02, yaw=0.0), spine=dict(yaw=0.0), clav_r=dict(prot=0.0),
        foot_l=F(pv=0.0, lift=0.02),
        hand_r=_hw((-0.12, 0.40, 1.10), FIST_V_R, e=(-0.4, -0.5, -1.0)))
    c.k(19, ease="io", foot_l=F(**E_FOOT_L), foot_r=F(**E_FOOT_R), pel=dict(x=0.0, y=-0.07, z=-0.295, pitch=4.0),
        spine=dict(pitch=-0.5))
    c.k(24, ease="io", base=True)
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
    # from the hold: draw the mass in, then both palms drive it away with a short slide into a bow stance
    c = Clip("e_throw", 24, "e_seize", base_end="e_stance", contact=8, priority="P0",
             technique="double palm push / release of the held mass", hands=("palm", "palm"), metric="fwd_hand_l",
             offsets={"pel": 1.5, "spine": 0.75}, antic=4)
    c.k(4, ease="io", pel=dict(dz=-0.02, dy=-0.035, pitch=2.0), spine=dict(pitch=1.0), foot_l=F(pv=0.0, lift=0.02),
        hand_l=_hw((0.15, 0.26, 1.13), dict(f=(-0.3, 0.4, 0.9), m=(-0.5, 0.8, 0.0)), e=(0.7, -0.1, -0.7)),
        hand_r=_hw((-0.15, 0.26, 1.13), dict(f=(0.3, 0.4, 0.9), m=(0.5, 0.8, 0.0)), e=(-0.7, -0.1, -0.7)),
        fing="palm")
    c.k(8, ease="in3", pel=dict(x=0.03, y=0.06, z=-0.25, pitch=7.0, yaw=0.0), spine=dict(pitch=5.0),
        clav=dict(lift=2.0, prot=9.0), foot_l=F(at=(0.37, 0.13), yaw=4.0, pv=1.0), foot_r=F(yaw=-26.0),
        hand_l=_hw((0.13, 0.76, 1.12), PALM_FWD, e=(0.45, -0.2, -1.0)),
        hand_r=_hw((-0.13, 0.76, 1.12), PALM_FWD, e=(-0.45, -0.2, -1.0)))
    c.hold(10)
    c.k(16, ease="out", pel=dict(dy=-0.06, dz=-0.02), clav=dict(prot=2.0), foot_l=F(pv=0.0, lift=0.02),
        hand_l=_hw((0.13, 0.48, 1.12), dict(f=(-0.1, 0.3, 1.0), m=(-0.1, 1.0, -0.2))),
        hand_r=_hw((-0.17, 0.30, 1.04), dict(f=(0.05, 0.45, 1.0), m=(0.1, 1.0, -0.3))))
    c.k(20, ease="io", foot_l=F(**E_FOOT_L), foot_r=F(**E_FOOT_R), pel=dict(x=0.0, y=-0.075, z=-0.30, pitch=4.0),
        spine=dict(pitch=-1.0), fing="tiger")
    c.k(24, ease="io", base=True)
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
