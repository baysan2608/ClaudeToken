"""Air clips - Baguazhang (MARTIAL_ARTS.md §2.5, §3.5): circle walking, palm changes, coiling and uncoiling."""
from ffa_dsl import BASES, Clip, F, H, HW, both, copy_state

from . import clip


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
