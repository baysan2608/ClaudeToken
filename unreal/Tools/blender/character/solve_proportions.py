"""Re-derive the measurement-target weights of ch_body.RECIPE (run only if the macro recipe changes).

    /home/user/tools/bpyenv/bin/python unreal/Tools/blender/character/solve_proportions.py

Damped Gauss-Newton on 8 signed MakeHuman measurement targets so that, after the uniform scale to 1.79 m, the base
mesh's hip / knee / shoulder heights, head pivot, skull top, arm segment lengths and shoulder width land near the
frozen rig (the per-limb fit in ch_fit then removes the rest). Prints the weights to paste into RECIPE["targets"].
"""
import os
import sys

import numpy as np
from scipy.optimize import lsq_linear

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ch_mh  # noqa: E402
import ch_body  # noqa: E402

VARS = ["measure/measure-upperleg-height", "measure/measure-lowerleg-height", "torso/torso-scale-vert",
        "measure/measure-neck-height", "head/head-scale-vert", "measure/measure-upperarm-length",
        "measure/measure-lowerarm-length", "measure/measure-shoulder-dist"]
GOAL = np.array([0.92, 0.50, 1.44, 1.615, 1.79, 0.29, 0.255, 0.183])
WEIGHT = np.array([3, 2, 2, 1.5, 2, 1.5, 1.5, 1.0])


def main():
    obj = ch_mh.ObjMesh(os.path.join(ch_mh.MH_DIR, "3dobjs", "base.obj"))
    v0 = ch_mh.apply_targets(obj.v, ch_mh.macro_target_weights(ch_body.RECIPE["macro"]))
    sk = ch_mh.load_skeleton()
    body = obj.group_verts(lambda g: g == "body")
    T = os.path.join(ch_mh.MH_DIR, "targets")

    def meas(v):
        V = ch_mh.to_ff(v)
        V[:, 2] -= V[body, 2].min()
        be = ch_mh.bone_ends(sk, V)
        hip, knee = be["upperleg01.L"][0], be["lowerleg01.L"][0]
        sh, el, wr, hd = be["upperarm01.L"][0], be["lowerarm01.L"][0], be["wrist.L"][0], be["head"][0]
        return np.array([hip[2], knee[2], sh[2], hd[2], V[body, 2].max(), np.linalg.norm(el - sh),
                         np.linalg.norm(wr - el), sh[0]])
    D = [(ch_mh.read_target(os.path.join(T, n + "-incr.target"), len(v0)),
          ch_mh.read_target(os.path.join(T, n + "-decr.target"), len(v0))) for n in VARS]

    def apply(x):
        v = v0.copy()
        for (inc, dec), xi in zip(D, x):
            v += xi * inc if xi > 0 else (-xi) * dec
        return v
    x = np.zeros(len(D))
    for _ in range(6):
        m = meas(apply(x))
        s = 1.79 / m[4]
        J = np.zeros((len(GOAL), len(D)))
        for j in range(len(D)):
            e = np.zeros(len(D))
            e[j] = 0.05 if x[j] >= 0 else -0.05
            J[:, j] = (meas(apply(x + e)) - m) / e[j]
        A = np.vstack([s * J * WEIGHT[:, None], 0.02 * np.eye(len(D))])
        b = np.concatenate([(GOAL - s * m) * WEIGHT, -0.02 * x])
        x = np.clip(x + lsq_linear(A, b, bounds=(-1 - x, 1 - x)).x, -1, 1)
    for n, xi in zip(VARS, x):
        print(f'"{n}-{"incr" if xi >= 0 else "decr"}": {abs(xi):.2f},')


if __name__ == "__main__":
    main()
