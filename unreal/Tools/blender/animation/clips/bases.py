"""Base stances (MARTIAL_ARTS.md §1.3 / §2): every clip starts and ends on one of these poses.

All positions are A-frame metres (x = character left, y = forward, z = up), origin on the floor between the feet.
Rest landmarks: shoulder joints (+-0.175, 0, 1.44), hip joints (+-0.095, 0, 0.92), ankles (+-0.105, -0.03, 0.085).
"""
from ffa_dsl import BASES, F, H, HW, apply_spec, neutral_state


def make(name, spec, parent=None):
    base = BASES[parent] if parent else neutral_state()
    BASES[name] = apply_spec(base, spec, base)
    return BASES[name]


def build_bases():
    # ---------------------------------------------------------------- idle: wu ji ready (feet shoulder width, soft knees)
    make("idle", dict(
        pel=(0.0, 0.0, -0.016, 0.5, 0.0, 0.0), spine=(0.5, 0.0, 0.0), neck=(-1.0, 0.0, 0.0), gaze=(0.75, 0.0, 4.0),
        clav_l=(-2.0, 0.0), clav_r=(-2.0, 0.0),
        foot_l=F(at=(0.145, -0.025), yaw=8.0, pv=1.0), foot_r=F(at=(-0.145, -0.025), yaw=-8.0, pv=1.0),
        hand_l=HW(p=(0.215, 0.055, 0.905), f=(0.05, 0.25, -1.0), m=(-1.0, 0.25, 0.0), e=(0.4, -1.0, -0.3)),
        hand_r=HW(p=(-0.215, 0.055, 0.905), f=(-0.05, 0.25, -1.0), m=(1.0, 0.25, 0.0), e=(-0.4, -1.0, -0.3)),
        fing="relaxed"))

    # ---------------------------------------------------------------- guard: generic fighting guard (left lead)
    make("guard", dict(
        pel=(0.0, -0.03, -0.085, 3.0, 0.0, -24.0), spine=(5.0, 0.0, -8.0), neck=(-4.0, 0.0, 0.0), gaze=(0.85, 0.0, 3.0),
        clav_l=(2.0, 3.0), clav_r=(2.0, 2.0),
        foot_l=F(at=(0.12, 0.21), yaw=-4.0, pv=1.0, kyaw=4.0), foot_r=F(at=(-0.15, -0.20), yaw=-38.0, pv=1.0, kyaw=6.0),
        hand_l=HW(p=(0.115, 0.37, 1.37), f=(-0.25, 0.55, 0.8), m=(-0.85, 0.0, 0.3), e=(0.3, -0.2, -1.0)),
        hand_r=HW(p=(-0.03, 0.21, 1.33), f=(0.25, 0.45, 0.85), m=(0.85, 0.0, 0.25), e=(-0.3, -0.3, -1.0)),
        fing="fist"))

    # ---------------------------------------------------------------- Earth: sei ping ma, double tiger claws
    make("e_stance", dict(
        pel=(0.0, -0.075, -0.300, 4.0, 0.0, 0.0), spine=(-1.0, 0.0, 0.0), neck=(-4.0, 0.0, 0.0), gaze=(0.85, 0.0, 3.0),
        clav_l=(-3.0, 1.0), clav_r=(-3.0, 0.0),
        foot_l=F(at=(0.37, 0.02), yaw=8.0, pv=1.0, kyaw=22.0), foot_r=F(at=(-0.37, 0.02), yaw=-8.0, pv=1.0, kyaw=-22.0),
        hand_l=HW(p=(0.10, 0.40, 1.16), f=(-0.1, 0.25, 1.0), m=(-0.15, 1.0, -0.1), e=(0.45, -0.1, -1.0)),
        hand_r=HW(p=(-0.20, 0.08, 0.98), f=(0.05, 0.45, 1.0), m=(0.15, 1.0, -0.3), e=(-0.3, -1.0, -0.4)),
        fing_l="tiger", fing_r="tiger"))

    # ---------------------------------------------------------------- Water: ward-off ready (peng), weight 60 / 40
    make("w_stance", dict(
        pel=(0.01, -0.015, -0.105, 1.0, 0.0, -14.0), spine=(1.0, 0.0, -6.0), neck=(-2.0, 0.0, 0.0), gaze=(0.85, 0.0, 4.0),
        clav_l=(-2.0, 2.0), clav_r=(-2.0, 0.0),
        foot_l=F(at=(0.11, 0.25), yaw=0.0, pv=1.0), foot_r=F(at=(-0.13, -0.20), yaw=-42.0, pv=1.0, kyaw=8.0),
        hand_l=HW(p=(0.03, 0.40, 1.24), f=(-1.0, 0.25, 0.1), m=(0.0, -1.0, 0.1), e=(0.5, -0.1, -1.0)),
        hand_r=HW(p=(-0.19, 0.16, 0.97), f=(0.15, 1.0, -0.05), m=(0.0, 0.0, -1.0), e=(-0.4, -1.0, -0.5)),
        fing="willow"))

    # ---------------------------------------------------------------- Fire: long-fist ready, springy, fists up
    make("f_stance", dict(
        pel=(0.0, -0.02, -0.095, 4.0, 0.0, -20.0), spine=(5.0, 0.0, -6.0), neck=(-5.0, 0.0, 0.0), gaze=(0.85, 0.0, 3.0),
        clav_l=(1.0, 4.0), clav_r=(1.0, 2.0),
        foot_l=F(at=(0.12, 0.27), yaw=-4.0, pv=1.0), foot_r=F(at=(-0.15, -0.23), yaw=-36.0, pv=1.0, pitch=-9.0, kyaw=5.0),
        hand_l=HW(p=(0.12, 0.44, 1.39), f=(-0.15, 0.75, 0.65), m=(-0.95, 0.0, 0.2), e=(0.25, -0.2, -1.0)),
        hand_r=HW(p=(-0.035, 0.22, 1.30), f=(0.3, 0.5, 0.8), m=(0.9, 0.0, 0.2), e=(-0.3, -0.3, -1.0)),
        fing="fist"))

    # ---------------------------------------------------------------- Air: Bagua dragon posture (torso to the centre)
    make("a_stance", dict(
        pel=(0.0, -0.04, -0.12, 2.0, 0.0, -38.0), spine=(3.0, 0.0, 22.0), neck=(-3.0, 0.0, 0.0), gaze=(0.9, 0.0, 2.0),
        clav_l=(1.0, 4.0), clav_r=(0.0, 3.0),
        foot_l=F(at=(0.07, 0.24), yaw=8.0, pv=1.0), foot_r=F(at=(-0.15, -0.17), yaw=-45.0, pv=1.0, kyaw=4.0),
        hand_l=HW(p=(0.06, 0.52, 1.52), f=(-0.1, 0.35, 1.0), m=(-0.35, 1.0, 0.0), e=(0.2, -0.2, -1.0)),
        hand_r=HW(p=(0.0, 0.29, 1.20), f=(0.25, 0.4, 1.0), m=(-0.1, 1.0, 0.2), e=(-0.4, -0.3, -1.0)),
        fing="oxtongue"))


def build_bases_2():
    """Hold / guard poses that loops start from (registered as bases so related clips can start / end on them)."""
    # Earth bridge-arm guard: horse stance, forearms crossed in front of the chest (iron-wire tension)
    make("e_guard", dict(
        pel=dict(dz=0.01), spine=dict(pitch=1.0), clav_l=(0.0, 4.0), clav_r=(0.0, 4.0),
        hand_l=HW(p=(-0.07, 0.33, 1.20), f=(-0.55, 0.35, 0.75), m=(0.1, 1.0, 0.25), e=(0.55, -0.2, -1.0)),
        hand_r=HW(p=(0.07, 0.31, 1.17), f=(0.55, 0.35, 0.75), m=(-0.1, 1.0, 0.25), e=(-0.55, -0.2, -1.0)),
        fing_l="palm", fing_r="palm"), parent="e_stance")
    # Earth seize: embrace a heavy mass at the chest, elbows out, knees a little deeper
    make("e_seize", dict(
        pel=dict(dz=-0.02, pitch=6.0, dy=-0.01), spine=dict(pitch=6.0), neck=dict(pitch=-6.0), clav_l=(3.0, 6.0),
        clav_r=(3.0, 6.0),
        hand_l=HW(p=(0.17, 0.38, 1.10), f=(-0.35, 0.45, 0.8), m=(-1.0, 0.1, 0.0), e=(0.7, 0.1, -0.6)),
        hand_r=HW(p=(-0.17, 0.38, 1.10), f=(0.35, 0.45, 0.8), m=(1.0, 0.1, 0.0), e=(-0.7, 0.1, -0.6)),
        fing_l="spread", fing_r="spread"), parent="e_stance")
