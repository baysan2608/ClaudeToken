"""The fighter's body: CC0 MakeHuman base mesh -> our proportions (targets) -> fitted onto the frozen rig.

Pipeline (numpy, no bpy):
  1. base mesh hm08 + macro targets (adult male, athletic, lean) + measurement / face targets (RECIPE)
  2. MakeHuman -> Fourfold axes, grounded, uniform scale to the rig height
  3. ch_fit: per-limb stretch + dual-quaternion rigid fit so every joint lands on ff_rig_spec (arms straightened into
     the rig's A-pose, palms toward the thighs, legs parallel)
Returns every vertex of the OBJ (body + helpers: tights, skirt, eyes ...) in rig space, so garments built from the
helpers stay registered with the body.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "common"))
import ff_rig_spec as spec  # noqa: E402
import ch_mh  # noqa: E402
import ch_fit  # noqa: E402

# ------------------------------------------------------------------------------------------------ design recipe
# Macro: adult male, athletic (muscle 0.72), lean (weight 0.38), slightly idealised proportions; mixed ancestry so the
# face is nobody in particular. Then measurement targets solved (least squares, see docs/character/README.md) so the
# joints land near the frozen rig before the fit, and a few face / build targets for a calm, determined look.
RECIPE = {
    "macro": {"muscle": 0.72, "weight": 0.38, "proportions": 0.60,
              "races": {"asian": 0.45, "caucasian": 0.30, "african": 0.25}},
    "targets": {
        # proportions (solved against the rig landmarks)
        "measure/measure-upperleg-height-decr": 0.12,
        "measure/measure-lowerleg-height-decr": 0.29,
        "torso/torso-scale-vert-incr": 0.42,
        "measure/measure-neck-height-decr": 0.67,
        "head/head-scale-vert-incr": 0.26,
        "measure/measure-upperarm-length-incr": 0.41,
        "measure/measure-lowerarm-length-decr": 0.38,
        "measure/measure-shoulder-dist-decr": 0.36,
        # athletic build (mostly under the tunic, but it sets the silhouette)
        "torso/torso-vshape-incr": 0.45,
        "torso/torso-muscle-dorsi-incr": 0.35,
        "torso/torso-muscle-pectoral-incr": 0.25,
        "neck/neck-scale-horiz-incr": 0.35,
        "armslegs/l-upperarm-shoulder-muscle-incr": 0.35,
        "armslegs/r-upperarm-shoulder-muscle-incr": 0.35,
        "armslegs/l-lowerarm-muscle-incr": 0.3,
        "armslegs/r-lowerarm-muscle-incr": 0.3,
        # face: defined jaw and cheekbones, straight nose, calm brow
        "head/head-square": 0.30,
        "head/head-scale-horiz-incr": 0.08,
        "eyes/l-eye-scale-incr": 0.15,
        "eyes/r-eye-scale-incr": 0.15,
        "chin/chin-prominent-incr": 0.25,
        "chin/chin-width-incr": 0.30,
        "cheek/l-cheek-bones-incr": 0.35,
        "cheek/r-cheek-bones-incr": 0.35,
        "cheek/l-cheek-volume-decr": 0.25,
        "cheek/r-cheek-volume-decr": 0.25,
        "nose/nose-scale-vert-incr": 0.15,
        "nose/nose-point-width-decr": 0.25,
        "nose/nose-hump-decr": 0.3,
        "eyebrows/eyebrows-angle-down": 0.25,
        "mouth/mouth-scale-horiz-decr": 0.1,
        # ears lie closer to the head with a rounder helix (the base ears' pointed tops stand out in a front view)
        "ears/l-ear-wing-decr": 0.40,
        "ears/r-ear-wing-decr": 0.40,
        "ears/l-ear-flap-decr": 0.30,
        "ears/r-ear-flap-decr": 0.30,
        "ears/l-ear-shape-round": 0.70,
        "ears/r-ear-shape-round": 0.70,
        "ears/l-ear-shape-pointed": -0.35,
        "ears/r-ear-shape-pointed": -0.35,
    },
}

RIG_HEIGHT = 1.79       # skull top (head bone tail is 1.80)
SIDES = (("l", ".L"), ("r", ".R"))


def recipe_target_weights(recipe=RECIPE):
    w = dict(ch_mh.macro_target_weights(recipe["macro"]))
    for k, v in recipe["targets"].items():
        if abs(v) > 1e-9:
            w[k + ".target"] = w.get(k + ".target", 0.0) + float(v)
    return w


def used_target_files(recipe=RECIPE):
    return sorted(recipe_target_weights(recipe).keys())


# ------------------------------------------------------------------------------------------------ fit groups
def mh_to_fit_group(mh_bone):
    """MakeHuman default-skeleton bone -> our fit bone (= ff bone name, or 'torso')."""
    b = mh_bone
    side = None
    if b.endswith(".L"):
        side, b = "l", b[:-2]
    elif b.endswith(".R"):
        side, b = "r", b[:-2]
    if side is None:
        return "torso"
    fingers = {"2": "index", "3": "middle", "4": "ring", "5": "pinky"}
    if b in ("clavicle", "shoulder01"):
        return "clavicle_" + side
    if b.startswith("upperarm"):
        return "upperarm_" + side
    if b.startswith("lowerarm"):
        return "lowerarm_" + side
    if b == "wrist":
        return "hand_" + side
    if b.startswith("metacarpal"):
        k = int(b[-1])
        return f"{('index', 'middle', 'ring', 'pinky')[k - 1]}_metacarpal_{side}"
    if b.startswith("finger"):
        f, j = b[6:].split("-")
        if f == "1":
            return f"thumb_0{j}_{side}"
        return f"{fingers[f]}_0{j}_{side}"
    if b.startswith("upperleg"):
        return "thigh_" + side
    if b.startswith("lowerleg"):
        return "calf_" + side
    if b == "foot":
        return "foot_" + side
    if b.startswith("toe"):
        return "ball_" + side
    return "torso"      # pelvis.L/R, breast, eyes, facial, ... follow the torso / head


def _palm_normal(be, s):
    """Palm normal of the MakeHuman hand (points out of the palm)."""
    S = ".L" if s == "l" else ".R"
    wr = be["wrist" + S][0]
    k3 = be["finger3-1" + S][0]
    k2 = be["finger2-1" + S][0]
    k5 = be["finger5-1" + S][0]
    y = ch_fit._n(k3 - wr)
    across = ch_fit._n(k2 - k5)                       # pinky -> index
    n = ch_fit._n(np.cross(y, across))
    # the palm faces the thigh in the base pose: make it point toward the body's mid-line / down
    ref = np.array([-1.0 if s == "l" else 1.0, 0.0, -1.0])
    return n if n @ ref > 0 else -n


def _ff_palm(s):
    p = np.array(spec.PALM_N_L)
    return p if s == "l" else p * np.array([-1, 1, 1])


def build_fit_bones(be):
    """be: MakeHuman bone ends {bone: (head, tail)} in rig space (after the uniform scale)."""
    B = {b["name"]: b for b in spec.BONES}
    bones = [ch_fit.FitBone.identity("torso")]

    def ff(name):
        return np.array(B[name]["head"]), np.array(B[name]["tail"])

    up = np.array([0, 0, 1.0])
    fwd = np.array([0, -1.0, 0])
    for s, S in SIDES:
        # clavicle: sternum end -> shoulder joint
        h, t = ff("clavicle_" + s)
        bones.append(ch_fit.FitBone("clavicle_" + s, be["clavicle" + S][0], be["upperarm01" + S][0], up, h, t, up))
        # elbow hinge (MH arm is slightly bent; the rig's is straight: hinge = local X of the rig's arm bones)
        sh, el, wr = be["upperarm01" + S][0], be["lowerarm01" + S][0], be["wrist" + S][0]
        hinge = ch_fit._n(np.cross(el - sh, wr - el))
        h, t = ff("upperarm_" + s)
        ff_x = ch_fit.frame(h, t, fwd)[:, 0]
        if hinge @ ff_x < 0:
            hinge = -hinge
        bones.append(ch_fit.FitBone("upperarm_" + s, sh, el, hinge, h, t, ff_x, ref_is_x=True))
        h, t = ff("lowerarm_" + s)
        ff_x = ch_fit.frame(h, t, fwd)[:, 0]
        bones.append(ch_fit.FitBone("lowerarm_" + s, el, wr, hinge, h, t, ff_x, ref_is_x=True))
        # hand + fingers: palm normal as reference
        pn, pf = _palm_normal(be, s), _ff_palm(s)
        h, _ = ff("hand_" + s)
        mid = np.array(B["middle_01_" + s]["head"])
        bones.append(ch_fit.FitBone("hand_" + s, wr, be["finger3-1" + S][0], pn, h, mid, pf))
        for k, fname in enumerate(("index", "middle", "ring", "pinky"), start=1):
            h, t = ff(f"{fname}_metacarpal_{s}")
            bones.append(ch_fit.FitBone(f"{fname}_metacarpal_{s}", be[f"metacarpal{k}{S}"][0],
                                        be[f"metacarpal{k}{S}"][1], pn, h, t, pf))
            for j in (1, 2, 3):
                h, t = ff(f"{fname}_0{j}_{s}")
                src = be[f"finger{k + 1}-{j}{S}"]
                bones.append(ch_fit.FitBone(f"{fname}_0{j}_{s}", src[0], src[1], pn, h, t, pf))
        for j in (1, 2, 3):
            h, t = ff(f"thumb_0{j}_{s}")
            src = be[f"finger1-{j}{S}"]
            bones.append(ch_fit.FitBone(f"thumb_0{j}_{s}", src[0], src[1], pn, h, t, pf))
        # legs: forward as roll reference
        hip, knee, ank = be["upperleg01" + S][0], be["lowerleg01" + S][0], be["foot" + S][0]
        h, t = ff("thigh_" + s)
        bones.append(ch_fit.FitBone("thigh_" + s, hip, knee, fwd, h, t, fwd))
        h, t = ff("calf_" + s)
        bones.append(ch_fit.FitBone("calf_" + s, knee, ank, fwd, h, t, fwd))
        ball = np.mean([be[f"toe{k}-1{S}"][0] for k in (2, 3, 4)], axis=0)
        toe = np.mean([be[f"toe{k}-3{S}"][1] if f"toe{k}-3{S}" in be else be[f"toe{k}-2{S}"][1] for k in (2, 3)], axis=0)
        h, t = ff("foot_" + s)
        bones.append(ch_fit.FitBone("foot_" + s, ank, ball, up, h, t, up))
        h, t = ff("ball_" + s)
        bones.append(ch_fit.FitBone("ball_" + s, ball, toe, up, h, t, up, stretch=False))
    return bones


def fit_weights(nverts, mh_weights, bone_names):
    col = {n: i for i, n in enumerate(bone_names)}
    W = np.zeros((nverts, len(bone_names)))
    for b, (ix, w) in mh_weights.items():
        W[ix, col[mh_to_fit_group(b)]] += w
    s = W.sum(axis=1)
    W[s <= 0, col["torso"]] = 1.0
    s = W.sum(axis=1, keepdims=True)
    return W / s


class BodyState:
    """Everything the later stages need: OBJ topology, rig-space vertices of every group, MH weights, joints."""

    def __init__(self, recipe=RECIPE):
        self.obj = ch_mh.ObjMesh(os.path.join(ch_mh.MH_DIR, "3dobjs", "base.obj"))
        v = ch_mh.apply_targets(self.obj.v, recipe_target_weights(recipe))
        V = ch_mh.to_ff(v)
        body = self.obj.group_verts(lambda g: g == "body")
        V[:, 2] -= V[body, 2].min()
        self.scale = RIG_HEIGHT / V[body, 2].max()
        V *= self.scale
        V[:, 0] -= 0.5 * (V[body, 0].max() + V[body, 0].min())
        self.skel = ch_mh.load_skeleton()
        self.mh_weights = ch_mh.load_weights()
        be = ch_mh.bone_ends(self.skel, V)
        self.src_bone_ends = be
        self.fit_bones = build_fit_bones(be)
        names = [b.name for b in self.fit_bones]
        self.fit_W = fit_weights(len(V), self.mh_weights, names)
        self.v_pre = V
        self.v = ch_fit.deform(V, self.fit_W, self.fit_bones)
        self.bone_ends = ch_mh.bone_ends(self.skel, self.v)

    def report(self):
        """Joint landing errors (cm) after the fit."""
        be = self.bone_ends
        B = {b["name"]: b for b in spec.BONES}
        pairs = []
        for s, S in SIDES:
            pairs += [("upperarm01" + S, "upperarm_" + s), ("lowerarm01" + S, "lowerarm_" + s), ("wrist" + S, "hand_" + s),
                      ("upperleg01" + S, "thigh_" + s), ("lowerleg01" + S, "calf_" + s), ("foot" + S, "foot_" + s),
                      ("finger3-1" + S, "middle_01_" + s), ("finger1-1" + S, "thumb_01_" + s)]
        return {f: round(float(np.linalg.norm(be[m][0] - np.array(B[f]["head"]))) * 100, 2) for m, f in pairs}
