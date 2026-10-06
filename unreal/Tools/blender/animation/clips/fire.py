"""Fire clips - Northern Shaolin long fist / Tan Tui (MARTIAL_ARTS.md §2.3, §2.4, §3.4): long lines, hip snap."""
from ffa_dsl import BASES, Clip, F, H, HW, both, copy_state

from . import clip


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
