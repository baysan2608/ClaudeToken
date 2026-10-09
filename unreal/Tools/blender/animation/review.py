"""Review helper (not part of the build): render a film strip of any clip for close inspection.

    $B review.py <clip> [--frames 0,4,8 | --every 3] [--views side,game] [--panel 220x260] [--out DIR]
Writes <out>/<clip>_strip.png (default out = the stream scratch directory).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "common")))

import clips as catalog  # noqa: E402
import ffa_render as render  # noqa: E402

SCRATCH = os.environ.get("FFA_REVIEW_DIR") or os.path.join(__import__("tempfile").gettempdir(), "ffa_review")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("clip")
    ap.add_argument("--frames", default="")
    ap.add_argument("--every", type=int, default=0)
    ap.add_argument("--views", default="side,game")
    ap.add_argument("--panel", default="200x250")
    ap.add_argument("--out", default=SCRATCH)
    a = ap.parse_args()
    cat = catalog.load()
    res = cat[a.clip]().build()
    if a.frames:
        fr = [int(x) for x in a.frames.split(",")]
    else:
        step = a.every or max(1, res.frames // 8)
        fr = list(range(0, res.frames + 1, step))
        if fr[-1] != res.frames:
            fr.append(res.frames)
    pw, ph = (int(x) for x in a.panel.split("x"))
    os.makedirs(a.out, exist_ok=True)
    p = render.contact_sheet(res, [(f, "") for f in fr], os.path.join(a.out, a.clip + "_strip.png"),
                             views=tuple(a.views.split(",")), panel=(pw, ph), title=f"{a.clip} {res.clip.technique}")
    print(p)


if __name__ == "__main__":
    import numpy as np
    with np.errstate(all="ignore"):   # numpy 2.0 + macOS Accelerate: spurious matmul FP warnings
        main()
