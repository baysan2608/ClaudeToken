"""Water clips - Tai Chi, Yang style (MARTIAL_ARTS.md §2.2, §3.3): waist-led circles, empty / full weight shifts."""
from ffa_dsl import BASES, Clip, F, H, HW, both, copy_state

from . import clip


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
