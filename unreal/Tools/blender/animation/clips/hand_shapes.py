"""The ten hand shapes (MARTIAL_ARTS.md §1.4) as 2-key clips (frames 0 and 1 identical).  The runtime samples them
at time 0 and copies only the finger bones (metacarpals, phalanges, thumbs) of each side."""
import ffa_hands as hands
from ffa_dsl import Clip, neutral_state

from . import clip


def _make(shape):
    def build():
        st = neutral_state()
        st["fing_l"] = tuple(hands.get(shape))
        st["fing_r"] = tuple(hands.get(shape))
        c = Clip("hand_" + shape, 1, None, start=st, priority="P0", technique=f"hand shape: {shape}",
                 hands=(shape, shape), offsets={"neck": 0.0}, no_balance=True)
        return c
    return build


for _s in hands.SHAPE_NAMES:
    clip("hand_" + _s)(_make(_s))
